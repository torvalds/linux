#include <stdio.h>
#include "evsel.h"
#include "stat.h"
#include "color.h"
#include "pmu.h"
#include "rblist.h"
#include "evlist.h"
#include "expr.h"

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
static struct stats runtime_topdown_total_slots[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_topdown_slots_issued[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_topdown_slots_retired[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_topdown_fetch_bubbles[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_topdown_recovery_bubbles[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_smi_num_stats[NUM_CTX][MAX_NR_CPUS];
static struct stats runtime_aperf_stats[NUM_CTX][MAX_NR_CPUS];
static struct rblist runtime_saved_values;
static bool have_frontend_stalled;

struct stats walltime_nsecs_stats;

struct saved_value {
	struct rb_node rb_node;
	struct perf_evsel *evsel;
	int cpu;
	int ctx;
	struct stats stats;
};

static int saved_value_cmp(struct rb_node *rb_node, const void *entry)
{
	struct saved_value *a = container_of(rb_node,
					     struct saved_value,
					     rb_node);
	const struct saved_value *b = entry;

	if (a->ctx != b->ctx)
		return a->ctx - b->ctx;
	if (a->cpu != b->cpu)
		return a->cpu - b->cpu;
	return a->evsel - b->evsel;
}

static struct rb_node *saved_value_new(struct rblist *rblist __maybe_unused,
				     const void *entry)
{
	struct saved_value *nd = malloc(sizeof(struct saved_value));

	if (!nd)
		return NULL;
	memcpy(nd, entry, sizeof(struct saved_value));
	return &nd->rb_node;
}

static struct saved_value *saved_value_lookup(struct perf_evsel *evsel,
					      int cpu, int ctx,
					      bool create)
{
	struct rb_node *nd;
	struct saved_value dm = {
		.cpu = cpu,
		.ctx = ctx,
		.evsel = evsel,
	};
	nd = rblist__find(&runtime_saved_values, &dm);
	if (nd)
		return container_of(nd, struct saved_value, rb_node);
	if (create) {
		rblist__add_node(&runtime_saved_values, &dm);
		nd = rblist__find(&runtime_saved_values, &dm);
		if (nd)
			return container_of(nd, struct saved_value, rb_node);
	}
	return NULL;
}

void perf_stat__init_shadow_stats(void)
{
	have_frontend_stalled = pmu_have_event("cpu", "stalled-cycles-frontend");
	rblist__init(&runtime_saved_values);
	runtime_saved_values.node_cmp = saved_value_cmp;
	runtime_saved_values.node_new = saved_value_new;
	/* No delete for now */
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
	struct rb_node *pos, *next;

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
	memset(runtime_topdown_total_slots, 0, sizeof(runtime_topdown_total_slots));
	memset(runtime_topdown_slots_retired, 0, sizeof(runtime_topdown_slots_retired));
	memset(runtime_topdown_slots_issued, 0, sizeof(runtime_topdown_slots_issued));
	memset(runtime_topdown_fetch_bubbles, 0, sizeof(runtime_topdown_fetch_bubbles));
	memset(runtime_topdown_recovery_bubbles, 0, sizeof(runtime_topdown_recovery_bubbles));
	memset(runtime_smi_num_stats, 0, sizeof(runtime_smi_num_stats));
	memset(runtime_aperf_stats, 0, sizeof(runtime_aperf_stats));

	next = rb_first(&runtime_saved_values.entries);
	while (next) {
		pos = next;
		next = rb_next(pos);
		memset(&container_of(pos, struct saved_value, rb_node)->stats,
		       0,
		       sizeof(struct stats));
	}
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

	if (perf_evsel__match(counter, SOFTWARE, SW_TASK_CLOCK) ||
	    perf_evsel__match(counter, SOFTWARE, SW_CPU_CLOCK))
		update_stats(&runtime_nsecs_stats[cpu], count[0]);
	else if (perf_evsel__match(counter, HARDWARE, HW_CPU_CYCLES))
		update_stats(&runtime_cycles_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, CYCLES_IN_TX))
		update_stats(&runtime_cycles_in_tx_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, TRANSACTION_START))
		update_stats(&runtime_transaction_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, ELISION_START))
		update_stats(&runtime_elision_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, TOPDOWN_TOTAL_SLOTS))
		update_stats(&runtime_topdown_total_slots[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, TOPDOWN_SLOTS_ISSUED))
		update_stats(&runtime_topdown_slots_issued[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, TOPDOWN_SLOTS_RETIRED))
		update_stats(&runtime_topdown_slots_retired[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, TOPDOWN_FETCH_BUBBLES))
		update_stats(&runtime_topdown_fetch_bubbles[ctx][cpu],count[0]);
	else if (perf_stat_evsel__is(counter, TOPDOWN_RECOVERY_BUBBLES))
		update_stats(&runtime_topdown_recovery_bubbles[ctx][cpu], count[0]);
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
	else if (perf_stat_evsel__is(counter, SMI_NUM))
		update_stats(&runtime_smi_num_stats[ctx][cpu], count[0]);
	else if (perf_stat_evsel__is(counter, APERF))
		update_stats(&runtime_aperf_stats[ctx][cpu], count[0]);

	if (counter->collect_stat) {
		struct saved_value *v = saved_value_lookup(counter, cpu, ctx,
							   true);
		update_stats(&v->stats, count[0]);
	}
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

static struct perf_evsel *perf_stat__find_event(struct perf_evlist *evsel_list,
						const char *name)
{
	struct perf_evsel *c2;

	evlist__for_each_entry (evsel_list, c2) {
		if (!strcasecmp(c2->name, name))
			return c2;
	}
	return NULL;
}

/* Mark MetricExpr target events and link events using them to them. */
void perf_stat__collect_metric_expr(struct perf_evlist *evsel_list)
{
	struct perf_evsel *counter, *leader, **metric_events, *oc;
	bool found;
	const char **metric_names;
	int i;
	int num_metric_names;

	evlist__for_each_entry(evsel_list, counter) {
		bool invalid = false;

		leader = counter->leader;
		if (!counter->metric_expr)
			continue;
		metric_events = counter->metric_events;
		if (!metric_events) {
			if (expr__find_other(counter->metric_expr, counter->name,
						&metric_names, &num_metric_names) < 0)
				continue;

			metric_events = calloc(sizeof(struct perf_evsel *),
					       num_metric_names + 1);
			if (!metric_events)
				return;
			counter->metric_events = metric_events;
		}

		for (i = 0; i < num_metric_names; i++) {
			found = false;
			if (leader) {
				/* Search in group */
				for_each_group_member (oc, leader) {
					if (!strcasecmp(oc->name, metric_names[i])) {
						found = true;
						break;
					}
				}
			}
			if (!found) {
				/* Search ignoring groups */
				oc = perf_stat__find_event(evsel_list, metric_names[i]);
			}
			if (!oc) {
				/* Deduping one is good enough to handle duplicated PMUs. */
				static char *printed;

				/*
				 * Adding events automatically would be difficult, because
				 * it would risk creating groups that are not schedulable.
				 * perf stat doesn't understand all the scheduling constraints
				 * of events. So we ask the user instead to add the missing
				 * events.
				 */
				if (!printed || strcasecmp(printed, metric_names[i])) {
					fprintf(stderr,
						"Add %s event to groups to get metric expression for %s\n",
						metric_names[i],
						counter->name);
					printed = strdup(metric_names[i]);
				}
				invalid = true;
				continue;
			}
			metric_events[i] = oc;
			oc->collect_stat = true;
		}
		metric_events[i] = NULL;
		free(metric_names);
		if (invalid) {
			free(metric_events);
			counter->metric_events = NULL;
			counter->metric_expr = NULL;
		}
	}
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

	out->print_metric(out->ctx, color, "%7.2f%%", "backend cycles idle", ratio);
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

/*
 * High level "TopDown" CPU core pipe line bottleneck break down.
 *
 * Basic concept following
 * Yasin, A Top Down Method for Performance analysis and Counter architecture
 * ISPASS14
 *
 * The CPU pipeline is divided into 4 areas that can be bottlenecks:
 *
 * Frontend -> Backend -> Retiring
 * BadSpeculation in addition means out of order execution that is thrown away
 * (for example branch mispredictions)
 * Frontend is instruction decoding.
 * Backend is execution, like computation and accessing data in memory
 * Retiring is good execution that is not directly bottlenecked
 *
 * The formulas are computed in slots.
 * A slot is an entry in the pipeline each for the pipeline width
 * (for example a 4-wide pipeline has 4 slots for each cycle)
 *
 * Formulas:
 * BadSpeculation = ((SlotsIssued - SlotsRetired) + RecoveryBubbles) /
 *			TotalSlots
 * Retiring = SlotsRetired / TotalSlots
 * FrontendBound = FetchBubbles / TotalSlots
 * BackendBound = 1.0 - BadSpeculation - Retiring - FrontendBound
 *
 * The kernel provides the mapping to the low level CPU events and any scaling
 * needed for the CPU pipeline width, for example:
 *
 * TotalSlots = Cycles * 4
 *
 * The scaling factor is communicated in the sysfs unit.
 *
 * In some cases the CPU may not be able to measure all the formulas due to
 * missing events. In this case multiple formulas are combined, as possible.
 *
 * Full TopDown supports more levels to sub-divide each area: for example
 * BackendBound into computing bound and memory bound. For now we only
 * support Level 1 TopDown.
 */

static double sanitize_val(double x)
{
	if (x < 0 && x >= -0.02)
		return 0.0;
	return x;
}

static double td_total_slots(int ctx, int cpu)
{
	return avg_stats(&runtime_topdown_total_slots[ctx][cpu]);
}

static double td_bad_spec(int ctx, int cpu)
{
	double bad_spec = 0;
	double total_slots;
	double total;

	total = avg_stats(&runtime_topdown_slots_issued[ctx][cpu]) -
		avg_stats(&runtime_topdown_slots_retired[ctx][cpu]) +
		avg_stats(&runtime_topdown_recovery_bubbles[ctx][cpu]);
	total_slots = td_total_slots(ctx, cpu);
	if (total_slots)
		bad_spec = total / total_slots;
	return sanitize_val(bad_spec);
}

static double td_retiring(int ctx, int cpu)
{
	double retiring = 0;
	double total_slots = td_total_slots(ctx, cpu);
	double ret_slots = avg_stats(&runtime_topdown_slots_retired[ctx][cpu]);

	if (total_slots)
		retiring = ret_slots / total_slots;
	return retiring;
}

static double td_fe_bound(int ctx, int cpu)
{
	double fe_bound = 0;
	double total_slots = td_total_slots(ctx, cpu);
	double fetch_bub = avg_stats(&runtime_topdown_fetch_bubbles[ctx][cpu]);

	if (total_slots)
		fe_bound = fetch_bub / total_slots;
	return fe_bound;
}

static double td_be_bound(int ctx, int cpu)
{
	double sum = (td_fe_bound(ctx, cpu) +
		      td_bad_spec(ctx, cpu) +
		      td_retiring(ctx, cpu));
	if (sum == 0)
		return 0;
	return sanitize_val(1.0 - sum);
}

static void print_smi_cost(int cpu, struct perf_evsel *evsel,
			   struct perf_stat_output_ctx *out)
{
	double smi_num, aperf, cycles, cost = 0.0;
	int ctx = evsel_context(evsel);
	const char *color = NULL;

	smi_num = avg_stats(&runtime_smi_num_stats[ctx][cpu]);
	aperf = avg_stats(&runtime_aperf_stats[ctx][cpu]);
	cycles = avg_stats(&runtime_cycles_stats[ctx][cpu]);

	if ((cycles == 0) || (aperf == 0))
		return;

	if (smi_num)
		cost = (aperf - cycles) / aperf * 100.00;

	if (cost > 10)
		color = PERF_COLOR_RED;
	out->print_metric(out->ctx, color, "%8.1f%%", "SMI cycles%", cost);
	out->print_metric(out->ctx, NULL, "%4.0f", "SMI#", smi_num);
}

void perf_stat__print_shadow_stats(struct perf_evsel *evsel,
				   double avg, int cpu,
				   struct perf_stat_output_ctx *out)
{
	void *ctxp = out->ctx;
	print_metric_t print_metric = out->print_metric;
	double total, ratio = 0.0, total2;
	const char *color = NULL;
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
	} else if (perf_evsel__match(evsel, SOFTWARE, SW_TASK_CLOCK) ||
		   perf_evsel__match(evsel, SOFTWARE, SW_CPU_CLOCK)) {
		if ((ratio = avg_stats(&walltime_nsecs_stats)) != 0)
			print_metric(ctxp, NULL, "%8.3f", "CPUs utilized",
				     avg / ratio);
		else
			print_metric(ctxp, NULL, NULL, "CPUs utilized", 0);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_FETCH_BUBBLES)) {
		double fe_bound = td_fe_bound(ctx, cpu);

		if (fe_bound > 0.2)
			color = PERF_COLOR_RED;
		print_metric(ctxp, color, "%8.1f%%", "frontend bound",
				fe_bound * 100.);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_SLOTS_RETIRED)) {
		double retiring = td_retiring(ctx, cpu);

		if (retiring > 0.7)
			color = PERF_COLOR_GREEN;
		print_metric(ctxp, color, "%8.1f%%", "retiring",
				retiring * 100.);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_RECOVERY_BUBBLES)) {
		double bad_spec = td_bad_spec(ctx, cpu);

		if (bad_spec > 0.1)
			color = PERF_COLOR_RED;
		print_metric(ctxp, color, "%8.1f%%", "bad speculation",
				bad_spec * 100.);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_SLOTS_ISSUED)) {
		double be_bound = td_be_bound(ctx, cpu);
		const char *name = "backend bound";
		static int have_recovery_bubbles = -1;

		/* In case the CPU does not support topdown-recovery-bubbles */
		if (have_recovery_bubbles < 0)
			have_recovery_bubbles = pmu_have_event("cpu",
					"topdown-recovery-bubbles");
		if (!have_recovery_bubbles)
			name = "backend bound/bad spec";

		if (be_bound > 0.2)
			color = PERF_COLOR_RED;
		if (td_total_slots(ctx, cpu) > 0)
			print_metric(ctxp, color, "%8.1f%%", name,
					be_bound * 100.);
		else
			print_metric(ctxp, NULL, NULL, name, 0);
	} else if (evsel->metric_expr) {
		struct parse_ctx pctx;
		int i;

		expr__ctx_init(&pctx);
		expr__add_id(&pctx, evsel->name, avg);
		for (i = 0; evsel->metric_events[i]; i++) {
			struct saved_value *v;

			v = saved_value_lookup(evsel->metric_events[i], cpu, ctx, false);
			if (!v)
				break;
			expr__add_id(&pctx, evsel->metric_events[i]->name,
					     avg_stats(&v->stats));
		}
		if (!evsel->metric_events[i]) {
			const char *p = evsel->metric_expr;

			if (expr__parse(&ratio, &pctx, &p) == 0)
				print_metric(ctxp, NULL, "%8.1f",
					evsel->metric_name ?
					evsel->metric_name :
					out->force_header ?  evsel->name : "",
					ratio);
			else
				print_metric(ctxp, NULL, NULL, "", 0);
		} else
			print_metric(ctxp, NULL, NULL, "", 0);
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
	} else if (perf_stat_evsel__is(evsel, SMI_NUM)) {
		print_smi_cost(cpu, evsel, out);
	} else {
		print_metric(ctxp, NULL, NULL, NULL, 0);
	}
}
