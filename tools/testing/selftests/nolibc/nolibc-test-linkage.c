/* SPDX-License-Identifier: GPL-2.0 */

#include "nolibc-test-linkage.h"

#include <errno.h>

void *linkage_test_errno_addr(void)
{
	return &errno;
}

int linkage_test_constructor_test_value = 0;

__attribute__((constructor))
static void constructor1(void)
{
	linkage_test_constructor_test_value |= 1 << 0;
}

__attribute__((constructor))
static void constructor2(void)
{
	linkage_test_constructor_test_value |= 1 << 1;
}
