/*
 * Copyright 2016, Jack Miller, IBM Corp.
 * Licensed under GPLv2.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "ebb.h"
#include "ebb_lmr.h"

#define CHECKS 10000

int ebb_lmr_regs(void)
{
	int i;

	SKIP_IF(!lmr_is_supported());

	ebb_global_enable();

	for (i = 0; i < CHECKS; i++) {
		mtspr(SPRN_LMRR, i << 25);	// skip size and rsvd bits
		mtspr(SPRN_LMSER, i);

		FAIL_IF(mfspr(SPRN_LMRR) != (i << 25));
		FAIL_IF(mfspr(SPRN_LMSER) != i);
	}

	return 0;
}

int main(void)
{
	return test_harness(ebb_lmr_regs, "ebb_lmr_regs");
}
