// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "tracing_struct.skel.h"
#include "tracing_struct_many_args.skel.h"
#include "tracing_struct_int128.skel.h"
#include "tracing_struct_int128_tgt.skel.h"
#include "tracing_struct_int128_tracer.skel.h"
#include "tracing_stack_arg_tailcall_tgt.skel.h"
#include "tracing_stack_arg_tailcall_tracer.skel.h"

static void test_struct_args(void)
{
	struct tracing_struct *skel;
	int err;

	skel = tracing_struct__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_struct__open_and_load"))
		return;

	err = tracing_struct__attach(skel);
	if (!ASSERT_OK(err, "tracing_struct__attach"))
		goto destroy_skel;

	ASSERT_OK(trigger_module_test_read(256), "trigger_read");

	ASSERT_EQ(skel->bss->t1_a_a, 2, "t1:a.a");
	ASSERT_EQ(skel->bss->t1_a_b, 3, "t1:a.b");
	ASSERT_EQ(skel->bss->t1_b, 1, "t1:b");
	ASSERT_EQ(skel->bss->t1_c, 4, "t1:c");

	ASSERT_EQ(skel->bss->t1_nregs, 4, "t1 nregs");
	ASSERT_EQ(skel->bss->t1_reg0, 2, "t1 reg0");
	ASSERT_EQ(skel->bss->t1_reg1, 3, "t1 reg1");
	ASSERT_EQ(skel->bss->t1_reg2, 1, "t1 reg2");
	ASSERT_EQ(skel->bss->t1_reg3, 4, "t1 reg3");
	ASSERT_EQ(skel->bss->t1_ret, 10, "t1 ret");

	ASSERT_EQ(skel->bss->t2_a, 1, "t2:a");
	ASSERT_EQ(skel->bss->t2_b_a, 2, "t2:b.a");
	ASSERT_EQ(skel->bss->t2_b_b, 3, "t2:b.b");
	ASSERT_EQ(skel->bss->t2_c, 4, "t2:c");
	ASSERT_EQ(skel->bss->t2_ret, 10, "t2 ret");

	ASSERT_EQ(skel->bss->t3_a, 1, "t3:a");
	ASSERT_EQ(skel->bss->t3_b, 4, "t3:b");
	ASSERT_EQ(skel->bss->t3_c_a, 2, "t3:c.a");
	ASSERT_EQ(skel->bss->t3_c_b, 3, "t3:c.b");
	ASSERT_EQ(skel->bss->t3_ret, 10, "t3 ret");

	ASSERT_EQ(skel->bss->t4_a_a, 10, "t4:a.a");
	ASSERT_EQ(skel->bss->t4_b, 1, "t4:b");
	ASSERT_EQ(skel->bss->t4_c, 2, "t4:c");
	ASSERT_EQ(skel->bss->t4_d, 3, "t4:d");
	ASSERT_EQ(skel->bss->t4_e_a, 2, "t4:e.a");
	ASSERT_EQ(skel->bss->t4_e_b, 3, "t4:e.b");
	ASSERT_EQ(skel->bss->t4_ret, 21, "t4 ret");

	ASSERT_EQ(skel->bss->t5_ret, 1, "t5 ret");

	ASSERT_EQ(skel->bss->t6, 1, "t6 ret");

destroy_skel:
	tracing_struct__destroy(skel);
}

static void test_struct_many_args(void)
{
	struct tracing_struct_many_args *skel;
	int err;

	skel = tracing_struct_many_args__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_struct_many_args__open_and_load"))
		return;

	err = tracing_struct_many_args__attach(skel);
	if (!ASSERT_OK(err, "tracing_struct_many_args__attach"))
		goto destroy_skel;

	ASSERT_OK(trigger_module_test_read(256), "trigger_read");

	ASSERT_EQ(skel->bss->t7_a, 16, "t7:a");
	ASSERT_EQ(skel->bss->t7_b, 17, "t7:b");
	ASSERT_EQ(skel->bss->t7_c, 18, "t7:c");
	ASSERT_EQ(skel->bss->t7_d, 19, "t7:d");
	ASSERT_EQ(skel->bss->t7_e, 20, "t7:e");
	ASSERT_EQ(skel->bss->t7_f_a, 21, "t7:f.a");
	ASSERT_EQ(skel->bss->t7_f_b, 22, "t7:f.b");
	ASSERT_EQ(skel->bss->t7_ret, 133, "t7 ret");

	ASSERT_EQ(skel->bss->t8_a, 16, "t8:a");
	ASSERT_EQ(skel->bss->t8_b, 17, "t8:b");
	ASSERT_EQ(skel->bss->t8_c, 18, "t8:c");
	ASSERT_EQ(skel->bss->t8_d, 19, "t8:d");
	ASSERT_EQ(skel->bss->t8_e, 20, "t8:e");
	ASSERT_EQ(skel->bss->t8_f_a, 21, "t8:f.a");
	ASSERT_EQ(skel->bss->t8_f_b, 22, "t8:f.b");
	ASSERT_EQ(skel->bss->t8_g, 23, "t8:g");
	ASSERT_EQ(skel->bss->t8_ret, 156, "t8 ret");

	ASSERT_EQ(skel->bss->t9_a, 16, "t9:a");
	ASSERT_EQ(skel->bss->t9_b, 17, "t9:b");
	ASSERT_EQ(skel->bss->t9_c, 18, "t9:c");
	ASSERT_EQ(skel->bss->t9_d, 19, "t9:d");
	ASSERT_EQ(skel->bss->t9_e, 20, "t9:e");
	ASSERT_EQ(skel->bss->t9_f, 21, "t9:f");
	ASSERT_EQ(skel->bss->t9_g, 22, "t9:f");
	ASSERT_EQ(skel->bss->t9_h_a, 23, "t9:h.a");
	ASSERT_EQ(skel->bss->t9_h_b, 24, "t9:h.b");
	ASSERT_EQ(skel->bss->t9_h_c, 25, "t9:h.c");
	ASSERT_EQ(skel->bss->t9_h_d, 26, "t9:h.d");
	ASSERT_EQ(skel->bss->t9_i, 27, "t9:i");
	ASSERT_EQ(skel->bss->t9_ret, 258, "t9 ret");

destroy_skel:
	tracing_struct_many_args__destroy(skel);
}

static void test_int128_args(void)
{
	/*
	 * __int128 arguments are passed in a register pair on x86_64 and
	 * arm64, which the trampoline packs into two context slots. Past the
	 * registers, both start an argument aligned to 16 bytes at an even
	 * stack slot, which the trampoline packs too. Other architectures
	 * pass a __int128 differently (e.g. s390x passes larger arguments by
	 * reference), so only exercise this on x86_64 and arm64.
	 */
#if defined(__x86_64__) || defined(__aarch64__)
	struct tracing_struct_int128 *skel;
	int err;

	skel = tracing_struct_int128__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_struct_int128__open_and_load"))
		return;

	err = tracing_struct_int128__attach(skel);
	if (!ASSERT_OK(err, "tracing_struct_int128__attach"))
		goto destroy_skel;

	ASSERT_OK(trigger_module_test_read(256), "trigger_read");

	ASSERT_EQ(skel->bss->t_b, 2, "t:b");
	ASSERT_EQ(skel->bss->t_c, 3, "t:c");
	ASSERT_EQ(skel->bss->t_ret, 6, "t ret");

	ASSERT_EQ(skel->bss->s_g, 7, "s:g");
	ASSERT_EQ(skel->bss->s_h_lo, 8, "s:h.lo");
	ASSERT_EQ(skel->bss->s_h_hi, 9, "s:h.hi");
	ASSERT_EQ(skel->bss->s_i, 10, "s:i");
	ASSERT_EQ(skel->bss->s_ret, 385, "s ret");

	ASSERT_EQ(skel->bss->b_f_a, 6, "b:f.a");
	ASSERT_EQ(skel->bss->b_f_b, 7, "b:f.b");
	ASSERT_EQ(skel->bss->b_g, 8, "b:g");
	ASSERT_EQ(skel->bss->b_h, 9, "b:h");
	ASSERT_EQ(skel->bss->b_i_lo, 10, "b:i.lo");
	ASSERT_EQ(skel->bss->b_i_hi, 11, "b:i.hi");
	ASSERT_EQ(skel->bss->b_ret, 506, "b ret");

destroy_skel:
	tracing_struct_int128__destroy(skel);
#else
	test__skip();
#endif
}

static void test_int128_tgt_prog(void)
{
#if defined(__x86_64__) || defined(__aarch64__) || defined(__s390x__)
	struct tracing_struct_int128_tracer *tracer = NULL;
	struct tracing_struct_int128_tgt *tgt;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, fd;

	tgt = tracing_struct_int128_tgt__open_and_load();
	if (!ASSERT_OK_PTR(tgt, "tgt__open_and_load"))
		return;
	if (!tgt->rodata->has_struct_arg) {
		test__skip();
		goto out;
	}
	fd = bpf_program__fd(tgt->progs.run);

	tracer = tracing_struct_int128_tracer__open();
	if (!ASSERT_OK_PTR(tracer, "tracer__open"))
		goto out;

	err = bpf_program__set_attach_target(tracer->progs.odd_entry, fd, "callee_odd");
	err = err ?: bpf_program__set_attach_target(tracer->progs.odd_exit, fd, "callee_odd");
	if (tgt->rodata->has_stack_arg) {
		err = err ?: bpf_program__set_attach_target(tracer->progs.stack_entry, fd,
							     "callee_stack");
		err = err ?: bpf_program__set_attach_target(tracer->progs.stack_exit, fd,
							     "callee_stack");
		err = err ?: bpf_program__set_attach_target(tracer->progs.split_entry, fd,
							     "callee_split");
		err = err ?: bpf_program__set_attach_target(tracer->progs.split_exit, fd,
							     "callee_split");
	} else {
		bpf_program__set_autoload(tracer->progs.stack_entry, false);
		bpf_program__set_autoload(tracer->progs.stack_exit, false);
		bpf_program__set_autoload(tracer->progs.split_entry, false);
		bpf_program__set_autoload(tracer->progs.split_exit, false);
	}
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	err = tracing_struct_int128_tracer__load(tracer);
	if (!ASSERT_OK(err, "tracer__load"))
		goto out;
	err = tracing_struct_int128_tracer__attach(tracer);
	if (!ASSERT_OK(err, "tracer__attach"))
		goto out;

	err = bpf_prog_test_run_opts(fd, &topts);
	if (!ASSERT_OK(err, "test_run") || !ASSERT_EQ(topts.retval, 0, "retval"))
		goto out;

	ASSERT_EQ(tracer->bss->odd_a, 1, "odd:a");
	ASSERT_EQ(tracer->bss->odd_lo, 8, "odd:v.lo");
	ASSERT_EQ(tracer->bss->odd_hi, 9, "odd:v.hi");
	ASSERT_EQ(tracer->bss->odd_b, 2, "odd:b");
	ASSERT_EQ(tgt->bss->ret_odd, 52, "odd ret");
	ASSERT_EQ(tracer->bss->odd_ret, 52, "odd fexit ret");

	if (tgt->rodata->has_stack_arg) {
		ASSERT_EQ(tracer->bss->stack_g, 7, "stack:g");
		ASSERT_EQ(tracer->bss->stack_lo, 8, "stack:h.lo");
		ASSERT_EQ(tracer->bss->stack_hi, 9, "stack:h.hi");
		ASSERT_EQ(tracer->bss->stack_i, 10, "stack:i");
		ASSERT_EQ(tgt->bss->ret_stack, 385, "stack ret");
		ASSERT_EQ(tracer->bss->stack_ret, 385, "stack fexit ret");

		ASSERT_EQ(tracer->bss->split_e, 5, "split:e");
		ASSERT_EQ(tracer->bss->split_lo, 8, "split:h.lo");
		ASSERT_EQ(tracer->bss->split_hi, 9, "split:h.hi");
		ASSERT_EQ(tracer->bss->split_i, 10, "split:i");
		ASSERT_EQ(tgt->bss->ret_split, 246, "split ret");
		ASSERT_EQ(tracer->bss->split_ret, 246, "split fexit ret");
	}

out:
	tracing_struct_int128_tracer__destroy(tracer);
	tracing_struct_int128_tgt__destroy(tgt);
#else
	test__skip();
#endif
}

/*
 * A static function whose BTF is unreliable gets a trampoline that only knows
 * of the register arguments, so one taking stack arguments must be refused.
 */
static void test_tgt_prog_unreliable(void)
{
#if defined(__x86_64__) || defined(__aarch64__)
	struct tracing_struct_int128_tracer *tracer = NULL;
	struct tracing_struct_int128_tgt *tgt;
	char log_buf[4096] = {};
	struct bpf_program *prog;
	int err, fd;

	tgt = tracing_struct_int128_tgt__open_and_load();
	if (!ASSERT_OK_PTR(tgt, "tgt__open_and_load"))
		return;
	if (!tgt->rodata->has_stack_arg) {
		test__skip();
		goto out;
	}
	fd = bpf_program__fd(tgt->progs.run);

	tracer = tracing_struct_int128_tracer__open();
	if (!ASSERT_OK_PTR(tracer, "tracer__open"))
		goto out;

	bpf_object__for_each_program(prog, tracer->obj)
		bpf_program__set_autoload(prog, false);
	bpf_program__set_autoload(tracer->progs.unreliable_entry, true);
	bpf_program__set_log_buf(tracer->progs.unreliable_entry, log_buf, sizeof(log_buf));
	err = bpf_program__set_attach_target(tracer->progs.unreliable_entry, fd,
					     "callee_unreliable");
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	err = tracing_struct_int128_tracer__load(tracer);
	ASSERT_ERR(err, "tracer__load");
	ASSERT_HAS_SUBSTR(log_buf, "unreliable BTF and stack arguments", "log");

out:
	tracing_struct_int128_tracer__destroy(tracer);
	tracing_struct_int128_tgt__destroy(tgt);
#else
	test__skip();
#endif
}

/*
 * A trampoline on a function of a tail_call_reachable program keeps the tail
 * call counter pointer on its stack, and has to hand the function its stack
 * arguments past it.
 */
static void test_stack_arg_tailcall(void)
{
#if defined(__x86_64__) || defined(__aarch64__)
	struct tracing_stack_arg_tailcall_tracer *tracer = NULL;
	struct tracing_stack_arg_tailcall_tgt *tgt;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, fd;

	tgt = tracing_stack_arg_tailcall_tgt__open_and_load();
	if (!ASSERT_OK_PTR(tgt, "tgt__open_and_load"))
		return;
	if (!tgt->rodata->has_stack_arg) {
		test__skip();
		goto out;
	}
	fd = bpf_program__fd(tgt->progs.run);

	tracer = tracing_stack_arg_tailcall_tracer__open();
	if (!ASSERT_OK_PTR(tracer, "tracer__open"))
		goto out;

	err = bpf_program__set_attach_target(tracer->progs.stack_entry, fd, "callee_stack");
	err = err ?: bpf_program__set_attach_target(tracer->progs.stack_exit, fd, "callee_stack");
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	err = tracing_stack_arg_tailcall_tracer__load(tracer);
	if (!ASSERT_OK(err, "tracer__load"))
		goto out;
	err = tracing_stack_arg_tailcall_tracer__attach(tracer);
	if (!ASSERT_OK(err, "tracer__attach"))
		goto out;

	err = bpf_prog_test_run_opts(fd, &topts);
	if (!ASSERT_OK(err, "test_run") || !ASSERT_EQ(topts.retval, 0, "retval"))
		goto out;

	ASSERT_EQ(tracer->bss->stack_g, 7, "stack:g");
	ASSERT_EQ(tracer->bss->stack_h, 8, "stack:h");
	ASSERT_EQ(tgt->bss->ret_stack, 204, "stack ret");
	ASSERT_EQ(tracer->bss->stack_ret, 204, "stack fexit ret");

out:
	tracing_stack_arg_tailcall_tracer__destroy(tracer);
	tracing_stack_arg_tailcall_tgt__destroy(tgt);
#else
	test__skip();
#endif
}

static void test_union_args(void)
{
	struct tracing_struct *skel;
	int err;

	skel = tracing_struct__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_struct__open_and_load"))
		return;

	err = tracing_struct__attach(skel);
	if (!ASSERT_OK(err, "tracing_struct__attach"))
		goto out;

	ASSERT_OK(trigger_module_test_read(256), "trigger_read");

	ASSERT_EQ(skel->bss->ut1_a_a, 1, "ut1:a.arg.a");
	ASSERT_EQ(skel->bss->ut1_b, 4, "ut1:b");
	ASSERT_EQ(skel->bss->ut1_c, 5, "ut1:c");

	ASSERT_EQ(skel->bss->ut2_a, 6, "ut2:a");
	ASSERT_EQ(skel->bss->ut2_b_a, 2, "ut2:b.arg.a");
	ASSERT_EQ(skel->bss->ut2_b_b, 3, "ut2:b.arg.b");

out:
	tracing_struct__destroy(skel);
}

void test_tracing_struct(void)
{
	if (test__start_subtest("struct_args"))
		test_struct_args();
	if (test__start_subtest("struct_many_args"))
		test_struct_many_args();
	if (test__start_subtest("int128_args"))
		test_int128_args();
	if (test__start_subtest("int128_tgt_prog"))
		test_int128_tgt_prog();
	if (test__start_subtest("tgt_prog_unreliable"))
		test_tgt_prog_unreliable();
	if (test__start_subtest("stack_arg_tailcall"))
		test_stack_arg_tailcall();
	if (test__start_subtest("union_args"))
		test_union_args();
}
