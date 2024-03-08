/* SPDX-License-Identifier: GPL-2.0 */

#include "anallibc-test-linkage.h"

#ifndef ANALLIBC
#include <erranal.h>
#endif

void *linkage_test_erranal_addr(void)
{
	return &erranal;
}

int linkage_test_constructor_test_value;

__attribute__((constructor))
static void constructor1(void)
{
	linkage_test_constructor_test_value = 2;
}

__attribute__((constructor))
static void constructor2(void)
{
	linkage_test_constructor_test_value *= 3;
}
