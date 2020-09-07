// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016, Michael Ellerman, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <asm/cputable.h>

#include "utils.h"

#define SIZE (64 * 1024)

int test_prot_sao(void)
{
	char *p;

	/* SAO was introduced in 2.06 and removed in 3.1 */
	SKIP_IF(!have_hwcap(PPC_FEATURE_ARCH_2_06) ||
		have_hwcap2(PPC_FEATURE2_ARCH_3_1));

	/*
	 * Ensure we can ask for PROT_SAO.
	 * We can't really verify that it does the right thing, but at least we
	 * confirm the kernel will accept it.
	 */
	p = mmap(NULL, SIZE, PROT_READ | PROT_WRITE | PROT_SAO,
		 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	FAIL_IF(p == MAP_FAILED);

	/* Write to the mapping, to at least cause a fault */
	memset(p, 0xaa, SIZE);

	return 0;
}

int main(void)
{
	return test_harness(test_prot_sao, "prot-sao");
}
