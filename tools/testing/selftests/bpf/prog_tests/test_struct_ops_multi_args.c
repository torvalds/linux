// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "struct_ops_multi_args.skel.h"

static void test_refcounted_multi(void)
{
	struct struct_ops_multi_args *skel;
	char log_buf[4096] = {};
	int err;

	skel = struct_ops_multi_args__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_multi_args__open"))
		return;

	bpf_program__set_log_buf(skel->progs.test_refcounted_multi, log_buf, sizeof(log_buf));

	err = struct_ops_multi_args__load(skel);
	if (!ASSERT_EQ(err, -EINVAL, "struct_ops_multi_args__load"))
		goto out;

	ASSERT_HAS_SUBSTR(log_buf, "program with __ref argument cannot tail call",
			  "check_verifier_log_message");

out:
	struct_ops_multi_args__destroy(skel);
}

static void test_trampoline_stack_args(void)
{
	struct struct_ops_multi_args *skel;
	struct bpf_link *link = NULL;
	int err;

	skel = struct_ops_multi_args__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_multi_args__open"))
		return;

	bpf_program__set_autoload(skel->progs.test_refcounted_multi, false);
	skel->struct_ops.testmod_ref_acquire->test_refcounted_multi = NULL;

	err = struct_ops_multi_args__load(skel);
	if (!ASSERT_OK(err, "struct_ops_multi_args__load"))
		goto out;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_ref_acquire);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops"))
		goto out;

	ASSERT_EQ(skel->bss->trampoline_stack_arg9, 9999, "check_stack_passed_arg9");

out:
	bpf_link__destroy(link);
	struct_ops_multi_args__destroy(skel);
}

void test_struct_ops_multi_args(void)
{
	if (test__start_subtest("test_refcounted_multi"))
		test_refcounted_multi();

	if (test__start_subtest("test_trampoline_stack_args"))
		test_trampoline_stack_args();
}
