// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */
/* Copyright (C) 2020 Facebook, Inc. */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "testing_helpers.h"

int parse_num_list(const char *s, bool **num_set, int *num_set_len)
{
	int i, set_len = 0, new_len, num, start = 0, end = -1;
	bool *set = NULL, *tmp, parsing_end = false;
	char *next;

	while (s[0]) {
		errno = 0;
		num = strtol(s, &next, 10);
		if (errno)
			return -errno;

		if (parsing_end)
			end = num;
		else
			start = num;

		if (!parsing_end && *next == '-') {
			s = next + 1;
			parsing_end = true;
			continue;
		} else if (*next == ',') {
			parsing_end = false;
			s = next + 1;
			end = num;
		} else if (*next == '\0') {
			parsing_end = false;
			s = next;
			end = num;
		} else {
			return -EINVAL;
		}

		if (start > end)
			return -EINVAL;

		if (end + 1 > set_len) {
			new_len = end + 1;
			tmp = realloc(set, new_len);
			if (!tmp) {
				free(set);
				return -ENOMEM;
			}
			for (i = set_len; i < start; i++)
				tmp[i] = false;
			set = tmp;
			set_len = new_len;
		}
		for (i = start; i <= end; i++)
			set[i] = true;
	}

	if (!set)
		return -EINVAL;

	*num_set = set;
	*num_set_len = set_len;

	return 0;
}

__u32 link_info_prog_id(const struct bpf_link *link, struct bpf_link_info *info)
{
	__u32 info_len = sizeof(*info);
	int err;

	memset(info, 0, sizeof(*info));
	err = bpf_obj_get_info_by_fd(bpf_link__fd(link), info, &info_len);
	if (err) {
		printf("failed to get link info: %d\n", -errno);
		return 0;
	}
	return info->prog_id;
}

int extra_prog_load_log_flags = 0;

int bpf_prog_test_load(const char *file, enum bpf_prog_type type,
		       struct bpf_object **pobj, int *prog_fd)
{
	struct bpf_object_load_attr attr = {};
	struct bpf_object *obj;
	struct bpf_program *prog;
	int err;

	obj = bpf_object__open(file);
	if (!obj)
		return -errno;

	prog = bpf_object__next_program(obj, NULL);
	if (!prog) {
		err = -ENOENT;
		goto err_out;
	}

	if (type != BPF_PROG_TYPE_UNSPEC)
		bpf_program__set_type(prog, type);

	bpf_program__set_extra_flags(prog, BPF_F_TEST_RND_HI32);

	attr.obj = obj;
	attr.log_level = extra_prog_load_log_flags;
	err = bpf_object__load_xattr(&attr);
	if (err)
		goto err_out;

	*pobj = obj;
	*prog_fd = bpf_program__fd(prog);

	return 0;
err_out:
	bpf_object__close(obj);
	return err;
}

int bpf_test_load_program(enum bpf_prog_type type, const struct bpf_insn *insns,
			  size_t insns_cnt, const char *license,
			  __u32 kern_version, char *log_buf,
			  size_t log_buf_sz)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.kern_version = kern_version,
		.prog_flags = BPF_F_TEST_RND_HI32,
		.log_level = extra_prog_load_log_flags,
		.log_buf = log_buf,
		.log_size = log_buf_sz,
	);

	return bpf_prog_load(type, NULL, license, insns, insns_cnt, &opts);
}
