// SPDX-License-Identifier: GPL-2.0-only
#include <sys/mman.h>
#include <mmap_test.h>

#include "../../kselftest_harness.h"

TEST(default_rlimit)
{
// Only works on 64 bit
#if __riscv_xlen == 64
	struct addresses mmap_addresses;

	EXPECT_EQ(TOP_DOWN, memory_layout());

	do_mmaps(&mmap_addresses);

	EXPECT_NE(MAP_FAILED, mmap_addresses.no_hint);
	EXPECT_NE(MAP_FAILED, mmap_addresses.on_37_addr);
	EXPECT_NE(MAP_FAILED, mmap_addresses.on_38_addr);
	EXPECT_NE(MAP_FAILED, mmap_addresses.on_46_addr);
	EXPECT_NE(MAP_FAILED, mmap_addresses.on_47_addr);
	EXPECT_NE(MAP_FAILED, mmap_addresses.on_55_addr);
	EXPECT_NE(MAP_FAILED, mmap_addresses.on_56_addr);

	EXPECT_GT(1UL << 47, (unsigned long)mmap_addresses.no_hint);
	EXPECT_GT(1UL << 38, (unsigned long)mmap_addresses.on_37_addr);
	EXPECT_GT(1UL << 38, (unsigned long)mmap_addresses.on_38_addr);
	EXPECT_GT(1UL << 38, (unsigned long)mmap_addresses.on_46_addr);
	EXPECT_GT(1UL << 47, (unsigned long)mmap_addresses.on_47_addr);
	EXPECT_GT(1UL << 47, (unsigned long)mmap_addresses.on_55_addr);
	EXPECT_GT(1UL << 56, (unsigned long)mmap_addresses.on_56_addr);
#endif
}

TEST_HARNESS_MAIN
