/*
 * builtin-buildid-list.c
 *
 * Builtin buildid-list command: list buildids in perf.data
 *
 * Copyright (C) 2009, Red Hat Inc.
 * Copyright (C) 2009, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "builtin.h"
#include "perf.h"
#include "util/build-id.h"
#include "util/cache.h"
#include "util/debug.h"
#include "util/parse-options.h"
#include "util/session.h"
#include "util/symbol.h"

static char const *input_name = "perf.data";
static bool force;
static bool with_hits;

static const char * const buildid_list_usage[] = {
	"perf buildid-list [<options>]",
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('H', "with-hits", &with_hits, "Show only DSOs with hits"),
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose"),
	OPT_END()
};

static int __cmd_buildid_list(void)
{
	int err = -1;
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, force, false);
	if (session == NULL)
		return -1;

	if (with_hits) {
		symbol_conf.full_paths = true;
		perf_session__process_events(session, &build_id__mark_dso_hit_ops);
	}

	perf_session__fprintf_dsos_buildid(session, stdout, with_hits);

	perf_session__delete(session);
	return err;
}

int cmd_buildid_list(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, buildid_list_usage, 0);
	setup_pager();
	return __cmd_buildid_list();
}
