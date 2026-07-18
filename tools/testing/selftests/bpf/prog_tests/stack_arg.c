// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <network_helpers.h>
#include "stack_arg.skel.h"
#include "stack_arg_kfunc.skel.h"

static void run_subtest(struct bpf_program *prog, int expected)
{
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	prog_fd = bpf_program__fd(prog);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, expected, "retval");
}

static void test_global_many(void)
{
	struct stack_arg *skel;

	skel = stack_arg__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	if (!skel->rodata->has_stack_arg) {
		test__skip();
		goto out;
	}

	if (!ASSERT_OK(stack_arg__load(skel), "load"))
		goto out;

	run_subtest(skel->progs.test_global_many_args, 55);

out:
	stack_arg__destroy(skel);
}

static void test_async_cb_many(void)
{
	struct stack_arg *skel;

	skel = stack_arg__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	if (!skel->rodata->has_stack_arg) {
		test__skip();
		goto out;
	}

	if (!ASSERT_OK(stack_arg__load(skel), "load"))
		goto out;

	run_subtest(skel->progs.test_async_cb_many_args, 0);

	/* Wait for the timer callback to fire and verify the result.
	 * 10+20+30+40+50+60+70+80+90+100 = 550
	 */
	usleep(50);
	ASSERT_EQ(skel->bss->timer_result, 550, "timer_result");

out:
	stack_arg__destroy(skel);
}

static void test_bpf2bpf(void)
{
	struct stack_arg *skel;

	skel = stack_arg__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	if (!skel->rodata->has_stack_arg) {
		test__skip();
		goto out;
	}

	if (!ASSERT_OK(stack_arg__load(skel), "load"))
		goto out;

	run_subtest(skel->progs.test_bpf2bpf_ptr_stack_arg, 75);
	run_subtest(skel->progs.test_bpf2bpf_mix_stack_args, 66);
	run_subtest(skel->progs.test_bpf2bpf_nesting_stack_arg, 84);
	run_subtest(skel->progs.test_bpf2bpf_dynptr_stack_arg, 99);
	run_subtest(skel->progs.test_two_callees, 133);

out:
	stack_arg__destroy(skel);
}

static void test_kfunc(void)
{
	struct stack_arg_kfunc *skel;

	skel = stack_arg_kfunc__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	if (!skel->rodata->has_stack_arg) {
		test__skip();
		goto out;
	}

	if (!ASSERT_OK(stack_arg_kfunc__load(skel), "load"))
		goto out;

	run_subtest(skel->progs.test_stack_arg_scalar, 55);
	run_subtest(skel->progs.test_stack_arg_ptr, 75);
	run_subtest(skel->progs.test_stack_arg_mix, 66);
	run_subtest(skel->progs.test_stack_arg_dynptr, 99);
	run_subtest(skel->progs.test_stack_arg_mem, 151);
	run_subtest(skel->progs.test_stack_arg_iter, 145);
	run_subtest(skel->progs.test_stack_arg_const_str, 45);
	run_subtest(skel->progs.test_stack_arg_timer, 45);

out:
	stack_arg_kfunc__destroy(skel);
}

void test_stack_arg(void)
{
	if (test__start_subtest("global_many_args"))
		test_global_many();
	if (test__start_subtest("async_cb_many_args"))
		test_async_cb_many();
	if (test__start_subtest("bpf2bpf"))
		test_bpf2bpf();
	if (test__start_subtest("kfunc"))
		test_kfunc();
}
