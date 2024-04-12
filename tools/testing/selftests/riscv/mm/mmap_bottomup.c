// SPDX-License-Identifier: GPL-2.0-only
#include <sys/mman.h>
#include <mmap_test.h>

#include "../../kselftest_harness.h"

TEST(infinite_rlimit)
{
	EXPECT_EQ(BOTTOM_UP, memory_layout());

	TEST_MMAPS;
}

TEST_HARNESS_MAIN
