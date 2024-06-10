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
#include "util/build-id.h"
#include "util/debug.h"
#include "util/dso.h"
#include "util/map.h"
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include "util/session.h"
#include "util/symbol.h"
#include "util/data.h"
#include "util/util.h"
#include <errno.h>
#include <inttypes.h>
#include <linux/err.h>

static int buildid__map_cb(struct map *map, void *arg __maybe_unused)
{
	const struct dso *dso = map__dso(map);
	char bid_buf[SBUILD_ID_SIZE];
	const char *dso_long_name = dso__long_name(dso);
	const char *dso_short_name = dso__short_name(dso);

	memset(bid_buf, 0, sizeof(bid_buf));
	if (dso__has_build_id(dso))
		build_id__sprintf(dso__bid_const(dso), bid_buf);
	printf("%s %16" PRIx64 " %16" PRIx64, bid_buf, map__start(map), map__end(map));
	if (dso_long_name != NULL)
		printf(" %s", dso_long_name);
	else if (dso_short_name != NULL)
		printf(" %s", dso_short_name);

	printf("\n");

	return 0;
}

static void buildid__show_kernel_maps(void)
{
	struct machine *machine;

	machine = machine__new_host();
	machine__for_each_kernel_map(machine, buildid__map_cb, NULL);
	machine__delete(machine);
}

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
	return with_hits && !dso__hit(dso);
}

static int perf_session__list_build_ids(bool force, bool with_hits)
{
	struct perf_session *session;
	struct perf_data data = {
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

	session = perf_session__new(&data, &build_id__mark_dso_hit_ops);
	if (IS_ERR(session))
		return PTR_ERR(session);

	/*
	 * We take all buildids when the file contains AUX area tracing data
	 * because we do not decode the trace because it would take too long.
	 */
	if (!perf_data__is_pipe(&data) &&
	    perf_header__has_feat(&session->header, HEADER_AUXTRACE))
		with_hits = false;

	if (!perf_header__has_feat(&session->header, HEADER_BUILD_ID))
		with_hits = true;

	if (zstd_init(&(session->zstd_data), 0) < 0)
		pr_warning("Decompression initialization failed. Reported data may be incomplete.\n");

	/*
	 * in pipe-mode, the only way to get the buildids is to parse
	 * the record stream. Buildids are stored as RECORD_HEADER_BUILD_ID
	 */
	if (with_hits || perf_data__is_pipe(&data))
		perf_session__process_events(session);

	perf_session__fprintf_dsos_buildid(session, stdout, dso__skip_buildid, with_hits);
	perf_session__delete(session);
out:
	return 0;
}

int cmd_buildid_list(int argc, const char **argv)
{
	bool show_kernel = false;
	bool show_kernel_maps = false;
	bool with_hits = false;
	bool force = false;
	const struct option options[] = {
	OPT_BOOLEAN('H', "with-hits", &with_hits, "Show only DSOs with hits"),
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('k', "kernel", &show_kernel, "Show current kernel build id"),
	OPT_BOOLEAN('m', "kernel-maps", &show_kernel_maps,
	    "Show build id of current kernel + modules"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};
	const char * const buildid_list_usage[] = {
		"perf buildid-list [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, options, buildid_list_usage, 0);
	setup_pager();

	if (show_kernel) {
		return !(sysfs__fprintf_build_id(stdout) > 0);
	} else if (show_kernel_maps) {
		buildid__show_kernel_maps();
		return 0;
	}

	return perf_session__list_build_ids(force, with_hits);
}
