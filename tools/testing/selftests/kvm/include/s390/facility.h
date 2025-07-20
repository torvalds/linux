/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright IBM Corp. 2024
 *
 * Authors:
 *  Hariharan Mari <hari55@linux.ibm.com>
 *
 * Get the facility bits with the STFLE instruction
 */

#ifndef SELFTEST_KVM_FACILITY_H
#define SELFTEST_KVM_FACILITY_H

#include <linux/bitops.h>

/* alt_stfle_fac_list[16] + stfle_fac_list[16] */
#define NB_STFL_DOUBLEWORDS 32

extern uint64_t stfl_doublewords[NB_STFL_DOUBLEWORDS];
extern bool stfle_flag;

static inline bool test_bit_inv(unsigned long nr, const unsigned long *ptr)
{
	return test_bit(nr ^ (BITS_PER_LONG - 1), ptr);
}

static inline void stfle(uint64_t *fac, unsigned int nb_doublewords)
{
	register unsigned long r0 asm("0") = nb_doublewords - 1;

	asm volatile("	.insn	s,0xb2b00000,0(%1)\n"
			: "+d" (r0)
			: "a" (fac)
			: "memory", "cc");
}

static inline void setup_facilities(void)
{
	stfle(stfl_doublewords, NB_STFL_DOUBLEWORDS);
	stfle_flag = true;
}

static inline bool test_facility(int nr)
{
	if (!stfle_flag)
		setup_facilities();
	return test_bit_inv(nr, stfl_doublewords);
}

#endif
