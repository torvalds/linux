// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "tracing_struct.skel.h"
#include "tracing_struct_many_args.skel.h"

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

void test_tracing_struct(void)
{
	if (test__start_subtest("struct_args"))
		test_struct_args();
	if (test__start_subtest("struct_many_args"))
		test_struct_many_args();
}
