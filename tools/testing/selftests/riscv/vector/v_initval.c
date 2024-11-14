// SPDX-License-Identifier: GPL-2.0-only

#include "../../kselftest_harness.h"
#include "v_helpers.h"

#define NEXT_PROGRAM "./v_exec_initval_nolibc"

TEST(v_initval)
{
	if (!is_vector_supported())
		SKIP(return, "Vector not supported");

	ASSERT_EQ(0, launch_test(NEXT_PROGRAM, 0));
}

TEST_HARNESS_MAIN
