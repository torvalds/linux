#include <stdio.h>
#include "evsel.h"
#include "stat.h"
#include "color.h"
#include "pmu.h"

enum {
	CTX_BIT_USER	= 1 << 0,
	CTX_BIT_KERNEL	= 1 << 1,
	CTX_BIT_HV	= 1 << 2,
	CTX_BIT_HOST	= 1 << 3,
	CTX_BIT_IDLE	= 1 << 4,
	CTX_BIT_MAX	= 1 << 5,
};

#define NUM_CTX CTX_BIT_MAX

/*
 * AGGR_GLOBAL: Use CPU 0
 * AGGR_SOCKET: Use first CPU of socket
 * AGGR_CORE: Use first CPU of core
 * AGGR_NONE: Use matching CPU
 * AGGR_THREAD: Not supported?
 */
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
static bool have_frontend_stalled;

struct stats walltime_nsecs_stats;

void perf_stat__init_shadow_stats(void)
{
	have_frontend_stalled = pmu_have_event("cpu", "stalled-cycles-frontend");
}

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

static void print_stalled_cycles_frontend(int cpu,
					  struct perf_evsel *evsel, double avg,
					  struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_cycles_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_FE, ratio);

	if (ratio)
		out->print_metric(out->ctx, color, "%7.2f%%", "frontend cycles idle",
				  ratio);
	else
		out->print_metric(out->ctx, NULL, NULL, "frontend cycles idle", 0);
}

static void print_stalled_cycles_backend(int cpu,
					 struct perf_evsel *evsel, double avg,
					 struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_cycles_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_BE, ratio);

	out->print_metric(out->ctx, color, "%6.2f%%", "backend cycles idle", ratio);
}

static void print_branch_misses(int cpu,
				struct perf_evsel *evsel,
				double avg,
				struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_branches_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	out->print_metric(out->ctx, color, "%7.2f%%", "of all branches", ratio);
}

static void print_l1_dcache_misses(int cpu,
				   struct perf_evsel *evsel,
				   double avg,
				   struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_l1_dcache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	out->print_metric(out->ctx, color, "%7.2f%%", "of all L1-dcache hits", ratio);
}

static void print_l1_icache_misses(int cpu,
				   struct perf_evsel *evsel,
				   double avg,
				   struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_l1_icache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(out->ctx, color, "%7.2f%%", "of all L1-icache hits", ratio);
}

static void print_dtlb_cache_misses(int cpu,
				    struct perf_evsel *evsel,
				    double avg,
				    struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_dtlb_cache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(out->ctx, color, "%7.2f%%", "of all dTLB cache hits", ratio);
}

static void print_itlb_cache_misses(int cpu,
				    struct perf_evsel *evsel,
				    double avg,
				    struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_itlb_cache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(out->ctx, color, "%7.2f%%", "of all iTLB cache hits", ratio);
}

static void print_ll_cache_misses(int cpu,
				  struct perf_evsel *evsel,
				  double avg,
				  struct perf_stat_output_ctx *out)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = avg_stats(&runtime_ll_cache_stats[ctx][cpu]);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(out->ctx, color, "%7.2f%%", "of all LL-cache hits", ratio);
}

void perf_stat__print_shadow_stats(struct perf_evsel *evsel,
				   double avg, int cpu,
				   struct perf_stat_output_ctx *out)
{
	void *ctxp = out->ctx;
	print_metric_t print_metric = out->print_metric;
	double total, ratio = 0.0, total2;
	int ctx = evsel_context(evsel);

	if (perf_evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS)) {
		total = avg_stats(&runtime_cycles_stats[ctx][cpu]);
		if (total) {
			ratio = avg / total;
			print_metric(ctxp, NULL, "%7.2f ",
					"insn per cycle", ratio);
		} else {
			print_metric(ctxp, NULL, NULL, "insn per cycle", 0);
		}
		total = avg_stats(&runtime_stalled_cycles_front_stats[ctx][cpu]);
		total = max(total, avg_stats(&runtime_stalled_cycles_back_stats[ctx][cpu]));

		if (total && avg) {
			out->new_line(ctxp);
			ratio = total / avg;
			print_metric(ctxp, NULL, "%7.2f ",
					"stalled cycles per insn",
					ratio);
		} else if (have_frontend_stalled) {
			print_metric(ctxp, NULL, NULL,
				     "stalled cycles per insn", 0);
		}
	} else if (perf_evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES)) {
		if (runtime_branches_stats[ctx][cpu].n != 0)
			print_branch_misses(cpu, evsel, avg, out);
		else
			print_metric(ctxp, NULL, NULL, "of all branches", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_L1D |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {
		if (runtime_l1_dcache_stats[ctx][cpu].n != 0)
			print_l1_dcache_misses(cpu, evsel, avg, out);
		else
			print_metric(ctxp, NULL, NULL, "of all L1-dcache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_L1I |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {
		if (runtime_l1_icache_stats[ctx][cpu].n != 0)
			print_l1_icache_misses(cpu, evsel, avg, out);
		else
			print_metric(ctxp, NULL, NULL, "of all L1-icache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_DTLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {
		if (runtime_dtlb_cache_stats[ctx][cpu].n != 0)
			print_dtlb_cache_misses(cpu, evsel, avg, out);
		else
			print_metric(ctxp, NULL, NULL, "of all dTLB cache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_ITLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {
		if (runtime_itlb_cache_stats[ctx][cpu].n != 0)
			print_itlb_cache_misses(cpu, evsel, avg, out);
		else
			print_metric(ctxp, NULL, NULL, "of all iTLB cache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_LL |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {
		if (runtime_ll_cache_stats[ctx][cpu].n != 0)
			print_ll_cache_misses(cpu, evsel, avg, out);
		else
			print_metric(ctxp, NULL, NULL, "of all LL-cache hits", 0);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_CACHE_MISSES)) {
		total = avg_stats(&runtime_cacherefs_stats[ctx][cpu]);

		if (total)
			ratio = avg * 100 / total;

		if (runtime_cacherefs_stats[ctx][cpu].n != 0)
			print_metric(ctxp, NULL, "%8.3f %%",
				     "of all cache refs", ratio);
		else
			print_metric(ctxp, NULL, NULL, "of all cache refs", 0);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_FRONTEND)) {
		print_stalled_cycles_frontend(cpu, evsel, avg, out);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_BACKEND)) {
		print_stalled_cycles_backend(cpu, evsel, avg, out);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_CPU_CYCLES)) {
		total = avg_stats(&runtime_nsecs_stats[cpu]);

		if (total) {
			ratio = avg / total;
			print_metric(ctxp, NULL, "%8.3f", "GHz", ratio);
		} else {
			print_metric(ctxp, NULL, NULL, "Ghz", 0);
		}
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX)) {
		total = avg_stats(&runtime_cycles_stats[ctx][cpu]);
		if (total)
			print_metric(ctxp, NULL,
					"%7.2f%%", "transactional cycles",
					100.0 * (avg / total));
		else
			print_metric(ctxp, NULL, NULL, "transactional cycles",
				     0);
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX_CP)) {
		total = avg_stats(&runtime_cycles_stats[ctx][cpu]);
		total2 = avg_stats(&runtime_cycles_in_tx_stats[ctx][cpu]);
		if (total2 < avg)
			total2 = avg;
		if (total)
			print_metric(ctxp, NULL, "%7.2f%%", "aborted cycles",
				100.0 * ((total2-avg) / total));
		else
			print_metric(ctxp, NULL, NULL, "aborted cycles", 0);
	} else if (perf_stat_evsel__is(evsel, TRANSACTION_START)) {
		total = avg_stats(&runtime_cycles_in_tx_stats[ctx][cpu]);

		if (avg)
			ratio = total / avg;

		if (runtime_cycles_in_tx_stats[ctx][cpu].n != 0)
			print_metric(ctxp, NULL, "%8.0f",
				     "cycles / transaction", ratio);
		else
			print_metric(ctxp, NULL, NULL, "cycles / transaction",
				     0);
	} else if (perf_stat_evsel__is(evsel, ELISION_START)) {
		total = avg_stats(&runtime_cycles_in_tx_stats[ctx][cpu]);

		if (avg)
			ratio = total / avg;

		print_metric(ctxp, NULL, "%8.0f", "cycles / elision", ratio);
	} else if (perf_evsel__match(evsel, SOFTWARE, SW_TASK_CLOCK)) {
		if ((ratio = avg_stats(&walltime_nsecs_stats)) != 0)
			print_metric(ctxp, NULL, "%8.3f", "CPUs utilized",
				     avg / ratio);
		else
			print_metric(ctxp, NULL, NULL, "CPUs utilized", 0);
	} else if (runtime_nsecs_stats[cpu].n != 0) {
		char unit = 'M';
		char unit_buf[10];

		total = avg_stats(&runtime_nsecs_stats[cpu]);

		if (total)
			ratio = 1000.0 * avg / total;
		if (ratio < 0.001) {
			ratio *= 1000;
			unit = 'K';
		}
		snprintf(unit_buf, sizeof(unit_buf), "%c/sec", unit);
		print_metric(ctxp, NULL, "%8.3f", unit_buf, ratio);
	} else {
		print_metric(ctxp, NULL, NULL, NULL, 0);
	}
}
