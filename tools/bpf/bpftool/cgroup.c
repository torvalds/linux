// SPDX-License-Identifier: GPL-2.0+
// Copyright (C) 2017 Facebook
// Author: Roman Gushchin <guro@fb.com>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <bpf.h>

#include "main.h"

#define HELP_SPEC_ATTACH_FLAGS						\
	"ATTACH_FLAGS := { multi | override }"

#define HELP_SPEC_ATTACH_TYPES						\
	"ATTACH_TYPE := { ingress | egress | sock_create | sock_ops | device }"

static const char * const attach_type_strings[] = {
	[BPF_CGROUP_INET_INGRESS] = "ingress",
	[BPF_CGROUP_INET_EGRESS] = "egress",
	[BPF_CGROUP_INET_SOCK_CREATE] = "sock_create",
	[BPF_CGROUP_SOCK_OPS] = "sock_ops",
	[BPF_CGROUP_DEVICE] = "device",
	[__MAX_BPF_ATTACH_TYPE] = NULL,
};

static enum bpf_attach_type parse_attach_type(const char *str)
{
	enum bpf_attach_type type;

	for (type = 0; type < __MAX_BPF_ATTACH_TYPE; type++) {
		if (attach_type_strings[type] &&
		    is_prefix(str, attach_type_strings[type]))
			return type;
	}

	return __MAX_BPF_ATTACH_TYPE;
}

static int list_bpf_prog(int id, const char *attach_type_str,
			 const char *attach_flags_str)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int prog_fd;

	prog_fd = bpf_prog_get_fd_by_id(id);
	if (prog_fd < 0)
		return -1;

	if (bpf_obj_get_info_by_fd(prog_fd, &info, &info_len)) {
		close(prog_fd);
		return -1;
	}

	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_uint_field(json_wtr, "id", info.id);
		jsonw_string_field(json_wtr, "attach_type",
				   attach_type_str);
		jsonw_string_field(json_wtr, "attach_flags",
				   attach_flags_str);
		jsonw_string_field(json_wtr, "name", info.name);
		jsonw_end_object(json_wtr);
	} else {
		printf("%-8u %-15s %-15s %-15s\n", info.id,
		       attach_type_str,
		       attach_flags_str,
		       info.name);
	}

	close(prog_fd);
	return 0;
}

static int list_attached_bpf_progs(int cgroup_fd, enum bpf_attach_type type)
{
	__u32 prog_ids[1024] = {0};
	char *attach_flags_str;
	__u32 prog_cnt, iter;
	__u32 attach_flags;
	char buf[32];
	int ret;

	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(cgroup_fd, type, 0, &attach_flags, prog_ids,
			     &prog_cnt);
	if (ret)
		return ret;

	if (prog_cnt == 0)
		return 0;

	switch (attach_flags) {
	case BPF_F_ALLOW_MULTI:
		attach_flags_str = "multi";
		break;
	case BPF_F_ALLOW_OVERRIDE:
		attach_flags_str = "override";
		break;
	case 0:
		attach_flags_str = "";
		break;
	default:
		snprintf(buf, sizeof(buf), "unknown(%x)", attach_flags);
		attach_flags_str = buf;
	}

	for (iter = 0; iter < prog_cnt; iter++)
		list_bpf_prog(prog_ids[iter], attach_type_strings[type],
			      attach_flags_str);

	return 0;
}

static int do_list(int argc, char **argv)
{
	enum bpf_attach_type type;
	int cgroup_fd;
	int ret = -1;

	if (argc < 1) {
		p_err("too few parameters for cgroup list\n");
		goto exit;
	} else if (argc > 1) {
		p_err("too many parameters for cgroup list\n");
		goto exit;
	}

	cgroup_fd = open(argv[0], O_RDONLY);
	if (cgroup_fd < 0) {
		p_err("can't open cgroup %s\n", argv[1]);
		goto exit;
	}

	if (json_output)
		jsonw_start_array(json_wtr);
	else
		printf("%-8s %-15s %-15s %-15s\n", "ID", "AttachType",
		       "AttachFlags", "Name");

	for (type = 0; type < __MAX_BPF_ATTACH_TYPE; type++) {
		/*
		 * Not all attach types may be supported, so it's expected,
		 * that some requests will fail.
		 * If we were able to get the list for at least one
		 * attach type, let's return 0.
		 */
		if (list_attached_bpf_progs(cgroup_fd, type) == 0)
			ret = 0;
	}

	if (json_output)
		jsonw_end_array(json_wtr);

	close(cgroup_fd);
exit:
	return ret;
}

static int do_attach(int argc, char **argv)
{
	enum bpf_attach_type attach_type;
	int cgroup_fd, prog_fd;
	int attach_flags = 0;
	int ret = -1;
	int i;

	if (argc < 4) {
		p_err("too few parameters for cgroup attach\n");
		goto exit;
	}

	cgroup_fd = open(argv[0], O_RDONLY);
	if (cgroup_fd < 0) {
		p_err("can't open cgroup %s\n", argv[1]);
		goto exit;
	}

	attach_type = parse_attach_type(argv[1]);
	if (attach_type == __MAX_BPF_ATTACH_TYPE) {
		p_err("invalid attach type\n");
		goto exit_cgroup;
	}

	argc -= 2;
	argv = &argv[2];
	prog_fd = prog_parse_fd(&argc, &argv);
	if (prog_fd < 0)
		goto exit_cgroup;

	for (i = 0; i < argc; i++) {
		if (is_prefix(argv[i], "multi")) {
			attach_flags |= BPF_F_ALLOW_MULTI;
		} else if (is_prefix(argv[i], "override")) {
			attach_flags |= BPF_F_ALLOW_OVERRIDE;
		} else {
			p_err("unknown option: %s\n", argv[i]);
			goto exit_cgroup;
		}
	}

	if (bpf_prog_attach(prog_fd, cgroup_fd, attach_type, attach_flags)) {
		p_err("failed to attach program");
		goto exit_prog;
	}

	if (json_output)
		jsonw_null(json_wtr);

	ret = 0;

exit_prog:
	close(prog_fd);
exit_cgroup:
	close(cgroup_fd);
exit:
	return ret;
}

static int do_detach(int argc, char **argv)
{
	enum bpf_attach_type attach_type;
	int prog_fd, cgroup_fd;
	int ret = -1;

	if (argc < 4) {
		p_err("too few parameters for cgroup detach\n");
		goto exit;
	}

	cgroup_fd = open(argv[0], O_RDONLY);
	if (cgroup_fd < 0) {
		p_err("can't open cgroup %s\n", argv[1]);
		goto exit;
	}

	attach_type = parse_attach_type(argv[1]);
	if (attach_type == __MAX_BPF_ATTACH_TYPE) {
		p_err("invalid attach type");
		goto exit_cgroup;
	}

	argc -= 2;
	argv = &argv[2];
	prog_fd = prog_parse_fd(&argc, &argv);
	if (prog_fd < 0)
		goto exit_cgroup;

	if (bpf_prog_detach2(prog_fd, cgroup_fd, attach_type)) {
		p_err("failed to detach program");
		goto exit_prog;
	}

	if (json_output)
		jsonw_null(json_wtr);

	ret = 0;

exit_prog:
	close(prog_fd);
exit_cgroup:
	close(cgroup_fd);
exit:
	return ret;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s %s list CGROUP\n"
		"       %s %s attach CGROUP ATTACH_TYPE PROG [ATTACH_FLAGS]\n"
		"       %s %s detach CGROUP ATTACH_TYPE PROG\n"
		"       %s %s help\n"
		"\n"
		"       " HELP_SPEC_ATTACH_TYPES "\n"
		"       " HELP_SPEC_ATTACH_FLAGS "\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "list",	do_list },
	{ "attach",	do_attach },
	{ "detach",	do_detach },
	{ "help",	do_help },
	{ 0 }
};

int do_cgroup(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
