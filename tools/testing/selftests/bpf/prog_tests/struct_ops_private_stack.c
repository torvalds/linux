// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "struct_ops_private_stack.skel.h"
#include "struct_ops_private_stack_fail.skel.h"
#include "struct_ops_private_stack_recur.skel.h"
#include "struct_ops_private_stack_large.skel.h"

#if defined(__x86_64__) || defined(__aarch64__) || defined(__powerpc64__)
static void test_private_stack(void)
{
	struct struct_ops_private_stack *skel;
	struct bpf_link *link;
	int err;

	skel = struct_ops_private_stack__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_private_stack__open"))
		return;

	err = struct_ops_private_stack__load(skel);
	if (!ASSERT_OK(err, "struct_ops_private_stack__load"))
		goto cleanup;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_1);
	if (!ASSERT_OK_PTR(link, "attach_struct_ops"))
		goto cleanup;

	ASSERT_OK(trigger_module_test_read(256), "trigger_read");

	ASSERT_EQ(skel->bss->val_i, 3, "val_i");
	ASSERT_EQ(skel->bss->val_j, 8, "val_j");

	bpf_link__destroy(link);

cleanup:
	struct_ops_private_stack__destroy(skel);
}

static void test_private_stack_fail(void)
{
	struct struct_ops_private_stack_fail *skel;
	int err;

	skel = struct_ops_private_stack_fail__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_private_stack_fail__open"))
		return;

	err = struct_ops_private_stack_fail__load(skel);
	ASSERT_ERR(err, "struct_ops_private_stack_fail__load");

	struct_ops_private_stack_fail__destroy(skel);
}

static void test_private_stack_recur(void)
{
	struct struct_ops_private_stack_recur *skel;
	struct bpf_link *link;
	int err;

	skel = struct_ops_private_stack_recur__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_private_stack_recur__open"))
		return;

	err = struct_ops_private_stack_recur__load(skel);
	if (!ASSERT_OK(err, "struct_ops_private_stack_recur__load"))
		goto cleanup;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_1);
	if (!ASSERT_OK_PTR(link, "attach_struct_ops"))
		goto cleanup;

	ASSERT_OK(trigger_module_test_read(256), "trigger_read");

	ASSERT_EQ(skel->bss->val_j, 3, "val_j");

	bpf_link__destroy(link);

cleanup:
	struct_ops_private_stack_recur__destroy(skel);
}

/* Two frames of 2 KiB each on the private stack */
static void test_private_stack_large(void)
{
	struct struct_ops_private_stack_large *skel;
	struct bpf_link *link;

	if (!is_large_stack_supported()) {
		test__skip();
		return;
	}

	skel = struct_ops_private_stack_large__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_private_stack_large__open_and_load"))
		return;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_1);
	if (!ASSERT_OK_PTR(link, "attach_struct_ops"))
		goto cleanup;

	ASSERT_OK(trigger_module_test_read(256), "trigger_read");

	ASSERT_EQ(skel->bss->val, 100 + 30 + 12, "val");

	bpf_link__destroy(link);
cleanup:
	struct_ops_private_stack_large__destroy(skel);
}

static void __test_struct_ops_private_stack(void)
{
	if (test__start_subtest("private_stack"))
		test_private_stack();
	if (test__start_subtest("private_stack_fail"))
		test_private_stack_fail();
	if (test__start_subtest("private_stack_recur"))
		test_private_stack_recur();
	if (test__start_subtest("private_stack_large"))
		test_private_stack_large();
}
#else
static void __test_struct_ops_private_stack(void)
{
	test__skip();
}
#endif

void test_struct_ops_private_stack(void)
{
	__test_struct_ops_private_stack();
}
