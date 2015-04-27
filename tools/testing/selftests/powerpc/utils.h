/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#ifndef _SELFTESTS_POWERPC_UTILS_H
#define _SELFTESTS_POWERPC_UTILS_H

#include <stdint.h>
#include <stdbool.h>

/* Avoid headaches with PRI?64 - just use %ll? always */
typedef unsigned long long u64;
typedef   signed long long s64;

/* Just for familiarity */
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;


int test_harness(int (test_function)(void), char *name);
extern void *get_auxv_entry(int type);

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

#endif /* _SELFTESTS_POWERPC_UTILS_H */
