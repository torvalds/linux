// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, Intel Corporation.
 */

/* Manage metrics and groups of metrics from JSON files */

#include "metricgroup.h"
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "strbuf.h"
#include "pmu.h"
#include "pmus.h"
#include "print-events.h"
#include "smt.h"
#include "expr.h"
#include "rblist.h"
#include <string.h>
#include <errno.h>
#include "strlist.h"
#include <assert.h>
#include <linux/ctype.h>
#include <linux/list_sort.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <perf/cpumap.h>
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include "util.h"
#include <asm/bug.h>
#include "cgroup.h"
#include "util/hashmap.h"

struct metric_event *metricgroup__lookup(struct rblist *metric_events,
					 struct evsel *evsel,
					 bool create)
{
	struct rb_node *nd;
	struct metric_event me = {
		.evsel = evsel
	};

	if (!metric_events)
		return NULL;

	nd = rblist__find(metric_events, &me);
	if (nd)
		return container_of(nd, struct metric_event, nd);
	if (create) {
		rblist__add_node(metric_events, &me);
		nd = rblist__find(metric_events, &me);
		if (nd)
			return container_of(nd, struct metric_event, nd);
	}
	return NULL;
}

static int metric_event_cmp(struct rb_node *rb_node, const void *entry)
{
	struct metric_event *a = container_of(rb_node,
					      struct metric_event,
					      nd);
	const struct metric_event *b = entry;

	if (a->evsel == b->evsel)
		return 0;
	if ((char *)a->evsel < (char *)b->evsel)
		return -1;
	return +1;
}

static struct rb_node *metric_event_new(struct rblist *rblist __maybe_unused,
					const void *entry)
{
	struct metric_event *me = malloc(sizeof(struct metric_event));

	if (!me)
		return NULL;
	memcpy(me, entry, sizeof(struct metric_event));
	me->evsel = ((struct metric_event *)entry)->evsel;
	me->is_default = false;
	INIT_LIST_HEAD(&me->head);
	return &me->nd;
}

static void metric_event_delete(struct rblist *rblist __maybe_unused,
				struct rb_node *rb_node)
{
	struct metric_event *me = container_of(rb_node, struct metric_event, nd);
	struct metric_expr *expr, *tmp;

	list_for_each_entry_safe(expr, tmp, &me->head, nd) {
		zfree(&expr->metric_name);
		zfree(&expr->metric_refs);
		zfree(&expr->metric_events);
		free(expr);
	}

	free(me);
}

static void metricgroup__rblist_init(struct rblist *metric_events)
{
	rblist__init(metric_events);
	metric_events->node_cmp = metric_event_cmp;
	metric_events->node_new = metric_event_new;
	metric_events->node_delete = metric_event_delete;
}

void metricgroup__rblist_exit(struct rblist *metric_events)
{
	rblist__exit(metric_events);
}

/**
 * The metric under construction. The data held here will be placed in a
 * metric_expr.
 */
struct metric {
	struct list_head nd;
	/**
	 * The expression parse context importantly holding the IDs contained
	 * within the expression.
	 */
	struct expr_parse_ctx *pctx;
	const char *pmu;
	/** The name of the metric such as "IPC". */
	const char *metric_name;
	/** Modifier on the metric such as "u" or NULL for none. */
	const char *modifier;
	/** The expression to parse, for example, "instructions/cycles". */
	const char *metric_expr;
	/** Optional threshold expression where zero value is green, otherwise red. */
	const char *metric_threshold;
	/**
	 * The "ScaleUnit" that scales and adds a unit to the metric during
	 * output.
	 */
	const char *metric_unit;
	/**
	 * Optional name of the metric group reported
	 * if the Default metric group is being processed.
	 */
	const char *default_metricgroup_name;
	/** Optional null terminated array of referenced metrics. */
	struct metric_ref *metric_refs;
	/**
	 * Should events of the metric be grouped?
	 */
	bool group_events;
	/**
	 * Parsed events for the metric. Optional as events may be taken from a
	 * different metric whose group contains all the IDs necessary for this
	 * one.
	 */
	struct evlist *evlist;
};

static void metric__watchdog_constraint_hint(const char *name, bool foot)
{
	static bool violate_nmi_constraint;

	if (!foot) {
		pr_warning("Not grouping metric %s's events.\n", name);
		violate_nmi_constraint = true;
		return;
	}

	if (!violate_nmi_constraint)
		return;

	pr_warning("Try disabling the NMI watchdog to comply NO_NMI_WATCHDOG metric constraint:\n"
		   "    echo 0 > /proc/sys/kernel/nmi_watchdog\n"
		   "    perf stat ...\n"
		   "    echo 1 > /proc/sys/kernel/nmi_watchdog\n");
}

static bool metric__group_events(const struct pmu_metric *pm)
{
	switch (pm->event_grouping) {
	case MetricNoGroupEvents:
		return false;
	case MetricNoGroupEventsNmi:
		if (!sysctl__nmi_watchdog_enabled())
			return true;
		metric__watchdog_constraint_hint(pm->metric_name, /*foot=*/false);
		return false;
	case MetricNoGroupEventsSmt:
		return !smt_on();
	case MetricGroupEvents:
	default:
		return true;
	}
}

static void metric__free(struct metric *m)
{
	if (!m)
		return;

	zfree(&m->metric_refs);
	expr__ctx_free(m->pctx);
	zfree(&m->modifier);
	evlist__delete(m->evlist);
	free(m);
}

static struct metric *metric__new(const struct pmu_metric *pm,
				  const char *modifier,
				  bool metric_no_group,
				  int runtime,
				  const char *user_requested_cpu_list,
				  bool system_wide)
{
	struct metric *m;

	m = zalloc(sizeof(*m));
	if (!m)
		return NULL;

	m->pctx = expr__ctx_new();
	if (!m->pctx)
		goto out_err;

	m->pmu = pm->pmu ?: "cpu";
	m->metric_name = pm->metric_name;
	m->default_metricgroup_name = pm->default_metricgroup_name ?: "";
	m->modifier = NULL;
	if (modifier) {
		m->modifier = strdup(modifier);
		if (!m->modifier)
			goto out_err;
	}
	m->metric_expr = pm->metric_expr;
	m->metric_threshold = pm->metric_threshold;
	m->metric_unit = pm->unit;
	m->pctx->sctx.user_requested_cpu_list = NULL;
	if (user_requested_cpu_list) {
		m->pctx->sctx.user_requested_cpu_list = strdup(user_requested_cpu_list);
		if (!m->pctx->sctx.user_requested_cpu_list)
			goto out_err;
	}
	m->pctx->sctx.runtime = runtime;
	m->pctx->sctx.system_wide = system_wide;
	m->group_events = !metric_no_group && metric__group_events(pm);
	m->metric_refs = NULL;
	m->evlist = NULL;

	return m;
out_err:
	metric__free(m);
	return NULL;
}

static bool contains_metric_id(struct evsel **metric_events, int num_events,
			       const char *metric_id)
{
	int i;

	for (i = 0; i < num_events; i++) {
		if (!strcmp(evsel__metric_id(metric_events[i]), metric_id))
			return true;
	}
	return false;
}

/**
 * setup_metric_events - Find a group of events in metric_evlist that correspond
 *                       to the IDs from a parsed metric expression.
 * @pmu: The PMU for the IDs.
 * @ids: the metric IDs to match.
 * @metric_evlist: the list of perf events.
 * @out_metric_events: holds the created metric events array.
 */
static int setup_metric_events(const char *pmu, struct hashmap *ids,
			       struct evlist *metric_evlist,
			       struct evsel ***out_metric_events)
{
	struct evsel **metric_events;
	const char *metric_id;
	struct evsel *ev;
	size_t ids_size, matched_events, i;
	bool all_pmus = !strcmp(pmu, "all") || perf_pmus__num_core_pmus() == 1 || !is_pmu_core(pmu);

	*out_metric_events = NULL;
	ids_size = hashmap__size(ids);

	metric_events = calloc(ids_size + 1, sizeof(void *));
	if (!metric_events)
		return -ENOMEM;

	matched_events = 0;
	evlist__for_each_entry(metric_evlist, ev) {
		struct expr_id_data *val_ptr;

		/* Don't match events for the wrong hybrid PMU. */
		if (!all_pmus && ev->pmu_name && evsel__is_hybrid(ev) &&
		    strcmp(ev->pmu_name, pmu))
			continue;
		/*
		 * Check for duplicate events with the same name. For
		 * example, uncore_imc/cas_count_read/ will turn into 6
		 * events per socket on skylakex. Only the first such
		 * event is placed in metric_events.
		 */
		metric_id = evsel__metric_id(ev);
		if (contains_metric_id(metric_events, matched_events, metric_id))
			continue;
		/*
		 * Does this event belong to the parse context? For
		 * combined or shared groups, this metric may not care
		 * about this event.
		 */
		if (hashmap__find(ids, metric_id, &val_ptr)) {
			pr_debug("Matched metric-id %s to %s\n", metric_id, evsel__name(ev));
			metric_events[matched_events++] = ev;

			if (matched_events >= ids_size)
				break;
		}
	}
	if (matched_events < ids_size) {
		free(metric_events);
		return -EINVAL;
	}
	for (i = 0; i < ids_size; i++) {
		ev = metric_events[i];
		ev->collect_stat = true;

		/*
		 * The metric leader points to the identically named
		 * event in metric_events.
		 */
		ev->metric_leader = ev;
		/*
		 * Mark two events with identical names in the same
		 * group (or globally) as being in use as uncore events
		 * may be duplicated for each pmu. Set the metric leader
		 * of such events to be the event that appears in
		 * metric_events.
		 */
		metric_id = evsel__metric_id(ev);
		evlist__for_each_entry_continue(metric_evlist, ev) {
			if (!strcmp(evsel__metric_id(ev), metric_id))
				ev->metric_leader = metric_events[i];
		}
	}
	*out_metric_events = metric_events;
	return 0;
}

static bool match_metric(const char *n, const char *list)
{
	int len;
	char *m;

	if (!list)
		return false;
	if (!strcmp(list, "all"))
		return true;
	if (!n)
		return !strcasecmp(list, "No_group");
	len = strlen(list);
	m = strcasestr(n, list);
	if (!m)
		return false;
	if ((m == n || m[-1] == ';' || m[-1] == ' ') &&
	    (m[len] == 0 || m[len] == ';'))
		return true;
	return false;
}

static bool match_pm_metric(const struct pmu_metric *pm, const char *pmu, const char *metric)
{
	const char *pm_pmu = pm->pmu ?: "cpu";

	if (strcmp(pmu, "all") && strcmp(pm_pmu, pmu))
		return false;

	return match_metric(pm->metric_group, metric) ||
	       match_metric(pm->metric_name, metric);
}

/** struct mep - RB-tree node for building printing information. */
struct mep {
	/** nd - RB-tree element. */
	struct rb_node nd;
	/** @metric_group: Owned metric group name, separated others with ';'. */
	char *metric_group;
	const char *metric_name;
	const char *metric_desc;
	const char *metric_long_desc;
	const char *metric_expr;
	const char *metric_threshold;
	const char *metric_unit;
};

static int mep_cmp(struct rb_node *rb_node, const void *entry)
{
	struct mep *a = container_of(rb_node, struct mep, nd);
	struct mep *b = (struct mep *)entry;
	int ret;

	ret = strcmp(a->metric_group, b->metric_group);
	if (ret)
		return ret;

	return strcmp(a->metric_name, b->metric_name);
}

static struct rb_node *mep_new(struct rblist *rl __maybe_unused, const void *entry)
{
	struct mep *me = malloc(sizeof(struct mep));

	if (!me)
		return NULL;

	memcpy(me, entry, sizeof(struct mep));
	return &me->nd;
}

static void mep_delete(struct rblist *rl __maybe_unused,
		       struct rb_node *nd)
{
	struct mep *me = container_of(nd, struct mep, nd);

	zfree(&me->metric_group);
	free(me);
}

static struct mep *mep_lookup(struct rblist *groups, const char *metric_group,
			      const char *metric_name)
{
	struct rb_node *nd;
	struct mep me = {
		.metric_group = strdup(metric_group),
		.metric_name = metric_name,
	};
	nd = rblist__find(groups, &me);
	if (nd) {
		free(me.metric_group);
		return container_of(nd, struct mep, nd);
	}
	rblist__add_node(groups, &me);
	nd = rblist__find(groups, &me);
	if (nd)
		return container_of(nd, struct mep, nd);
	return NULL;
}

static int metricgroup__add_to_mep_groups(const struct pmu_metric *pm,
					struct rblist *groups)
{
	const char *g;
	char *omg, *mg;

	mg = strdup(pm->metric_group ?: "No_group");
	if (!mg)
		return -ENOMEM;
	omg = mg;
	while ((g = strsep(&mg, ";")) != NULL) {
		struct mep *me;

		g = skip_spaces(g);
		if (strlen(g))
			me = mep_lookup(groups, g, pm->metric_name);
		else
			me = mep_lookup(groups, "No_group", pm->metric_name);

		if (me) {
			me->metric_desc = pm->desc;
			me->metric_long_desc = pm->long_desc;
			me->metric_expr = pm->metric_expr;
			me->metric_threshold = pm->metric_threshold;
			me->metric_unit = pm->unit;
		}
	}
	free(omg);

	return 0;
}

struct metricgroup_iter_data {
	pmu_metric_iter_fn fn;
	void *data;
};

static int metricgroup__sys_event_iter(const struct pmu_metric *pm,
				       const struct pmu_metrics_table *table,
				       void *data)
{
	struct metricgroup_iter_data *d = data;
	struct perf_pmu *pmu = NULL;

	if (!pm->metric_expr || !pm->compat)
		return 0;

	while ((pmu = perf_pmus__scan(pmu))) {

		if (!pmu->id || !pmu_uncore_identifier_match(pm->compat, pmu->id))
			continue;

		return d->fn(pm, table, d->data);
	}
	return 0;
}

static int metricgroup__add_to_mep_groups_callback(const struct pmu_metric *pm,
					const struct pmu_metrics_table *table __maybe_unused,
					void *vdata)
{
	struct rblist *groups = vdata;

	return metricgroup__add_to_mep_groups(pm, groups);
}

void metricgroup__print(const struct print_callbacks *print_cb, void *print_state)
{
	struct rblist groups;
	const struct pmu_metrics_table *table;
	struct rb_node *node, *next;

	rblist__init(&groups);
	groups.node_new = mep_new;
	groups.node_cmp = mep_cmp;
	groups.node_delete = mep_delete;
	table = pmu_metrics_table__find();
	if (table) {
		pmu_metrics_table__for_each_metric(table,
						 metricgroup__add_to_mep_groups_callback,
						 &groups);
	}
	{
		struct metricgroup_iter_data data = {
			.fn = metricgroup__add_to_mep_groups_callback,
			.data = &groups,
		};
		pmu_for_each_sys_metric(metricgroup__sys_event_iter, &data);
	}

	for (node = rb_first_cached(&groups.entries); node; node = next) {
		struct mep *me = container_of(node, struct mep, nd);

		print_cb->print_metric(print_state,
				me->metric_group,
				me->metric_name,
				me->metric_desc,
				me->metric_long_desc,
				me->metric_expr,
				me->metric_threshold,
				me->metric_unit);
		next = rb_next(node);
		rblist__remove_node(&groups, node);
	}
}

static const char *code_characters = ",-=@";

static int encode_metric_id(struct strbuf *sb, const char *x)
{
	char *c;
	int ret = 0;

	for (; *x; x++) {
		c = strchr(code_characters, *x);
		if (c) {
			ret = strbuf_addch(sb, '!');
			if (ret)
				break;

			ret = strbuf_addch(sb, '0' + (c - code_characters));
			if (ret)
				break;
		} else {
			ret = strbuf_addch(sb, *x);
			if (ret)
				break;
		}
	}
	return ret;
}

static int decode_metric_id(struct strbuf *sb, const char *x)
{
	const char *orig = x;
	size_t i;
	char c;
	int ret;

	for (; *x; x++) {
		c = *x;
		if (*x == '!') {
			x++;
			i = *x - '0';
			if (i > strlen(code_characters)) {
				pr_err("Bad metric-id encoding in: '%s'", orig);
				return -1;
			}
			c = code_characters[i];
		}
		ret = strbuf_addch(sb, c);
		if (ret)
			return ret;
	}
	return 0;
}

static int decode_all_metric_ids(struct evlist *perf_evlist, const char *modifier)
{
	struct evsel *ev;
	struct strbuf sb = STRBUF_INIT;
	char *cur;
	int ret = 0;

	evlist__for_each_entry(perf_evlist, ev) {
		if (!ev->metric_id)
			continue;

		ret = strbuf_setlen(&sb, 0);
		if (ret)
			break;

		ret = decode_metric_id(&sb, ev->metric_id);
		if (ret)
			break;

		free((char *)ev->metric_id);
		ev->metric_id = strdup(sb.buf);
		if (!ev->metric_id) {
			ret = -ENOMEM;
			break;
		}
		/*
		 * If the name is just the parsed event, use the metric-id to
		 * give a more friendly display version.
		 */
		if (strstr(ev->name, "metric-id=")) {
			bool has_slash = false;

			zfree(&ev->name);
			for (cur = strchr(sb.buf, '@') ; cur; cur = strchr(++cur, '@')) {
				*cur = '/';
				has_slash = true;
			}

			if (modifier) {
				if (!has_slash && !strchr(sb.buf, ':')) {
					ret = strbuf_addch(&sb, ':');
					if (ret)
						break;
				}
				ret = strbuf_addstr(&sb, modifier);
				if (ret)
					break;
			}
			ev->name = strdup(sb.buf);
			if (!ev->name) {
				ret = -ENOMEM;
				break;
			}
		}
	}
	strbuf_release(&sb);
	return ret;
}

static int metricgroup__build_event_string(struct strbuf *events,
					   const struct expr_parse_ctx *ctx,
					   const char *modifier,
					   bool group_events)
{
	struct hashmap_entry *cur;
	size_t bkt;
	bool no_group = true, has_tool_events = false;
	bool tool_events[PERF_TOOL_MAX] = {false};
	int ret = 0;

#define RETURN_IF_NON_ZERO(x) do { if (x) return x; } while (0)

	hashmap__for_each_entry(ctx->ids, cur, bkt) {
		const char *sep, *rsep, *id = cur->pkey;
		enum perf_tool_event ev;

		pr_debug("found event %s\n", id);

		/* Always move tool events outside of the group. */
		ev = perf_tool_event__from_str(id);
		if (ev != PERF_TOOL_NONE) {
			has_tool_events = true;
			tool_events[ev] = true;
			continue;
		}
		/* Separate events with commas and open the group if necessary. */
		if (no_group) {
			if (group_events) {
				ret = strbuf_addch(events, '{');
				RETURN_IF_NON_ZERO(ret);
			}

			no_group = false;
		} else {
			ret = strbuf_addch(events, ',');
			RETURN_IF_NON_ZERO(ret);
		}
		/*
		 * Encode the ID as an event string. Add a qualifier for
		 * metric_id that is the original name except with characters
		 * that parse-events can't parse replaced. For example,
		 * 'msr@tsc@' gets added as msr/tsc,metric-id=msr!3tsc!3/
		 */
		sep = strchr(id, '@');
		if (sep != NULL) {
			ret = strbuf_add(events, id, sep - id);
			RETURN_IF_NON_ZERO(ret);
			ret = strbuf_addch(events, '/');
			RETURN_IF_NON_ZERO(ret);
			rsep = strrchr(sep, '@');
			ret = strbuf_add(events, sep + 1, rsep - sep - 1);
			RETURN_IF_NON_ZERO(ret);
			ret = strbuf_addstr(events, ",metric-id=");
			RETURN_IF_NON_ZERO(ret);
			sep = rsep;
		} else {
			sep = strchr(id, ':');
			if (sep != NULL) {
				ret = strbuf_add(events, id, sep - id);
				RETURN_IF_NON_ZERO(ret);
			} else {
				ret = strbuf_addstr(events, id);
				RETURN_IF_NON_ZERO(ret);
			}
			ret = strbuf_addstr(events, "/metric-id=");
			RETURN_IF_NON_ZERO(ret);
		}
		ret = encode_metric_id(events, id);
		RETURN_IF_NON_ZERO(ret);
		ret = strbuf_addstr(events, "/");
		RETURN_IF_NON_ZERO(ret);

		if (sep != NULL) {
			ret = strbuf_addstr(events, sep + 1);
			RETURN_IF_NON_ZERO(ret);
		}
		if (modifier) {
			ret = strbuf_addstr(events, modifier);
			RETURN_IF_NON_ZERO(ret);
		}
	}
	if (!no_group && group_events) {
		ret = strbuf_addf(events, "}:W");
		RETURN_IF_NON_ZERO(ret);
	}
	if (has_tool_events) {
		int i;

		perf_tool_event__for_each_event(i) {
			if (tool_events[i]) {
				if (!no_group) {
					ret = strbuf_addch(events, ',');
					RETURN_IF_NON_ZERO(ret);
				}
				no_group = false;
				ret = strbuf_addstr(events, perf_tool_event__to_str(i));
				RETURN_IF_NON_ZERO(ret);
			}
		}
	}

	return ret;
#undef RETURN_IF_NON_ZERO
}

int __weak arch_get_runtimeparam(const struct pmu_metric *pm __maybe_unused)
{
	return 1;
}

/*
 * A singly linked list on the stack of the names of metrics being
 * processed. Used to identify recursion.
 */
struct visited_metric {
	const char *name;
	const struct visited_metric *parent;
};

struct metricgroup_add_iter_data {
	struct list_head *metric_list;
	const char *pmu;
	const char *metric_name;
	const char *modifier;
	int *ret;
	bool *has_match;
	bool metric_no_group;
	bool metric_no_threshold;
	const char *user_requested_cpu_list;
	bool system_wide;
	struct metric *root_metric;
	const struct visited_metric *visited;
	const struct pmu_metrics_table *table;
};

static bool metricgroup__find_metric(const char *pmu,
				     const char *metric,
				     const struct pmu_metrics_table *table,
				     struct pmu_metric *pm);

static int add_metric(struct list_head *metric_list,
		      const struct pmu_metric *pm,
		      const char *modifier,
		      bool metric_no_group,
		      bool metric_no_threshold,
		      const char *user_requested_cpu_list,
		      bool system_wide,
		      struct metric *root_metric,
		      const struct visited_metric *visited,
		      const struct pmu_metrics_table *table);

/**
 * resolve_metric - Locate metrics within the root metric and recursively add
 *                    references to them.
 * @metric_list: The list the metric is added to.
 * @pmu: The PMU name to resolve metrics on, or "all" for all PMUs.
 * @modifier: if non-null event modifiers like "u".
 * @metric_no_group: Should events written to events be grouped "{}" or
 *                   global. Grouping is the default but due to multiplexing the
 *                   user may override.
 * @user_requested_cpu_list: Command line specified CPUs to record on.
 * @system_wide: Are events for all processes recorded.
 * @root_metric: Metrics may reference other metrics to form a tree. In this
 *               case the root_metric holds all the IDs and a list of referenced
 *               metrics. When adding a root this argument is NULL.
 * @visited: A singly linked list of metric names being added that is used to
 *           detect recursion.
 * @table: The table that is searched for metrics, most commonly the table for the
 *       architecture perf is running upon.
 */
static int resolve_metric(struct list_head *metric_list,
			  const char *pmu,
			  const char *modifier,
			  bool metric_no_group,
			  bool metric_no_threshold,
			  const char *user_requested_cpu_list,
			  bool system_wide,
			  struct metric *root_metric,
			  const struct visited_metric *visited,
			  const struct pmu_metrics_table *table)
{
	struct hashmap_entry *cur;
	size_t bkt;
	struct to_resolve {
		/* The metric to resolve. */
		struct pmu_metric pm;
		/*
		 * The key in the IDs map, this may differ from in case,
		 * etc. from pm->metric_name.
		 */
		const char *key;
	} *pending = NULL;
	int i, ret = 0, pending_cnt = 0;

	/*
	 * Iterate all the parsed IDs and if there's a matching metric and it to
	 * the pending array.
	 */
	hashmap__for_each_entry(root_metric->pctx->ids, cur, bkt) {
		struct pmu_metric pm;

		if (metricgroup__find_metric(pmu, cur->pkey, table, &pm)) {
			pending = realloc(pending,
					(pending_cnt + 1) * sizeof(struct to_resolve));
			if (!pending)
				return -ENOMEM;

			memcpy(&pending[pending_cnt].pm, &pm, sizeof(pm));
			pending[pending_cnt].key = cur->pkey;
			pending_cnt++;
		}
	}

	/* Remove the metric IDs from the context. */
	for (i = 0; i < pending_cnt; i++)
		expr__del_id(root_metric->pctx, pending[i].key);

	/*
	 * Recursively add all the metrics, IDs are added to the root metric's
	 * context.
	 */
	for (i = 0; i < pending_cnt; i++) {
		ret = add_metric(metric_list, &pending[i].pm, modifier, metric_no_group,
				 metric_no_threshold, user_requested_cpu_list, system_wide,
				 root_metric, visited, table);
		if (ret)
			break;
	}

	free(pending);
	return ret;
}

/**
 * __add_metric - Add a metric to metric_list.
 * @metric_list: The list the metric is added to.
 * @pm: The pmu_metric containing the metric to be added.
 * @modifier: if non-null event modifiers like "u".
 * @metric_no_group: Should events written to events be grouped "{}" or
 *                   global. Grouping is the default but due to multiplexing the
 *                   user may override.
 * @metric_no_threshold: Should threshold expressions be ignored?
 * @runtime: A special argument for the parser only known at runtime.
 * @user_requested_cpu_list: Command line specified CPUs to record on.
 * @system_wide: Are events for all processes recorded.
 * @root_metric: Metrics may reference other metrics to form a tree. In this
 *               case the root_metric holds all the IDs and a list of referenced
 *               metrics. When adding a root this argument is NULL.
 * @visited: A singly linked list of metric names being added that is used to
 *           detect recursion.
 * @table: The table that is searched for metrics, most commonly the table for the
 *       architecture perf is running upon.
 */
static int __add_metric(struct list_head *metric_list,
			const struct pmu_metric *pm,
			const char *modifier,
			bool metric_no_group,
			bool metric_no_threshold,
			int runtime,
			const char *user_requested_cpu_list,
			bool system_wide,
			struct metric *root_metric,
			const struct visited_metric *visited,
			const struct pmu_metrics_table *table)
{
	const struct visited_metric *vm;
	int ret;
	bool is_root = !root_metric;
	const char *expr;
	struct visited_metric visited_node = {
		.name = pm->metric_name,
		.parent = visited,
	};

	for (vm = visited; vm; vm = vm->parent) {
		if (!strcmp(pm->metric_name, vm->name)) {
			pr_err("failed: recursion detected for %s\n", pm->metric_name);
			return -1;
		}
	}

	if (is_root) {
		/*
		 * This metric is the root of a tree and may reference other
		 * metrics that are added recursively.
		 */
		root_metric = metric__new(pm, modifier, metric_no_group, runtime,
					  user_requested_cpu_list, system_wide);
		if (!root_metric)
			return -ENOMEM;

	} else {
		int cnt = 0;

		/*
		 * This metric was referenced in a metric higher in the
		 * tree. Check if the same metric is already resolved in the
		 * metric_refs list.
		 */
		if (root_metric->metric_refs) {
			for (; root_metric->metric_refs[cnt].metric_name; cnt++) {
				if (!strcmp(pm->metric_name,
					    root_metric->metric_refs[cnt].metric_name))
					return 0;
			}
		}

		/* Create reference. Need space for the entry and the terminator. */
		root_metric->metric_refs = realloc(root_metric->metric_refs,
						(cnt + 2) * sizeof(struct metric_ref));
		if (!root_metric->metric_refs)
			return -ENOMEM;

		/*
		 * Intentionally passing just const char pointers,
		 * from 'pe' object, so they never go away. We don't
		 * need to change them, so there's no need to create
		 * our own copy.
		 */
		root_metric->metric_refs[cnt].metric_name = pm->metric_name;
		root_metric->metric_refs[cnt].metric_expr = pm->metric_expr;

		/* Null terminate array. */
		root_metric->metric_refs[cnt+1].metric_name = NULL;
		root_metric->metric_refs[cnt+1].metric_expr = NULL;
	}

	/*
	 * For both the parent and referenced metrics, we parse
	 * all the metric's IDs and add it to the root context.
	 */
	ret = 0;
	expr = pm->metric_expr;
	if (is_root && pm->metric_threshold) {
		/*
		 * Threshold expressions are built off the actual metric. Switch
		 * to use that in case of additional necessary events. Change
		 * the visited node name to avoid this being flagged as
		 * recursion. If the threshold events are disabled, just use the
		 * metric's name as a reference. This allows metric threshold
		 * computation if there are sufficient events.
		 */
		assert(strstr(pm->metric_threshold, pm->metric_name));
		expr = metric_no_threshold ? pm->metric_name : pm->metric_threshold;
		visited_node.name = "__threshold__";
	}
	if (expr__find_ids(expr, NULL, root_metric->pctx) < 0) {
		/* Broken metric. */
		ret = -EINVAL;
	}
	if (!ret) {
		/* Resolve referenced metrics. */
		const char *pmu = pm->pmu ?: "cpu";

		ret = resolve_metric(metric_list, pmu, modifier, metric_no_group,
				     metric_no_threshold, user_requested_cpu_list,
				     system_wide, root_metric, &visited_node,
				     table);
	}
	if (ret) {
		if (is_root)
			metric__free(root_metric);

	} else if (is_root)
		list_add(&root_metric->nd, metric_list);

	return ret;
}

struct metricgroup__find_metric_data {
	const char *pmu;
	const char *metric;
	struct pmu_metric *pm;
};

static int metricgroup__find_metric_callback(const struct pmu_metric *pm,
					     const struct pmu_metrics_table *table  __maybe_unused,
					     void *vdata)
{
	struct metricgroup__find_metric_data *data = vdata;
	const char *pm_pmu = pm->pmu ?: "cpu";

	if (strcmp(data->pmu, "all") && strcmp(pm_pmu, data->pmu))
		return 0;

	if (!match_metric(pm->metric_name, data->metric))
		return 0;

	memcpy(data->pm, pm, sizeof(*pm));
	return 1;
}

static bool metricgroup__find_metric(const char *pmu,
				     const char *metric,
				     const struct pmu_metrics_table *table,
				     struct pmu_metric *pm)
{
	struct metricgroup__find_metric_data data = {
		.pmu = pmu,
		.metric = metric,
		.pm = pm,
	};

	return pmu_metrics_table__for_each_metric(table, metricgroup__find_metric_callback, &data)
		? true : false;
}

static int add_metric(struct list_head *metric_list,
		      const struct pmu_metric *pm,
		      const char *modifier,
		      bool metric_no_group,
		      bool metric_no_threshold,
		      const char *user_requested_cpu_list,
		      bool system_wide,
		      struct metric *root_metric,
		      const struct visited_metric *visited,
		      const struct pmu_metrics_table *table)
{
	int ret = 0;

	pr_debug("metric expr %s for %s\n", pm->metric_expr, pm->metric_name);

	if (!strstr(pm->metric_expr, "?")) {
		ret = __add_metric(metric_list, pm, modifier, metric_no_group,
				   metric_no_threshold, 0, user_requested_cpu_list,
				   system_wide, root_metric, visited, table);
	} else {
		int j, count;

		count = arch_get_runtimeparam(pm);

		/* This loop is added to create multiple
		 * events depend on count value and add
		 * those events to metric_list.
		 */

		for (j = 0; j < count && !ret; j++)
			ret = __add_metric(metric_list, pm, modifier, metric_no_group,
					   metric_no_threshold, j, user_requested_cpu_list,
					   system_wide, root_metric, visited, table);
	}

	return ret;
}

static int metricgroup__add_metric_sys_event_iter(const struct pmu_metric *pm,
					const struct pmu_metrics_table *table __maybe_unused,
					void *data)
{
	struct metricgroup_add_iter_data *d = data;
	int ret;

	if (!match_pm_metric(pm, d->pmu, d->metric_name))
		return 0;

	ret = add_metric(d->metric_list, pm, d->modifier, d->metric_no_group,
			 d->metric_no_threshold, d->user_requested_cpu_list,
			 d->system_wide, d->root_metric, d->visited, d->table);
	if (ret)
		goto out;

	*(d->has_match) = true;

out:
	*(d->ret) = ret;
	return ret;
}

/**
 * metric_list_cmp - list_sort comparator that sorts metrics with more events to
 *                   the front. tool events are excluded from the count.
 */
static int metric_list_cmp(void *priv __maybe_unused, const struct list_head *l,
			   const struct list_head *r)
{
	const struct metric *left = container_of(l, struct metric, nd);
	const struct metric *right = container_of(r, struct metric, nd);
	struct expr_id_data *data;
	int i, left_count, right_count;

	left_count = hashmap__size(left->pctx->ids);
	perf_tool_event__for_each_event(i) {
		if (!expr__get_id(left->pctx, perf_tool_event__to_str(i), &data))
			left_count--;
	}

	right_count = hashmap__size(right->pctx->ids);
	perf_tool_event__for_each_event(i) {
		if (!expr__get_id(right->pctx, perf_tool_event__to_str(i), &data))
			right_count--;
	}

	return right_count - left_count;
}

/**
 * default_metricgroup_cmp - Implements complex key for the Default metricgroup
 *			     that first sorts by default_metricgroup_name, then
 *			     metric_name.
 */
static int default_metricgroup_cmp(void *priv __maybe_unused,
				   const struct list_head *l,
				   const struct list_head *r)
{
	const struct metric *left = container_of(l, struct metric, nd);
	const struct metric *right = container_of(r, struct metric, nd);
	int diff = strcmp(right->default_metricgroup_name, left->default_metricgroup_name);

	if (diff)
		return diff;

	return strcmp(right->metric_name, left->metric_name);
}

struct metricgroup__add_metric_data {
	struct list_head *list;
	const char *pmu;
	const char *metric_name;
	const char *modifier;
	const char *user_requested_cpu_list;
	bool metric_no_group;
	bool metric_no_threshold;
	bool system_wide;
	bool has_match;
};

static int metricgroup__add_metric_callback(const struct pmu_metric *pm,
					    const struct pmu_metrics_table *table,
					    void *vdata)
{
	struct metricgroup__add_metric_data *data = vdata;
	int ret = 0;

	if (pm->metric_expr && match_pm_metric(pm, data->pmu, data->metric_name)) {
		bool metric_no_group = data->metric_no_group ||
			match_metric(pm->metricgroup_no_group, data->metric_name);

		data->has_match = true;
		ret = add_metric(data->list, pm, data->modifier, metric_no_group,
				 data->metric_no_threshold, data->user_requested_cpu_list,
				 data->system_wide, /*root_metric=*/NULL,
				 /*visited_metrics=*/NULL, table);
	}
	return ret;
}

/**
 * metricgroup__add_metric - Find and add a metric, or a metric group.
 * @pmu: The PMU name to search for metrics on, or "all" for all PMUs.
 * @metric_name: The name of the metric or metric group. For example, "IPC"
 *               could be the name of a metric and "TopDownL1" the name of a
 *               metric group.
 * @modifier: if non-null event modifiers like "u".
 * @metric_no_group: Should events written to events be grouped "{}" or
 *                   global. Grouping is the default but due to multiplexing the
 *                   user may override.
 * @user_requested_cpu_list: Command line specified CPUs to record on.
 * @system_wide: Are events for all processes recorded.
 * @metric_list: The list that the metric or metric group are added to.
 * @table: The table that is searched for metrics, most commonly the table for the
 *       architecture perf is running upon.
 */
static int metricgroup__add_metric(const char *pmu, const char *metric_name, const char *modifier,
				   bool metric_no_group, bool metric_no_threshold,
				   const char *user_requested_cpu_list,
				   bool system_wide,
				   struct list_head *metric_list,
				   const struct pmu_metrics_table *table)
{
	LIST_HEAD(list);
	int ret;
	bool has_match = false;

	{
		struct metricgroup__add_metric_data data = {
			.list = &list,
			.pmu = pmu,
			.metric_name = metric_name,
			.modifier = modifier,
			.metric_no_group = metric_no_group,
			.metric_no_threshold = metric_no_threshold,
			.user_requested_cpu_list = user_requested_cpu_list,
			.system_wide = system_wide,
			.has_match = false,
		};
		/*
		 * Iterate over all metrics seeing if metric matches either the
		 * name or group. When it does add the metric to the list.
		 */
		ret = pmu_metrics_table__for_each_metric(table, metricgroup__add_metric_callback,
						       &data);
		if (ret)
			goto out;

		has_match = data.has_match;
	}
	{
		struct metricgroup_iter_data data = {
			.fn = metricgroup__add_metric_sys_event_iter,
			.data = (void *) &(struct metricgroup_add_iter_data) {
				.metric_list = &list,
				.pmu = pmu,
				.metric_name = metric_name,
				.modifier = modifier,
				.metric_no_group = metric_no_group,
				.user_requested_cpu_list = user_requested_cpu_list,
				.system_wide = system_wide,
				.has_match = &has_match,
				.ret = &ret,
				.table = table,
			},
		};

		pmu_for_each_sys_metric(metricgroup__sys_event_iter, &data);
	}
	/* End of pmu events. */
	if (!has_match)
		ret = -EINVAL;

out:
	/*
	 * add to metric_list so that they can be released
	 * even if it's failed
	 */
	list_splice(&list, metric_list);
	return ret;
}

/**
 * metricgroup__add_metric_list - Find and add metrics, or metric groups,
 *                                specified in a list.
 * @pmu: A pmu to restrict the metrics to, or "all" for all PMUS.
 * @list: the list of metrics or metric groups. For example, "IPC,CPI,TopDownL1"
 *        would match the IPC and CPI metrics, and TopDownL1 would match all
 *        the metrics in the TopDownL1 group.
 * @metric_no_group: Should events written to events be grouped "{}" or
 *                   global. Grouping is the default but due to multiplexing the
 *                   user may override.
 * @user_requested_cpu_list: Command line specified CPUs to record on.
 * @system_wide: Are events for all processes recorded.
 * @metric_list: The list that metrics are added to.
 * @table: The table that is searched for metrics, most commonly the table for the
 *       architecture perf is running upon.
 */
static int metricgroup__add_metric_list(const char *pmu, const char *list,
					bool metric_no_group,
					bool metric_no_threshold,
					const char *user_requested_cpu_list,
					bool system_wide, struct list_head *metric_list,
					const struct pmu_metrics_table *table)
{
	char *list_itr, *list_copy, *metric_name, *modifier;
	int ret, count = 0;

	list_copy = strdup(list);
	if (!list_copy)
		return -ENOMEM;
	list_itr = list_copy;

	while ((metric_name = strsep(&list_itr, ",")) != NULL) {
		modifier = strchr(metric_name, ':');
		if (modifier)
			*modifier++ = '\0';

		ret = metricgroup__add_metric(pmu, metric_name, modifier,
					      metric_no_group, metric_no_threshold,
					      user_requested_cpu_list,
					      system_wide, metric_list, table);
		if (ret == -EINVAL)
			pr_err("Cannot find metric or group `%s'\n", metric_name);

		if (ret)
			break;

		count++;
	}
	free(list_copy);

	if (!ret) {
		/*
		 * Warn about nmi_watchdog if any parsed metrics had the
		 * NO_NMI_WATCHDOG constraint.
		 */
		metric__watchdog_constraint_hint(NULL, /*foot=*/true);
		/* No metrics. */
		if (count == 0)
			return -EINVAL;
	}
	return ret;
}

static void metricgroup__free_metrics(struct list_head *metric_list)
{
	struct metric *m, *tmp;

	list_for_each_entry_safe (m, tmp, metric_list, nd) {
		list_del_init(&m->nd);
		metric__free(m);
	}
}

/**
 * find_tool_events - Search for the pressence of tool events in metric_list.
 * @metric_list: List to take metrics from.
 * @tool_events: Array of false values, indices corresponding to tool events set
 *               to true if tool event is found.
 */
static void find_tool_events(const struct list_head *metric_list,
			     bool tool_events[PERF_TOOL_MAX])
{
	struct metric *m;

	list_for_each_entry(m, metric_list, nd) {
		int i;

		perf_tool_event__for_each_event(i) {
			struct expr_id_data *data;

			if (!tool_events[i] &&
			    !expr__get_id(m->pctx, perf_tool_event__to_str(i), &data))
				tool_events[i] = true;
		}
	}
}

/**
 * build_combined_expr_ctx - Make an expr_parse_ctx with all !group_events
 *                           metric IDs, as the IDs are held in a set,
 *                           duplicates will be removed.
 * @metric_list: List to take metrics from.
 * @combined: Out argument for result.
 */
static int build_combined_expr_ctx(const struct list_head *metric_list,
				   struct expr_parse_ctx **combined)
{
	struct hashmap_entry *cur;
	size_t bkt;
	struct metric *m;
	char *dup;
	int ret;

	*combined = expr__ctx_new();
	if (!*combined)
		return -ENOMEM;

	list_for_each_entry(m, metric_list, nd) {
		if (!m->group_events && !m->modifier) {
			hashmap__for_each_entry(m->pctx->ids, cur, bkt) {
				dup = strdup(cur->pkey);
				if (!dup) {
					ret = -ENOMEM;
					goto err_out;
				}
				ret = expr__add_id(*combined, dup);
				if (ret)
					goto err_out;
			}
		}
	}
	return 0;
err_out:
	expr__ctx_free(*combined);
	*combined = NULL;
	return ret;
}

/**
 * parse_ids - Build the event string for the ids and parse them creating an
 *             evlist. The encoded metric_ids are decoded.
 * @metric_no_merge: is metric sharing explicitly disabled.
 * @fake_pmu: used when testing metrics not supported by the current CPU.
 * @ids: the event identifiers parsed from a metric.
 * @modifier: any modifiers added to the events.
 * @group_events: should events be placed in a weak group.
 * @tool_events: entries set true if the tool event of index could be present in
 *               the overall list of metrics.
 * @out_evlist: the created list of events.
 */
static int parse_ids(bool metric_no_merge, struct perf_pmu *fake_pmu,
		     struct expr_parse_ctx *ids, const char *modifier,
		     bool group_events, const bool tool_events[PERF_TOOL_MAX],
		     struct evlist **out_evlist)
{
	struct parse_events_error parse_error;
	struct evlist *parsed_evlist;
	struct strbuf events = STRBUF_INIT;
	int ret;

	*out_evlist = NULL;
	if (!metric_no_merge || hashmap__size(ids->ids) == 0) {
		bool added_event = false;
		int i;
		/*
		 * We may fail to share events between metrics because a tool
		 * event isn't present in one metric. For example, a ratio of
		 * cache misses doesn't need duration_time but the same events
		 * may be used for a misses per second. Events without sharing
		 * implies multiplexing, that is best avoided, so place
		 * all tool events in every group.
		 *
		 * Also, there may be no ids/events in the expression parsing
		 * context because of constant evaluation, e.g.:
		 *    event1 if #smt_on else 0
		 * Add a tool event to avoid a parse error on an empty string.
		 */
		perf_tool_event__for_each_event(i) {
			if (tool_events[i]) {
				char *tmp = strdup(perf_tool_event__to_str(i));

				if (!tmp)
					return -ENOMEM;
				ids__insert(ids->ids, tmp);
				added_event = true;
			}
		}
		if (!added_event && hashmap__size(ids->ids) == 0) {
			char *tmp = strdup("duration_time");

			if (!tmp)
				return -ENOMEM;
			ids__insert(ids->ids, tmp);
		}
	}
	ret = metricgroup__build_event_string(&events, ids, modifier,
					      group_events);
	if (ret)
		return ret;

	parsed_evlist = evlist__new();
	if (!parsed_evlist) {
		ret = -ENOMEM;
		goto err_out;
	}
	pr_debug("Parsing metric events '%s'\n", events.buf);
	parse_events_error__init(&parse_error);
	ret = __parse_events(parsed_evlist, events.buf, /*pmu_filter=*/NULL,
			     &parse_error, fake_pmu, /*warn_if_reordered=*/false);
	if (ret) {
		parse_events_error__print(&parse_error, events.buf);
		goto err_out;
	}
	ret = decode_all_metric_ids(parsed_evlist, modifier);
	if (ret)
		goto err_out;

	*out_evlist = parsed_evlist;
	parsed_evlist = NULL;
err_out:
	parse_events_error__exit(&parse_error);
	evlist__delete(parsed_evlist);
	strbuf_release(&events);
	return ret;
}

static int parse_groups(struct evlist *perf_evlist,
			const char *pmu, const char *str,
			bool metric_no_group,
			bool metric_no_merge,
			bool metric_no_threshold,
			const char *user_requested_cpu_list,
			bool system_wide,
			struct perf_pmu *fake_pmu,
			struct rblist *metric_events_list,
			const struct pmu_metrics_table *table)
{
	struct evlist *combined_evlist = NULL;
	LIST_HEAD(metric_list);
	struct metric *m;
	bool tool_events[PERF_TOOL_MAX] = {false};
	bool is_default = !strcmp(str, "Default");
	int ret;

	if (metric_events_list->nr_entries == 0)
		metricgroup__rblist_init(metric_events_list);
	ret = metricgroup__add_metric_list(pmu, str, metric_no_group, metric_no_threshold,
					   user_requested_cpu_list,
					   system_wide, &metric_list, table);
	if (ret)
		goto out;

	/* Sort metrics from largest to smallest. */
	list_sort(NULL, &metric_list, metric_list_cmp);

	if (!metric_no_merge) {
		struct expr_parse_ctx *combined = NULL;

		find_tool_events(&metric_list, tool_events);

		ret = build_combined_expr_ctx(&metric_list, &combined);

		if (!ret && combined && hashmap__size(combined->ids)) {
			ret = parse_ids(metric_no_merge, fake_pmu, combined,
					/*modifier=*/NULL,
					/*group_events=*/false,
					tool_events,
					&combined_evlist);
		}
		if (combined)
			expr__ctx_free(combined);

		if (ret)
			goto out;
	}

	if (is_default)
		list_sort(NULL, &metric_list, default_metricgroup_cmp);

	list_for_each_entry(m, &metric_list, nd) {
		struct metric_event *me;
		struct evsel **metric_events;
		struct evlist *metric_evlist = NULL;
		struct metric *n;
		struct metric_expr *expr;

		if (combined_evlist && !m->group_events) {
			metric_evlist = combined_evlist;
		} else if (!metric_no_merge) {
			/*
			 * See if the IDs for this metric are a subset of an
			 * earlier metric.
			 */
			list_for_each_entry(n, &metric_list, nd) {
				if (m == n)
					break;

				if (n->evlist == NULL)
					continue;

				if ((!m->modifier && n->modifier) ||
				    (m->modifier && !n->modifier) ||
				    (m->modifier && n->modifier &&
					    strcmp(m->modifier, n->modifier)))
					continue;

				if ((!m->pmu && n->pmu) ||
				    (m->pmu && !n->pmu) ||
				    (m->pmu && n->pmu && strcmp(m->pmu, n->pmu)))
					continue;

				if (expr__subset_of_ids(n->pctx, m->pctx)) {
					pr_debug("Events in '%s' fully contained within '%s'\n",
						 m->metric_name, n->metric_name);
					metric_evlist = n->evlist;
					break;
				}

			}
		}
		if (!metric_evlist) {
			ret = parse_ids(metric_no_merge, fake_pmu, m->pctx, m->modifier,
					m->group_events, tool_events, &m->evlist);
			if (ret)
				goto out;

			metric_evlist = m->evlist;
		}
		ret = setup_metric_events(fake_pmu ? "all" : m->pmu, m->pctx->ids,
					  metric_evlist, &metric_events);
		if (ret) {
			pr_err("Cannot resolve IDs for %s: %s\n",
				m->metric_name, m->metric_expr);
			goto out;
		}

		me = metricgroup__lookup(metric_events_list, metric_events[0], true);

		expr = malloc(sizeof(struct metric_expr));
		if (!expr) {
			ret = -ENOMEM;
			free(metric_events);
			goto out;
		}

		expr->metric_refs = m->metric_refs;
		m->metric_refs = NULL;
		expr->metric_expr = m->metric_expr;
		if (m->modifier) {
			char *tmp;

			if (asprintf(&tmp, "%s:%s", m->metric_name, m->modifier) < 0)
				expr->metric_name = NULL;
			else
				expr->metric_name = tmp;
		} else
			expr->metric_name = strdup(m->metric_name);

		if (!expr->metric_name) {
			ret = -ENOMEM;
			free(metric_events);
			goto out;
		}
		expr->metric_threshold = m->metric_threshold;
		expr->metric_unit = m->metric_unit;
		expr->metric_events = metric_events;
		expr->runtime = m->pctx->sctx.runtime;
		expr->default_metricgroup_name = m->default_metricgroup_name;
		me->is_default = is_default;
		list_add(&expr->nd, &me->head);
	}


	if (combined_evlist) {
		evlist__splice_list_tail(perf_evlist, &combined_evlist->core.entries);
		evlist__delete(combined_evlist);
	}

	list_for_each_entry(m, &metric_list, nd) {
		if (m->evlist)
			evlist__splice_list_tail(perf_evlist, &m->evlist->core.entries);
	}

out:
	metricgroup__free_metrics(&metric_list);
	return ret;
}

int metricgroup__parse_groups(struct evlist *perf_evlist,
			      const char *pmu,
			      const char *str,
			      bool metric_no_group,
			      bool metric_no_merge,
			      bool metric_no_threshold,
			      const char *user_requested_cpu_list,
			      bool system_wide,
			      struct rblist *metric_events)
{
	const struct pmu_metrics_table *table = pmu_metrics_table__find();

	if (!table)
		return -EINVAL;

	return parse_groups(perf_evlist, pmu, str, metric_no_group, metric_no_merge,
			    metric_no_threshold, user_requested_cpu_list, system_wide,
			    /*fake_pmu=*/NULL, metric_events, table);
}

int metricgroup__parse_groups_test(struct evlist *evlist,
				   const struct pmu_metrics_table *table,
				   const char *str,
				   struct rblist *metric_events)
{
	return parse_groups(evlist, "all", str,
			    /*metric_no_group=*/false,
			    /*metric_no_merge=*/false,
			    /*metric_no_threshold=*/false,
			    /*user_requested_cpu_list=*/NULL,
			    /*system_wide=*/false,
			    &perf_pmu__fake, metric_events, table);
}

struct metricgroup__has_metric_data {
	const char *pmu;
	const char *metric;
};
static int metricgroup__has_metric_callback(const struct pmu_metric *pm,
					    const struct pmu_metrics_table *table __maybe_unused,
					    void *vdata)
{
	struct metricgroup__has_metric_data *data = vdata;

	return match_pm_metric(pm, data->pmu, data->metric) ? 1 : 0;
}

bool metricgroup__has_metric(const char *pmu, const char *metric)
{
	const struct pmu_metrics_table *table = pmu_metrics_table__find();
	struct metricgroup__has_metric_data data = {
		.pmu = pmu,
		.metric = metric,
	};

	if (!table)
		return false;

	return pmu_metrics_table__for_each_metric(table, metricgroup__has_metric_callback, &data)
		? true : false;
}

static int metricgroup__topdown_max_level_callback(const struct pmu_metric *pm,
					    const struct pmu_metrics_table *table __maybe_unused,
					    void *data)
{
	unsigned int *max_level = data;
	unsigned int level;
	const char *p = strstr(pm->metric_group ?: "", "TopdownL");

	if (!p || p[8] == '\0')
		return 0;

	level = p[8] - '0';
	if (level > *max_level)
		*max_level = level;

	return 0;
}

unsigned int metricgroups__topdown_max_level(void)
{
	unsigned int max_level = 0;
	const struct pmu_metrics_table *table = pmu_metrics_table__find();

	if (!table)
		return false;

	pmu_metrics_table__for_each_metric(table, metricgroup__topdown_max_level_callback,
					  &max_level);
	return max_level;
}

int metricgroup__copy_metric_events(struct evlist *evlist, struct cgroup *cgrp,
				    struct rblist *new_metric_events,
				    struct rblist *old_metric_events)
{
	unsigned int i;

	for (i = 0; i < rblist__nr_entries(old_metric_events); i++) {
		struct rb_node *nd;
		struct metric_event *old_me, *new_me;
		struct metric_expr *old_expr, *new_expr;
		struct evsel *evsel;
		size_t alloc_size;
		int idx, nr;

		nd = rblist__entry(old_metric_events, i);
		old_me = container_of(nd, struct metric_event, nd);

		evsel = evlist__find_evsel(evlist, old_me->evsel->core.idx);
		if (!evsel)
			return -EINVAL;
		new_me = metricgroup__lookup(new_metric_events, evsel, true);
		if (!new_me)
			return -ENOMEM;

		pr_debug("copying metric event for cgroup '%s': %s (idx=%d)\n",
			 cgrp ? cgrp->name : "root", evsel->name, evsel->core.idx);

		list_for_each_entry(old_expr, &old_me->head, nd) {
			new_expr = malloc(sizeof(*new_expr));
			if (!new_expr)
				return -ENOMEM;

			new_expr->metric_expr = old_expr->metric_expr;
			new_expr->metric_threshold = old_expr->metric_threshold;
			new_expr->metric_name = strdup(old_expr->metric_name);
			if (!new_expr->metric_name)
				return -ENOMEM;

			new_expr->metric_unit = old_expr->metric_unit;
			new_expr->runtime = old_expr->runtime;

			if (old_expr->metric_refs) {
				/* calculate number of metric_events */
				for (nr = 0; old_expr->metric_refs[nr].metric_name; nr++)
					continue;
				alloc_size = sizeof(*new_expr->metric_refs);
				new_expr->metric_refs = calloc(nr + 1, alloc_size);
				if (!new_expr->metric_refs) {
					free(new_expr);
					return -ENOMEM;
				}

				memcpy(new_expr->metric_refs, old_expr->metric_refs,
				       nr * alloc_size);
			} else {
				new_expr->metric_refs = NULL;
			}

			/* calculate number of metric_events */
			for (nr = 0; old_expr->metric_events[nr]; nr++)
				continue;
			alloc_size = sizeof(*new_expr->metric_events);
			new_expr->metric_events = calloc(nr + 1, alloc_size);
			if (!new_expr->metric_events) {
				zfree(&new_expr->metric_refs);
				free(new_expr);
				return -ENOMEM;
			}

			/* copy evsel in the same position */
			for (idx = 0; idx < nr; idx++) {
				evsel = old_expr->metric_events[idx];
				evsel = evlist__find_evsel(evlist, evsel->core.idx);
				if (evsel == NULL) {
					zfree(&new_expr->metric_events);
					zfree(&new_expr->metric_refs);
					free(new_expr);
					return -EINVAL;
				}
				new_expr->metric_events[idx] = evsel;
			}

			list_add(&new_expr->nd, &new_me->head);
		}
	}
	return 0;
}
