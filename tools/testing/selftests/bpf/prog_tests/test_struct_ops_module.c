// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <time.h>

#include "struct_ops_module.skel.h"
#include "struct_ops_nulled_out_cb.skel.h"
#include "struct_ops_forgotten_cb.skel.h"

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

static int attach_ops_and_check(struct struct_ops_module *skel,
				struct bpf_map *map,
				int expected_test_2_result)
{
	struct bpf_link *link;

	link = bpf_map__attach_struct_ops(map);
	ASSERT_OK_PTR(link, "attach_test_mod_1");
	if (!link)
		return -1;

	/* test_{1,2}() would be called from bpf_dummy_reg() in bpf_testmod.c */
	ASSERT_EQ(skel->bss->test_1_result, 0xdeadbeef, "test_1_result");
	ASSERT_EQ(skel->bss->test_2_result, expected_test_2_result, "test_2_result");

	bpf_link__destroy(link);
	return 0;
}

static void test_struct_ops_load(void)
{
	struct struct_ops_module *skel;
	struct bpf_map_info info = {};
	int err;
	u32 len;

	skel = struct_ops_module__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_module_open"))
		return;

	skel->struct_ops.testmod_1->data = 13;
	skel->struct_ops.testmod_1->test_2 = skel->progs.test_3;
	/* Since test_2() is not being used, it should be disabled from
	 * auto-loading, or it will fail to load.
	 */
	bpf_program__set_autoload(skel->progs.test_2, false);
	bpf_map__set_autocreate(skel->maps.testmod_zeroed, false);

	err = struct_ops_module__load(skel);
	if (!ASSERT_OK(err, "struct_ops_module_load"))
		goto cleanup;

	len = sizeof(info);
	err = bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.testmod_1), &info,
				     &len);
	if (!ASSERT_OK(err, "bpf_map_get_info_by_fd"))
		goto cleanup;

	check_map_info(&info);
	/* test_3() will be called from bpf_dummy_reg() in bpf_testmod.c
	 *
	 * In bpf_testmod.c it will pass 4 and 13 (the value of data) to
	 * .test_2.  So, the value of test_2_result should be 20 (4 + 13 +
	 * 3).
	 */
	if (!attach_ops_and_check(skel, skel->maps.testmod_1, 20))
		goto cleanup;
	if (!attach_ops_and_check(skel, skel->maps.testmod_2, 12))
		goto cleanup;

cleanup:
	struct_ops_module__destroy(skel);
}

static void test_struct_ops_not_zeroed(void)
{
	struct struct_ops_module *skel;
	int err;

	/* zeroed is 0, and zeroed_op is null */
	skel = struct_ops_module__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_module_open"))
		return;

	skel->struct_ops.testmod_zeroed->zeroed = 0;
	/* zeroed_op prog should be not loaded automatically now */
	skel->struct_ops.testmod_zeroed->zeroed_op = NULL;

	err = struct_ops_module__load(skel);
	ASSERT_OK(err, "struct_ops_module_load");

	struct_ops_module__destroy(skel);

	/* zeroed is not 0 */
	skel = struct_ops_module__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_module_open_not_zeroed"))
		return;

	/* libbpf should reject the testmod_zeroed since struct
	 * bpf_testmod_ops in the kernel has no "zeroed" field and the
	 * value of "zeroed" is non-zero.
	 */
	skel->struct_ops.testmod_zeroed->zeroed = 0xdeadbeef;
	skel->struct_ops.testmod_zeroed->zeroed_op = NULL;
	err = struct_ops_module__load(skel);
	ASSERT_ERR(err, "struct_ops_module_load_not_zeroed");

	struct_ops_module__destroy(skel);

	/* zeroed_op is not null */
	skel = struct_ops_module__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_module_open_not_zeroed_op"))
		return;

	/* libbpf should reject the testmod_zeroed since the value of its
	 * "zeroed_op" is not null.
	 */
	skel->struct_ops.testmod_zeroed->zeroed_op = skel->progs.test_3;
	err = struct_ops_module__load(skel);
	ASSERT_ERR(err, "struct_ops_module_load_not_zeroed_op");

	struct_ops_module__destroy(skel);
}

/* The signature of an implementation might not match the signature of the
 * function pointer prototype defined in the BPF program. This mismatch
 * should be allowed as long as the behavior of the operator program
 * adheres to the signature in the kernel. Libbpf should not enforce the
 * signature; rather, let the kernel verifier handle the enforcement.
 */
static void test_struct_ops_incompatible(void)
{
	struct struct_ops_module *skel;
	struct bpf_link *link;
	int err;

	skel = struct_ops_module__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_module_open"))
		return;

	bpf_map__set_autocreate(skel->maps.testmod_zeroed, false);

	err = struct_ops_module__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_incompatible);
	if (ASSERT_OK_PTR(link, "attach_struct_ops"))
		bpf_link__destroy(link);

cleanup:
	struct_ops_module__destroy(skel);
}

/* validate that it's ok to "turn off" callback that kernel supports */
static void test_struct_ops_nulled_out_cb(void)
{
	struct struct_ops_nulled_out_cb *skel;
	int err;

	skel = struct_ops_nulled_out_cb__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	/* kernel knows about test_1, but we still null it out */
	skel->struct_ops.ops->test_1 = NULL;

	err = struct_ops_nulled_out_cb__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	ASSERT_FALSE(bpf_program__autoload(skel->progs.test_1_turn_off), "prog_autoload");
	ASSERT_LT(bpf_program__fd(skel->progs.test_1_turn_off), 0, "prog_fd");

cleanup:
	struct_ops_nulled_out_cb__destroy(skel);
}

/* validate that libbpf generates reasonable error message if struct_ops is
 * not referenced in any struct_ops map
 */
static void test_struct_ops_forgotten_cb(void)
{
	struct struct_ops_forgotten_cb *skel;
	char *log;
	int err;

	skel = struct_ops_forgotten_cb__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	start_libbpf_log_capture();

	err = struct_ops_forgotten_cb__load(skel);
	if (!ASSERT_ERR(err, "skel_load"))
		goto cleanup;

	log = stop_libbpf_log_capture();
	ASSERT_HAS_SUBSTR(log,
			  "prog 'test_1_forgotten': SEC(\"struct_ops\") program isn't referenced anywhere, did you forget to use it?",
			  "libbpf_log");
	free(log);

	struct_ops_forgotten_cb__destroy(skel);

	/* now let's programmatically use it, we should be fine now */
	skel = struct_ops_forgotten_cb__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->struct_ops.ops->test_1 = skel->progs.test_1_forgotten; /* not anymore */

	err = struct_ops_forgotten_cb__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

cleanup:
	struct_ops_forgotten_cb__destroy(skel);
}

void serial_test_struct_ops_module(void)
{
	if (test__start_subtest("struct_ops_load"))
		test_struct_ops_load();
	if (test__start_subtest("struct_ops_not_zeroed"))
		test_struct_ops_not_zeroed();
	if (test__start_subtest("struct_ops_incompatible"))
		test_struct_ops_incompatible();
	if (test__start_subtest("struct_ops_null_out_cb"))
		test_struct_ops_nulled_out_cb();
	if (test__start_subtest("struct_ops_forgotten_cb"))
		test_struct_ops_forgotten_cb();
}

