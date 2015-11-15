#include <stdio.h>
#include "evsel.h"
#include "stat.h"
#include "color.h"

enum {
	CTX_BIT_USER	= 1 << 0,
	CTX_BIT_KERNEL	= 1 << 1,
	CTX_BIT_HV	= 1 << 2,
	CTX_BIT_HOST	= 1 << 3,
	CTX_BIT_IDLE	= 1 << 4,
	CTX_BIT_MAX	= 1 << 5,
};

#define NUM_CTX CTX_BIT_MAX

static struct stats runtime_nsecs_stats[MAX_NR_CPUS];
static struct stats runtime_cycles_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_stalled_cycles_front_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_stalled_cycles_back_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_branches_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_cacherefs_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_l1_dcache_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_l1_icache_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_ll_cache_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_itlb_cache_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_dtlb_cache_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_cycles_in_tx_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_transaction_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_elision_stats[NUM_CTX][MAX_NR_CPUS];

struct stats walltime_nsecs_stats;

static int evsel_context(struct perf_evsel *evsel)
{
	int ctx = 0;

	if (evsel->attr.exclude_kernel)
		ctx |= CTX_BIT_KERNEL;
	if (evsel->attr.exclude_user)
		ctx |= CTX_BIT_USER;
	if (evsel->attr.exclude_hv)
		ctx |= CTX_BIT_HV;
	if (evsel->attr.exclude_host)
		ctx |= CTX_BIT_HOST;
	if (evsel->attr.exclude_idle)
		ctx |= CTX_BIT_IDLE;

	return ctx;
}

void perf_stat__reset_shadow_stats(void)
{
	memset(runtime_nsecs_stats, 0, sizeof(runtime_nsecs_stats));
	memset(runtime_cycles_stats, 0, sizeof(runtime_cycles_stats));
	memset(runtime_stalled_cycles_front_stats, 0, sizeof(runtime_stalled_cycles_front_stats));
	memset(runtime_stalled_cycles_back_stats, 0, sizeof(runtime_stalled_cycles_back_stats));
	memset(runtime_branches_stats, 0, sizeof(runtime_branches_stats));
	memset(runtime_cacherefs_stats, 0, sizeof(runtime_cacherefs_stats));
	memset(runtime_l1_dcache_stats, 0, sizeof(runtime_l1_dcache_stats));
	memset(runtime_l1_icache_stats, 0, sizeof(runtime_l1_icache_stats));
	memset(runtime_ll_cache_stats, 0, sizeof(runtime_ll_cache_stats));
	memset(runtime_itlb_cache_stats, 0, sizeof(runtime_itlb_cache_stats));
	memset(runtime_dtlb_cache_stats, 0, sizeof(runtime_dtlb_cache_stats));
	memset(runtime_cycles_in_tx_stats, 0,
			sizeof(runtime_cycles_in_tx_stats));
	memset(runtime_transaction_stats, 0,
		sizeof(runtime_transaction_stats));
	memset(runtime_elision_stats, 0, sizeof(runtime_elision_stats));
	memset(&walltime_nsecs_stats, 0, sizeof(walltime_nsecs_stats));
}

/*
 * Update various tracking values we maintain to print
 * more semantic information such as miss/hit ratios,
 * instruction rates, etc:
 */
void perf_stat__update_shadow_stats(struct perf_evsel *counter, u64 *count,
				    int cpu)
{
	int ctx = evsel_context(counter);

	if (perf_evsel__match(counter, SOFTWARE, SW_TASK_CLOCK))
		update_stats(&runtime_nsecs_stats[cpu], count[0]);
	else if (perf_evsel__match(counter, HARDWARE, HW_CPU_CYCLES))
		update_stats(&runtime_cycles_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, CYCLES_IN_TX))
		update_stats(&runtime_cycles_in_tx_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, TRANSACTION_START))
		update_stats(&runtime_transaction_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, ELISION_START))
		update_stats(&runtime_elision_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HARDWARE, HW_STALLED_CYCLES_FRONTEND))
		update_stats(&runtime_stalled_cycles_front_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HARDWARE, HW_STALLED_CYCLES_BACKEND))
		update_stats(&runtime_stalled_cycles_back_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HARDWARE, HW_BRANCH_INSTRUCTIONS))
		update_stats(&runtime_branches_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HARDWARE, HW_CACHE_REFERENCES))
		update_stats(&runtime_cacherefs_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_L1D))
		update_stats(&runtime_l1_dcache_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_L1I))
		update_stats(&runtime_ll_cache_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_LL))
		update_stats(&runtime_ll_cache_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_DTLB))
		update_stats(&runtime_dtlb_cache_stats[ctx][cpu], count[0]);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_ITLB))
		update_stats(&runtime_itlb_cache_stats[ctx][cpu], count[0]);
}

/* used for get_ratio_color() */
enum grc_type {
	GRC_STALLED_CYCLES_FE,
	GRC_STALLED_CYCLES_BE,
	GRC_CACHE_MISSES,
	GRC_MAX_NR
};

static const char *get_ratio_color(enum grc_type type, double ratio)
{
	static const double grc_table[GRC_MAX_NR][3] = {
		[GRC_STALLED_CYCLES_FE] = { 50.0, 30.0, 10.0 },
		[GRC_STALLED_CYCLES_BE] = { 75.0, 50.0, 20.0 },
		[GRC_CACHE_MISSES] 	= { 20.0, 10.0, 5.0 },
	};
	const char *color = PERF_COLOR_NORMAL;

	if (ratio > grc_table[type][0])
		color = PERF_COLOR_RED;
	else if (ratio > grc_table[type][1])
		color = PERF_COLOR_MAGENTA;
	else if (ratio > grc_table[type][2])
		color = PERF_COLOR_YELLOW;

	return color;
}

static void print_stalled_cycles_frontend(FILE *out, int cpu,
					  struct perf_evsel *evsel
					  __maybe_unused, double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_cycles_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_FE, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " frontend cycles idle   ");
}

static void print_stalled_cycles_backend(FILE *out, int cpu,
					 struct perf_evsel *evsel
					 __maybe_unused, double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_cycles_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_BE, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " backend  cycles idle   ");
}

static void print_branch_misses(FILE *out, int cpu,
				struct perf_evsel *evsel __maybe_unused,
				double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_branches_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " of all branches        ");
}

static void print_l1_dcache_misses(FILE *out, int cpu,
				   struct perf_evsel *evsel __maybe_unused,
				   double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_l1_dcache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " of all L1-dcache hits  ");
}

static void print_l1_icache_misses(FILE *out, int cpu,
				   struct perf_evsel *evsel __maybe_unused,
				   double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_l1_icache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " of all L1-icache hits  ");
}

static void print_dtlb_cache_misses(FILE *out, int cpu,
				    struct perf_evsel *evsel __maybe_unused,
				    double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_dtlb_cache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " of all dTLB cache hits ");
}

static void print_itlb_cache_misses(FILE *out, int cpu,
				    struct perf_evsel *evsel __maybe_unused,
				    double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_itlb_cache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " of all iTLB cache hits ");
}

static void print_ll_cache_misses(FILE *out, int cpu,
				  struct perf_evsel *evsel __maybe_unused,
				  double avg)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_ll_cache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	fprintf(out, " #  ");
	color_fprintf(out, color, "%6.2f%%", ratio);
	fprintf(out, " of all LL-cache hits   ");
}

void perf_stat__print_shadow_stats(FILE *out, struct perf_evsel *evsel,
				   double avg, int cpu, enum aggr_mode aggr)
{
	double total, ratio = 0.0, total2;
	int ctx = evsel_context(evsel);

	if (perf_evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS)) {
		total = avg_stats(&runtime_cycles_stats[ctx][cpu]);
		if (total) {
			ratio = avg / total;
			fprintf(out, " #   %5.2f  insns per cycle        ", ratio);
		} else {
			fprintf(out, "                                   ");
		}
		total = avg_stats(&runtime_stalled_cycles_front_stats[ctx][cpu]);
		total = max(total, avg_stats(&runtime_stalled_cycles_back_stats[ctx][cpu]));

		if (total && avg) {
			ratio = total / avg;
			fprintf(out, "\n");
			if (aggr == AGGR_NONE)
				fprintf(out, "        ");
			fprintf(out, "                                                  #   %5.2f  stalled cycles per insn", ratio);
		}

	} else if (perf_evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES) &&
			runtime_branches_stats[ctx][cpu].n != 0) {
		print_branch_misses(out, cpu, evsel, avg);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_L1D |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)) &&
			runtime_l1_dcache_stats[ctx][cpu].n != 0) {
		print_l1_dcache_misses(out, cpu, evsel, avg);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_L1I |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)) &&
			runtime_l1_icache_stats[ctx][cpu].n != 0) {
		print_l1_icache_misses(out, cpu, evsel, avg);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_DTLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)) &&
			runtime_dtlb_cache_stats[ctx][cpu].n != 0) {
		print_dtlb_cache_misses(out, cpu, evsel, avg);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_ITLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)) &&
			runtime_itlb_cache_stats[ctx][cpu].n != 0) {
		print_itlb_cache_misses(out, cpu, evsel, avg);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_LL |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)) &&
			runtime_ll_cache_stats[ctx][cpu].n != 0) {
		print_ll_cache_misses(out, cpu, evsel, avg);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_CACHE_MISSES) &&
			runtime_cacherefs_stats[ctx][cpu].n != 0) {
		total = avg_stats(&runtime_cacherefs_stats[ctx][cpu]);

		if (total)
			ratio = avg * 100 / total;

		fprintf(out, " # %8.3f %% of all cache refs    ", ratio);

	} else if (perf_evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_FRONTEND)) {
		print_stalled_cycles_frontend(out, cpu, evsel, avg);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_BACKEND)) {
		print_stalled_cycles_backend(out, cpu, evsel, avg);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_CPU_CYCLES)) {
		total = avg_stats(&runtime_nsecs_stats[cpu]);

		if (total) {
			ratio = avg / total;
			fprintf(out, " # %8.3f GHz                    ", ratio);
		} else {
			fprintf(out, "                                   ");
		}
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX)) {
		total = avg_stats(&runtime_cycles_stats[ctx][cpu]);
		if (total)
			fprintf(out,
				" #   %5.2f%% transactional cycles   ",
				100.0 * (avg / total));
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX_CP)) {
		total = avg_stats(&runtime_cycles_stats[ctx][cpu]);
		total2 = avg_stats(&runtime_cycles_in_tx_stats[ctx][cpu]);
		if (total2 < avg)
			total2 = avg;
		if (total)
			fprintf(out,
				" #   %5.2f%% aborted cycles         ",
				100.0 * ((total2-avg) / total));
	} else if (perf_stat_evsel__is(evsel, TRANSACTION_START) &&
		   runtime_cycles_in_tx_stats[ctx][cpu].n != 0) {
		total = avg_stats(&runtime_cycles_in_tx_stats[ctx][cpu]);

		if (avg)
			ratio = total / avg;

		fprintf(out, " # %8.0f cycles / transaction   ", ratio);
	} else if (perf_stat_evsel__is(evsel, ELISION_START) &&
		   runtime_cycles_in_tx_stats[ctx][cpu].n != 0) {
		total = avg_stats(&runtime_cycles_in_tx_stats[ctx][cpu]);

		if (avg)
			ratio = total / avg;

		fprintf(out, " # %8.0f cycles / elision       ", ratio);
	} else if (runtime_nsecs_stats[cpu].n != 0) {
		char unit = 'M';

		total = avg_stats(&runtime_nsecs_stats[cpu]);

		if (total)
			ratio = 1000.0 * avg / total;
		if (ratio < 0.001) {
			ratio *= 1000;
			unit = 'K';
		}

		fprintf(out, " # %8.3f %c/sec                  ", ratio, unit);
	} else {
		fprintf(out, "                                   ");
	}
}
