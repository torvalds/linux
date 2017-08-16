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
#include <subcmd/parse-options.h>
#include "util/session.h"
#include "util/symbol.h"
#include "util/data.h"
#include <errno.h>

static int sysfs__fprintf_build_id(FILE *fp)
{
	char sbuild_id[SBUILD_ID_SIZE];
	int ret;

	ret = sysfs__sprintf_build_id("/", sbuild_id);
	if (ret != sizeof(sbuild_id))
		return ret < 0 ? ret : -EINVAL;

	return fprintf(fp, "%s\n", sbuild_id);
}

static int filename__fprintf_build_id(const char *name, FILE *fp)
{
	char sbuild_id[SBUILD_ID_SIZE];
	int ret;

	ret = filename__sprintf_build_id(name, sbuild_id);
	if (ret != sizeof(sbuild_id))
		return ret < 0 ? ret : -EINVAL;

	return fprintf(fp, "%s\n", sbuild_id);
}

static bool dso__skip_buildid(struct dso *dso, int with_hits)
{
	return with_hits && !dso->hit;
}

static int perf_session__list_build_ids(bool force, bool with_hits)
{
	struct perf_session *session;
	struct perf_data_file file = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = force,
	};

	symbol__elf_init();
	/*
	 * See if this is an ELF file first:
	 */
	if (filename__fprintf_build_id(input_name, stdout) > 0)
		goto out;

	session = perf_session__new(&file, false, &build_id__mark_dso_hit_ops);
	if (session == NULL)
		return -1;

	/*
	 * We take all buildids when the file contains AUX area tracing data
	 * because we do not decode the trace because it would take too long.
	 */
	if (!perf_data_file__is_pipe(&file) &&
	    perf_header__has_feat(&session->header, HEADER_AUXTRACE))
		with_hits = false;

	/*
	 * in pipe-mode, the only way to get the buildids is to parse
	 * the record stream. Buildids are stored as RECORD_HEADER_BUILD_ID
	 */
	if (with_hits || perf_data_file__is_pipe(&file))
		perf_session__process_events(session);

	perf_session__fprintf_dsos_buildid(session, stdout, dso__skip_buildid, with_hits);
	perf_session__delete(session);
out:
	return 0;
}

int cmd_buildid_list(int argc, const char **argv)
{
	bool show_kernel = false;
	bool with_hits = false;
	bool force = false;
	const struct option options[] = {
	OPT_BOOLEAN('H', "with-hits", &with_hits, "Show only DSOs with hits"),
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('k', "kernel", &show_kernel, "Show current kernel build id"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};
	const char * const buildid_list_usage[] = {
		"perf buildid-list [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, options, buildid_list_usage, 0);
	setup_pager();

	if (show_kernel)
		return !(sysfs__fprintf_build_id(stdout) > 0);

	return perf_session__list_build_ids(force, with_hits);
}
