// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>
#include <linux/compiler.h>

#include "debug.h"
#include "tests/tests.h"
#include "util/find-map.c"

#define VECTORS__MAP_NAME "[vectors]"

static int test__vectors_page(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	void *start, *end;

	if (find_map(&start, &end, VECTORS__MAP_NAME)) {
		pr_err("%s not found, is CONFIG_KUSER_HELPERS enabled?\n",
		       VECTORS__MAP_NAME);
		return TEST_FAIL;
	}

	return TEST_OK;
}

DEFINE_SUITE("Vectors page", vectors_page);
