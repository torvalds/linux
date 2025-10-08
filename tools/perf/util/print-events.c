// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/param.h>
#include <unistd.h>

#include <api/fs/tracing_path.h>
#include <api/io.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include <subcmd/pager.h>

#include "build-id.h"
#include "debug.h"
#include "evsel.h"
#include "metricgroup.h"
#include "parse-events.h"
#include "pmu.h"
#include "pmus.h"
#include "print-events.h"
#include "probe-file.h"
#include "string2.h"
#include "strlist.h"
#include "tracepoint.h"
#include "pfm.h"
#include "thread_map.h"
#include "tool_pmu.h"
#include "util.h"

#define MAX_NAME_LEN 100

/** Strings corresponding to enum perf_type_id. */
static const char * const event_type_descriptors[] = {
	"Hardware event",
	"Software event",
	"Tracepoint event",
	"Hardware cache event",
	"Raw event descriptor",
	"Hardware breakpoint",
};

void print_sdt_events(const struct print_callbacks *print_cb, void *print_state)
{
	struct strlist *bidlist, *sdtlist;
	struct str_node *bid_nd, *sdt_name, *next_sdt_name;
	const char *last_sdt_name = NULL;

	/*
	 * The implicitly sorted sdtlist will hold the tracepoint name followed
	 * by @<buildid>. If the tracepoint name is unique (determined by
	 * looking at the adjacent nodes) the @<buildid> is dropped otherwise
	 * the executable path and buildid are added to the name.
	 */
	sdtlist = strlist__new(NULL, NULL);
	if (!sdtlist) {
		pr_debug("Failed to allocate new strlist for SDT\n");
		return;
	}
	bidlist = build_id_cache__list_all(true);
	if (!bidlist) {
		pr_debug("Failed to get buildids: %d\n", errno);
		return;
	}
	strlist__for_each_entry(bid_nd, bidlist) {
		struct probe_cache *pcache;
		struct probe_cache_entry *ent;

		pcache = probe_cache__new(bid_nd->s, NULL);
		if (!pcache)
			continue;
		list_for_each_entry(ent, &pcache->entries, node) {
			char buf[1024];

			snprintf(buf, sizeof(buf), "%s:%s@%s",
				 ent->pev.group, ent->pev.event, bid_nd->s);
			strlist__add(sdtlist, buf);
		}
		probe_cache__delete(pcache);
	}
	strlist__delete(bidlist);

	strlist__for_each_entry(sdt_name, sdtlist) {
		bool show_detail = false;
		char *bid = strchr(sdt_name->s, '@');
		char *evt_name = NULL;

		if (bid)
			*(bid++) = '\0';

		if (last_sdt_name && !strcmp(last_sdt_name, sdt_name->s)) {
			show_detail = true;
		} else {
			next_sdt_name = strlist__next(sdt_name);
			if (next_sdt_name) {
				char *bid2 = strchr(next_sdt_name->s, '@');

				if (bid2)
					*bid2 = '\0';
				if (strcmp(sdt_name->s, next_sdt_name->s) == 0)
					show_detail = true;
				if (bid2)
					*bid2 = '@';
			}
		}
		last_sdt_name = sdt_name->s;

		if (show_detail) {
			char *path = build_id_cache__origname(bid);

			if (path) {
				if (asprintf(&evt_name, "%s@%s(%.12s)", sdt_name->s, path, bid) < 0)
					evt_name = NULL;
				free(path);
			}
		}
		print_cb->print_event(print_state,
				/*topic=*/NULL,
				/*pmu_name=*/NULL,
				PERF_TYPE_TRACEPOINT,
				evt_name ?: sdt_name->s,
				/*event_alias=*/NULL,
				/*deprecated=*/false,
				/*scale_unit=*/NULL,
				"SDT event",
				/*desc=*/NULL,
				/*long_desc=*/NULL,
				/*encoding_desc=*/NULL);

		free(evt_name);
	}
	strlist__delete(sdtlist);
}

bool is_event_supported(u8 type, u64 config)
{
	bool ret = true;
	struct evsel *evsel;
	struct perf_event_attr attr = {
		.type = type,
		.config = config,
		.disabled = 1,
	};
	struct perf_thread_map *tmap = thread_map__new_by_tid(0);

	if (tmap == NULL)
		return false;

	evsel = evsel__new(&attr);
	if (evsel) {
		ret = evsel__open(evsel, NULL, tmap) >= 0;

		if (!ret) {
			/*
			 * The event may fail to open if the paranoid value
			 * /proc/sys/kernel/perf_event_paranoid is set to 2
			 * Re-run with exclude_kernel set; we don't do that by
			 * default as some ARM machines do not support it.
			 */
			evsel->core.attr.exclude_kernel = 1;
			ret = evsel__open(evsel, NULL, tmap) >= 0;
		}

		if (!ret) {
			/*
			 * The event may fail to open if the PMU requires
			 * exclude_guest to be set (e.g. as the Apple M1 PMU
			 * requires).
			 * Re-run with exclude_guest set; we don't do that by
			 * default as it's equally legitimate for another PMU
			 * driver to require that exclude_guest is clear.
			 */
			evsel->core.attr.exclude_guest = 1;
			ret = evsel__open(evsel, NULL, tmap) >= 0;
		}

		evsel__close(evsel);
		evsel__delete(evsel);
	}

	perf_thread_map__put(tmap);
	return ret;
}

int print_hwcache_events(const struct print_callbacks *print_cb, void *print_state)
{
	struct perf_pmu *pmu = NULL;
	const char *event_type_descriptor = event_type_descriptors[PERF_TYPE_HW_CACHE];

	/*
	 * Only print core PMUs, skipping uncore for performance and
	 * PERF_TYPE_SOFTWARE that can succeed in opening legacy cache evenst.
	 */
	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		if (pmu->is_uncore || pmu->type == PERF_TYPE_SOFTWARE)
			continue;

		for (int type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
			for (int op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
				/* skip invalid cache type */
				if (!evsel__is_cache_op_valid(type, op))
					continue;

				for (int res = 0; res < PERF_COUNT_HW_CACHE_RESULT_MAX; res++) {
					char name[64];
					char alias_name[128];
					__u64 config;
					int ret;

					__evsel__hw_cache_type_op_res_name(type, op, res,
									name, sizeof(name));

					ret = parse_events__decode_legacy_cache(name, pmu->type,
										&config);
					if (ret || !is_event_supported(PERF_TYPE_HW_CACHE, config))
						continue;
					snprintf(alias_name, sizeof(alias_name), "%s/%s/",
						 pmu->name, name);
					print_cb->print_event(print_state,
							"cache",
							pmu->name,
							pmu->type,
							name,
							alias_name,
							/*scale_unit=*/NULL,
							/*deprecated=*/false,
							event_type_descriptor,
							/*desc=*/NULL,
							/*long_desc=*/NULL,
							/*encoding_desc=*/NULL);
				}
			}
		}
	}
	return 0;
}

void print_symbol_events(const struct print_callbacks *print_cb, void *print_state,
			 unsigned int type, const struct event_symbol *syms,
			 unsigned int max)
{
	struct strlist *evt_name_list = strlist__new(NULL, NULL);
	struct str_node *nd;

	if (!evt_name_list) {
		pr_debug("Failed to allocate new strlist for symbol events\n");
		return;
	}
	for (unsigned int i = 0; i < max; i++) {
		/*
		 * New attr.config still not supported here, the latest
		 * example was PERF_COUNT_SW_CGROUP_SWITCHES
		 */
		if (syms[i].symbol == NULL)
			continue;

		if (!is_event_supported(type, i))
			continue;

		if (strlen(syms[i].alias)) {
			char name[MAX_NAME_LEN];

			snprintf(name, MAX_NAME_LEN, "%s OR %s", syms[i].symbol, syms[i].alias);
			strlist__add(evt_name_list, name);
		} else
			strlist__add(evt_name_list, syms[i].symbol);
	}

	strlist__for_each_entry(nd, evt_name_list) {
		char *alias = strstr(nd->s, " OR ");

		if (alias) {
			*alias = '\0';
			alias += 4;
		}
		print_cb->print_event(print_state,
				/*topic=*/NULL,
				/*pmu_name=*/NULL,
				type,
				nd->s,
				alias,
				/*scale_unit=*/NULL,
				/*deprecated=*/false,
				event_type_descriptors[type],
				/*desc=*/NULL,
				/*long_desc=*/NULL,
				/*encoding_desc=*/NULL);
	}
	strlist__delete(evt_name_list);
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
	const char *pmu_name;
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

static int metricgroup__add_to_mep_groups_callback(const struct pmu_metric *pm,
					const struct pmu_metrics_table *table __maybe_unused,
					void *vdata)
{
	struct rblist *groups = vdata;
	const char *g;
	char *omg, *mg;

	mg = strdup(pm->metric_group ?: pm->metric_name);
	if (!mg)
		return -ENOMEM;
	omg = mg;
	while ((g = strsep(&mg, ";")) != NULL) {
		struct mep *me;

		g = skip_spaces(g);
		if (strlen(g))
			me = mep_lookup(groups, g, pm->metric_name);
		else
			me = mep_lookup(groups, pm->metric_name, pm->metric_name);

		if (me) {
			me->metric_desc = pm->desc;
			me->metric_long_desc = pm->long_desc;
			me->metric_expr = pm->metric_expr;
			me->metric_threshold = pm->metric_threshold;
			me->metric_unit = pm->unit;
			me->pmu_name = pm->pmu;
		}
	}
	free(omg);

	return 0;
}

void metricgroup__print(const struct print_callbacks *print_cb, void *print_state)
{
	struct rblist groups;
	struct rb_node *node, *next;
	const struct pmu_metrics_table *table = pmu_metrics_table__find();

	rblist__init(&groups);
	groups.node_new = mep_new;
	groups.node_cmp = mep_cmp;
	groups.node_delete = mep_delete;

	metricgroup__for_each_metric(table, metricgroup__add_to_mep_groups_callback, &groups);

	for (node = rb_first_cached(&groups.entries); node; node = next) {
		struct mep *me = container_of(node, struct mep, nd);

		print_cb->print_metric(print_state,
				me->metric_group,
				me->metric_name,
				me->metric_desc,
				me->metric_long_desc,
				me->metric_expr,
				me->metric_threshold,
				me->metric_unit,
				me->pmu_name);
		next = rb_next(node);
		rblist__remove_node(&groups, node);
	}
}

/*
 * Print the help text for the event symbols:
 */
void print_events(const struct print_callbacks *print_cb, void *print_state)
{
	print_symbol_events(print_cb, print_state, PERF_TYPE_HARDWARE,
			event_symbols_hw, PERF_COUNT_HW_MAX);

	print_hwcache_events(print_cb, print_state);

	perf_pmus__print_pmu_events(print_cb, print_state);

	print_cb->print_event(print_state,
			/*topic=*/NULL,
			/*pmu_name=*/NULL,
			PERF_TYPE_RAW,
			"rNNN",
			/*event_alias=*/NULL,
			/*scale_unit=*/NULL,
			/*deprecated=*/false,
			event_type_descriptors[PERF_TYPE_RAW],
			/*desc=*/NULL,
			/*long_desc=*/NULL,
			/*encoding_desc=*/NULL);

	perf_pmus__print_raw_pmu_events(print_cb, print_state);

	print_cb->print_event(print_state,
			/*topic=*/NULL,
			/*pmu_name=*/NULL,
			PERF_TYPE_BREAKPOINT,
			"mem:<addr>[/len][:access]",
			/*scale_unit=*/NULL,
			/*event_alias=*/NULL,
			/*deprecated=*/false,
			event_type_descriptors[PERF_TYPE_BREAKPOINT],
			/*desc=*/NULL,
			/*long_desc=*/NULL,
			/*encoding_desc=*/NULL);

	print_sdt_events(print_cb, print_state);

	metricgroup__print(print_cb, print_state);

	print_libpfm_events(print_cb, print_state);
}
