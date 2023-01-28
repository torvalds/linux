// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "nested_trust_failure.skel.h"
#include "nested_trust_success.skel.h"

void test_nested_trust(void)
{
	RUN_TESTS(nested_trust_success);
	RUN_TESTS(nested_trust_failure);
}
