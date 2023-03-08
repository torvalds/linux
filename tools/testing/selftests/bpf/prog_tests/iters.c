// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>

#include "iters.skel.h"
#include "iters_state_safety.skel.h"
#include "iters_looping.skel.h"

void test_iters(void)
{
	RUN_TESTS(iters_state_safety);
	RUN_TESTS(iters_looping);
	RUN_TESTS(iters);
}
