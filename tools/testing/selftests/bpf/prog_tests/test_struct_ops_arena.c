// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>

#include "struct_ops_arena.skel.h"
#include "struct_ops_arena_fail.skel.h"

#if defined(__x86_64__)
/*
 * Attach callbacks with __arena and __arena__nullable arguments and drive
 * them through the bpf_testmod_ops3_call_test_arena*() kfuncs.
 */
static void arena_arg(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct struct_ops_arena *skel;
	struct bpf_link *link = NULL;
	int err;

	skel = struct_ops_arena__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_arena__open_and_load"))
		return;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_arena);
	if (!ASSERT_OK_PTR(link, "attach_struct_ops"))
		goto out;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.trigger),
				     &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "trigger_retval");

out:
	bpf_link__destroy(link);
	struct_ops_arena__destroy(skel);
}

/*
 * A program with no arena cannot attach to a member with an __arena
 * argument.
 */
static void arena_arg_fail(void)
{
	struct struct_ops_arena_fail *skel;

	skel = struct_ops_arena_fail__open_and_load();
	if (ASSERT_ERR_PTR(skel, "struct_ops_arena_fail__open_and_load"))
		return;

	struct_ops_arena_fail__destroy(skel);
}
#endif

/*
 * Serialized because it attaches the singleton bpf_testmod_ops3, which
 * test_struct_ops_private_stack also attaches; registering it twice fails
 * with -EEXIST.
 */
void serial_test_struct_ops_arena(void)
{
	/*
	 * Arena struct_ops arguments need JIT support, currently x86-64 only.
	 * Elsewhere verification fails with "JIT does not support arena
	 * arguments", so the programs cannot even load.
	 */
#if defined(__x86_64__)
	if (test__start_subtest("arena_arg"))
		arena_arg();
	if (test__start_subtest("arena_arg_fail"))
		arena_arg_fail();
#else
	test__skip();
#endif
}
