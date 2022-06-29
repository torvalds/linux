// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test basic matrix multiply assist (MMA) functionality if available.
 *
 * Copyright 2020, Alistair Popple, IBM Corp.
 */
#include <stdio.h>
#include <stdint.h>

#include "utils.h"

extern void test_mma(uint16_t (*)[8], uint16_t (*)[8], uint32_t (*)[4*4]);

static int mma(void)
{
	int i;
	int rc = 0;
	uint16_t x[] = {1, 0, 2, 0, 3, 0, 4, 0};
	uint16_t y[] = {1, 0, 2, 0, 3, 0, 4, 0};
	uint32_t z[4*4];
	uint32_t exp[4*4] = {1, 2, 3, 4,
			     2, 4, 6, 8,
			     3, 6, 9, 12,
			     4, 8, 12, 16};

	SKIP_IF_MSG(!have_hwcap2(PPC_FEATURE2_ARCH_3_1), "Need ISAv3.1");
	SKIP_IF_MSG(!have_hwcap2(PPC_FEATURE2_MMA), "Need MMA");

	test_mma(&x, &y, &z);

	for (i = 0; i < 16; i++) {
		printf("MMA[%d] = %d ", i, z[i]);

		if (z[i] == exp[i]) {
			printf(" (Correct)\n");
		} else {
			printf(" (Incorrect)\n");
			rc = 1;
		}
	}

	return rc;
}

int main(int argc, char *argv[])
{
	return test_harness(mma, "mma");
}
