/*
 * Copyright 2016, Jack Miller, IBM Corp.
 * Licensed under GPLv2.
 */

#include <stdlib.h>
#include <stdio.h>

#include "ebb.h"
#include "ebb_lmr.h"

#define SIZE		(32 * 1024 * 1024)	/* 32M */
#define LM_SIZE		0	/* Smallest encoding, 32M */

#define SECTIONS	64	/* 1 per bit in LMSER */
#define SECTION_SIZE	(SIZE / SECTIONS)
#define SECTION_LONGS   (SECTION_SIZE / sizeof(long))

static unsigned long *test_mem;

static int lmr_count = 0;

void ebb_lmr_handler(void)
{
	lmr_count++;
}

void ldmx_full_section(unsigned long *mem, int section)
{
	unsigned long *ptr;
	int i;

	for (i = 0; i < SECTION_LONGS; i++) {
		ptr = &mem[(SECTION_LONGS * section) + i];
		ldmx((unsigned long) &ptr);
		ebb_lmr_reset();
	}
}

unsigned long section_masks[] = {
	0x8000000000000000,
	0xFF00000000000000,
	0x0000000F70000000,
	0x8000000000000001,
	0xF0F0F0F0F0F0F0F0,
	0x0F0F0F0F0F0F0F0F,
	0x0
};

int ebb_lmr_section_test(unsigned long *mem)
{
	unsigned long *mask = section_masks;
	int i;

	for (; *mask; mask++) {
		mtspr(SPRN_LMSER, *mask);
		printf("Testing mask 0x%016lx\n", mfspr(SPRN_LMSER));

		for (i = 0; i < 64; i++) {
			lmr_count = 0;
			ldmx_full_section(mem, i);
			if (*mask & (1UL << (63 - i)))
				FAIL_IF(lmr_count != SECTION_LONGS);
			else
				FAIL_IF(lmr_count);
		}
	}

	return 0;
}

int ebb_lmr(void)
{
	int i;

	SKIP_IF(!lmr_is_supported());

	setup_ebb_handler(ebb_lmr_handler);

	ebb_global_enable();

	FAIL_IF(posix_memalign((void **)&test_mem, SIZE, SIZE) != 0);

	mtspr(SPRN_LMSER, 0);

	FAIL_IF(mfspr(SPRN_LMSER) != 0);

	mtspr(SPRN_LMRR, ((unsigned long)test_mem | LM_SIZE));

	FAIL_IF(mfspr(SPRN_LMRR) != ((unsigned long)test_mem | LM_SIZE));

	/* Read every single byte to ensure we get no false positives */
	for (i = 0; i < SECTIONS; i++)
		ldmx_full_section(test_mem, i);

	FAIL_IF(lmr_count != 0);

	/* Turn on the first section */

	mtspr(SPRN_LMSER, (1UL << 63));
	FAIL_IF(mfspr(SPRN_LMSER) != (1UL << 63));

	/* Enable LM (BESCR) */

	mtspr(SPRN_BESCR, mfspr(SPRN_BESCR) | BESCR_LME);
	FAIL_IF(!(mfspr(SPRN_BESCR) & BESCR_LME));

	ldmx((unsigned long)&test_mem);

	FAIL_IF(lmr_count != 1);	// exactly one exception
	FAIL_IF(mfspr(SPRN_BESCR) & BESCR_LME);	// LM now disabled
	FAIL_IF(!(mfspr(SPRN_BESCR) & BESCR_LMEO));	// occurred bit set

	printf("Simple LMR EBB OK\n");

	/* This shouldn't cause an EBB since it's been disabled */
	ldmx((unsigned long)&test_mem);
	FAIL_IF(lmr_count != 1);

	printf("LMR disable on EBB OK\n");

	ebb_lmr_reset();

	/* This should cause an EBB or reset is broken */
	ldmx((unsigned long)&test_mem);
	FAIL_IF(lmr_count != 2);

	printf("LMR reset EBB OK\n");

	ebb_lmr_reset();

	return ebb_lmr_section_test(test_mem);
}

int main(void)
{
	int ret = test_harness(ebb_lmr, "ebb_lmr");

	if (test_mem)
		free(test_mem);

	return ret;
}
