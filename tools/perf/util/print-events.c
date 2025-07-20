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

/*
 * Print the events from <debugfs_mount_point>/tracing/events
 */
void print_tracepoint_events(const struct print_callbacks *print_cb __maybe_unused, void *print_state __maybe_unused)
{
	char *events_path = get_tracing_file("events");
	int events_fd = open(events_path, O_PATH);
	struct dirent **sys_namelist = NULL;
	int sys_items;

	if (events_fd < 0) {
		pr_err("Error: failed to open tracing events directory\n");
		pr_err("%s: %s\n", events_path, strerror(errno));
		return;
	}
	put_tracing_file(events_path);

	sys_items = tracing_events__scandir_alphasort(&sys_namelist);

	for (int i = 0; i < sys_items; i++) {
		struct dirent *sys_dirent = sys_namelist[i];
		struct dirent **evt_namelist = NULL;
		int dir_fd;
		int evt_items;

		if (sys_dirent->d_type != DT_DIR ||
		    !strcmp(sys_dirent->d_name, ".") ||
		    !strcmp(sys_dirent->d_name, ".."))
			goto next_sys;

		dir_fd = openat(events_fd, sys_dirent->d_name, O_PATH);
		if (dir_fd < 0)
			goto next_sys;

		evt_items = scandirat(events_fd, sys_dirent->d_name, &evt_namelist, NULL, alphasort);
		for (int j = 0; j < evt_items; j++) {
			/*
			 * Buffer sized at twice the max filename length + 1
			 * separator + 1 \0 terminator.
			 */
			char buf[NAME_MAX * 2 + 2];
			/* 16 possible hex digits and 22 other characters and \0. */
			char encoding[16 + 22];
			struct dirent *evt_dirent = evt_namelist[j];
			struct io id;
			__u64 config;

			if (evt_dirent->d_type != DT_DIR ||
			    !strcmp(evt_dirent->d_name, ".") ||
			    !strcmp(evt_dirent->d_name, ".."))
				goto next_evt;

			snprintf(buf, sizeof(buf), "%s/id", evt_dirent->d_name);
			io__init(&id, openat(dir_fd, buf, O_RDONLY), buf, sizeof(buf));

			if (id.fd < 0)
				goto next_evt;

			if (io__get_dec(&id, &config) < 0) {
				close(id.fd);
				goto next_evt;
			}
			close(id.fd);

			snprintf(buf, sizeof(buf), "%s:%s",
				 sys_dirent->d_name, evt_dirent->d_name);
			snprintf(encoding, sizeof(encoding), "tracepoint/config=0x%llx/", config);
			print_cb->print_event(print_state,
					/*topic=*/NULL,
					/*pmu_name=*/NULL, /* really "tracepoint" */
					/*event_name=*/buf,
					/*event_alias=*/NULL,
					/*scale_unit=*/NULL,
					/*deprecated=*/false,
					"Tracepoint event",
					/*desc=*/NULL,
					/*long_desc=*/NULL,
					encoding);
next_evt:
			free(evt_namelist[j]);
		}
		close(dir_fd);
		free(evt_namelist);
next_sys:
		free(sys_namelist[i]);
	}

	free(sys_namelist);
	close(events_fd);
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

/*
 * Print the help text for the event symbols:
 */
void print_events(const struct print_callbacks *print_cb, void *print_state)
{
	print_symbol_events(print_cb, print_state, PERF_TYPE_HARDWARE,
			event_symbols_hw, PERF_COUNT_HW_MAX);
	print_symbol_events(print_cb, print_state, PERF_TYPE_SOFTWARE,
			event_symbols_sw, PERF_COUNT_SW_MAX);

	print_hwcache_events(print_cb, print_state);

	perf_pmus__print_pmu_events(print_cb, print_state);

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
			/*encoding_desc=*/NULL);

	perf_pmus__print_raw_pmu_events(print_cb, print_state);

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
			/*encoding_desc=*/NULL);

	print_tracepoint_events(print_cb, print_state);

	print_sdt_events(print_cb, print_state);

	metricgroup__print(print_cb, print_state);

	print_libpfm_events(print_cb, print_state);
}
