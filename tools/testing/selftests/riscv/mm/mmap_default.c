// SPDX-License-Identifier: GPL-2.0-only
#include <sys/mman.h>
#include <mmap_test.h>

#include "../../kselftest_harness.h"

TEST(default_rlimit)
{
	EXPECT_EQ(TOP_DOWN, memory_layout());

	TEST_MMAPS;
}

TEST_HARNESS_MAIN
