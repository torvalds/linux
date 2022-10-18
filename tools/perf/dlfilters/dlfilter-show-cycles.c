// SPDX-License-Identifier: GPL-2.0
/*
 * dlfilter-show-cycles.c: Print the number of cycles at the start of each line
 * Copyright (c) 2021, Intel Corporation.
 */
#include <perf/perf_dlfilter.h>
#include <string.h>
#include <stdio.h>

#define MAX_CPU 4096

enum {
	INSTR_CYC,
	BRNCH_CYC,
	OTHER_CYC,
	MAX_ENTRY
};

static __u64 cycles[MAX_CPU][MAX_ENTRY];
static __u64 cycles_rpt[MAX_CPU][MAX_ENTRY];

#define BITS		16
#define TABLESZ		(1 << BITS)
#define TABLEMAX	(TABLESZ / 2)
#define MASK		(TABLESZ - 1)

static struct entry {
	__u32 used;
	__s32 tid;
	__u64 cycles[MAX_ENTRY];
	__u64 cycles_rpt[MAX_ENTRY];
} table[TABLESZ];

static int tid_cnt;

static int event_entry(const char *event)
{
	if (!event)
		return OTHER_CYC;
	if (!strncmp(event, "instructions", 12))
		return INSTR_CYC;
	if (!strncmp(event, "branches", 8))
		return BRNCH_CYC;
	return OTHER_CYC;
}

static struct entry *find_entry(__s32 tid)
{
	__u32 pos = tid & MASK;
	struct entry *e;

	e = &table[pos];
	while (e->used) {
		if (e->tid == tid)
			return e;
		if (++pos == TABLESZ)
			pos = 0;
		e = &table[pos];
	}

	if (tid_cnt >= TABLEMAX) {
		fprintf(stderr, "Too many threads\n");
		return NULL;
	}

	tid_cnt += 1;
	e->used = 1;
	e->tid = tid;
	return e;
}

static void add_entry(__s32 tid, int pos, __u64 cnt)
{
	struct entry *e = find_entry(tid);

	if (e)
		e->cycles[pos] += cnt;
}

int filter_event_early(void *data, const struct perf_dlfilter_sample *sample, void *ctx)
{
	__s32 cpu = sample->cpu;
	__s32 tid = sample->tid;
	int pos;

	if (!sample->cyc_cnt)
		return 0;

	pos = event_entry(sample->event);

	if (cpu >= 0 && cpu < MAX_CPU)
		cycles[cpu][pos] += sample->cyc_cnt;
	else if (tid != -1)
		add_entry(tid, pos, sample->cyc_cnt);
	return 0;
}

static void print_vals(__u64 cycles, __u64 delta)
{
	if (delta)
		printf("%10llu %10llu ", (unsigned long long)cycles, (unsigned long long)delta);
	else
		printf("%10llu %10s ", (unsigned long long)cycles, "");
}

int filter_event(void *data, const struct perf_dlfilter_sample *sample, void *ctx)
{
	__s32 cpu = sample->cpu;
	__s32 tid = sample->tid;
	int pos;

	pos = event_entry(sample->event);

	if (cpu >= 0 && cpu < MAX_CPU) {
		print_vals(cycles[cpu][pos], cycles[cpu][pos] - cycles_rpt[cpu][pos]);
		cycles_rpt[cpu][pos] = cycles[cpu][pos];
		return 0;
	}

	if (tid != -1) {
		struct entry *e = find_entry(tid);

		if (e) {
			print_vals(e->cycles[pos], e->cycles[pos] - e->cycles_rpt[pos]);
			e->cycles_rpt[pos] = e->cycles[pos];
			return 0;
		}
	}

	printf("%22s", "");
	return 0;
}

const char *filter_description(const char **long_description)
{
	static char *long_desc = "Cycle counts are accumulated per CPU (or "
		"per thread if CPU is not recorded) from IPC information, and "
		"printed together with the change since the last print, at the "
		"start of each line. Separate counts are kept for branches, "
		"instructions or other events.";

	*long_description = long_desc;
	return "Print the number of cycles at the start of each line";
}
