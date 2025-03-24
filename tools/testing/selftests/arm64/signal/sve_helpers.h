/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 ARM Limited
 *
 * Common helper functions for SVE and SME functionality.
 */

#ifndef __SVE_HELPERS_H__
#define __SVE_HELPERS_H__

#include <stdbool.h>

#define VLS_USE_SVE	false
#define VLS_USE_SME	true

extern unsigned int vls[];
extern unsigned int nvls;

int sve_fill_vls(bool use_sme, int min_vls);

static inline uint64_t get_svcr(void)
{
	uint64_t val;

	asm volatile (
		"mrs	%0, S3_3_C4_C2_2\n"
		: "=r"(val)
		:
		: "cc");

	return val;
}

#endif
