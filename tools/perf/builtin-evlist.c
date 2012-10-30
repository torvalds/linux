/*
 * Builtin evlist command: Show the list of event selectors present
 * in a perf.data file.
 */
#include "builtin.h"

#include "util/util.h"

#include <linux/list.h>

#include "perf.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/parse-events.h"
#include "util/parse-options.h"
#include "util/session.h"

struct perf_attr_details {
	bool freq;
	bool verbose;
};

static int comma_printf(bool *first, const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (!*first) {
		ret += printf(",");
	} else {
		ret += printf(":");
		*first = false;
	}

	va_start(args, fmt);
	ret += vprintf(fmt, args);
	va_end(args);
	return ret;
}

static int __if_print(bool *first, const char *field, u64 value)
{
	if (value == 0)
		return 0;

	return comma_printf(first, " %s: %" PRIu64, field, value);
}

#define if_print(field) __if_print(&first, #field, pos->attr.field)

static int __cmd_evlist(const char *input_name, struct perf_attr_details *details)
{
	struct perf_session *session;
	struct perf_evsel *pos;

	session = perf_session__new(input_name, O_RDONLY, 0, false, NULL);
	if (session == NULL)
		return -ENOMEM;

	list_for_each_entry(pos, &session->evlist->entries, node) {
		bool first = true;

		printf("%s", perf_evsel__name(pos));

		if (details->verbose || details->freq) {
			comma_printf(&first, " sample_freq=%" PRIu64,
				     (u64)pos->attr.sample_freq);
		}

		if (details->verbose) {
			if_print(type);
			if_print(config);
			if_print(config1);
			if_print(config2);
			if_print(size);
			if_print(sample_type);
			if_print(read_format);
			if_print(disabled);
			if_print(inherit);
			if_print(pinned);
			if_print(exclusive);
			if_print(exclude_user);
			if_print(exclude_kernel);
			if_print(exclude_hv);
			if_print(exclude_idle);
			if_print(mmap);
			if_print(comm);
			if_print(freq);
			if_print(inherit_stat);
			if_print(enable_on_exec);
			if_print(task);
			if_print(watermark);
			if_print(precise_ip);
			if_print(mmap_data);
			if_print(sample_id_all);
			if_print(exclude_host);
			if_print(exclude_guest);
			if_print(__reserved_1);
			if_print(wakeup_events);
			if_print(bp_type);
			if_print(branch_sample_type);
		}

		putchar('\n');
	}

	perf_session__delete(session);
	return 0;
}

int cmd_evlist(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct perf_attr_details details = { .verbose = false, };
	const char *input_name = NULL;
	const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file", "Input file name"),
	OPT_BOOLEAN('F', "freq", &details.freq, "Show the sample frequency"),
	OPT_BOOLEAN('v', "verbose", &details.verbose,
		    "Show all event attr details"),
	OPT_END()
	};
	const char * const evlist_usage[] = {
		"perf evlist [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, options, evlist_usage, 0);
	if (argc)
		usage_with_options(evlist_usage, options);

	return __cmd_evlist(input_name, &details);
}
