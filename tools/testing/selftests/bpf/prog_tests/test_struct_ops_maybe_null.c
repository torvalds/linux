// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>

#include "struct_ops_maybe_null.skel.h"
#include "struct_ops_maybe_null_fail.skel.h"

/* Test that the verifier accepts a program that access a nullable pointer
 * with a proper check.
 */
static void maybe_null(void)
{
	struct struct_ops_maybe_null *skel;

	skel = struct_ops_maybe_null__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_module_open_and_load"))
		return;

	struct_ops_maybe_null__destroy(skel);
}

/* Test that the verifier rejects a program that access a nullable pointer
 * without a check beforehand.
 */
static void maybe_null_fail(void)
{
	struct struct_ops_maybe_null_fail *skel;

	skel = struct_ops_maybe_null_fail__open_and_load();
	if (ASSERT_ERR_PTR(skel, "struct_ops_module_fail__open_and_load"))
		return;

	struct_ops_maybe_null_fail__destroy(skel);
}

void test_struct_ops_maybe_null(void)
{
	/* The verifier verifies the programs at load time, so testing both
	 * programs in the same compile-unit is complicated. We run them in
	 * separate objects to simplify the testing.
	 */
	if (test__start_subtest("maybe_null"))
		maybe_null();
	if (test__start_subtest("maybe_null_fail"))
		maybe_null_fail();
}
