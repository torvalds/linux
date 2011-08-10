/*
 *  (C) 2010,2011       Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 */

#if defined(__i386__) || defined(__x86_64__)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <cpufreq.h>

#include "helpers/helpers.h"
#include "idle_monitor/cpupower-monitor.h"

#define MSR_APERF	0xE8
#define MSR_MPERF	0xE7

#define MSR_TSC	0x10

enum mperf_id { C0 = 0, Cx, AVG_FREQ, MPERF_CSTATE_COUNT };

static int mperf_get_count_percent(unsigned int self_id, double *percent,
				   unsigned int cpu);
static int mperf_get_count_freq(unsigned int id, unsigned long long *count,
				unsigned int cpu);

static cstate_t mperf_cstates[MPERF_CSTATE_COUNT] = {
	{
		.name			= "C0",
		.desc			= N_("Processor Core not idle"),
		.id			= C0,
		.range			= RANGE_THREAD,
		.get_count_percent	= mperf_get_count_percent,
	},
	{
		.name			= "Cx",
		.desc			= N_("Processor Core in an idle state"),
		.id			= Cx,
		.range			= RANGE_THREAD,
		.get_count_percent	= mperf_get_count_percent,
	},

	{
		.name			= "Freq",
		.desc			= N_("Average Frequency (including boost) in MHz"),
		.id			= AVG_FREQ,
		.range			= RANGE_THREAD,
		.get_count		= mperf_get_count_freq,
	},
};

static unsigned long long tsc_at_measure_start;
static unsigned long long tsc_at_measure_end;
static unsigned long max_frequency;
static unsigned long long *mperf_previous_count;
static unsigned long long *aperf_previous_count;
static unsigned long long *mperf_current_count;
static unsigned long long *aperf_current_count;
/* valid flag for all CPUs. If a MSR read failed it will be zero */
static int *is_valid;

static int mperf_get_tsc(unsigned long long *tsc)
{
	return read_msr(0, MSR_TSC, tsc);
}

static int mperf_init_stats(unsigned int cpu)
{
	unsigned long long val;
	int ret;

	ret = read_msr(cpu, MSR_APERF, &val);
	aperf_previous_count[cpu] = val;
	ret |= read_msr(cpu, MSR_MPERF, &val);
	mperf_previous_count[cpu] = val;
	is_valid[cpu] = !ret;

	return 0;
}

static int mperf_measure_stats(unsigned int cpu)
{
	unsigned long long val;
	int ret;

	ret = read_msr(cpu, MSR_APERF, &val);
	aperf_current_count[cpu] = val;
	ret |= read_msr(cpu, MSR_MPERF, &val);
	mperf_current_count[cpu] = val;
	is_valid[cpu] = !ret;

	return 0;
}

/*
 * get_average_perf()
 *
 * Returns the average performance (also considers boosted frequencies)
 *
 * Input:
 *   aperf_diff: Difference of the aperf register over a time period
 *   mperf_diff: Difference of the mperf register over the same time period
 *   max_freq:   Maximum frequency (P0)
 *
 * Returns:
 *   Average performance over the time period
 */
static unsigned long get_average_perf(unsigned long long aperf_diff,
				      unsigned long long mperf_diff)
{
	unsigned int perf_percent = 0;
	if (((unsigned long)(-1) / 100) < aperf_diff) {
		int shift_count = 7;
		aperf_diff >>= shift_count;
		mperf_diff >>= shift_count;
	}
	perf_percent = (aperf_diff * 100) / mperf_diff;
	return (max_frequency * perf_percent) / 100;
}

static int mperf_get_count_percent(unsigned int id, double *percent,
				   unsigned int cpu)
{
	unsigned long long aperf_diff, mperf_diff, tsc_diff;

	if (!is_valid[cpu])
		return -1;

	if (id != C0 && id != Cx)
		return -1;

	mperf_diff = mperf_current_count[cpu] - mperf_previous_count[cpu];
	aperf_diff = aperf_current_count[cpu] - aperf_previous_count[cpu];
	tsc_diff = tsc_at_measure_end - tsc_at_measure_start;

	*percent = 100.0 * mperf_diff / tsc_diff;
	dprint("%s: mperf_diff: %llu, tsc_diff: %llu\n",
	       mperf_cstates[id].name, mperf_diff, tsc_diff);

	if (id == Cx)
		*percent = 100.0 - *percent;

	dprint("%s: previous: %llu - current: %llu - (%u)\n",
		mperf_cstates[id].name, mperf_diff, aperf_diff, cpu);
	dprint("%s: %f\n", mperf_cstates[id].name, *percent);
	return 0;
}

static int mperf_get_count_freq(unsigned int id, unsigned long long *count,
				unsigned int cpu)
{
	unsigned long long aperf_diff, mperf_diff;

	if (id != AVG_FREQ)
		return 1;

	if (!is_valid[cpu])
		return -1;

	mperf_diff = mperf_current_count[cpu] - mperf_previous_count[cpu];
	aperf_diff = aperf_current_count[cpu] - aperf_previous_count[cpu];

	/* Return MHz for now, might want to return KHz if column width is more
	   generic */
	*count = get_average_perf(aperf_diff, mperf_diff) / 1000;
	dprint("%s: %llu\n", mperf_cstates[id].name, *count);

	return 0;
}

static int mperf_start(void)
{
	int cpu;
	unsigned long long dbg;

	mperf_get_tsc(&tsc_at_measure_start);

	for (cpu = 0; cpu < cpu_count; cpu++)
		mperf_init_stats(cpu);

	mperf_get_tsc(&dbg);
	dprint("TSC diff: %llu\n", dbg - tsc_at_measure_start);
	return 0;
}

static int mperf_stop(void)
{
	unsigned long long dbg;
	int cpu;

	mperf_get_tsc(&tsc_at_measure_end);

	for (cpu = 0; cpu < cpu_count; cpu++)
		mperf_measure_stats(cpu);

	mperf_get_tsc(&dbg);
	dprint("TSC diff: %llu\n", dbg - tsc_at_measure_end);

	return 0;
}

struct cpuidle_monitor mperf_monitor;

struct cpuidle_monitor *mperf_register(void)
{
	unsigned long min;

	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_APERF))
		return NULL;

	/* Assume min/max all the same on all cores */
	if (cpufreq_get_hardware_limits(0, &min, &max_frequency)) {
		dprint("Cannot retrieve max freq from cpufreq kernel "
		       "subsystem\n");
		return NULL;
	}

	/* Free this at program termination */
	is_valid = calloc(cpu_count, sizeof(int));
	mperf_previous_count = calloc(cpu_count, sizeof(unsigned long long));
	aperf_previous_count = calloc(cpu_count, sizeof(unsigned long long));
	mperf_current_count = calloc(cpu_count, sizeof(unsigned long long));
	aperf_current_count = calloc(cpu_count, sizeof(unsigned long long));

	mperf_monitor.name_len = strlen(mperf_monitor.name);
	return &mperf_monitor;
}

void mperf_unregister(void)
{
	free(mperf_previous_count);
	free(aperf_previous_count);
	free(mperf_current_count);
	free(aperf_current_count);
	free(is_valid);
}

struct cpuidle_monitor mperf_monitor = {
	.name			= "Mperf",
	.hw_states_num		= MPERF_CSTATE_COUNT,
	.hw_states		= mperf_cstates,
	.start			= mperf_start,
	.stop			= mperf_stop,
	.do_register		= mperf_register,
	.unregister		= mperf_unregister,
	.needs_root		= 1,
	.overflow_s		= 922000000 /* 922337203 seconds TSC overflow
					       at 20GHz */
};
#endif /* #if defined(__i386__) || defined(__x86_64__) */
