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

static int cmp_string(const void *a, const void *b)
{
	const char * const *as = a;
	const char * const *bs = b;

	return strcmp(*as, *bs);
}

/*
 * Print the events from <debugfs_mount_point>/tracing/events
 */
void print_tracepoint_events(const char *subsys_glob,
			     const char *event_glob, bool name_only)
{
	struct dirent **sys_namelist = NULL;
	bool printed = false;
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

		if (subsys_glob != NULL &&
		    !strglobmatch(sys_dirent->d_name, subsys_glob))
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

			if (event_glob != NULL &&
			    !strglobmatch(evt_dirent->d_name, event_glob))
				continue;

			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent->d_name, evt_dirent->d_name);
			if (name_only)
				printf("%s ", evt_path);
			else {
				printf("  %-50s [%s]\n", evt_path,
				       event_type_descriptors[PERF_TYPE_TRACEPOINT]);
			}
			printed = true;
		}
		free(dir_path);
		free(evt_namelist);
	}
	free(sys_namelist);
	if (printed && pager_in_use())
		printf("\n");
}

void print_sdt_events(const char *subsys_glob, const char *event_glob,
		      bool name_only)
{
	struct probe_cache *pcache;
	struct probe_cache_entry *ent;
	struct strlist *bidlist, *sdtlist;
	struct strlist_config cfg = {.dont_dupstr = true};
	struct str_node *nd, *nd2;
	char *buf, *path, *ptr = NULL;
	bool show_detail = false;
	int ret;

	sdtlist = strlist__new(NULL, &cfg);
	if (!sdtlist) {
		pr_debug("Failed to allocate new strlist for SDT\n");
		return;
	}
	bidlist = build_id_cache__list_all(true);
	if (!bidlist) {
		pr_debug("Failed to get buildids: %d\n", errno);
		return;
	}
	strlist__for_each_entry(nd, bidlist) {
		pcache = probe_cache__new(nd->s, NULL);
		if (!pcache)
			continue;
		list_for_each_entry(ent, &pcache->entries, node) {
			if (!ent->sdt)
				continue;
			if (subsys_glob &&
			    !strglobmatch(ent->pev.group, subsys_glob))
				continue;
			if (event_glob &&
			    !strglobmatch(ent->pev.event, event_glob))
				continue;
			ret = asprintf(&buf, "%s:%s@%s", ent->pev.group,
					ent->pev.event, nd->s);
			if (ret > 0)
				strlist__add(sdtlist, buf);
		}
		probe_cache__delete(pcache);
	}
	strlist__delete(bidlist);

	strlist__for_each_entry(nd, sdtlist) {
		buf = strchr(nd->s, '@');
		if (buf)
			*(buf++) = '\0';
		if (name_only) {
			printf("%s ", nd->s);
			continue;
		}
		nd2 = strlist__next(nd);
		if (nd2) {
			ptr = strchr(nd2->s, '@');
			if (ptr)
				*ptr = '\0';
			if (strcmp(nd->s, nd2->s) == 0)
				show_detail = true;
		}
		if (show_detail) {
			path = build_id_cache__origname(buf);
			ret = asprintf(&buf, "%s@%s(%.12s)", nd->s, path, buf);
			if (ret > 0) {
				printf("  %-50s [%s]\n", buf, "SDT event");
				free(buf);
			}
			free(path);
		} else
			printf("  %-50s [%s]\n", nd->s, "SDT event");
		if (nd2) {
			if (strcmp(nd->s, nd2->s) != 0)
				show_detail = false;
			if (ptr)
				*ptr = '@';
		}
	}
	strlist__delete(sdtlist);
}

int print_hwcache_events(const char *event_glob, bool name_only)
{
	unsigned int type, op, i, evt_i = 0, evt_num = 0, npmus = 0;
	char name[64], new_name[128];
	char **evt_list = NULL, **evt_pmus = NULL;
	bool evt_num_known = false;
	struct perf_pmu *pmu = NULL;

	if (perf_pmu__has_hybrid()) {
		npmus = perf_pmu__hybrid_pmu_num();
		evt_pmus = zalloc(sizeof(char *) * npmus);
		if (!evt_pmus)
			goto out_enomem;
	}

restart:
	if (evt_num_known) {
		evt_list = zalloc(sizeof(char *) * evt_num);
		if (!evt_list)
			goto out_enomem;
	}

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				unsigned int hybrid_supported = 0, j;
				bool supported;

				__evsel__hw_cache_type_op_res_name(type, op, i, name, sizeof(name));
				if (event_glob != NULL && !strglobmatch(name, event_glob))
					continue;

				if (!perf_pmu__has_hybrid()) {
					if (!is_event_supported(PERF_TYPE_HW_CACHE,
								type | (op << 8) | (i << 16))) {
						continue;
					}
				} else {
					perf_pmu__for_each_hybrid_pmu(pmu) {
						if (!evt_num_known) {
							evt_num++;
							continue;
						}

						supported = is_event_supported(
							PERF_TYPE_HW_CACHE,
							type | (op << 8) | (i << 16) |
							((__u64)pmu->type << PERF_PMU_TYPE_SHIFT));
						if (supported) {
							snprintf(new_name, sizeof(new_name),
								 "%s/%s/", pmu->name, name);
							evt_pmus[hybrid_supported] =
								strdup(new_name);
							hybrid_supported++;
						}
					}

					if (hybrid_supported == 0)
						continue;
				}

				if (!evt_num_known) {
					evt_num++;
					continue;
				}

				if ((hybrid_supported == 0) ||
				    (hybrid_supported == npmus)) {
					evt_list[evt_i] = strdup(name);
					for (j = 0; j < npmus; j++)
						zfree(&evt_pmus[j]);
				} else {
					for (j = 0; j < hybrid_supported; j++) {
						evt_list[evt_i++] = evt_pmus[j];
						evt_pmus[j] = NULL;
					}
					continue;
				}

				if (evt_list[evt_i] == NULL)
					goto out_enomem;
				evt_i++;
			}
		}
	}

	if (!evt_num_known) {
		evt_num_known = true;
		goto restart;
	}

	for (evt_i = 0; evt_i < evt_num; evt_i++) {
		if (!evt_list[evt_i])
			break;
	}

	evt_num = evt_i;
	qsort(evt_list, evt_num, sizeof(char *), cmp_string);
	evt_i = 0;
	while (evt_i < evt_num) {
		if (name_only) {
			printf("%s ", evt_list[evt_i++]);
			continue;
		}
		printf("  %-50s [%s]\n", evt_list[evt_i++],
				event_type_descriptors[PERF_TYPE_HW_CACHE]);
	}
	if (evt_num && pager_in_use())
		printf("\n");

out_free:
	evt_num = evt_i;
	for (evt_i = 0; evt_i < evt_num; evt_i++)
		zfree(&evt_list[evt_i]);
	zfree(&evt_list);

	for (evt_i = 0; evt_i < npmus; evt_i++)
		zfree(&evt_pmus[evt_i]);
	zfree(&evt_pmus);
	return evt_num;

out_enomem:
	printf("FATAL: not enough memory to print %s\n",
		event_type_descriptors[PERF_TYPE_HW_CACHE]);
	if (evt_list)
		goto out_free;
	return evt_num;
}

static void print_tool_event(const struct event_symbol *syms, const char *event_glob,
			     bool name_only)
{
	if (syms->symbol == NULL)
		return;

	if (event_glob && !(strglobmatch(syms->symbol, event_glob) ||
	      (syms->alias && strglobmatch(syms->alias, event_glob))))
		return;

	if (name_only)
		printf("%s ", syms->symbol);
	else {
		char name[MAX_NAME_LEN];

		if (syms->alias && strlen(syms->alias))
			snprintf(name, MAX_NAME_LEN, "%s OR %s", syms->symbol, syms->alias);
		else
			strlcpy(name, syms->symbol, MAX_NAME_LEN);
		printf("  %-50s [%s]\n", name, "Tool event");
	}
}

void print_tool_events(const char *event_glob, bool name_only)
{
	// Start at 1 because the first enum entry means no tool event.
	for (int i = 1; i < PERF_TOOL_MAX; ++i)
		print_tool_event(event_symbols_tool + i, event_glob, name_only);

	if (pager_in_use())
		printf("\n");
}

void print_symbol_events(const char *event_glob, unsigned int type,
			 struct event_symbol *syms, unsigned int max,
			 bool name_only)
{
	unsigned int i, evt_i = 0, evt_num = 0;
	char name[MAX_NAME_LEN];
	char **evt_list = NULL;
	bool evt_num_known = false;

restart:
	if (evt_num_known) {
		evt_list = zalloc(sizeof(char *) * evt_num);
		if (!evt_list)
			goto out_enomem;
		syms -= max;
	}

	for (i = 0; i < max; i++, syms++) {
		/*
		 * New attr.config still not supported here, the latest
		 * example was PERF_COUNT_SW_CGROUP_SWITCHES
		 */
		if (syms->symbol == NULL)
			continue;

		if (event_glob != NULL && !(strglobmatch(syms->symbol, event_glob) ||
		      (syms->alias && strglobmatch(syms->alias, event_glob))))
			continue;

		if (!is_event_supported(type, i))
			continue;

		if (!evt_num_known) {
			evt_num++;
			continue;
		}

		if (!name_only && strlen(syms->alias))
			snprintf(name, MAX_NAME_LEN, "%s OR %s", syms->symbol, syms->alias);
		else
			strlcpy(name, syms->symbol, MAX_NAME_LEN);

		evt_list[evt_i] = strdup(name);
		if (evt_list[evt_i] == NULL)
			goto out_enomem;
		evt_i++;
	}

	if (!evt_num_known) {
		evt_num_known = true;
		goto restart;
	}
	qsort(evt_list, evt_num, sizeof(char *), cmp_string);
	evt_i = 0;
	while (evt_i < evt_num) {
		if (name_only) {
			printf("%s ", evt_list[evt_i++]);
			continue;
		}
		printf("  %-50s [%s]\n", evt_list[evt_i++], event_type_descriptors[type]);
	}
	if (evt_num && pager_in_use())
		printf("\n");

out_free:
	evt_num = evt_i;
	for (evt_i = 0; evt_i < evt_num; evt_i++)
		zfree(&evt_list[evt_i]);
	zfree(&evt_list);
	return;

out_enomem:
	printf("FATAL: not enough memory to print %s\n", event_type_descriptors[type]);
	if (evt_list)
		goto out_free;
}

/*
 * Print the help text for the event symbols:
 */
void print_events(const char *event_glob, bool name_only, bool quiet_flag,
			bool long_desc, bool details_flag, bool deprecated,
			const char *pmu_name)
{
	print_symbol_events(event_glob, PERF_TYPE_HARDWARE,
			    event_symbols_hw, PERF_COUNT_HW_MAX, name_only);

	print_symbol_events(event_glob, PERF_TYPE_SOFTWARE,
			    event_symbols_sw, PERF_COUNT_SW_MAX, name_only);
	print_tool_events(event_glob, name_only);

	print_hwcache_events(event_glob, name_only);

	print_pmu_events(event_glob, name_only, quiet_flag, long_desc,
			details_flag, deprecated, pmu_name);

	if (event_glob != NULL)
		return;

	if (!name_only) {
		printf("  %-50s [%s]\n",
		       "rNNN",
		       event_type_descriptors[PERF_TYPE_RAW]);
		printf("  %-50s [%s]\n",
		       "cpu/t1=v1[,t2=v2,t3 ...]/modifier",
		       event_type_descriptors[PERF_TYPE_RAW]);
		if (pager_in_use())
			printf("   (see 'man perf-list' on how to encode it)\n\n");

		printf("  %-50s [%s]\n",
		       "mem:<addr>[/len][:access]",
			event_type_descriptors[PERF_TYPE_BREAKPOINT]);
		if (pager_in_use())
			printf("\n");
	}

	print_tracepoint_events(NULL, NULL, name_only);

	print_sdt_events(NULL, NULL, name_only);

	metricgroup__print(true, true, NULL, name_only, details_flag,
			   pmu_name);

	print_libpfm_events(name_only, long_desc);
}
