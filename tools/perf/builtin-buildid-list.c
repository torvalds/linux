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
#include "util/data_map.h"
#include "util/debug.h"
#include "util/header.h"
#include "util/parse-options.h"
#include "util/symbol.h"

static char const *input_name = "perf.data";
static int force;

static const char *const buildid_list_usage[] = {
	"perf report [<options>]",
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
	struct perf_header *header;
	struct perf_file_header f_header;
	struct stat input_stat;
	int input = open(input_name, O_RDONLY);

	if (input < 0) {
		pr_err("failed to open file: %s", input_name);
		if (!strcmp(input_name, "perf.data"))
			pr_err("  (try 'perf record' first)");
		pr_err("\n");
		goto out;
	}

	err = fstat(input, &input_stat);
	if (err < 0) {
		perror("failed to stat file");
		goto out_close;
	}

	if (!force && input_stat.st_uid && (input_stat.st_uid != geteuid())) {
		pr_err("file %s not owned by current user or root\n",
		       input_name);
		goto out_close;
	}

	if (!input_stat.st_size) {
		pr_info("zero-sized file, nothing to do!\n");
		goto out_close;
	}

	err = -1;
	header = perf_header__new();
	if (header == NULL)
		goto out_close;

	if (perf_file_header__read(&f_header, header, input) < 0) {
		pr_warning("incompatible file format");
		goto out_close;
	}

	err = perf_header__process_sections(header, input,
				         perf_file_section__process_buildids);

	if (err < 0)
		goto out_close;

	dsos__fprintf_buildid(stdout);
out_close:
	close(input);
out:
	return err;
}

int cmd_buildid_list(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, buildid_list_usage, 0);
	setup_pager();
	return __cmd_buildid_list();
}
