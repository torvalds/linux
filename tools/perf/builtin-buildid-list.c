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
#include "util/cache.h"
#include "util/debug.h"
#include "util/parse-options.h"
#include "util/session.h"
#include "util/symbol.h"

static char const *input_name = "perf.data";
static int force;

static const char * const buildid_list_usage[] = {
	"perf buildid-list [<options>]",
	NULL
};

static const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose"),
	OPT_END()
};

static int perf_file_section__process_buildids(struct perf_file_section *self,
					       int feat, int fd)
{
	if (feat != HEADER_BUILD_ID)
		return 0;

	if (lseek(fd, self->offset, SEEK_SET) < 0) {
		pr_warning("Failed to lseek to %Ld offset for buildids!\n",
			   self->offset);
		return -1;
	}

	if (perf_header__read_build_ids(fd, self->offset, self->size)) {
		pr_warning("Failed to read buildids!\n");
		return -1;
	}

	return 0;
}

static int __cmd_buildid_list(void)
{
	int err = -1;
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, force);
	if (session == NULL)
		return -1;

	err = perf_header__process_sections(&session->header, session->fd,
				         perf_file_section__process_buildids);
	if (err >= 0)
		dsos__fprintf_buildid(stdout);

	perf_session__delete(session);
	return err;
}

int cmd_buildid_list(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, buildid_list_usage, 0);
	setup_pager();
	return __cmd_buildid_list();
}
