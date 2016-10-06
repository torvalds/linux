/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#ifndef _SELFTESTS_POWERPC_UTILS_H
#define _SELFTESTS_POWERPC_UTILS_H

#define __cacheline_aligned __attribute__((aligned(128)))

#include <stdint.h>
#include <stdbool.h>
#include <linux/auxvec.h>
#include "reg.h"

/* Avoid headaches with PRI?64 - just use %ll? always */
typedef unsigned long long u64;
typedef   signed long long s64;

/* Just for familiarity */
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;


int test_harness(int (test_function)(void), char *name);
extern void *get_auxv_entry(int type);
int pick_online_cpu(void);

static inline bool have_hwcap(unsigned long ftr)
{
	return ((unsigned long)get_auxv_entry(AT_HWCAP) & ftr) == ftr;
}

static inline bool have_hwcap2(unsigned long ftr2)
{
	return ((unsigned long)get_auxv_entry(AT_HWCAP2) & ftr2) == ftr2;
}

/* Yes, this is evil */
#define FAIL_IF(x)						\
do {								\
	if ((x)) {						\
		fprintf(stderr,					\
		"[FAIL] Test FAILED on line %d\n", __LINE__);	\
		return 1;					\
	}							\
} while (0)

/* The test harness uses this, yes it's gross */
#define MAGIC_SKIP_RETURN_VALUE	99

#define SKIP_IF(x)						\
do {								\
	if ((x)) {						\
		fprintf(stderr,					\
		"[SKIP] Test skipped on line %d\n", __LINE__);	\
		return MAGIC_SKIP_RETURN_VALUE;			\
	}							\
} while (0)

#define _str(s) #s
#define str(s) _str(s)

/* POWER9 feature */
#ifndef PPC_FEATURE2_ARCH_3_00
#define PPC_FEATURE2_ARCH_3_00 0x00800000
#endif

#endif /* _SELFTESTS_POWERPC_UTILS_H */
