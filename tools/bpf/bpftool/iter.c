// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (C) 2020 Facebook

#define _GNU_SOURCE
#include <linux/err.h>
#include <bpf/libbpf.h>

#include "main.h"

static int do_pin(int argc, char **argv)
{
	const char *objfile, *path;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_link *link;
	int err;

	if (!REQ_ARGS(2))
		usage();

	objfile = GET_ARG();
	path = GET_ARG();

	obj = bpf_object__open(objfile);
	if (IS_ERR(obj)) {
		p_err("can't open objfile %s", objfile);
		return -1;
	}

	err = bpf_object__load(obj);
	if (err) {
		p_err("can't load objfile %s", objfile);
		goto close_obj;
	}

	prog = bpf_program__next(NULL, obj);
	if (!prog) {
		p_err("can't find bpf program in objfile %s", objfile);
		goto close_obj;
	}

	link = bpf_program__attach_iter(prog, NULL);
	if (IS_ERR(link)) {
		err = PTR_ERR(link);
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
	return err;
}

static int do_help(int argc, char **argv)
{
	fprintf(stderr,
		"Usage: %1$s %2$s pin OBJ PATH\n"
		"       %1$s %2$s help\n"
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
