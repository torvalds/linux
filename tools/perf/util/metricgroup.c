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
#include "expr.h"
#include "rblist.h"
#include <string.h>
#include <errno.h>
#include "strlist.h"
#include <assert.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include "util.h"
#include <asm/bug.h>
#include "cgroup.h"

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
	INIT_LIST_HEAD(&me->head);
	return &me->nd;
}

static void metric_event_delete(struct rblist *rblist __maybe_unused,
				struct rb_node *rb_node)
{
	struct metric_event *me = container_of(rb_node, struct metric_event, nd);
	struct metric_expr *expr, *tmp;

	list_for_each_entry_safe(expr, tmp, &me->head, nd) {
		free(expr->metric_refs);
		free(expr->metric_events);
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

/*
 * A node in the list of referenced metrics. metric_expr
 * is held as a convenience to avoid a search through the
 * metric list.
 */
struct metric_ref_node {
	const char *metric_name;
	const char *metric_expr;
	struct list_head list;
};

struct metric {
	struct list_head nd;
	struct expr_parse_ctx pctx;
	const char *metric_name;
	const char *metric_expr;
	const char *metric_unit;
	struct list_head metric_refs;
	int metric_refs_cnt;
	int runtime;
	bool has_constraint;
};

#define RECURSION_ID_MAX 1000

struct expr_ids {
	struct expr_id	id[RECURSION_ID_MAX];
	int		cnt;
};

static struct expr_id *expr_ids__alloc(struct expr_ids *ids)
{
	if (ids->cnt >= RECURSION_ID_MAX)
		return NULL;
	return &ids->id[ids->cnt++];
}

static void expr_ids__exit(struct expr_ids *ids)
{
	int i;

	for (i = 0; i < ids->cnt; i++)
		free(ids->id[i].id);
}

static bool contains_event(struct evsel **metric_events, int num_events,
			const char *event_name)
{
	int i;

	for (i = 0; i < num_events; i++) {
		if (!strcmp(metric_events[i]->name, event_name))
			return true;
	}
	return false;
}

static bool evsel_same_pmu(struct evsel *ev1, struct evsel *ev2)
{
	if (!ev1->pmu_name || !ev2->pmu_name)
		return false;

	return !strcmp(ev1->pmu_name, ev2->pmu_name);
}

/**
 * Find a group of events in perf_evlist that correspond to those from a parsed
 * metric expression. Note, as find_evsel_group is called in the same order as
 * perf_evlist was constructed, metric_no_merge doesn't need to test for
 * underfilling a group.
 * @perf_evlist: a list of events something like: {metric1 leader, metric1
 * sibling, metric1 sibling}:W,duration_time,{metric2 leader, metric2 sibling,
 * metric2 sibling}:W,duration_time
 * @pctx: the parse context for the metric expression.
 * @metric_no_merge: don't attempt to share events for the metric with other
 * metrics.
 * @has_constraint: is there a contraint on the group of events? In which case
 * the events won't be grouped.
 * @metric_events: out argument, null terminated array of evsel's associated
 * with the metric.
 * @evlist_used: in/out argument, bitmap tracking which evlist events are used.
 * @return the first metric event or NULL on failure.
 */
static struct evsel *find_evsel_group(struct evlist *perf_evlist,
				      struct expr_parse_ctx *pctx,
				      bool metric_no_merge,
				      bool has_constraint,
				      struct evsel **metric_events,
				      unsigned long *evlist_used)
{
	struct evsel *ev, *current_leader = NULL;
	struct expr_id_data *val_ptr;
	int i = 0, matched_events = 0, events_to_match;
	const int idnum = (int)hashmap__size(&pctx->ids);

	/*
	 * duration_time is always grouped separately, when events are grouped
	 * (ie has_constraint is false) then ignore it in the matching loop and
	 * add it to metric_events at the end.
	 */
	if (!has_constraint &&
	    hashmap__find(&pctx->ids, "duration_time", (void **)&val_ptr))
		events_to_match = idnum - 1;
	else
		events_to_match = idnum;

	evlist__for_each_entry (perf_evlist, ev) {
		/*
		 * Events with a constraint aren't grouped and match the first
		 * events available.
		 */
		if (has_constraint && ev->weak_group)
			continue;
		/* Ignore event if already used and merging is disabled. */
		if (metric_no_merge && test_bit(ev->idx, evlist_used))
			continue;
		if (!has_constraint && ev->leader != current_leader) {
			/*
			 * Start of a new group, discard the whole match and
			 * start again.
			 */
			matched_events = 0;
			memset(metric_events, 0,
				sizeof(struct evsel *) * idnum);
			current_leader = ev->leader;
		}
		/*
		 * Check for duplicate events with the same name. For example,
		 * uncore_imc/cas_count_read/ will turn into 6 events per socket
		 * on skylakex. Only the first such event is placed in
		 * metric_events. If events aren't grouped then this also
		 * ensures that the same event in different sibling groups
		 * aren't both added to metric_events.
		 */
		if (contains_event(metric_events, matched_events, ev->name))
			continue;
		/* Does this event belong to the parse context? */
		if (hashmap__find(&pctx->ids, ev->name, (void **)&val_ptr))
			metric_events[matched_events++] = ev;

		if (matched_events == events_to_match)
			break;
	}

	if (events_to_match != idnum) {
		/* Add the first duration_time. */
		evlist__for_each_entry(perf_evlist, ev) {
			if (!strcmp(ev->name, "duration_time")) {
				metric_events[matched_events++] = ev;
				break;
			}
		}
	}

	if (matched_events != idnum) {
		/* Not a whole match */
		return NULL;
	}

	metric_events[idnum] = NULL;

	for (i = 0; i < idnum; i++) {
		ev = metric_events[i];
		/* Don't free the used events. */
		set_bit(ev->idx, evlist_used);
		/*
		 * The metric leader points to the identically named event in
		 * metric_events.
		 */
		ev->metric_leader = ev;
		/*
		 * Mark two events with identical names in the same group (or
		 * globally) as being in use as uncore events may be duplicated
		 * for each pmu. Set the metric leader of such events to be the
		 * event that appears in metric_events.
		 */
		evlist__for_each_entry_continue(perf_evlist, ev) {
			/*
			 * If events are grouped then the search can terminate
			 * when then group is left.
			 */
			if (!has_constraint &&
			    ev->leader != metric_events[i]->leader &&
			    evsel_same_pmu(ev->leader, metric_events[i]->leader))
				break;
			if (!strcmp(metric_events[i]->name, ev->name)) {
				set_bit(ev->idx, evlist_used);
				ev->metric_leader = metric_events[i];
			}
		}
	}

	return metric_events[0];
}

static int metricgroup__setup_events(struct list_head *groups,
				     bool metric_no_merge,
				     struct evlist *perf_evlist,
				     struct rblist *metric_events_list)
{
	struct metric_event *me;
	struct metric_expr *expr;
	int i = 0;
	int ret = 0;
	struct metric *m;
	struct evsel *evsel, *tmp;
	unsigned long *evlist_used;

	evlist_used = bitmap_alloc(perf_evlist->core.nr_entries);
	if (!evlist_used)
		return -ENOMEM;

	list_for_each_entry (m, groups, nd) {
		struct evsel **metric_events;
		struct metric_ref *metric_refs = NULL;

		metric_events = calloc(sizeof(void *),
				hashmap__size(&m->pctx.ids) + 1);
		if (!metric_events) {
			ret = -ENOMEM;
			break;
		}
		evsel = find_evsel_group(perf_evlist, &m->pctx,
					 metric_no_merge,
					 m->has_constraint, metric_events,
					 evlist_used);
		if (!evsel) {
			pr_debug("Cannot resolve %s: %s\n",
					m->metric_name, m->metric_expr);
			free(metric_events);
			continue;
		}
		for (i = 0; metric_events[i]; i++)
			metric_events[i]->collect_stat = true;
		me = metricgroup__lookup(metric_events_list, evsel, true);
		if (!me) {
			ret = -ENOMEM;
			free(metric_events);
			break;
		}
		expr = malloc(sizeof(struct metric_expr));
		if (!expr) {
			ret = -ENOMEM;
			free(metric_events);
			break;
		}

		/*
		 * Collect and store collected nested expressions
		 * for metric processing.
		 */
		if (m->metric_refs_cnt) {
			struct metric_ref_node *ref;

			metric_refs = zalloc(sizeof(struct metric_ref) * (m->metric_refs_cnt + 1));
			if (!metric_refs) {
				ret = -ENOMEM;
				free(metric_events);
				free(expr);
				break;
			}

			i = 0;
			list_for_each_entry(ref, &m->metric_refs, list) {
				/*
				 * Intentionally passing just const char pointers,
				 * originally from 'struct pmu_event' object.
				 * We don't need to change them, so there's no
				 * need to create our own copy.
				 */
				metric_refs[i].metric_name = ref->metric_name;
				metric_refs[i].metric_expr = ref->metric_expr;
				i++;
			}
		};

		expr->metric_refs = metric_refs;
		expr->metric_expr = m->metric_expr;
		expr->metric_name = m->metric_name;
		expr->metric_unit = m->metric_unit;
		expr->metric_events = metric_events;
		expr->runtime = m->runtime;
		list_add(&expr->nd, &me->head);
	}

	evlist__for_each_entry_safe(perf_evlist, tmp, evsel) {
		if (!test_bit(evsel->idx, evlist_used)) {
			evlist__remove(perf_evlist, evsel);
			evsel__delete(evsel);
		}
	}
	bitmap_free(evlist_used);

	return ret;
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

static bool match_pe_metric(struct pmu_event *pe, const char *metric)
{
	return match_metric(pe->metric_group, metric) ||
	       match_metric(pe->metric_name, metric);
}

struct mep {
	struct rb_node nd;
	const char *name;
	struct strlist *metrics;
};

static int mep_cmp(struct rb_node *rb_node, const void *entry)
{
	struct mep *a = container_of(rb_node, struct mep, nd);
	struct mep *b = (struct mep *)entry;

	return strcmp(a->name, b->name);
}

static struct rb_node *mep_new(struct rblist *rl __maybe_unused,
					const void *entry)
{
	struct mep *me = malloc(sizeof(struct mep));

	if (!me)
		return NULL;
	memcpy(me, entry, sizeof(struct mep));
	me->name = strdup(me->name);
	if (!me->name)
		goto out_me;
	me->metrics = strlist__new(NULL, NULL);
	if (!me->metrics)
		goto out_name;
	return &me->nd;
out_name:
	zfree(&me->name);
out_me:
	free(me);
	return NULL;
}

static struct mep *mep_lookup(struct rblist *groups, const char *name)
{
	struct rb_node *nd;
	struct mep me = {
		.name = name
	};
	nd = rblist__find(groups, &me);
	if (nd)
		return container_of(nd, struct mep, nd);
	rblist__add_node(groups, &me);
	nd = rblist__find(groups, &me);
	if (nd)
		return container_of(nd, struct mep, nd);
	return NULL;
}

static void mep_delete(struct rblist *rl __maybe_unused,
		       struct rb_node *nd)
{
	struct mep *me = container_of(nd, struct mep, nd);

	strlist__delete(me->metrics);
	zfree(&me->name);
	free(me);
}

static void metricgroup__print_strlist(struct strlist *metrics, bool raw)
{
	struct str_node *sn;
	int n = 0;

	strlist__for_each_entry (sn, metrics) {
		if (raw)
			printf("%s%s", n > 0 ? " " : "", sn->s);
		else
			printf("  %s\n", sn->s);
		n++;
	}
	if (raw)
		putchar('\n');
}

static int metricgroup__print_pmu_event(struct pmu_event *pe,
					bool metricgroups, char *filter,
					bool raw, bool details,
					struct rblist *groups,
					struct strlist *metriclist)
{
	const char *g;
	char *omg, *mg;

	g = pe->metric_group;
	if (!g && pe->metric_name) {
		if (pe->name)
			return 0;
		g = "No_group";
	}

	if (!g)
		return 0;

	mg = strdup(g);

	if (!mg)
		return -ENOMEM;
	omg = mg;
	while ((g = strsep(&mg, ";")) != NULL) {
		struct mep *me;
		char *s;

		g = skip_spaces(g);
		if (*g == 0)
			g = "No_group";
		if (filter && !strstr(g, filter))
			continue;
		if (raw)
			s = (char *)pe->metric_name;
		else {
			if (asprintf(&s, "%s\n%*s%s]",
				     pe->metric_name, 8, "[", pe->desc) < 0)
				return -1;
			if (details) {
				if (asprintf(&s, "%s\n%*s%s]",
					     s, 8, "[", pe->metric_expr) < 0)
					return -1;
			}
		}

		if (!s)
			continue;

		if (!metricgroups) {
			strlist__add(metriclist, s);
		} else {
			me = mep_lookup(groups, g);
			if (!me)
				continue;
			strlist__add(me->metrics, s);
		}

		if (!raw)
			free(s);
	}
	free(omg);

	return 0;
}

struct metricgroup_print_sys_idata {
	struct strlist *metriclist;
	char *filter;
	struct rblist *groups;
	bool metricgroups;
	bool raw;
	bool details;
};

typedef int (*metricgroup_sys_event_iter_fn)(struct pmu_event *pe, void *);

struct metricgroup_iter_data {
	metricgroup_sys_event_iter_fn fn;
	void *data;
};

static int metricgroup__sys_event_iter(struct pmu_event *pe, void *data)
{
	struct metricgroup_iter_data *d = data;
	struct perf_pmu *pmu = NULL;

	if (!pe->metric_expr || !pe->compat)
		return 0;

	while ((pmu = perf_pmu__scan(pmu))) {

		if (!pmu->id || strcmp(pmu->id, pe->compat))
			continue;

		return d->fn(pe, d->data);
	}

	return 0;
}

static int metricgroup__print_sys_event_iter(struct pmu_event *pe, void *data)
{
	struct metricgroup_print_sys_idata *d = data;

	return metricgroup__print_pmu_event(pe, d->metricgroups, d->filter, d->raw,
				     d->details, d->groups, d->metriclist);
}

void metricgroup__print(bool metrics, bool metricgroups, char *filter,
			bool raw, bool details)
{
	struct pmu_events_map *map = perf_pmu__find_map(NULL);
	struct pmu_event *pe;
	int i;
	struct rblist groups;
	struct rb_node *node, *next;
	struct strlist *metriclist = NULL;

	if (!metricgroups) {
		metriclist = strlist__new(NULL, NULL);
		if (!metriclist)
			return;
	}

	rblist__init(&groups);
	groups.node_new = mep_new;
	groups.node_cmp = mep_cmp;
	groups.node_delete = mep_delete;
	for (i = 0; map; i++) {
		pe = &map->table[i];

		if (!pe->name && !pe->metric_group && !pe->metric_name)
			break;
		if (!pe->metric_expr)
			continue;
		if (metricgroup__print_pmu_event(pe, metricgroups, filter,
						 raw, details, &groups,
						 metriclist) < 0)
			return;
	}

	{
		struct metricgroup_iter_data data = {
			.fn = metricgroup__print_sys_event_iter,
			.data = (void *) &(struct metricgroup_print_sys_idata){
				.metriclist = metriclist,
				.metricgroups = metricgroups,
				.filter = filter,
				.raw = raw,
				.details = details,
				.groups = &groups,
			},
		};

		pmu_for_each_sys_event(metricgroup__sys_event_iter, &data);
	}

	if (!filter || !rblist__empty(&groups)) {
		if (metricgroups && !raw)
			printf("\nMetric Groups:\n\n");
		else if (metrics && !raw)
			printf("\nMetrics:\n\n");
	}

	for (node = rb_first_cached(&groups.entries); node; node = next) {
		struct mep *me = container_of(node, struct mep, nd);

		if (metricgroups)
			printf("%s%s%s", me->name, metrics && !raw ? ":" : "", raw ? " " : "\n");
		if (metrics)
			metricgroup__print_strlist(me->metrics, raw);
		next = rb_next(node);
		rblist__remove_node(&groups, node);
	}
	if (!metricgroups)
		metricgroup__print_strlist(metriclist, raw);
	strlist__delete(metriclist);
}

static void metricgroup__add_metric_weak_group(struct strbuf *events,
					       struct expr_parse_ctx *ctx)
{
	struct hashmap_entry *cur;
	size_t bkt;
	bool no_group = true, has_duration = false;

	hashmap__for_each_entry((&ctx->ids), cur, bkt) {
		pr_debug("found event %s\n", (const char *)cur->key);
		/*
		 * Duration time maps to a software event and can make
		 * groups not count. Always use it outside a
		 * group.
		 */
		if (!strcmp(cur->key, "duration_time")) {
			has_duration = true;
			continue;
		}
		strbuf_addf(events, "%s%s",
			no_group ? "{" : ",",
			(const char *)cur->key);
		no_group = false;
	}
	if (!no_group) {
		strbuf_addf(events, "}:W");
		if (has_duration)
			strbuf_addf(events, ",duration_time");
	} else if (has_duration)
		strbuf_addf(events, "duration_time");
}

static void metricgroup__add_metric_non_group(struct strbuf *events,
					      struct expr_parse_ctx *ctx)
{
	struct hashmap_entry *cur;
	size_t bkt;
	bool first = true;

	hashmap__for_each_entry((&ctx->ids), cur, bkt) {
		if (!first)
			strbuf_addf(events, ",");
		strbuf_addf(events, "%s", (const char *)cur->key);
		first = false;
	}
}

static void metricgroup___watchdog_constraint_hint(const char *name, bool foot)
{
	static bool violate_nmi_constraint;

	if (!foot) {
		pr_warning("Splitting metric group %s into standalone metrics.\n", name);
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

static bool metricgroup__has_constraint(struct pmu_event *pe)
{
	if (!pe->metric_constraint)
		return false;

	if (!strcmp(pe->metric_constraint, "NO_NMI_WATCHDOG") &&
	    sysctl__nmi_watchdog_enabled()) {
		metricgroup___watchdog_constraint_hint(pe->metric_name, false);
		return true;
	}

	return false;
}

int __weak arch_get_runtimeparam(struct pmu_event *pe __maybe_unused)
{
	return 1;
}

struct metricgroup_add_iter_data {
	struct list_head *metric_list;
	const char *metric;
	struct expr_ids *ids;
	int *ret;
	bool *has_match;
	bool metric_no_group;
};

static int __add_metric(struct list_head *metric_list,
			struct pmu_event *pe,
			bool metric_no_group,
			int runtime,
			struct metric **mp,
			struct expr_id *parent,
			struct expr_ids *ids)
{
	struct metric_ref_node *ref;
	struct metric *m;

	if (*mp == NULL) {
		/*
		 * We got in here for the parent group,
		 * allocate it and put it on the list.
		 */
		m = zalloc(sizeof(*m));
		if (!m)
			return -ENOMEM;

		expr__ctx_init(&m->pctx);
		m->metric_name = pe->metric_name;
		m->metric_expr = pe->metric_expr;
		m->metric_unit = pe->unit;
		m->runtime = runtime;
		m->has_constraint = metric_no_group || metricgroup__has_constraint(pe);
		INIT_LIST_HEAD(&m->metric_refs);
		m->metric_refs_cnt = 0;

		parent = expr_ids__alloc(ids);
		if (!parent) {
			free(m);
			return -EINVAL;
		}

		parent->id = strdup(pe->metric_name);
		if (!parent->id) {
			free(m);
			return -ENOMEM;
		}
		*mp = m;
	} else {
		/*
		 * We got here for the referenced metric, via the
		 * recursive metricgroup__add_metric call, add
		 * it to the parent group.
		 */
		m = *mp;

		ref = malloc(sizeof(*ref));
		if (!ref)
			return -ENOMEM;

		/*
		 * Intentionally passing just const char pointers,
		 * from 'pe' object, so they never go away. We don't
		 * need to change them, so there's no need to create
		 * our own copy.
		 */
		ref->metric_name = pe->metric_name;
		ref->metric_expr = pe->metric_expr;

		list_add(&ref->list, &m->metric_refs);
		m->metric_refs_cnt++;
	}

	/* Force all found IDs in metric to have us as parent ID. */
	WARN_ON_ONCE(!parent);
	m->pctx.parent = parent;

	/*
	 * For both the parent and referenced metrics, we parse
	 * all the metric's IDs and add it to the parent context.
	 */
	if (expr__find_other(pe->metric_expr, NULL, &m->pctx, runtime) < 0) {
		if (m->metric_refs_cnt == 0) {
			expr__ctx_clear(&m->pctx);
			free(m);
			*mp = NULL;
		}
		return -EINVAL;
	}

	/*
	 * We add new group only in the 'parent' call,
	 * so bail out for referenced metric case.
	 */
	if (m->metric_refs_cnt)
		return 0;

	if (list_empty(metric_list))
		list_add(&m->nd, metric_list);
	else {
		struct list_head *pos;

		/* Place the largest groups at the front. */
		list_for_each_prev(pos, metric_list) {
			struct metric *old = list_entry(pos, struct metric, nd);

			if (hashmap__size(&m->pctx.ids) <=
			    hashmap__size(&old->pctx.ids))
				break;
		}
		list_add(&m->nd, pos);
	}

	return 0;
}

#define map_for_each_event(__pe, __idx, __map)					\
	if (__map)								\
		for (__idx = 0, __pe = &__map->table[__idx];			\
		     __pe->name || __pe->metric_group || __pe->metric_name;	\
		     __pe = &__map->table[++__idx])

#define map_for_each_metric(__pe, __idx, __map, __metric)		\
	map_for_each_event(__pe, __idx, __map)				\
		if (__pe->metric_expr &&				\
		    (match_metric(__pe->metric_group, __metric) ||	\
		     match_metric(__pe->metric_name, __metric)))

static struct pmu_event *find_metric(const char *metric, struct pmu_events_map *map)
{
	struct pmu_event *pe;
	int i;

	map_for_each_event(pe, i, map) {
		if (match_metric(pe->metric_name, metric))
			return pe;
	}

	return NULL;
}

static int recursion_check(struct metric *m, const char *id, struct expr_id **parent,
			   struct expr_ids *ids)
{
	struct expr_id_data *data;
	struct expr_id *p;
	int ret;

	/*
	 * We get the parent referenced by 'id' argument and
	 * traverse through all the parent object IDs to check
	 * if we already processed 'id', if we did, it's recursion
	 * and we fail.
	 */
	ret = expr__get_id(&m->pctx, id, &data);
	if (ret)
		return ret;

	p = expr_id_data__parent(data);

	while (p->parent) {
		if (!strcmp(p->id, id)) {
			pr_err("failed: recursion detected for %s\n", id);
			return -1;
		}
		p = p->parent;
	}

	/*
	 * If we are over the limit of static entris, the metric
	 * is too difficult/nested to process, fail as well.
	 */
	p = expr_ids__alloc(ids);
	if (!p) {
		pr_err("failed: too many nested metrics\n");
		return -EINVAL;
	}

	p->id     = strdup(id);
	p->parent = expr_id_data__parent(data);
	*parent   = p;

	return p->id ? 0 : -ENOMEM;
}

static int add_metric(struct list_head *metric_list,
		      struct pmu_event *pe,
		      bool metric_no_group,
		      struct metric **mp,
		      struct expr_id *parent,
		      struct expr_ids *ids);

static int __resolve_metric(struct metric *m,
			    bool metric_no_group,
			    struct list_head *metric_list,
			    struct pmu_events_map *map,
			    struct expr_ids *ids)
{
	struct hashmap_entry *cur;
	size_t bkt;
	bool all;
	int ret;

	/*
	 * Iterate all the parsed IDs and if there's metric,
	 * add it to the context.
	 */
	do {
		all = true;
		hashmap__for_each_entry((&m->pctx.ids), cur, bkt) {
			struct expr_id *parent;
			struct pmu_event *pe;

			pe = find_metric(cur->key, map);
			if (!pe)
				continue;

			ret = recursion_check(m, cur->key, &parent, ids);
			if (ret)
				return ret;

			all = false;
			/* The metric key itself needs to go out.. */
			expr__del_id(&m->pctx, cur->key);

			/* ... and it gets resolved to the parent context. */
			ret = add_metric(metric_list, pe, metric_no_group, &m, parent, ids);
			if (ret)
				return ret;

			/*
			 * We added new metric to hashmap, so we need
			 * to break the iteration and start over.
			 */
			break;
		}
	} while (!all);

	return 0;
}

static int resolve_metric(bool metric_no_group,
			  struct list_head *metric_list,
			  struct pmu_events_map *map,
			  struct expr_ids *ids)
{
	struct metric *m;
	int err;

	list_for_each_entry(m, metric_list, nd) {
		err = __resolve_metric(m, metric_no_group, metric_list, map, ids);
		if (err)
			return err;
	}
	return 0;
}

static int add_metric(struct list_head *metric_list,
		      struct pmu_event *pe,
		      bool metric_no_group,
		      struct metric **m,
		      struct expr_id *parent,
		      struct expr_ids *ids)
{
	struct metric *orig = *m;
	int ret = 0;

	pr_debug("metric expr %s for %s\n", pe->metric_expr, pe->metric_name);

	if (!strstr(pe->metric_expr, "?")) {
		ret = __add_metric(metric_list, pe, metric_no_group, 1, m, parent, ids);
	} else {
		int j, count;

		count = arch_get_runtimeparam(pe);

		/* This loop is added to create multiple
		 * events depend on count value and add
		 * those events to metric_list.
		 */

		for (j = 0; j < count && !ret; j++, *m = orig)
			ret = __add_metric(metric_list, pe, metric_no_group, j, m, parent, ids);
	}

	return ret;
}

static int metricgroup__add_metric_sys_event_iter(struct pmu_event *pe,
						  void *data)
{
	struct metricgroup_add_iter_data *d = data;
	struct metric *m = NULL;
	int ret;

	if (!match_pe_metric(pe, d->metric))
		return 0;

	ret = add_metric(d->metric_list, pe, d->metric_no_group, &m, NULL, d->ids);
	if (ret)
		return ret;

	ret = resolve_metric(d->metric_no_group,
				     d->metric_list, NULL, d->ids);
	if (ret)
		return ret;

	*(d->has_match) = true;

	return *d->ret;
}

static int metricgroup__add_metric(const char *metric, bool metric_no_group,
				   struct strbuf *events,
				   struct list_head *metric_list,
				   struct pmu_events_map *map)
{
	struct expr_ids ids = { .cnt = 0, };
	struct pmu_event *pe;
	struct metric *m;
	LIST_HEAD(list);
	int i, ret;
	bool has_match = false;

	map_for_each_metric(pe, i, map, metric) {
		has_match = true;
		m = NULL;

		ret = add_metric(&list, pe, metric_no_group, &m, NULL, &ids);
		if (ret)
			goto out;

		/*
		 * Process any possible referenced metrics
		 * included in the expression.
		 */
		ret = resolve_metric(metric_no_group,
				     &list, map, &ids);
		if (ret)
			goto out;
	}

	{
		struct metricgroup_iter_data data = {
			.fn = metricgroup__add_metric_sys_event_iter,
			.data = (void *) &(struct metricgroup_add_iter_data) {
				.metric_list = &list,
				.metric = metric,
				.metric_no_group = metric_no_group,
				.ids = &ids,
				.has_match = &has_match,
				.ret = &ret,
			},
		};

		pmu_for_each_sys_event(metricgroup__sys_event_iter, &data);
	}
	/* End of pmu events. */
	if (!has_match) {
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(m, &list, nd) {
		if (events->len > 0)
			strbuf_addf(events, ",");

		if (m->has_constraint) {
			metricgroup__add_metric_non_group(events,
							  &m->pctx);
		} else {
			metricgroup__add_metric_weak_group(events,
							   &m->pctx);
		}
	}

out:
	/*
	 * add to metric_list so that they can be released
	 * even if it's failed
	 */
	list_splice(&list, metric_list);
	expr_ids__exit(&ids);
	return ret;
}

static int metricgroup__add_metric_list(const char *list, bool metric_no_group,
					struct strbuf *events,
					struct list_head *metric_list,
					struct pmu_events_map *map)
{
	char *llist, *nlist, *p;
	int ret = -EINVAL;

	nlist = strdup(list);
	if (!nlist)
		return -ENOMEM;
	llist = nlist;

	strbuf_init(events, 100);
	strbuf_addf(events, "%s", "");

	while ((p = strsep(&llist, ",")) != NULL) {
		ret = metricgroup__add_metric(p, metric_no_group, events,
					      metric_list, map);
		if (ret == -EINVAL) {
			fprintf(stderr, "Cannot find metric or group `%s'\n",
					p);
			break;
		}
	}
	free(nlist);

	if (!ret)
		metricgroup___watchdog_constraint_hint(NULL, true);

	return ret;
}

static void metric__free_refs(struct metric *metric)
{
	struct metric_ref_node *ref, *tmp;

	list_for_each_entry_safe(ref, tmp, &metric->metric_refs, list) {
		list_del(&ref->list);
		free(ref);
	}
}

static void metricgroup__free_metrics(struct list_head *metric_list)
{
	struct metric *m, *tmp;

	list_for_each_entry_safe (m, tmp, metric_list, nd) {
		metric__free_refs(m);
		expr__ctx_clear(&m->pctx);
		list_del_init(&m->nd);
		free(m);
	}
}

static int parse_groups(struct evlist *perf_evlist, const char *str,
			bool metric_no_group,
			bool metric_no_merge,
			struct perf_pmu *fake_pmu,
			struct rblist *metric_events,
			struct pmu_events_map *map)
{
	struct parse_events_error parse_error;
	struct strbuf extra_events;
	LIST_HEAD(metric_list);
	int ret;

	if (metric_events->nr_entries == 0)
		metricgroup__rblist_init(metric_events);
	ret = metricgroup__add_metric_list(str, metric_no_group,
					   &extra_events, &metric_list, map);
	if (ret)
		goto out;
	pr_debug("adding %s\n", extra_events.buf);
	bzero(&parse_error, sizeof(parse_error));
	ret = __parse_events(perf_evlist, extra_events.buf, &parse_error, fake_pmu);
	if (ret) {
		parse_events_print_error(&parse_error, extra_events.buf);
		goto out;
	}
	ret = metricgroup__setup_events(&metric_list, metric_no_merge,
					perf_evlist, metric_events);
out:
	metricgroup__free_metrics(&metric_list);
	strbuf_release(&extra_events);
	return ret;
}

int metricgroup__parse_groups(const struct option *opt,
			      const char *str,
			      bool metric_no_group,
			      bool metric_no_merge,
			      struct rblist *metric_events)
{
	struct evlist *perf_evlist = *(struct evlist **)opt->value;
	struct pmu_events_map *map = perf_pmu__find_map(NULL);


	return parse_groups(perf_evlist, str, metric_no_group,
			    metric_no_merge, NULL, metric_events, map);
}

int metricgroup__parse_groups_test(struct evlist *evlist,
				   struct pmu_events_map *map,
				   const char *str,
				   bool metric_no_group,
				   bool metric_no_merge,
				   struct rblist *metric_events)
{
	return parse_groups(evlist, str, metric_no_group,
			    metric_no_merge, &perf_pmu__fake, metric_events, map);
}

bool metricgroup__has_metric(const char *metric)
{
	struct pmu_events_map *map = perf_pmu__find_map(NULL);
	struct pmu_event *pe;
	int i;

	if (!map)
		return false;

	for (i = 0; ; i++) {
		pe = &map->table[i];

		if (!pe->name && !pe->metric_group && !pe->metric_name)
			break;
		if (!pe->metric_expr)
			continue;
		if (match_metric(pe->metric_name, metric))
			return true;
	}
	return false;
}

int metricgroup__copy_metric_events(struct evlist *evlist, struct cgroup *cgrp,
				    struct rblist *new_metric_events,
				    struct rblist *old_metric_events)
{
	unsigned i;

	for (i = 0; i < rblist__nr_entries(old_metric_events); i++) {
		struct rb_node *nd;
		struct metric_event *old_me, *new_me;
		struct metric_expr *old_expr, *new_expr;
		struct evsel *evsel;
		size_t alloc_size;
		int idx, nr;

		nd = rblist__entry(old_metric_events, i);
		old_me = container_of(nd, struct metric_event, nd);

		evsel = evlist__find_evsel(evlist, old_me->evsel->idx);
		if (!evsel)
			return -EINVAL;
		new_me = metricgroup__lookup(new_metric_events, evsel, true);
		if (!new_me)
			return -ENOMEM;

		pr_debug("copying metric event for cgroup '%s': %s (idx=%d)\n",
			 cgrp ? cgrp->name : "root", evsel->name, evsel->idx);

		list_for_each_entry(old_expr, &old_me->head, nd) {
			new_expr = malloc(sizeof(*new_expr));
			if (!new_expr)
				return -ENOMEM;

			new_expr->metric_expr = old_expr->metric_expr;
			new_expr->metric_name = old_expr->metric_name;
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
				free(new_expr->metric_refs);
				free(new_expr);
				return -ENOMEM;
			}

			/* copy evsel in the same position */
			for (idx = 0; idx < nr; idx++) {
				evsel = old_expr->metric_events[idx];
				evsel = evlist__find_evsel(evlist, evsel->idx);
				if (evsel == NULL) {
					free(new_expr->metric_events);
					free(new_expr->metric_refs);
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
