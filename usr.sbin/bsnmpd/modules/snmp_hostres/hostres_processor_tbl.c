/*-
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Victor Cruceru <soc-victor@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Host Resources MIB for SNMPd. Implementation for hrProcessorTable
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

/*
 * This structure is used to hold a SNMP table entry
 * for HOST-RESOURCES-MIB's hrProcessorTable.
 * Note that index is external being allocated & maintained
 * by the hrDeviceTable code..
 */
struct processor_entry {
	int32_t		index;
	const struct asn_oid *frwId;
	int32_t		load;		/* average cpu usage */
	int32_t		sample_cnt;	/* number of usage samples */
	int32_t		cur_sample_idx;	/* current valid sample */
	TAILQ_ENTRY(processor_entry) link;
	u_char		cpu_no;		/* which cpu, counted from 0 */

	/* the samples from the last minute, as required by MIB */
	double		samples[MAX_CPU_SAMPLES];
	long		states[MAX_CPU_SAMPLES][CPUSTATES];
};
TAILQ_HEAD(processor_tbl, processor_entry);

/* the head of the list with hrDeviceTable's entries */
static struct processor_tbl processor_tbl =
    TAILQ_HEAD_INITIALIZER(processor_tbl);

/* number of processors in dev tbl */
static int32_t detected_processor_count;

/* sysctlbyname(hw.ncpu) */
static int hw_ncpu;

/* sysctlbyname(kern.cp_times) */
static int cpmib[2];
static size_t cplen;

/* periodic timer used to get cpu load stats */
static void *cpus_load_timer;

/**
 * Returns the CPU usage of a given processor entry.
 *
 * It needs at least two cp_times "tick" samples to calculate a delta and
 * thus, the usage over the sampling period.
 */
static int
get_avg_load(struct processor_entry *e)
{
	u_int i, oldest;
	long delta = 0;
	double usage = 0.0;

	assert(e != NULL);

	/* Need two samples to perform delta calculation. */
	if (e->sample_cnt <= 1)
		return (0);

	/* Oldest usable index, we wrap around. */
	if (e->sample_cnt == MAX_CPU_SAMPLES)
		oldest = (e->cur_sample_idx + 1) % MAX_CPU_SAMPLES;
	else
		oldest = 0;

	/* Sum delta for all states. */
	for (i = 0; i < CPUSTATES; i++) {
		delta += e->states[e->cur_sample_idx][i];
		delta -= e->states[oldest][i];
	}
	if (delta == 0)
		return 0;

	/* Take idle time from the last element and convert to
	 * percent usage by contrasting with total ticks delta. */
	usage = (double)(e->states[e->cur_sample_idx][CPUSTATES-1] -
	    e->states[oldest][CPUSTATES-1]) / delta;
	usage = 100 - (usage * 100);
	HRDBG("CPU no. %d, delta ticks %ld, pct usage %.2f", e->cpu_no,
	    delta, usage);

	return ((int)(usage));
}

/**
 * Save a new sample to proc entry and get the average usage.
 *
 * Samples are stored in a ringbuffer from 0..(MAX_CPU_SAMPLES-1)
 */
static void
save_sample(struct processor_entry *e, long *cp_times)
{
	int i;

	e->cur_sample_idx = (e->cur_sample_idx + 1) % MAX_CPU_SAMPLES;
	for (i = 0; cp_times != NULL && i < CPUSTATES; i++)
		e->states[e->cur_sample_idx][i] = cp_times[i];

	e->sample_cnt++;
	if (e->sample_cnt > MAX_CPU_SAMPLES)
		e->sample_cnt = MAX_CPU_SAMPLES;

	HRDBG("sample count for CPU no. %d went to %d", e->cpu_no, e->sample_cnt);
	e->load = get_avg_load(e);

}

/**
 * Create a new entry into the processor table.
 */
static struct processor_entry *
proc_create_entry(u_int cpu_no, struct device_map_entry *map)
{
	struct device_entry *dev;
	struct processor_entry *entry;
	char name[128];

	/*
	 * If there is no map entry create one by creating a device table
	 * entry.
	 */
	if (map == NULL) {
		snprintf(name, sizeof(name), "cpu%u", cpu_no);
		if ((dev = device_entry_create(name, "", "")) == NULL)
			return (NULL);
		dev->flags |= HR_DEVICE_IMMUTABLE;
		STAILQ_FOREACH(map, &device_map, link)
			if (strcmp(map->name_key, name) == 0)
				break;
		if (map == NULL)
			abort();
	}

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_ERR, "hrProcessorTable: %s malloc "
		    "failed: %m", __func__);
		return (NULL);
	}
	memset(entry, 0, sizeof(*entry));

	entry->index = map->hrIndex;
	entry->load = 0;
	entry->sample_cnt = 0;
	entry->cur_sample_idx = -1;
	entry->cpu_no = (u_char)cpu_no;
	entry->frwId = &oid_zeroDotZero; /* unknown id FIXME */

	INSERT_OBJECT_INT(entry, &processor_tbl);

	HRDBG("CPU %d added with SNMP index=%d",
	    entry->cpu_no, entry->index);

	return (entry);
}

/**
 * Scan the device map table for CPUs and create an entry into the
 * processor table for each CPU.
 *
 * Make sure that the number of processors announced by the kernel hw.ncpu
 * is equal to the number of processors we have found in the device table.
 */
static void
create_proc_table(void)
{
	struct device_map_entry *map;
	struct processor_entry *entry;
	int cpu_no;
	size_t len;

	detected_processor_count = 0;

	/*
	 * Because hrProcessorTable depends on hrDeviceTable,
	 * the device detection must be performed at this point.
	 * If not, no entries will be present in the hrProcessor Table.
	 *
	 * For non-ACPI system the processors are not in the device table,
	 * therefore insert them after checking hw.ncpu.
	 */
	STAILQ_FOREACH(map, &device_map, link)
		if (strncmp(map->name_key, "cpu", strlen("cpu")) == 0 &&
		    strstr(map->location_key, ".CPU") != NULL) {
			if (sscanf(map->name_key,"cpu%d", &cpu_no) != 1) {
				syslog(LOG_ERR, "hrProcessorTable: Failed to "
				    "get cpu no. from device named '%s'",
				    map->name_key);
				continue;
			}

			if ((entry = proc_create_entry(cpu_no, map)) == NULL)
				continue;

			detected_processor_count++;
		}

	len = sizeof(hw_ncpu);
	if (sysctlbyname("hw.ncpu", &hw_ncpu, &len, NULL, 0) == -1 ||
	    len != sizeof(hw_ncpu)) {
		syslog(LOG_ERR, "hrProcessorTable: sysctl(hw.ncpu) failed");
		hw_ncpu = 0;
	}

	HRDBG("%d CPUs detected via device table; hw.ncpu is %d",
	    detected_processor_count, hw_ncpu);

	/* XXX Can happen on non-ACPI systems? Create entries by hand. */
	for (; detected_processor_count < hw_ncpu; detected_processor_count++)
		proc_create_entry(detected_processor_count, NULL);

	len = 2;
	if (sysctlnametomib("kern.cp_times", cpmib, &len)) {
		syslog(LOG_ERR, "hrProcessorTable: sysctlnametomib(kern.cp_times) failed");
		cpmib[0] = 0;
		cpmib[1] = 0;
		cplen = 0;
	} else if (sysctl(cpmib, 2, NULL, &len, NULL, 0)) {
		syslog(LOG_ERR, "hrProcessorTable: sysctl(kern.cp_times) length query failed");
		cplen = 0;
	} else {
		cplen = len / sizeof(long);
	}
	HRDBG("%zu entries for kern.cp_times", cplen);

}

/**
 * Free the processor table
 */
static void
free_proc_table(void)
{
	struct processor_entry *n1;

	while ((n1 = TAILQ_FIRST(&processor_tbl)) != NULL) {
		TAILQ_REMOVE(&processor_tbl, n1, link);
		free(n1);
		detected_processor_count--;
	}

	assert(detected_processor_count == 0);
	detected_processor_count = 0;
}

/**
 * Refresh all values in the processor table. We call this once for
 * every PDU that accesses the table.
 */
static void
refresh_processor_tbl(void)
{
	struct processor_entry *entry;
	size_t size;

	long pcpu_cp_times[cplen];
	memset(pcpu_cp_times, 0, sizeof(pcpu_cp_times));

	size = cplen * sizeof(long);
	if (sysctl(cpmib, 2, pcpu_cp_times, &size, NULL, 0) == -1 &&
	    !(errno == ENOMEM && size >= cplen * sizeof(long))) {
		syslog(LOG_ERR, "hrProcessorTable: sysctl(kern.cp_times) failed");
		return;
	}

	TAILQ_FOREACH(entry, &processor_tbl, link) {
		assert(hr_kd != NULL);
		save_sample(entry, &pcpu_cp_times[entry->cpu_no * CPUSTATES]);
	}

}

/**
 * This function is called MAX_CPU_SAMPLES times per minute to collect the
 * CPU load.
 */
static void
get_cpus_samples(void *arg __unused)
{

	HRDBG("[%llu] ENTER", (unsigned long long)get_ticks());
	refresh_processor_tbl();
	HRDBG("[%llu] EXIT", (unsigned long long)get_ticks());
}

/**
 * Called to start this table. We need to start the periodic idle
 * time collection.
 */
void
start_processor_tbl(struct lmodule *mod)
{

	/*
	 * Start the cpu stats collector
	 * The semantics of timer_start parameters is in "SNMP ticks";
	 * we have 100 "SNMP ticks" per second, thus we are trying below
	 * to get MAX_CPU_SAMPLES per minute
	 */
	cpus_load_timer = timer_start_repeat(100, 100 * 60 / MAX_CPU_SAMPLES,
	    get_cpus_samples, NULL, mod);
}

/**
 * Init the things for hrProcessorTable.
 * Scan the device table for processor entries.
 */
void
init_processor_tbl(void)
{

	/* create the initial processor table */
	create_proc_table();
	/* and get first samples */
	refresh_processor_tbl();
}

/**
 * Finalization routine for hrProcessorTable.
 * It destroys the lists and frees any allocated heap memory.
 */
void
fini_processor_tbl(void)
{

	if (cpus_load_timer != NULL) {
		timer_stop(cpus_load_timer);
		cpus_load_timer = NULL;
	}

	free_proc_table();
}

/**
 * Access routine for the processor table.
 */
int
op_hrProcessorTable(struct snmp_context *ctx __unused,
    struct snmp_value *value, u_int sub, u_int iidx __unused,
    enum snmp_op curr_op)
{
	struct processor_entry *entry;

	switch (curr_op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&processor_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&processor_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&processor_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NO_CREATION);
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		abort();
	}
	abort();

  get:
	switch (value->var.subs[sub - 1]) {

	case LEAF_hrProcessorFrwID:
		assert(entry->frwId != NULL);
		value->v.oid = *entry->frwId;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrProcessorLoad:
		value->v.integer = entry->load;
		return (SNMP_ERR_NOERROR);
	}
	abort();
}
