// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include "evsel.h"
#include "stat.h"
#include "color.h"
#include "pmu.h"
#include "rblist.h"
#include "evlist.h"
#include "expr.h"
#include "metricgroup.h"
#include <linux/zalloc.h>

/*
 * AGGR_GLOBAL: Use CPU 0
 * AGGR_SOCKET: Use first CPU of socket
 * AGGR_DIE: Use first CPU of die
 * AGGR_CORE: Use first CPU of core
 * AGGR_NONE: Use matching CPU
 * AGGR_THREAD: Not supported?
 */
static bool have_frontend_stalled;

struct runtime_stat rt_stat;
struct stats walltime_nsecs_stats;

struct saved_value {
	struct rb_node rb_node;
	struct perf_evsel *evsel;
	enum stat_type type;
	int ctx;
	int cpu;
	struct runtime_stat *stat;
	struct stats stats;
};

static int saved_value_cmp(struct rb_node *rb_node, const void *entry)
{
	struct saved_value *a = container_of(rb_node,
					     struct saved_value,
					     rb_node);
	const struct saved_value *b = entry;

	if (a->cpu != b->cpu)
		return a->cpu - b->cpu;

	/*
	 * Previously the rbtree was used to link generic metrics.
	 * The keys were evsel/cpu. Now the rbtree is extended to support
	 * per-thread shadow stats. For shadow stats case, the keys
	 * are cpu/type/ctx/stat (evsel is NULL). For generic metrics
	 * case, the keys are still evsel/cpu (type/ctx/stat are 0 or NULL).
	 */
	if (a->type != b->type)
		return a->type - b->type;

	if (a->ctx != b->ctx)
		return a->ctx - b->ctx;

	if (a->evsel == NULL && b->evsel == NULL) {
		if (a->stat == b->stat)
			return 0;

		if ((char *)a->stat < (char *)b->stat)
			return -1;

		return 1;
	}

	if (a->evsel == b->evsel)
		return 0;
	if ((char *)a->evsel < (char *)b->evsel)
		return -1;
	return +1;
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

static void saved_value_delete(struct rblist *rblist __maybe_unused,
			       struct rb_node *rb_node)
{
	struct saved_value *v;

	BUG_ON(!rb_node);
	v = container_of(rb_node, struct saved_value, rb_node);
	free(v);
}

static struct saved_value *saved_value_lookup(struct perf_evsel *evsel,
					      int cpu,
					      bool create,
					      enum stat_type type,
					      int ctx,
					      struct runtime_stat *st)
{
	struct rblist *rblist;
	struct rb_node *nd;
	struct saved_value dm = {
		.cpu = cpu,
		.evsel = evsel,
		.type = type,
		.ctx = ctx,
		.stat = st,
	};

	rblist = &st->value_list;

	nd = rblist__find(rblist, &dm);
	if (nd)
		return container_of(nd, struct saved_value, rb_node);
	if (create) {
		rblist__add_node(rblist, &dm);
		nd = rblist__find(rblist, &dm);
		if (nd)
			return container_of(nd, struct saved_value, rb_node);
	}
	return NULL;
}

void runtime_stat__init(struct runtime_stat *st)
{
	struct rblist *rblist = &st->value_list;

	rblist__init(rblist);
	rblist->node_cmp = saved_value_cmp;
	rblist->node_new = saved_value_new;
	rblist->node_delete = saved_value_delete;
}

void runtime_stat__exit(struct runtime_stat *st)
{
	rblist__exit(&st->value_list);
}

void perf_stat__init_shadow_stats(void)
{
	have_frontend_stalled = pmu_have_event("cpu", "stalled-cycles-frontend");
	runtime_stat__init(&rt_stat);
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

static void reset_stat(struct runtime_stat *st)
{
	struct rblist *rblist;
	struct rb_node *pos, *next;

	rblist = &st->value_list;
	next = rb_first_cached(&rblist->entries);
	while (next) {
		pos = next;
		next = rb_next(pos);
		memset(&container_of(pos, struct saved_value, rb_node)->stats,
		       0,
		       sizeof(struct stats));
	}
}

void perf_stat__reset_shadow_stats(void)
{
	reset_stat(&rt_stat);
	memset(&walltime_nsecs_stats, 0, sizeof(walltime_nsecs_stats));
}

void perf_stat__reset_shadow_per_stat(struct runtime_stat *st)
{
	reset_stat(st);
}

static void update_runtime_stat(struct runtime_stat *st,
				enum stat_type type,
				int ctx, int cpu, u64 count)
{
	struct saved_value *v = saved_value_lookup(NULL, cpu, true,
						   type, ctx, st);

	if (v)
		update_stats(&v->stats, count);
}

/*
 * Update various tracking values we maintain to print
 * more semantic information such as miss/hit ratios,
 * instruction rates, etc:
 */
void perf_stat__update_shadow_stats(struct perf_evsel *counter, u64 count,
				    int cpu, struct runtime_stat *st)
{
	int ctx = evsel_context(counter);
	u64 count_ns = count;

	count *= counter->scale;

	if (perf_evsel__is_clock(counter))
		update_runtime_stat(st, STAT_NSECS, 0, cpu, count_ns);
	else if (perf_evsel__match(counter, HARDWARE, HW_CPU_CYCLES))
		update_runtime_stat(st, STAT_CYCLES, ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, CYCLES_IN_TX))
		update_runtime_stat(st, STAT_CYCLES_IN_TX, ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, TRANSACTION_START))
		update_runtime_stat(st, STAT_TRANSACTION, ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, ELISION_START))
		update_runtime_stat(st, STAT_ELISION, ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, TOPDOWN_TOTAL_SLOTS))
		update_runtime_stat(st, STAT_TOPDOWN_TOTAL_SLOTS,
				    ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, TOPDOWN_SLOTS_ISSUED))
		update_runtime_stat(st, STAT_TOPDOWN_SLOTS_ISSUED,
				    ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, TOPDOWN_SLOTS_RETIRED))
		update_runtime_stat(st, STAT_TOPDOWN_SLOTS_RETIRED,
				    ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, TOPDOWN_FETCH_BUBBLES))
		update_runtime_stat(st, STAT_TOPDOWN_FETCH_BUBBLES,
				    ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, TOPDOWN_RECOVERY_BUBBLES))
		update_runtime_stat(st, STAT_TOPDOWN_RECOVERY_BUBBLES,
				    ctx, cpu, count);
	else if (perf_evsel__match(counter, HARDWARE, HW_STALLED_CYCLES_FRONTEND))
		update_runtime_stat(st, STAT_STALLED_CYCLES_FRONT,
				    ctx, cpu, count);
	else if (perf_evsel__match(counter, HARDWARE, HW_STALLED_CYCLES_BACKEND))
		update_runtime_stat(st, STAT_STALLED_CYCLES_BACK,
				    ctx, cpu, count);
	else if (perf_evsel__match(counter, HARDWARE, HW_BRANCH_INSTRUCTIONS))
		update_runtime_stat(st, STAT_BRANCHES, ctx, cpu, count);
	else if (perf_evsel__match(counter, HARDWARE, HW_CACHE_REFERENCES))
		update_runtime_stat(st, STAT_CACHEREFS, ctx, cpu, count);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_L1D))
		update_runtime_stat(st, STAT_L1_DCACHE, ctx, cpu, count);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_L1I))
		update_runtime_stat(st, STAT_L1_ICACHE, ctx, cpu, count);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_LL))
		update_runtime_stat(st, STAT_LL_CACHE, ctx, cpu, count);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_DTLB))
		update_runtime_stat(st, STAT_DTLB_CACHE, ctx, cpu, count);
	else if (perf_evsel__match(counter, HW_CACHE, HW_CACHE_ITLB))
		update_runtime_stat(st, STAT_ITLB_CACHE, ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, SMI_NUM))
		update_runtime_stat(st, STAT_SMI_NUM, ctx, cpu, count);
	else if (perf_stat_evsel__is(counter, APERF))
		update_runtime_stat(st, STAT_APERF, ctx, cpu, count);

	if (counter->collect_stat) {
		struct saved_value *v = saved_value_lookup(counter, cpu, true,
							   STAT_NONE, 0, st);
		update_stats(&v->stats, count);
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
		if (!strcasecmp(c2->name, name) && !c2->collect_stat)
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
					if (!strcasecmp(oc->name, metric_names[i]) &&
						!oc->collect_stat) {
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

static double runtime_stat_avg(struct runtime_stat *st,
			       enum stat_type type, int ctx, int cpu)
{
	struct saved_value *v;

	v = saved_value_lookup(NULL, cpu, false, type, ctx, st);
	if (!v)
		return 0.0;

	return avg_stats(&v->stats);
}

static double runtime_stat_n(struct runtime_stat *st,
			     enum stat_type type, int ctx, int cpu)
{
	struct saved_value *v;

	v = saved_value_lookup(NULL, cpu, false, type, ctx, st);
	if (!v)
		return 0.0;

	return v->stats.n;
}

static void print_stalled_cycles_frontend(struct perf_stat_config *config,
					  int cpu,
					  struct perf_evsel *evsel, double avg,
					  struct perf_stat_output_ctx *out,
					  struct runtime_stat *st)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_CYCLES, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_FE, ratio);

	if (ratio)
		out->print_metric(config, out->ctx, color, "%7.2f%%", "frontend cycles idle",
				  ratio);
	else
		out->print_metric(config, out->ctx, NULL, NULL, "frontend cycles idle", 0);
}

static void print_stalled_cycles_backend(struct perf_stat_config *config,
					 int cpu,
					 struct perf_evsel *evsel, double avg,
					 struct perf_stat_output_ctx *out,
					 struct runtime_stat *st)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_CYCLES, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_STALLED_CYCLES_BE, ratio);

	out->print_metric(config, out->ctx, color, "%7.2f%%", "backend cycles idle", ratio);
}

static void print_branch_misses(struct perf_stat_config *config,
				int cpu,
				struct perf_evsel *evsel,
				double avg,
				struct perf_stat_output_ctx *out,
				struct runtime_stat *st)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_BRANCHES, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all branches", ratio);
}

static void print_l1_dcache_misses(struct perf_stat_config *config,
				   int cpu,
				   struct perf_evsel *evsel,
				   double avg,
				   struct perf_stat_output_ctx *out,
				   struct runtime_stat *st)

{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_L1_DCACHE, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);

	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all L1-dcache hits", ratio);
}

static void print_l1_icache_misses(struct perf_stat_config *config,
				   int cpu,
				   struct perf_evsel *evsel,
				   double avg,
				   struct perf_stat_output_ctx *out,
				   struct runtime_stat *st)

{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_L1_ICACHE, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all L1-icache hits", ratio);
}

static void print_dtlb_cache_misses(struct perf_stat_config *config,
				    int cpu,
				    struct perf_evsel *evsel,
				    double avg,
				    struct perf_stat_output_ctx *out,
				    struct runtime_stat *st)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_DTLB_CACHE, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all dTLB cache hits", ratio);
}

static void print_itlb_cache_misses(struct perf_stat_config *config,
				    int cpu,
				    struct perf_evsel *evsel,
				    double avg,
				    struct perf_stat_output_ctx *out,
				    struct runtime_stat *st)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_ITLB_CACHE, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all iTLB cache hits", ratio);
}

static void print_ll_cache_misses(struct perf_stat_config *config,
				  int cpu,
				  struct perf_evsel *evsel,
				  double avg,
				  struct perf_stat_output_ctx *out,
				  struct runtime_stat *st)
{
	double total, ratio = 0.0;
	const char *color;
	int ctx = evsel_context(evsel);

	total = runtime_stat_avg(st, STAT_LL_CACHE, ctx, cpu);

	if (total)
		ratio = avg / total * 100.0;

	color = get_ratio_color(GRC_CACHE_MISSES, ratio);
	out->print_metric(config, out->ctx, color, "%7.2f%%", "of all LL-cache hits", ratio);
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

static double td_total_slots(int ctx, int cpu, struct runtime_stat *st)
{
	return runtime_stat_avg(st, STAT_TOPDOWN_TOTAL_SLOTS, ctx, cpu);
}

static double td_bad_spec(int ctx, int cpu, struct runtime_stat *st)
{
	double bad_spec = 0;
	double total_slots;
	double total;

	total = runtime_stat_avg(st, STAT_TOPDOWN_SLOTS_ISSUED, ctx, cpu) -
		runtime_stat_avg(st, STAT_TOPDOWN_SLOTS_RETIRED, ctx, cpu) +
		runtime_stat_avg(st, STAT_TOPDOWN_RECOVERY_BUBBLES, ctx, cpu);

	total_slots = td_total_slots(ctx, cpu, st);
	if (total_slots)
		bad_spec = total / total_slots;
	return sanitize_val(bad_spec);
}

static double td_retiring(int ctx, int cpu, struct runtime_stat *st)
{
	double retiring = 0;
	double total_slots = td_total_slots(ctx, cpu, st);
	double ret_slots = runtime_stat_avg(st, STAT_TOPDOWN_SLOTS_RETIRED,
					    ctx, cpu);

	if (total_slots)
		retiring = ret_slots / total_slots;
	return retiring;
}

static double td_fe_bound(int ctx, int cpu, struct runtime_stat *st)
{
	double fe_bound = 0;
	double total_slots = td_total_slots(ctx, cpu, st);
	double fetch_bub = runtime_stat_avg(st, STAT_TOPDOWN_FETCH_BUBBLES,
					    ctx, cpu);

	if (total_slots)
		fe_bound = fetch_bub / total_slots;
	return fe_bound;
}

static double td_be_bound(int ctx, int cpu, struct runtime_stat *st)
{
	double sum = (td_fe_bound(ctx, cpu, st) +
		      td_bad_spec(ctx, cpu, st) +
		      td_retiring(ctx, cpu, st));
	if (sum == 0)
		return 0;
	return sanitize_val(1.0 - sum);
}

static void print_smi_cost(struct perf_stat_config *config,
			   int cpu, struct perf_evsel *evsel,
			   struct perf_stat_output_ctx *out,
			   struct runtime_stat *st)
{
	double smi_num, aperf, cycles, cost = 0.0;
	int ctx = evsel_context(evsel);
	const char *color = NULL;

	smi_num = runtime_stat_avg(st, STAT_SMI_NUM, ctx, cpu);
	aperf = runtime_stat_avg(st, STAT_APERF, ctx, cpu);
	cycles = runtime_stat_avg(st, STAT_CYCLES, ctx, cpu);

	if ((cycles == 0) || (aperf == 0))
		return;

	if (smi_num)
		cost = (aperf - cycles) / aperf * 100.00;

	if (cost > 10)
		color = PERF_COLOR_RED;
	out->print_metric(config, out->ctx, color, "%8.1f%%", "SMI cycles%", cost);
	out->print_metric(config, out->ctx, NULL, "%4.0f", "SMI#", smi_num);
}

static void generic_metric(struct perf_stat_config *config,
			   const char *metric_expr,
			   struct perf_evsel **metric_events,
			   char *name,
			   const char *metric_name,
			   double avg,
			   int cpu,
			   struct perf_stat_output_ctx *out,
			   struct runtime_stat *st)
{
	print_metric_t print_metric = out->print_metric;
	struct parse_ctx pctx;
	double ratio;
	int i;
	void *ctxp = out->ctx;
	char *n, *pn;

	expr__ctx_init(&pctx);
	expr__add_id(&pctx, name, avg);
	for (i = 0; metric_events[i]; i++) {
		struct saved_value *v;
		struct stats *stats;
		double scale;

		if (!strcmp(metric_events[i]->name, "duration_time")) {
			stats = &walltime_nsecs_stats;
			scale = 1e-9;
		} else {
			v = saved_value_lookup(metric_events[i], cpu, false,
					       STAT_NONE, 0, st);
			if (!v)
				break;
			stats = &v->stats;
			scale = 1.0;
		}

		n = strdup(metric_events[i]->name);
		if (!n)
			return;
		/*
		 * This display code with --no-merge adds [cpu] postfixes.
		 * These are not supported by the parser. Remove everything
		 * after the space.
		 */
		pn = strchr(n, ' ');
		if (pn)
			*pn = 0;
		expr__add_id(&pctx, n, avg_stats(stats)*scale);
	}
	if (!metric_events[i]) {
		const char *p = metric_expr;

		if (expr__parse(&ratio, &pctx, &p) == 0)
			print_metric(config, ctxp, NULL, "%8.1f",
				metric_name ?
				metric_name :
				out->force_header ?  name : "",
				ratio);
		else
			print_metric(config, ctxp, NULL, NULL,
				     out->force_header ?
				     (metric_name ? metric_name : name) : "", 0);
	} else
		print_metric(config, ctxp, NULL, NULL, "", 0);

	for (i = 1; i < pctx.num_ids; i++)
		zfree(&pctx.ids[i].name);
}

void perf_stat__print_shadow_stats(struct perf_stat_config *config,
				   struct perf_evsel *evsel,
				   double avg, int cpu,
				   struct perf_stat_output_ctx *out,
				   struct rblist *metric_events,
				   struct runtime_stat *st)
{
	void *ctxp = out->ctx;
	print_metric_t print_metric = out->print_metric;
	double total, ratio = 0.0, total2;
	const char *color = NULL;
	int ctx = evsel_context(evsel);
	struct metric_event *me;
	int num = 1;

	if (perf_evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS)) {
		total = runtime_stat_avg(st, STAT_CYCLES, ctx, cpu);

		if (total) {
			ratio = avg / total;
			print_metric(config, ctxp, NULL, "%7.2f ",
					"insn per cycle", ratio);
		} else {
			print_metric(config, ctxp, NULL, NULL, "insn per cycle", 0);
		}

		total = runtime_stat_avg(st, STAT_STALLED_CYCLES_FRONT,
					 ctx, cpu);

		total = max(total, runtime_stat_avg(st,
						    STAT_STALLED_CYCLES_BACK,
						    ctx, cpu));

		if (total && avg) {
			out->new_line(config, ctxp);
			ratio = total / avg;
			print_metric(config, ctxp, NULL, "%7.2f ",
					"stalled cycles per insn",
					ratio);
		} else if (have_frontend_stalled) {
			out->new_line(config, ctxp);
			print_metric(config, ctxp, NULL, "%7.2f ",
				     "stalled cycles per insn", 0);
		}
	} else if (perf_evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES)) {
		if (runtime_stat_n(st, STAT_BRANCHES, ctx, cpu) != 0)
			print_branch_misses(config, cpu, evsel, avg, out, st);
		else
			print_metric(config, ctxp, NULL, NULL, "of all branches", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_L1D |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_L1_DCACHE, ctx, cpu) != 0)
			print_l1_dcache_misses(config, cpu, evsel, avg, out, st);
		else
			print_metric(config, ctxp, NULL, NULL, "of all L1-dcache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_L1I |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_L1_ICACHE, ctx, cpu) != 0)
			print_l1_icache_misses(config, cpu, evsel, avg, out, st);
		else
			print_metric(config, ctxp, NULL, NULL, "of all L1-icache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_DTLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_DTLB_CACHE, ctx, cpu) != 0)
			print_dtlb_cache_misses(config, cpu, evsel, avg, out, st);
		else
			print_metric(config, ctxp, NULL, NULL, "of all dTLB cache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_ITLB |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_ITLB_CACHE, ctx, cpu) != 0)
			print_itlb_cache_misses(config, cpu, evsel, avg, out, st);
		else
			print_metric(config, ctxp, NULL, NULL, "of all iTLB cache hits", 0);
	} else if (
		evsel->attr.type == PERF_TYPE_HW_CACHE &&
		evsel->attr.config ==  ( PERF_COUNT_HW_CACHE_LL |
					((PERF_COUNT_HW_CACHE_OP_READ) << 8) |
					 ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))) {

		if (runtime_stat_n(st, STAT_LL_CACHE, ctx, cpu) != 0)
			print_ll_cache_misses(config, cpu, evsel, avg, out, st);
		else
			print_metric(config, ctxp, NULL, NULL, "of all LL-cache hits", 0);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_CACHE_MISSES)) {
		total = runtime_stat_avg(st, STAT_CACHEREFS, ctx, cpu);

		if (total)
			ratio = avg * 100 / total;

		if (runtime_stat_n(st, STAT_CACHEREFS, ctx, cpu) != 0)
			print_metric(config, ctxp, NULL, "%8.3f %%",
				     "of all cache refs", ratio);
		else
			print_metric(config, ctxp, NULL, NULL, "of all cache refs", 0);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_FRONTEND)) {
		print_stalled_cycles_frontend(config, cpu, evsel, avg, out, st);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_STALLED_CYCLES_BACKEND)) {
		print_stalled_cycles_backend(config, cpu, evsel, avg, out, st);
	} else if (perf_evsel__match(evsel, HARDWARE, HW_CPU_CYCLES)) {
		total = runtime_stat_avg(st, STAT_NSECS, 0, cpu);

		if (total) {
			ratio = avg / total;
			print_metric(config, ctxp, NULL, "%8.3f", "GHz", ratio);
		} else {
			print_metric(config, ctxp, NULL, NULL, "Ghz", 0);
		}
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX)) {
		total = runtime_stat_avg(st, STAT_CYCLES, ctx, cpu);

		if (total)
			print_metric(config, ctxp, NULL,
					"%7.2f%%", "transactional cycles",
					100.0 * (avg / total));
		else
			print_metric(config, ctxp, NULL, NULL, "transactional cycles",
				     0);
	} else if (perf_stat_evsel__is(evsel, CYCLES_IN_TX_CP)) {
		total = runtime_stat_avg(st, STAT_CYCLES, ctx, cpu);
		total2 = runtime_stat_avg(st, STAT_CYCLES_IN_TX, ctx, cpu);

		if (total2 < avg)
			total2 = avg;
		if (total)
			print_metric(config, ctxp, NULL, "%7.2f%%", "aborted cycles",
				100.0 * ((total2-avg) / total));
		else
			print_metric(config, ctxp, NULL, NULL, "aborted cycles", 0);
	} else if (perf_stat_evsel__is(evsel, TRANSACTION_START)) {
		total = runtime_stat_avg(st, STAT_CYCLES_IN_TX,
					 ctx, cpu);

		if (avg)
			ratio = total / avg;

		if (runtime_stat_n(st, STAT_CYCLES_IN_TX, ctx, cpu) != 0)
			print_metric(config, ctxp, NULL, "%8.0f",
				     "cycles / transaction", ratio);
		else
			print_metric(config, ctxp, NULL, NULL, "cycles / transaction",
				      0);
	} else if (perf_stat_evsel__is(evsel, ELISION_START)) {
		total = runtime_stat_avg(st, STAT_CYCLES_IN_TX,
					 ctx, cpu);

		if (avg)
			ratio = total / avg;

		print_metric(config, ctxp, NULL, "%8.0f", "cycles / elision", ratio);
	} else if (perf_evsel__is_clock(evsel)) {
		if ((ratio = avg_stats(&walltime_nsecs_stats)) != 0)
			print_metric(config, ctxp, NULL, "%8.3f", "CPUs utilized",
				     avg / (ratio * evsel->scale));
		else
			print_metric(config, ctxp, NULL, NULL, "CPUs utilized", 0);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_FETCH_BUBBLES)) {
		double fe_bound = td_fe_bound(ctx, cpu, st);

		if (fe_bound > 0.2)
			color = PERF_COLOR_RED;
		print_metric(config, ctxp, color, "%8.1f%%", "frontend bound",
				fe_bound * 100.);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_SLOTS_RETIRED)) {
		double retiring = td_retiring(ctx, cpu, st);

		if (retiring > 0.7)
			color = PERF_COLOR_GREEN;
		print_metric(config, ctxp, color, "%8.1f%%", "retiring",
				retiring * 100.);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_RECOVERY_BUBBLES)) {
		double bad_spec = td_bad_spec(ctx, cpu, st);

		if (bad_spec > 0.1)
			color = PERF_COLOR_RED;
		print_metric(config, ctxp, color, "%8.1f%%", "bad speculation",
				bad_spec * 100.);
	} else if (perf_stat_evsel__is(evsel, TOPDOWN_SLOTS_ISSUED)) {
		double be_bound = td_be_bound(ctx, cpu, st);
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
		if (td_total_slots(ctx, cpu, st) > 0)
			print_metric(config, ctxp, color, "%8.1f%%", name,
					be_bound * 100.);
		else
			print_metric(config, ctxp, NULL, NULL, name, 0);
	} else if (evsel->metric_expr) {
		generic_metric(config, evsel->metric_expr, evsel->metric_events, evsel->name,
				evsel->metric_name, avg, cpu, out, st);
	} else if (runtime_stat_n(st, STAT_NSECS, 0, cpu) != 0) {
		char unit = 'M';
		char unit_buf[10];

		total = runtime_stat_avg(st, STAT_NSECS, 0, cpu);

		if (total)
			ratio = 1000.0 * avg / total;
		if (ratio < 0.001) {
			ratio *= 1000;
			unit = 'K';
		}
		snprintf(unit_buf, sizeof(unit_buf), "%c/sec", unit);
		print_metric(config, ctxp, NULL, "%8.3f", unit_buf, ratio);
	} else if (perf_stat_evsel__is(evsel, SMI_NUM)) {
		print_smi_cost(config, cpu, evsel, out, st);
	} else {
		num = 0;
	}

	if ((me = metricgroup__lookup(metric_events, evsel, false)) != NULL) {
		struct metric_expr *mexp;

		list_for_each_entry (mexp, &me->head, nd) {
			if (num++ > 0)
				out->new_line(config, ctxp);
			generic_metric(config, mexp->metric_expr, mexp->metric_events,
					evsel->name, mexp->metric_name,
					avg, cpu, out, st);
		}
	}
	if (num == 0)
		print_metric(config, ctxp, NULL, NULL, NULL, 0);
}
