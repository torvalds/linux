// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (C) 2020 Facebook

#define _GNU_SOURCE
#include <unistd.h>
#include <linux/err.h>
#include <bpf/libbpf.h>

#include "main.h"

static int do_pin(int argc, char **argv)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, iter_opts);
	union bpf_iter_link_info linfo;
	const char *objfile, *path;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_link *link;
	int err = -1, map_fd = -1;

	if (!REQ_ARGS(2))
		usage();

	objfile = GET_ARG();
	path = GET_ARG();

	/* optional arguments */
	if (argc) {
		if (is_prefix(*argv, "map")) {
			NEXT_ARG();

			if (!REQ_ARGS(2)) {
				p_err("incorrect map spec");
				return -1;
			}

			map_fd = map_parse_fd(&argc, &argv);
			if (map_fd < 0)
				return -1;

			memset(&linfo, 0, sizeof(linfo));
			linfo.map.map_fd = map_fd;
			iter_opts.link_info = &linfo;
			iter_opts.link_info_len = sizeof(linfo);
		}
	}

	obj = bpf_object__open(objfile);
	err = libbpf_get_error(obj);
	if (err) {
		p_err("can't open objfile %s", objfile);
		goto close_map_fd;
	}

	err = bpf_object__load(obj);
	if (err) {
		p_err("can't load objfile %s", objfile);
		goto close_obj;
	}

	prog = bpf_object__next_program(obj, NULL);
	if (!prog) {
		p_err("can't find bpf program in objfile %s", objfile);
		goto close_obj;
	}

	link = bpf_program__attach_iter(prog, &iter_opts);
	err = libbpf_get_error(link);
	if (err) {
		p_err("attach_iter failed for program %s",
		      bpf_program__name(prog));
		goto close_obj;
	}

	err = mount_bpffs_for_pin(path);
	if (err)
		goto close_link;

	err = bpf_link__pin(link, path);
	if (err) {
		p_err("pin_iter failed for program %s to path %s",
		      bpf_program__name(prog), path);
		goto close_link;
	}

close_link:
	bpf_link__destroy(link);
close_obj:
	bpf_object__close(obj);
close_map_fd:
	if (map_fd >= 0)
		close(map_fd);
	return err;
}

static int do_help(int argc, char **argv)
{
	fprintf(stderr,
		"Usage: %1$s %2$s pin OBJ PATH [map MAP]\n"
		"       %1$s %2$s help\n"
		"\n"
		"       " HELP_SPEC_MAP "\n"
		"       " HELP_SPEC_OPTIONS " }\n"
		"",
		bin_name, "iter");

	return 0;
}

static const struct cmd cmds[] = {
	{ "help",	do_help },
	{ "pin",	do_pin },
	{ 0 }
};

int do_iter(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
