/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _TESTCASES_MMAP_TEST_H
#define _TESTCASES_MMAP_TEST_H
#include <sys/mman.h>
#include <sys/resource.h>
#include <stddef.h>
#include <strings.h>
#include "../../kselftest_harness.h"

#define TOP_DOWN 0
#define BOTTOM_UP 1

#define PROT (PROT_READ | PROT_WRITE)
#define FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)

static inline int memory_layout(void)
{
	void *value1 = mmap(NULL, sizeof(int), PROT, FLAGS, 0, 0);
	void *value2 = mmap(NULL, sizeof(int), PROT, FLAGS, 0, 0);

	return value2 > value1;
}
#endif /* _TESTCASES_MMAP_TEST_H */
