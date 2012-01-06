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

static const char *input_name;

static int __cmd_evlist(void)
{
	struct perf_session *session;
	struct perf_evsel *pos;

	session = perf_session__new(input_name, O_RDONLY, 0, false, NULL);
	if (session == NULL)
		return -ENOMEM;

	list_for_each_entry(pos, &session->evlist->entries, node)
		printf("%s\n", event_name(pos));

	perf_session__delete(session);
	return 0;
}

static const char * const evlist_usage[] = {
	"perf evlist [<options>]",
	NULL
};

static const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_END()
};

int cmd_evlist(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, evlist_usage, 0);
	if (argc)
		usage_with_options(evlist_usage, options);

	return __cmd_evlist();
}
