// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-list.c
 *
 * Builtin list command: list all event types
 *
 * Copyright (C) 2009, Thomas Gleixner <tglx@linutronix.de>
 * Copyright (C) 2008-2009, Red Hat Inc, Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "builtin.h"

#include "perf.h"

#include "util/parse-events.h"
#include "util/cache.h"
#include "util/pmu.h"
#include "util/debug.h"
#include "util/metricgroup.h"
#include <subcmd/parse-options.h>

static bool desc_flag = true;
static bool details_flag;

int cmd_list(int argc, const char **argv)
{
	int i;
	bool raw_dump = false;
	bool long_desc_flag = false;
	struct option list_options[] = {
		OPT_BOOLEAN(0, "raw-dump", &raw_dump, "Dump raw events"),
		OPT_BOOLEAN('d', "desc", &desc_flag,
			    "Print extra event descriptions. --no-desc to not print."),
		OPT_BOOLEAN('v', "long-desc", &long_desc_flag,
			    "Print longer event descriptions."),
		OPT_BOOLEAN(0, "details", &details_flag,
			    "Print information on the perf event names and expressions used internally by events."),
		OPT_INCR(0, "debug", &verbose,
			     "Enable debugging output"),
		OPT_END()
	};
	const char * const list_usage[] = {
		"perf list [<options>] [hw|sw|cache|tracepoint|pmu|sdt|event_glob]",
		NULL
	};

	set_option_flag(list_options, 0, "raw-dump", PARSE_OPT_HIDDEN);

	argc = parse_options(argc, argv, list_options, list_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	setup_pager();

	if (!raw_dump && pager_in_use())
		printf("\nList of pre-defined events (to be used in -e):\n\n");

	if (argc == 0) {
		print_events(NULL, raw_dump, !desc_flag, long_desc_flag,
				details_flag);
		return 0;
	}

	for (i = 0; i < argc; ++i) {
		char *sep, *s;

		if (strcmp(argv[i], "tracepoint") == 0)
			print_tracepoint_events(NULL, NULL, raw_dump);
		else if (strcmp(argv[i], "hw") == 0 ||
			 strcmp(argv[i], "hardware") == 0)
			print_symbol_events(NULL, PERF_TYPE_HARDWARE,
					event_symbols_hw, PERF_COUNT_HW_MAX, raw_dump);
		else if (strcmp(argv[i], "sw") == 0 ||
			 strcmp(argv[i], "software") == 0) {
			print_symbol_events(NULL, PERF_TYPE_SOFTWARE,
					event_symbols_sw, PERF_COUNT_SW_MAX, raw_dump);
			print_tool_events(NULL, raw_dump);
		} else if (strcmp(argv[i], "cache") == 0 ||
			 strcmp(argv[i], "hwcache") == 0)
			print_hwcache_events(NULL, raw_dump);
		else if (strcmp(argv[i], "pmu") == 0)
			print_pmu_events(NULL, raw_dump, !desc_flag,
						long_desc_flag, details_flag);
		else if (strcmp(argv[i], "sdt") == 0)
			print_sdt_events(NULL, NULL, raw_dump);
		else if (strcmp(argv[i], "metric") == 0)
			metricgroup__print(true, false, NULL, raw_dump, details_flag);
		else if (strcmp(argv[i], "metricgroup") == 0)
			metricgroup__print(false, true, NULL, raw_dump, details_flag);
		else if ((sep = strchr(argv[i], ':')) != NULL) {
			int sep_idx;

			if (sep == NULL) {
				print_events(argv[i], raw_dump, !desc_flag,
							long_desc_flag,
							details_flag);
				continue;
			}
			sep_idx = sep - argv[i];
			s = strdup(argv[i]);
			if (s == NULL)
				return -1;

			s[sep_idx] = '\0';
			print_tracepoint_events(s, s + sep_idx + 1, raw_dump);
			print_sdt_events(s, s + sep_idx + 1, raw_dump);
			metricgroup__print(true, true, s, raw_dump, details_flag);
			free(s);
		} else {
			if (asprintf(&s, "*%s*", argv[i]) < 0) {
				printf("Critical: Not enough memory! Trying to continue...\n");
				continue;
			}
			print_symbol_events(s, PERF_TYPE_HARDWARE,
					    event_symbols_hw, PERF_COUNT_HW_MAX, raw_dump);
			print_symbol_events(s, PERF_TYPE_SOFTWARE,
					    event_symbols_sw, PERF_COUNT_SW_MAX, raw_dump);
			print_tool_events(s, raw_dump);
			print_hwcache_events(s, raw_dump);
			print_pmu_events(s, raw_dump, !desc_flag,
						long_desc_flag,
						details_flag);
			print_tracepoint_events(NULL, s, raw_dump);
			print_sdt_events(NULL, s, raw_dump);
			metricgroup__print(true, true, s, raw_dump, details_flag);
			free(s);
		}
	}
	return 0;
}
