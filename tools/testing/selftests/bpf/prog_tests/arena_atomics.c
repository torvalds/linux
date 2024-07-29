// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "arena_atomics.skel.h"

static void test_add(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.add);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->add64_value, 3, "add64_value");
	ASSERT_EQ(skel->arena->add64_result, 1, "add64_result");

	ASSERT_EQ(skel->arena->add32_value, 3, "add32_value");
	ASSERT_EQ(skel->arena->add32_result, 1, "add32_result");

	ASSERT_EQ(skel->arena->add_stack_value_copy, 3, "add_stack_value");
	ASSERT_EQ(skel->arena->add_stack_result, 1, "add_stack_result");

	ASSERT_EQ(skel->arena->add_noreturn_value, 3, "add_noreturn_value");
}

static void test_sub(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.sub);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->sub64_value, -1, "sub64_value");
	ASSERT_EQ(skel->arena->sub64_result, 1, "sub64_result");

	ASSERT_EQ(skel->arena->sub32_value, -1, "sub32_value");
	ASSERT_EQ(skel->arena->sub32_result, 1, "sub32_result");

	ASSERT_EQ(skel->arena->sub_stack_value_copy, -1, "sub_stack_value");
	ASSERT_EQ(skel->arena->sub_stack_result, 1, "sub_stack_result");

	ASSERT_EQ(skel->arena->sub_noreturn_value, -1, "sub_noreturn_value");
}

static void test_and(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.and);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->and64_value, 0x010ull << 32, "and64_value");
	ASSERT_EQ(skel->arena->and32_value, 0x010, "and32_value");
}

static void test_or(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.or);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->or64_value, 0x111ull << 32, "or64_value");
	ASSERT_EQ(skel->arena->or32_value, 0x111, "or32_value");
}

static void test_xor(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.xor);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->xor64_value, 0x101ull << 32, "xor64_value");
	ASSERT_EQ(skel->arena->xor32_value, 0x101, "xor32_value");
}

static void test_cmpxchg(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.cmpxchg);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->cmpxchg64_value, 2, "cmpxchg64_value");
	ASSERT_EQ(skel->arena->cmpxchg64_result_fail, 1, "cmpxchg_result_fail");
	ASSERT_EQ(skel->arena->cmpxchg64_result_succeed, 1, "cmpxchg_result_succeed");

	ASSERT_EQ(skel->arena->cmpxchg32_value, 2, "lcmpxchg32_value");
	ASSERT_EQ(skel->arena->cmpxchg32_result_fail, 1, "cmpxchg_result_fail");
	ASSERT_EQ(skel->arena->cmpxchg32_result_succeed, 1, "cmpxchg_result_succeed");
}

static void test_xchg(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.xchg);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->xchg64_value, 2, "xchg64_value");
	ASSERT_EQ(skel->arena->xchg64_result, 1, "xchg64_result");

	ASSERT_EQ(skel->arena->xchg32_value, 2, "xchg32_value");
	ASSERT_EQ(skel->arena->xchg32_result, 1, "xchg32_result");
}

static void test_uaf(struct arena_atomics *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	/* No need to attach it, just run it directly */
	prog_fd = bpf_program__fd(skel->progs.uaf);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	ASSERT_EQ(skel->arena->uaf_recovery_fails, 0, "uaf_recovery_fails");
}

void test_arena_atomics(void)
{
	struct arena_atomics *skel;
	int err;

	skel = arena_atomics__open();
	if (!ASSERT_OK_PTR(skel, "arena atomics skeleton open"))
		return;

	if (skel->data->skip_tests) {
		printf("%s:SKIP:no ENABLE_ATOMICS_TESTS or no addr_space_cast support in clang",
		       __func__);
		test__skip();
		goto cleanup;
	}
	err = arena_atomics__load(skel);
	if (!ASSERT_OK(err, "arena atomics skeleton load"))
		return;
	skel->bss->pid = getpid();

	if (test__start_subtest("add"))
		test_add(skel);
	if (test__start_subtest("sub"))
		test_sub(skel);
	if (test__start_subtest("and"))
		test_and(skel);
	if (test__start_subtest("or"))
		test_or(skel);
	if (test__start_subtest("xor"))
		test_xor(skel);
	if (test__start_subtest("cmpxchg"))
		test_cmpxchg(skel);
	if (test__start_subtest("xchg"))
		test_xchg(skel);
	if (test__start_subtest("uaf"))
		test_uaf(skel);

cleanup:
	arena_atomics__destroy(skel);
}
