// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <time.h>

#include "struct_ops_module.skel.h"

static void check_map_info(struct bpf_map_info *info)
{
	struct bpf_btf_info btf_info;
	char btf_name[256];
	u32 btf_info_len = sizeof(btf_info);
	int err, fd;

	fd = bpf_btf_get_fd_by_id(info->btf_vmlinux_id);
	if (!ASSERT_GE(fd, 0, "get_value_type_btf_obj_fd"))
		return;

	memset(&btf_info, 0, sizeof(btf_info));
	btf_info.name = ptr_to_u64(btf_name);
	btf_info.name_len = sizeof(btf_name);
	err = bpf_btf_get_info_by_fd(fd, &btf_info, &btf_info_len);
	if (!ASSERT_OK(err, "get_value_type_btf_obj_info"))
		goto cleanup;

	if (!ASSERT_EQ(strcmp(btf_name, "bpf_testmod"), 0, "get_value_type_btf_obj_name"))
		goto cleanup;

cleanup:
	close(fd);
}

static void test_struct_ops_load(void)
{
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct struct_ops_module *skel;
	struct bpf_map_info info = {};
	struct bpf_link *link;
	int err;
	u32 len;

	skel = struct_ops_module__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "struct_ops_module_open"))
		return;

	err = struct_ops_module__load(skel);
	if (!ASSERT_OK(err, "struct_ops_module_load"))
		goto cleanup;

	len = sizeof(info);
	err = bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.testmod_1), &info,
				     &len);
	if (!ASSERT_OK(err, "bpf_map_get_info_by_fd"))
		goto cleanup;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_1);
	ASSERT_OK_PTR(link, "attach_test_mod_1");

	/* test_2() will be called from bpf_dummy_reg() in bpf_testmod.c */
	ASSERT_EQ(skel->bss->test_2_result, 7, "test_2_result");

	bpf_link__destroy(link);

	check_map_info(&info);

cleanup:
	struct_ops_module__destroy(skel);
}

void serial_test_struct_ops_module(void)
{
	if (test__start_subtest("test_struct_ops_load"))
		test_struct_ops_load();
}

