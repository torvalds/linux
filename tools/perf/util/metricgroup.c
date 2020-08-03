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
#include "pmu-events/pmu-events.h"
#include "strlist.h"
#include <assert.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include "util.h"

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

static void metricgroup__rblist_init(struct rblist *metric_events)
{
	rblist__init(metric_events);
	metric_events->node_cmp = metric_event_cmp;
	metric_events->node_new = metric_event_new;
}

struct egroup {
	struct list_head nd;
	struct expr_parse_ctx pctx;
	const char *metric_name;
	const char *metric_expr;
	const char *metric_unit;
	int runtime;
	bool has_constraint;
};

/**
 * Find a group of events in perf_evlist that correpond to those from a parsed
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
	double *val_ptr;
	int i = 0, matched_events = 0, events_to_match;
	const int idnum = (int)hashmap__size(&pctx->ids);

	/* duration_time is grouped separately. */
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
		if (hashmap__find(&pctx->ids, ev->name, (void **)&val_ptr)) {
			if (has_constraint) {
				/*
				 * Events aren't grouped, ensure the same event
				 * isn't matched from two groups.
				 */
				for (i = 0; i < matched_events; i++) {
					if (!strcmp(ev->name,
						    metric_events[i]->name)) {
						break;
					}
				}
				if (i != matched_events)
					continue;
			}
			metric_events[matched_events++] = ev;
		}
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
		/* Not whole match */
		return NULL;
	}

	metric_events[idnum] = NULL;

	for (i = 0; i < idnum; i++) {
		ev = metric_events[i];
		ev->metric_leader = ev;
		set_bit(ev->idx, evlist_used);
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
	struct egroup *eg;
	struct evsel *evsel, *tmp;
	unsigned long *evlist_used;

	evlist_used = bitmap_alloc(perf_evlist->core.nr_entries);
	if (!evlist_used)
		return -ENOMEM;

	list_for_each_entry (eg, groups, nd) {
		struct evsel **metric_events;

		metric_events = calloc(sizeof(void *),
				hashmap__size(&eg->pctx.ids) + 1);
		if (!metric_events) {
			ret = -ENOMEM;
			break;
		}
		evsel = find_evsel_group(perf_evlist, &eg->pctx,
					 metric_no_merge,
					 eg->has_constraint, metric_events,
					 evlist_used);
		if (!evsel) {
			pr_debug("Cannot resolve %s: %s\n",
					eg->metric_name, eg->metric_expr);
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
		expr->metric_expr = eg->metric_expr;
		expr->metric_name = eg->metric_name;
		expr->metric_unit = eg->metric_unit;
		expr->metric_events = metric_events;
		expr->runtime = eg->runtime;
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

void metricgroup__print(bool metrics, bool metricgroups, char *filter,
			bool raw, bool details)
{
	struct pmu_events_map *map = perf_pmu__find_map(NULL);
	struct pmu_event *pe;
	int i;
	struct rblist groups;
	struct rb_node *node, *next;
	struct strlist *metriclist = NULL;

	if (!map)
		return;

	if (!metricgroups) {
		metriclist = strlist__new(NULL, NULL);
		if (!metriclist)
			return;
	}

	rblist__init(&groups);
	groups.node_new = mep_new;
	groups.node_cmp = mep_cmp;
	groups.node_delete = mep_delete;
	for (i = 0; ; i++) {
		const char *g;
		pe = &map->table[i];

		if (!pe->name && !pe->metric_group && !pe->metric_name)
			break;
		if (!pe->metric_expr)
			continue;
		g = pe->metric_group;
		if (!g && pe->metric_name) {
			if (pe->name)
				continue;
			g = "No_group";
		}
		if (g) {
			char *omg;
			char *mg = strdup(g);

			if (!mg)
				return;
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
						return;

					if (details) {
						if (asprintf(&s, "%s\n%*s%s]",
							     s, 8, "[", pe->metric_expr) < 0)
							return;
					}
				}

				if (!s)
					continue;

				if (!metricgroups) {
					strlist__add(metriclist, s);
				} else {
					me = mep_lookup(&groups, g);
					if (!me)
						continue;
					strlist__add(me->metrics, s);
				}
			}
			free(omg);
		}
	}

	if (metricgroups && !raw)
		printf("\nMetric Groups:\n\n");
	else if (metrics && !raw)
		printf("\nMetrics:\n\n");

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

int __weak arch_get_runtimeparam(void)
{
	return 1;
}

static int __metricgroup__add_metric(struct list_head *group_list,
				     struct pmu_event *pe,
				     bool metric_no_group,
				     int runtime)
{
	struct egroup *eg;

	eg = malloc(sizeof(*eg));
	if (!eg)
		return -ENOMEM;

	expr__ctx_init(&eg->pctx);
	eg->metric_name = pe->metric_name;
	eg->metric_expr = pe->metric_expr;
	eg->metric_unit = pe->unit;
	eg->runtime = runtime;
	eg->has_constraint = metric_no_group || metricgroup__has_constraint(pe);

	if (expr__find_other(pe->metric_expr, NULL, &eg->pctx, runtime) < 0) {
		expr__ctx_clear(&eg->pctx);
		free(eg);
		return -EINVAL;
	}

	if (list_empty(group_list))
		list_add(&eg->nd, group_list);
	else {
		struct list_head *pos;

		/* Place the largest groups at the front. */
		list_for_each_prev(pos, group_list) {
			struct egroup *old = list_entry(pos, struct egroup, nd);

			if (hashmap__size(&eg->pctx.ids) <=
			    hashmap__size(&old->pctx.ids))
				break;
		}
		list_add(&eg->nd, pos);
	}

	return 0;
}

static int metricgroup__add_metric(const char *metric, bool metric_no_group,
				   struct strbuf *events,
				   struct list_head *group_list)
{
	struct pmu_events_map *map = perf_pmu__find_map(NULL);
	struct pmu_event *pe;
	struct egroup *eg;
	int i, ret;
	bool has_match = false;

	if (!map)
		return 0;

	for (i = 0; ; i++) {
		pe = &map->table[i];

		if (!pe->name && !pe->metric_group && !pe->metric_name) {
			/* End of pmu events. */
			if (!has_match)
				return -EINVAL;
			break;
		}
		if (!pe->metric_expr)
			continue;
		if (match_metric(pe->metric_group, metric) ||
		    match_metric(pe->metric_name, metric)) {
			has_match = true;
			pr_debug("metric expr %s for %s\n", pe->metric_expr, pe->metric_name);

			if (!strstr(pe->metric_expr, "?")) {
				ret = __metricgroup__add_metric(group_list,
								pe,
								metric_no_group,
								1);
				if (ret)
					return ret;
			} else {
				int j, count;

				count = arch_get_runtimeparam();

				/* This loop is added to create multiple
				 * events depend on count value and add
				 * those events to group_list.
				 */

				for (j = 0; j < count; j++) {
					ret = __metricgroup__add_metric(
						group_list, pe,
						metric_no_group, j);
					if (ret)
						return ret;
				}
			}
		}
	}
	list_for_each_entry(eg, group_list, nd) {
		if (events->len > 0)
			strbuf_addf(events, ",");

		if (eg->has_constraint) {
			metricgroup__add_metric_non_group(events,
							  &eg->pctx);
		} else {
			metricgroup__add_metric_weak_group(events,
							   &eg->pctx);
		}
	}
	return 0;
}

static int metricgroup__add_metric_list(const char *list, bool metric_no_group,
					struct strbuf *events,
				        struct list_head *group_list)
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
					      group_list);
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

static void metricgroup__free_egroups(struct list_head *group_list)
{
	struct egroup *eg, *egtmp;

	list_for_each_entry_safe (eg, egtmp, group_list, nd) {
		expr__ctx_clear(&eg->pctx);
		list_del_init(&eg->nd);
		free(eg);
	}
}

int metricgroup__parse_groups(const struct option *opt,
			      const char *str,
			      bool metric_no_group,
			      bool metric_no_merge,
			      struct rblist *metric_events)
{
	struct parse_events_error parse_error;
	struct evlist *perf_evlist = *(struct evlist **)opt->value;
	struct strbuf extra_events;
	LIST_HEAD(group_list);
	int ret;

	if (metric_events->nr_entries == 0)
		metricgroup__rblist_init(metric_events);
	ret = metricgroup__add_metric_list(str, metric_no_group,
					   &extra_events, &group_list);
	if (ret)
		return ret;
	pr_debug("adding %s\n", extra_events.buf);
	bzero(&parse_error, sizeof(parse_error));
	ret = parse_events(perf_evlist, extra_events.buf, &parse_error);
	if (ret) {
		parse_events_print_error(&parse_error, extra_events.buf);
		goto out;
	}
	strbuf_release(&extra_events);
	ret = metricgroup__setup_events(&group_list, metric_no_merge,
					perf_evlist, metric_events);
out:
	metricgroup__free_egroups(&group_list);
	return ret;
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
