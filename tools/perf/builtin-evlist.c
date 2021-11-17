// SPDX-License-Identifier: GPL-2.0
/*
 * Builtin evlist command: Show the list of event selectors present
 * in a perf.data file.
 */
#include "builtin.h"

#include <linux/list.h>

#include "perf.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/evsel_fprintf.h"
#include "util/parse-events.h"
#include <subcmd/parse-options.h>
#include "util/session.h"
#include "util/data.h"
#include "util/debug.h"
#include <linux/err.h>
#include "util/tool.h"

static int process_header_feature(struct perf_session *session __maybe_unused,
				  union perf_event *event __maybe_unused)
{
	session_done = 1;
	return 0;
}

static int __cmd_evlist(const char *file_name, struct perf_attr_details *details)
{
	struct perf_session *session;
	struct evsel *pos;
	struct perf_data data = {
		.path      = file_name,
		.mode      = PERF_DATA_MODE_READ,
		.force     = details->force,
	};
	struct perf_tool tool = {
		/* only needed for pipe mode */
		.attr = perf_event__process_attr,
		.feature = process_header_feature,
	};
	bool has_tracepoint = false;

	session = perf_session__new(&data, &tool);
	if (IS_ERR(session))
		return PTR_ERR(session);

	if (data.is_pipe)
		perf_session__process_events(session);

	evlist__for_each_entry(session->evlist, pos) {
		evsel__fprintf(pos, details, stdout);

		if (pos->core.attr.type == PERF_TYPE_TRACEPOINT)
			has_tracepoint = true;
	}

	if (has_tracepoint && !details->trace_fields)
		printf("# Tip: use 'perf evlist --trace-fields' to show fields for tracepoint events\n");

	perf_session__delete(session);
	return 0;
}

int cmd_evlist(int argc, const char **argv)
{
	struct perf_attr_details details = { .verbose = false, };
	const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file", "Input file name"),
	OPT_BOOLEAN('F', "freq", &details.freq, "Show the sample frequency"),
	OPT_BOOLEAN('v', "verbose", &details.verbose,
		    "Show all event attr details"),
	OPT_BOOLEAN('g', "group", &details.event_group,
		    "Show event group information"),
	OPT_BOOLEAN('f', "force", &details.force, "don't complain, do it"),
	OPT_BOOLEAN(0, "trace-fields", &details.trace_fields, "Show tracepoint fields"),
	OPT_END()
	};
	const char * const evlist_usage[] = {
		"perf evlist [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, options, evlist_usage, 0);
	if (argc)
		usage_with_options(evlist_usage, options);

	if (details.event_group && (details.verbose || details.freq)) {
		usage_with_options_msg(evlist_usage, options,
			"--group option is not compatible with other options\n");
	}

	return __cmd_evlist(input_name, &details);
}
