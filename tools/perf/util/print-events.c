// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <api/fs/tracing_path.h>
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
#include "print-events.h"
#include "probe-file.h"
#include "string2.h"
#include "strlist.h"
#include "tracepoint.h"
#include "pfm.h"
#include "pmu-hybrid.h"

#define MAX_NAME_LEN 100

/** Strings corresponding to enum perf_type_id. */
static const char * const event_type_descriptors[] = {
	"Hardware event",
	"Software event",
	"Tracepoint event",
	"Hardware cache event",
	"Raw hardware event descriptor",
	"Hardware breakpoint",
};

static const struct event_symbol event_symbols_tool[PERF_TOOL_MAX] = {
	[PERF_TOOL_DURATION_TIME] = {
		.symbol = "duration_time",
		.alias  = "",
	},
	[PERF_TOOL_USER_TIME] = {
		.symbol = "user_time",
		.alias  = "",
	},
	[PERF_TOOL_SYSTEM_TIME] = {
		.symbol = "system_time",
		.alias  = "",
	},
};

/*
 * Print the events from <debugfs_mount_point>/tracing/events
 */
void print_tracepoint_events(const struct print_callbacks *print_cb, void *print_state)
{
	struct dirent **sys_namelist = NULL;
	int sys_items = tracing_events__scandir_alphasort(&sys_namelist);

	for (int i = 0; i < sys_items; i++) {
		struct dirent *sys_dirent = sys_namelist[i];
		struct dirent **evt_namelist = NULL;
		char *dir_path;
		int evt_items;

		if (sys_dirent->d_type != DT_DIR ||
		    !strcmp(sys_dirent->d_name, ".") ||
		    !strcmp(sys_dirent->d_name, ".."))
			continue;

		dir_path = get_events_file(sys_dirent->d_name);
		if (!dir_path)
			continue;

		evt_items = scandir(dir_path, &evt_namelist, NULL, alphasort);
		for (int j = 0; j < evt_items; j++) {
			struct dirent *evt_dirent = evt_namelist[j];
			char evt_path[MAXPATHLEN];

			if (evt_dirent->d_type != DT_DIR ||
			    !strcmp(evt_dirent->d_name, ".") ||
			    !strcmp(evt_dirent->d_name, ".."))
				continue;

			if (tp_event_has_id(dir_path, evt_dirent) != 0)
				continue;

			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent->d_name, evt_dirent->d_name);
			print_cb->print_event(print_state,
					/*topic=*/NULL,
					/*pmu_name=*/NULL,
					evt_path,
					/*event_alias=*/NULL,
					/*scale_unit=*/NULL,
					/*deprecated=*/false,
					"Tracepoint event",
					/*desc=*/NULL,
					/*long_desc=*/NULL,
					/*encoding_desc=*/NULL,
					/*metric_name=*/NULL,
					/*metric_expr=*/NULL);
		}
		free(dir_path);
		free(evt_namelist);
	}
	free(sys_namelist);
}

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
				evt_name ?: sdt_name->s,
				/*event_alias=*/NULL,
				/*deprecated=*/false,
				/*scale_unit=*/NULL,
				"SDT event",
				/*desc=*/NULL,
				/*long_desc=*/NULL,
				/*encoding_desc=*/NULL,
				/*metric_name=*/NULL,
				/*metric_expr=*/NULL);

		free(evt_name);
	}
	strlist__delete(sdtlist);
}

int print_hwcache_events(const struct print_callbacks *print_cb, void *print_state)
{
	struct strlist *evt_name_list = strlist__new(NULL, NULL);
	struct str_node *nd;

	if (!evt_name_list) {
		pr_debug("Failed to allocate new strlist for hwcache events\n");
		return -ENOMEM;
	}
	for (int type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (int op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!evsel__is_cache_op_valid(type, op))
				continue;

			for (int i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				struct perf_pmu *pmu = NULL;
				char name[64];

				__evsel__hw_cache_type_op_res_name(type, op, i, name, sizeof(name));
				if (!perf_pmu__has_hybrid()) {
					if (is_event_supported(PERF_TYPE_HW_CACHE,
							       type | (op << 8) | (i << 16)))
						strlist__add(evt_name_list, name);
					continue;
				}
				perf_pmu__for_each_hybrid_pmu(pmu) {
					if (is_event_supported(PERF_TYPE_HW_CACHE,
					    type | (op << 8) | (i << 16) |
					    ((__u64)pmu->type << PERF_PMU_TYPE_SHIFT))) {
						char new_name[128];
							snprintf(new_name, sizeof(new_name),
								 "%s/%s/", pmu->name, name);
							strlist__add(evt_name_list, new_name);
					}
				}
			}
		}
	}

	strlist__for_each_entry(nd, evt_name_list) {
		print_cb->print_event(print_state,
				"cache",
				/*pmu_name=*/NULL,
				nd->s,
				/*event_alias=*/NULL,
				/*scale_unit=*/NULL,
				/*deprecated=*/false,
				event_type_descriptors[PERF_TYPE_HW_CACHE],
				/*desc=*/NULL,
				/*long_desc=*/NULL,
				/*encoding_desc=*/NULL,
				/*metric_name=*/NULL,
				/*metric_expr=*/NULL);
	}
	strlist__delete(evt_name_list);
	return 0;
}

void print_tool_events(const struct print_callbacks *print_cb, void *print_state)
{
	// Start at 1 because the first enum entry means no tool event.
	for (int i = 1; i < PERF_TOOL_MAX; ++i) {
		print_cb->print_event(print_state,
				"tool",
				/*pmu_name=*/NULL,
				event_symbols_tool[i].symbol,
				event_symbols_tool[i].alias,
				/*scale_unit=*/NULL,
				/*deprecated=*/false,
				"Tool event",
				/*desc=*/NULL,
				/*long_desc=*/NULL,
				/*encoding_desc=*/NULL,
				/*metric_name=*/NULL,
				/*metric_expr=*/NULL);
	}
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
				nd->s,
				alias,
				/*scale_unit=*/NULL,
				/*deprecated=*/false,
				event_type_descriptors[type],
				/*desc=*/NULL,
				/*long_desc=*/NULL,
				/*encoding_desc=*/NULL,
				/*metric_name=*/NULL,
				/*metric_expr=*/NULL);
	}
	strlist__delete(evt_name_list);
}

/*
 * Print the help text for the event symbols:
 */
void print_events(const struct print_callbacks *print_cb, void *print_state)
{
	print_symbol_events(print_cb, print_state, PERF_TYPE_HARDWARE,
			event_symbols_hw, PERF_COUNT_HW_MAX);
	print_symbol_events(print_cb, print_state, PERF_TYPE_SOFTWARE,
			event_symbols_sw, PERF_COUNT_SW_MAX);

	print_tool_events(print_cb, print_state);

	print_hwcache_events(print_cb, print_state);

	print_pmu_events(print_cb, print_state);

	print_cb->print_event(print_state,
			/*topic=*/NULL,
			/*pmu_name=*/NULL,
			"rNNN",
			/*event_alias=*/NULL,
			/*scale_unit=*/NULL,
			/*deprecated=*/false,
			event_type_descriptors[PERF_TYPE_RAW],
			/*desc=*/NULL,
			/*long_desc=*/NULL,
			/*encoding_desc=*/NULL,
			/*metric_name=*/NULL,
			/*metric_expr=*/NULL);

	print_cb->print_event(print_state,
			/*topic=*/NULL,
			/*pmu_name=*/NULL,
			"cpu/t1=v1[,t2=v2,t3 ...]/modifier",
			/*event_alias=*/NULL,
			/*scale_unit=*/NULL,
			/*deprecated=*/false,
			event_type_descriptors[PERF_TYPE_RAW],
			"(see 'man perf-list' on how to encode it)",
			/*long_desc=*/NULL,
			/*encoding_desc=*/NULL,
			/*metric_name=*/NULL,
			/*metric_expr=*/NULL);

	print_cb->print_event(print_state,
			/*topic=*/NULL,
			/*pmu_name=*/NULL,
			"mem:<addr>[/len][:access]",
			/*scale_unit=*/NULL,
			/*event_alias=*/NULL,
			/*deprecated=*/false,
			event_type_descriptors[PERF_TYPE_BREAKPOINT],
			/*desc=*/NULL,
			/*long_desc=*/NULL,
			/*encoding_desc=*/NULL,
			/*metric_name=*/NULL,
			/*metric_expr=*/NULL);

	print_tracepoint_events(print_cb, print_state);

	print_sdt_events(print_cb, print_state);

	metricgroup__print(print_cb, print_state);

	print_libpfm_events(print_cb, print_state);
}
