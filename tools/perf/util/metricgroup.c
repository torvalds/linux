/*
 * Copyright (c) 2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

/* Manage metrics and groups of metrics from JSON files */

#include "metricgroup.h"
#include "evlist.h"
#include "strbuf.h"
#include "pmu.h"
#include "expr.h"
#include "rblist.h"
#include "pmu.h"
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "pmu-events/pmu-events.h"
#include "strbuf.h"
#include "strlist.h"
#include <assert.h>
#include <ctype.h>

struct metric_event *metricgroup__lookup(struct rblist *metric_events,
					 struct perf_evsel *evsel,
					 bool create)
{
	struct rb_node *nd;
	struct metric_event me = {
		.evsel = evsel
	};
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
	int idnum;
	const char **ids;
	const char *metric_name;
	const char *metric_expr;
};

static struct perf_evsel *find_evsel(struct perf_evlist *perf_evlist,
				     const char **ids,
				     int idnum,
				     struct perf_evsel **metric_events)
{
	struct perf_evsel *ev, *start = NULL;
	int ind = 0;

	evlist__for_each_entry (perf_evlist, ev) {
		if (!strcmp(ev->name, ids[ind])) {
			metric_events[ind] = ev;
			if (ind == 0)
				start = ev;
			if (++ind == idnum) {
				metric_events[ind] = NULL;
				return start;
			}
		} else {
			ind = 0;
			start = NULL;
		}
	}
	/*
	 * This can happen when an alias expands to multiple
	 * events, like for uncore events.
	 * We don't support this case for now.
	 */
	return NULL;
}

static int metricgroup__setup_events(struct list_head *groups,
				     struct perf_evlist *perf_evlist,
				     struct rblist *metric_events_list)
{
	struct metric_event *me;
	struct metric_expr *expr;
	int i = 0;
	int ret = 0;
	struct egroup *eg;
	struct perf_evsel *evsel;

	list_for_each_entry (eg, groups, nd) {
		struct perf_evsel **metric_events;

		metric_events = calloc(sizeof(void *), eg->idnum + 1);
		if (!metric_events) {
			ret = -ENOMEM;
			break;
		}
		evsel = find_evsel(perf_evlist, eg->ids, eg->idnum,
				   metric_events);
		if (!evsel) {
			pr_debug("Cannot resolve %s: %s\n",
					eg->metric_name, eg->metric_expr);
			continue;
		}
		for (i = 0; i < eg->idnum; i++)
			metric_events[i]->collect_stat = true;
		me = metricgroup__lookup(metric_events_list, evsel, true);
		if (!me) {
			ret = -ENOMEM;
			break;
		}
		expr = malloc(sizeof(struct metric_expr));
		if (!expr) {
			ret = -ENOMEM;
			break;
		}
		expr->metric_expr = eg->metric_expr;
		expr->metric_name = eg->metric_name;
		expr->metric_events = metric_events;
		list_add(&expr->nd, &me->head);
	}
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

static int metricgroup__add_metric(const char *metric, struct strbuf *events,
				   struct list_head *group_list)
{
	struct pmu_events_map *map = perf_pmu__find_map();
	struct pmu_event *pe;
	int ret = -EINVAL;
	int i, j;

	strbuf_init(events, 100);
	strbuf_addf(events, "%s", "");

	if (!map)
		return 0;

	for (i = 0; ; i++) {
		pe = &map->table[i];

		if (!pe->name && !pe->metric_group && !pe->metric_name)
			break;
		if (!pe->metric_expr)
			continue;
		if (match_metric(pe->metric_group, metric) ||
		    match_metric(pe->metric_name, metric)) {
			const char **ids;
			int idnum;
			struct egroup *eg;

			pr_debug("metric expr %s for %s\n", pe->metric_expr, pe->metric_name);

			if (expr__find_other(pe->metric_expr,
					     NULL, &ids, &idnum) < 0)
				continue;
			if (events->len > 0)
				strbuf_addf(events, ",");
			for (j = 0; j < idnum; j++) {
				pr_debug("found event %s\n", ids[j]);
				strbuf_addf(events, "%s%s",
					j == 0 ? "{" : ",",
					ids[j]);
			}
			strbuf_addf(events, "}:W");

			eg = malloc(sizeof(struct egroup));
			if (!eg) {
				ret = -ENOMEM;
				break;
			}
			eg->ids = ids;
			eg->idnum = idnum;
			eg->metric_name = pe->metric_name;
			eg->metric_expr = pe->metric_expr;
			list_add_tail(&eg->nd, group_list);
			ret = 0;
		}
	}
	return ret;
}

static int metricgroup__add_metric_list(const char *list, struct strbuf *events,
				        struct list_head *group_list)
{
	char *llist, *nlist, *p;
	int ret = -EINVAL;

	nlist = strdup(list);
	if (!nlist)
		return -ENOMEM;
	llist = nlist;
	while ((p = strsep(&llist, ",")) != NULL) {
		ret = metricgroup__add_metric(p, events, group_list);
		if (ret == -EINVAL) {
			fprintf(stderr, "Cannot find metric or group `%s'\n",
					p);
			break;
		}
	}
	free(nlist);
	return ret;
}

static void metricgroup__free_egroups(struct list_head *group_list)
{
	struct egroup *eg, *egtmp;
	int i;

	list_for_each_entry_safe (eg, egtmp, group_list, nd) {
		for (i = 0; i < eg->idnum; i++)
			free((char *)eg->ids[i]);
		free(eg->ids);
		free(eg);
	}
}

int metricgroup__parse_groups(const struct option *opt,
			   const char *str,
			   struct rblist *metric_events)
{
	struct parse_events_error parse_error;
	struct perf_evlist *perf_evlist = *(struct perf_evlist **)opt->value;
	struct strbuf extra_events;
	LIST_HEAD(group_list);
	int ret;

	if (metric_events->nr_entries == 0)
		metricgroup__rblist_init(metric_events);
	ret = metricgroup__add_metric_list(str, &extra_events, &group_list);
	if (ret)
		return ret;
	pr_debug("adding %s\n", extra_events.buf);
	memset(&parse_error, 0, sizeof(struct parse_events_error));
	ret = parse_events(perf_evlist, extra_events.buf, &parse_error);
	if (ret) {
		pr_err("Cannot set up events %s\n", extra_events.buf);
		goto out;
	}
	strbuf_release(&extra_events);
	ret = metricgroup__setup_events(&group_list, perf_evlist,
					metric_events);
out:
	metricgroup__free_egroups(&group_list);
	return ret;
}
