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

#include "util/print-events.h"
#include "util/pmu.h"
#include "util/pmu-hybrid.h"
#include "util/debug.h"
#include "util/metricgroup.h"
#include "util/string2.h"
#include "util/strlist.h"
#include "util/strbuf.h"
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include <linux/zalloc.h>
#include <stdarg.h>
#include <stdio.h>

/**
 * struct print_state - State and configuration passed to the default_print
 * functions.
 */
struct print_state {
	/**
	 * @pmu_glob: Optionally restrict PMU and metric matching to PMU or
	 * debugfs subsystem name.
	 */
	char *pmu_glob;
	/** @event_glob: Optional pattern matching glob. */
	char *event_glob;
	/** @name_only: Print event or metric names only. */
	bool name_only;
	/** @desc: Print the event or metric description. */
	bool desc;
	/** @long_desc: Print longer event or metric description. */
	bool long_desc;
	/** @deprecated: Print deprecated events or metrics. */
	bool deprecated;
	/**
	 * @detailed: Print extra information on the perf event such as names
	 * and expressions used internally by events.
	 */
	bool detailed;
	/** @metrics: Controls printing of metric and metric groups. */
	bool metrics;
	/** @metricgroups: Controls printing of metric and metric groups. */
	bool metricgroups;
	/** @last_topic: The last printed event topic. */
	char *last_topic;
	/** @last_metricgroups: The last printed metric group. */
	char *last_metricgroups;
	/** @visited_metrics: Metrics that are printed to avoid duplicates. */
	struct strlist *visited_metrics;
};

static void default_print_start(void *ps)
{
	struct print_state *print_state = ps;

	if (!print_state->name_only && pager_in_use())
		printf("\nList of pre-defined events (to be used in -e or -M):\n\n");
}

static void default_print_end(void *print_state __maybe_unused) {}

static void wordwrap(const char *s, int start, int max, int corr)
{
	int column = start;
	int n;
	bool saw_newline = false;

	while (*s) {
		int wlen = strcspn(s, " \t\n");

		if ((column + wlen >= max && column > start) || saw_newline) {
			printf("\n%*s", start, "");
			column = start + corr;
		}
		n = printf("%s%.*s", column > start ? " " : "", wlen, s);
		if (n <= 0)
			break;
		saw_newline = s[wlen] == '\n';
		s += wlen;
		column += n;
		s = skip_spaces(s);
	}
}

static void default_print_event(void *ps, const char *pmu_name, const char *topic,
				const char *event_name, const char *event_alias,
				const char *scale_unit __maybe_unused,
				bool deprecated, const char *event_type_desc,
				const char *desc, const char *long_desc,
				const char *encoding_desc,
				const char *metric_name, const char *metric_expr)
{
	struct print_state *print_state = ps;
	int pos;

	if (deprecated && !print_state->deprecated)
		return;

	if (print_state->pmu_glob && pmu_name && !strglobmatch(pmu_name, print_state->pmu_glob))
		return;

	if (print_state->event_glob &&
	    (!event_name || !strglobmatch(event_name, print_state->event_glob)) &&
	    (!event_alias || !strglobmatch(event_alias, print_state->event_glob)) &&
	    (!topic || !strglobmatch_nocase(topic, print_state->event_glob)))
		return;

	if (print_state->name_only) {
		if (event_alias && strlen(event_alias))
			printf("%s ", event_alias);
		else
			printf("%s ", event_name);
		return;
	}

	if (strcmp(print_state->last_topic, topic ?: "")) {
		if (topic)
			printf("\n%s:\n", topic);
		free(print_state->last_topic);
		print_state->last_topic = strdup(topic ?: "");
	}

	if (event_alias && strlen(event_alias))
		pos = printf("  %s OR %s", event_name, event_alias);
	else
		pos = printf("  %s", event_name);

	if (!topic && event_type_desc) {
		for (; pos < 53; pos++)
			putchar(' ');
		printf("[%s]\n", event_type_desc);
	} else
		putchar('\n');

	if (desc && print_state->desc) {
		printf("%*s", 8, "[");
		wordwrap(desc, 8, pager_get_columns(), 0);
		printf("]\n");
	}
	long_desc = long_desc ?: desc;
	if (long_desc && print_state->long_desc) {
		printf("%*s", 8, "[");
		wordwrap(long_desc, 8, pager_get_columns(), 0);
		printf("]\n");
	}

	if (print_state->detailed && encoding_desc) {
		printf("%*s", 8, "");
		wordwrap(encoding_desc, 8, pager_get_columns(), 0);
		if (metric_name)
			printf(" MetricName: %s", metric_name);
		if (metric_expr)
			printf(" MetricExpr: %s", metric_expr);
		putchar('\n');
	}
}

static void default_print_metric(void *ps,
				const char *group,
				const char *name,
				const char *desc,
				const char *long_desc,
				const char *expr,
				const char *unit __maybe_unused)
{
	struct print_state *print_state = ps;

	if (print_state->event_glob &&
	    (!print_state->metrics || !name || !strglobmatch(name, print_state->event_glob)) &&
	    (!print_state->metricgroups || !group || !strglobmatch(group, print_state->event_glob)))
		return;

	if (!print_state->name_only && !print_state->last_metricgroups) {
		if (print_state->metricgroups) {
			printf("\nMetric Groups:\n");
			if (!print_state->metrics)
				putchar('\n');
		} else {
			printf("\nMetrics:\n\n");
		}
	}
	if (!print_state->last_metricgroups ||
	    strcmp(print_state->last_metricgroups, group ?: "")) {
		if (group && print_state->metricgroups) {
			if (print_state->name_only)
				printf("%s ", group);
			else if (print_state->metrics)
				printf("\n%s:\n", group);
			else
				printf("%s\n", group);
		}
		free(print_state->last_metricgroups);
		print_state->last_metricgroups = strdup(group ?: "");
	}
	if (!print_state->metrics)
		return;

	if (print_state->name_only) {
		if (print_state->metrics &&
		    !strlist__has_entry(print_state->visited_metrics, name)) {
			printf("%s ", name);
			strlist__add(print_state->visited_metrics, name);
		}
		return;
	}
	printf("  %s\n", name);

	if (desc && print_state->desc) {
		printf("%*s", 8, "[");
		wordwrap(desc, 8, pager_get_columns(), 0);
		printf("]\n");
	}
	if (long_desc && print_state->long_desc) {
		printf("%*s", 8, "[");
		wordwrap(long_desc, 8, pager_get_columns(), 0);
		printf("]\n");
	}
	if (expr && print_state->detailed) {
		printf("%*s", 8, "[");
		wordwrap(expr, 8, pager_get_columns(), 0);
		printf("]\n");
	}
}

struct json_print_state {
	/** Should a separator be printed prior to the next item? */
	bool need_sep;
};

static void json_print_start(void *print_state __maybe_unused)
{
	printf("[\n");
}

static void json_print_end(void *ps)
{
	struct json_print_state *print_state = ps;

	printf("%s]\n", print_state->need_sep ? "\n" : "");
}

static void fix_escape_printf(struct strbuf *buf, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	strbuf_setlen(buf, 0);
	for (size_t fmt_pos = 0; fmt_pos < strlen(fmt); fmt_pos++) {
		switch (fmt[fmt_pos]) {
		case '%':
			fmt_pos++;
			switch (fmt[fmt_pos]) {
			case 's': {
				const char *s = va_arg(args, const char*);

				strbuf_addstr(buf, s);
				break;
			}
			case 'S': {
				const char *s = va_arg(args, const char*);

				for (size_t s_pos = 0; s_pos < strlen(s); s_pos++) {
					switch (s[s_pos]) {
					case '\n':
						strbuf_addstr(buf, "\\n");
						break;
					case '\\':
						__fallthrough;
					case '\"':
						strbuf_addch(buf, '\\');
						__fallthrough;
					default:
						strbuf_addch(buf, s[s_pos]);
						break;
					}
				}
				break;
			}
			default:
				pr_err("Unexpected format character '%c'\n", fmt[fmt_pos]);
				strbuf_addch(buf, '%');
				strbuf_addch(buf, fmt[fmt_pos]);
			}
			break;
		default:
			strbuf_addch(buf, fmt[fmt_pos]);
			break;
		}
	}
	va_end(args);
	fputs(buf->buf, stdout);
}

static void json_print_event(void *ps, const char *pmu_name, const char *topic,
			     const char *event_name, const char *event_alias,
			     const char *scale_unit,
			     bool deprecated, const char *event_type_desc,
			     const char *desc, const char *long_desc,
			     const char *encoding_desc,
			     const char *metric_name, const char *metric_expr)
{
	struct json_print_state *print_state = ps;
	bool need_sep = false;
	struct strbuf buf;

	strbuf_init(&buf, 0);
	printf("%s{\n", print_state->need_sep ? ",\n" : "");
	print_state->need_sep = true;
	if (pmu_name) {
		fix_escape_printf(&buf, "\t\"Unit\": \"%S\"", pmu_name);
		need_sep = true;
	}
	if (topic) {
		fix_escape_printf(&buf, "%s\t\"Topic\": \"%S\"", need_sep ? ",\n" : "", topic);
		need_sep = true;
	}
	if (event_name) {
		fix_escape_printf(&buf, "%s\t\"EventName\": \"%S\"", need_sep ? ",\n" : "",
				  event_name);
		need_sep = true;
	}
	if (event_alias && strlen(event_alias)) {
		fix_escape_printf(&buf, "%s\t\"EventAlias\": \"%S\"", need_sep ? ",\n" : "",
				  event_alias);
		need_sep = true;
	}
	if (scale_unit && strlen(scale_unit)) {
		fix_escape_printf(&buf, "%s\t\"ScaleUnit\": \"%S\"", need_sep ? ",\n" : "",
				  scale_unit);
		need_sep = true;
	}
	if (event_type_desc) {
		fix_escape_printf(&buf, "%s\t\"EventType\": \"%S\"", need_sep ? ",\n" : "",
				  event_type_desc);
		need_sep = true;
	}
	if (deprecated) {
		fix_escape_printf(&buf, "%s\t\"Deprecated\": \"%S\"", need_sep ? ",\n" : "",
				  deprecated ? "1" : "0");
		need_sep = true;
	}
	if (desc) {
		fix_escape_printf(&buf, "%s\t\"BriefDescription\": \"%S\"", need_sep ? ",\n" : "",
				  desc);
		need_sep = true;
	}
	if (long_desc) {
		fix_escape_printf(&buf, "%s\t\"PublicDescription\": \"%S\"", need_sep ? ",\n" : "",
				  long_desc);
		need_sep = true;
	}
	if (encoding_desc) {
		fix_escape_printf(&buf, "%s\t\"Encoding\": \"%S\"", need_sep ? ",\n" : "",
				  encoding_desc);
		need_sep = true;
	}
	if (metric_name) {
		fix_escape_printf(&buf, "%s\t\"MetricName\": \"%S\"", need_sep ? ",\n" : "",
				  metric_name);
		need_sep = true;
	}
	if (metric_expr) {
		fix_escape_printf(&buf, "%s\t\"MetricExpr\": \"%S\"", need_sep ? ",\n" : "",
				  metric_expr);
		need_sep = true;
	}
	printf("%s}", need_sep ? "\n" : "");
	strbuf_release(&buf);
}

static void json_print_metric(void *ps __maybe_unused, const char *group,
			      const char *name, const char *desc,
			      const char *long_desc, const char *expr,
			      const char *unit)
{
	struct json_print_state *print_state = ps;
	bool need_sep = false;
	struct strbuf buf;

	strbuf_init(&buf, 0);
	printf("%s{\n", print_state->need_sep ? ",\n" : "");
	print_state->need_sep = true;
	if (group) {
		fix_escape_printf(&buf, "\t\"MetricGroup\": \"%S\"", group);
		need_sep = true;
	}
	if (name) {
		fix_escape_printf(&buf, "%s\t\"MetricName\": \"%S\"", need_sep ? ",\n" : "", name);
		need_sep = true;
	}
	if (expr) {
		fix_escape_printf(&buf, "%s\t\"MetricExpr\": \"%S\"", need_sep ? ",\n" : "", expr);
		need_sep = true;
	}
	if (unit) {
		fix_escape_printf(&buf, "%s\t\"ScaleUnit\": \"%S\"", need_sep ? ",\n" : "", unit);
		need_sep = true;
	}
	if (desc) {
		fix_escape_printf(&buf, "%s\t\"BriefDescription\": \"%S\"", need_sep ? ",\n" : "",
				  desc);
		need_sep = true;
	}
	if (long_desc) {
		fix_escape_printf(&buf, "%s\t\"PublicDescription\": \"%S\"", need_sep ? ",\n" : "",
				  long_desc);
		need_sep = true;
	}
	printf("%s}", need_sep ? "\n" : "");
	strbuf_release(&buf);
}

int cmd_list(int argc, const char **argv)
{
	int i, ret = 0;
	struct print_state default_ps = {};
	struct print_state json_ps = {};
	void *ps = &default_ps;
	struct print_callbacks print_cb = {
		.print_start = default_print_start,
		.print_end = default_print_end,
		.print_event = default_print_event,
		.print_metric = default_print_metric,
	};
	const char *hybrid_name = NULL;
	const char *unit_name = NULL;
	bool json = false;
	struct option list_options[] = {
		OPT_BOOLEAN(0, "raw-dump", &default_ps.name_only, "Dump raw events"),
		OPT_BOOLEAN('j', "json", &json, "JSON encode events and metrics"),
		OPT_BOOLEAN('d', "desc", &default_ps.desc,
			    "Print extra event descriptions. --no-desc to not print."),
		OPT_BOOLEAN('v', "long-desc", &default_ps.long_desc,
			    "Print longer event descriptions."),
		OPT_BOOLEAN(0, "details", &default_ps.detailed,
			    "Print information on the perf event names and expressions used internally by events."),
		OPT_BOOLEAN(0, "deprecated", &default_ps.deprecated,
			    "Print deprecated events."),
		OPT_STRING(0, "cputype", &hybrid_name, "hybrid cpu type",
			   "Limit PMU or metric printing to the given hybrid PMU (e.g. core or atom)."),
		OPT_STRING(0, "unit", &unit_name, "PMU name",
			   "Limit PMU or metric printing to the specified PMU."),
		OPT_INCR(0, "debug", &verbose,
			     "Enable debugging output"),
		OPT_END()
	};
	const char * const list_usage[] = {
		"perf list [<options>] [hw|sw|cache|tracepoint|pmu|sdt|metric|metricgroup|event_glob]",
		NULL
	};

	set_option_flag(list_options, 0, "raw-dump", PARSE_OPT_HIDDEN);
	/* Hide hybrid flag for the more generic 'unit' flag. */
	set_option_flag(list_options, 0, "cputype", PARSE_OPT_HIDDEN);

	argc = parse_options(argc, argv, list_options, list_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	setup_pager();

	if (!default_ps.name_only)
		setup_pager();

	if (json) {
		print_cb = (struct print_callbacks){
			.print_start = json_print_start,
			.print_end = json_print_end,
			.print_event = json_print_event,
			.print_metric = json_print_metric,
		};
		ps = &json_ps;
	} else {
		default_ps.desc = !default_ps.long_desc;
		default_ps.last_topic = strdup("");
		assert(default_ps.last_topic);
		default_ps.visited_metrics = strlist__new(NULL, NULL);
		assert(default_ps.visited_metrics);
		if (unit_name)
			default_ps.pmu_glob = strdup(unit_name);
		else if (hybrid_name) {
			default_ps.pmu_glob = perf_pmu__hybrid_type_to_pmu(hybrid_name);
			if (!default_ps.pmu_glob)
				pr_warning("WARNING: hybrid cputype is not supported!\n");
		}
	}
	print_cb.print_start(ps);

	if (argc == 0) {
		default_ps.metrics = true;
		default_ps.metricgroups = true;
		print_events(&print_cb, ps);
		goto out;
	}

	for (i = 0; i < argc; ++i) {
		char *sep, *s;

		if (strcmp(argv[i], "tracepoint") == 0)
			print_tracepoint_events(&print_cb, ps);
		else if (strcmp(argv[i], "hw") == 0 ||
			 strcmp(argv[i], "hardware") == 0)
			print_symbol_events(&print_cb, ps, PERF_TYPE_HARDWARE,
					event_symbols_hw, PERF_COUNT_HW_MAX);
		else if (strcmp(argv[i], "sw") == 0 ||
			 strcmp(argv[i], "software") == 0) {
			print_symbol_events(&print_cb, ps, PERF_TYPE_SOFTWARE,
					event_symbols_sw, PERF_COUNT_SW_MAX);
			print_tool_events(&print_cb, ps);
		} else if (strcmp(argv[i], "cache") == 0 ||
			 strcmp(argv[i], "hwcache") == 0)
			print_hwcache_events(&print_cb, ps);
		else if (strcmp(argv[i], "pmu") == 0)
			print_pmu_events(&print_cb, ps);
		else if (strcmp(argv[i], "sdt") == 0)
			print_sdt_events(&print_cb, ps);
		else if (strcmp(argv[i], "metric") == 0 || strcmp(argv[i], "metrics") == 0) {
			default_ps.metricgroups = false;
			default_ps.metrics = true;
			metricgroup__print(&print_cb, ps);
		} else if (strcmp(argv[i], "metricgroup") == 0 ||
			   strcmp(argv[i], "metricgroups") == 0) {
			default_ps.metricgroups = true;
			default_ps.metrics = false;
			metricgroup__print(&print_cb, ps);
		} else if ((sep = strchr(argv[i], ':')) != NULL) {
			char *old_pmu_glob = default_ps.pmu_glob;

			default_ps.event_glob = strdup(argv[i]);
			if (!default_ps.event_glob) {
				ret = -1;
				goto out;
			}

			print_tracepoint_events(&print_cb, ps);
			print_sdt_events(&print_cb, ps);
			default_ps.metrics = true;
			default_ps.metricgroups = true;
			metricgroup__print(&print_cb, ps);
			zfree(&default_ps.event_glob);
			default_ps.pmu_glob = old_pmu_glob;
		} else {
			if (asprintf(&s, "*%s*", argv[i]) < 0) {
				printf("Critical: Not enough memory! Trying to continue...\n");
				continue;
			}
			default_ps.event_glob = s;
			print_symbol_events(&print_cb, ps, PERF_TYPE_HARDWARE,
					event_symbols_hw, PERF_COUNT_HW_MAX);
			print_symbol_events(&print_cb, ps, PERF_TYPE_SOFTWARE,
					event_symbols_sw, PERF_COUNT_SW_MAX);
			print_tool_events(&print_cb, ps);
			print_hwcache_events(&print_cb, ps);
			print_pmu_events(&print_cb, ps);
			print_tracepoint_events(&print_cb, ps);
			print_sdt_events(&print_cb, ps);
			default_ps.metrics = true;
			default_ps.metricgroups = true;
			metricgroup__print(&print_cb, ps);
			free(s);
		}
	}

out:
	print_cb.print_end(ps);
	free(default_ps.pmu_glob);
	free(default_ps.last_topic);
	free(default_ps.last_metricgroups);
	strlist__delete(default_ps.visited_metrics);
	return ret;
}
