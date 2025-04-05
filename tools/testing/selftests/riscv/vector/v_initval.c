// SPDX-License-Identifier: GPL-2.0-only

#include "../../kselftest_harness.h"
#include "v_helpers.h"

#define NEXT_PROGRAM "./v_exec_initval_nolibc"

TEST(v_initval)
{
	int xtheadvector = 0;

	if (!is_vector_supported()) {
		if (is_xtheadvector_supported())
			xtheadvector = 1;
		else
			SKIP(return, "Vector not supported");
	}

	ASSERT_EQ(0, launch_test(NEXT_PROGRAM, 0, xtheadvector));
}

TEST_HARNESS_MAIN
