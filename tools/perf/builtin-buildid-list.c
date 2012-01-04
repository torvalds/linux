/*
 * builtin-buildid-list.c
 *
 * Builtin buildid-list command: list buildids in perf.data, in the running
 * kernel and in ELF files.
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

#include <libelf.h>

static char const *input_name = "perf.data";
static bool force;
static bool show_kernel;
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
	OPT_BOOLEAN('k', "kernel", &show_kernel, "Show current kernel build id"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose"),
	OPT_END()
};

static int perf_session__list_build_ids(void)
{
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, force, false,
				    &build_id__mark_dso_hit_ops);
	if (session == NULL)
		return -1;

	if (with_hits)
		perf_session__process_events(session, &build_id__mark_dso_hit_ops);

	perf_session__fprintf_dsos_buildid(session, stdout, with_hits);

	perf_session__delete(session);
	return 0;
}

static int sysfs__fprintf_build_id(FILE *fp)
{
	u8 kallsyms_build_id[BUILD_ID_SIZE];
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	if (sysfs__read_build_id("/sys/kernel/notes", kallsyms_build_id,
				 sizeof(kallsyms_build_id)) != 0)
		return -1;

	build_id__sprintf(kallsyms_build_id, sizeof(kallsyms_build_id),
			  sbuild_id);
	fprintf(fp, "%s\n", sbuild_id);
	return 0;
}

static int filename__fprintf_build_id(const char *name, FILE *fp)
{
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	if (filename__read_build_id(name, build_id,
				    sizeof(build_id)) != sizeof(build_id))
		return 0;

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	return fprintf(fp, "%s\n", sbuild_id);
}

static int __cmd_buildid_list(void)
{
	if (show_kernel)
		return sysfs__fprintf_build_id(stdout);

	elf_version(EV_CURRENT);
	/*
 	 * See if this is an ELF file first:
 	 */
	if (filename__fprintf_build_id(input_name, stdout))
		return 0;

	return perf_session__list_build_ids();
}

int cmd_buildid_list(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, buildid_list_usage, 0);
	setup_pager();
	return __cmd_buildid_list();
}
