// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "aggregate_arg_func.skel.h"
#include "aggregate_arg_kfunc.skel.h"

void test_aggregate_arg(void)
{
	RUN_TESTS(aggregate_arg_func);
	RUN_TESTS(aggregate_arg_kfunc);
}
