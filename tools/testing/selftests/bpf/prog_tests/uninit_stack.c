// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "uninit_stack.skel.h"

void test_uninit_stack(void)
{
	RUN_TESTS(uninit_stack);
}
