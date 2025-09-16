// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms Inc. */
#include <test_progs.h>
#include "test_btf_ext.skel.h"
#include "btf_helpers.h"

static void subtest_line_func_info(void)
{
	struct test_btf_ext *skel;
	struct bpf_prog_info info;
	struct bpf_line_info line_info[128], *libbpf_line_info;
	struct bpf_func_info func_info[128], *libbpf_func_info;
	__u32 info_len = sizeof(info), libbbpf_line_info_cnt, libbbpf_func_info_cnt;
	int err, fd;

	skel = test_btf_ext__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	fd = bpf_program__fd(skel->progs.global_func);

	memset(&info, 0, sizeof(info));
	info.line_info = ptr_to_u64(&line_info);
	info.nr_line_info = sizeof(line_info);
	info.line_info_rec_size = sizeof(*line_info);
	err = bpf_prog_get_info_by_fd(fd, &info, &info_len);
	if (!ASSERT_OK(err, "prog_line_info"))
		goto out;

	libbpf_line_info = bpf_program__line_info(skel->progs.global_func);
	libbbpf_line_info_cnt = bpf_program__line_info_cnt(skel->progs.global_func);

	memset(&info, 0, sizeof(info));
	info.func_info = ptr_to_u64(&func_info);
	info.nr_func_info = sizeof(func_info);
	info.func_info_rec_size = sizeof(*func_info);
	err = bpf_prog_get_info_by_fd(fd, &info, &info_len);
	if (!ASSERT_OK(err, "prog_func_info"))
		goto out;

	libbpf_func_info = bpf_program__func_info(skel->progs.global_func);
	libbbpf_func_info_cnt = bpf_program__func_info_cnt(skel->progs.global_func);

	if (!ASSERT_OK_PTR(libbpf_line_info, "bpf_program__line_info"))
		goto out;
	if (!ASSERT_EQ(libbbpf_line_info_cnt, info.nr_line_info, "line_info_cnt"))
		goto out;
	if (!ASSERT_OK_PTR(libbpf_func_info, "bpf_program__func_info"))
		goto out;
	if (!ASSERT_EQ(libbbpf_func_info_cnt, info.nr_func_info, "func_info_cnt"))
		goto out;
	ASSERT_MEMEQ(libbpf_line_info, line_info, libbbpf_line_info_cnt * sizeof(*line_info),
		     "line_info");
	ASSERT_MEMEQ(libbpf_func_info, func_info, libbbpf_func_info_cnt * sizeof(*func_info),
		     "func_info");
out:
	test_btf_ext__destroy(skel);
}

void test_btf_ext(void)
{
	if (test__start_subtest("line_func_info"))
		subtest_line_func_info();
}
