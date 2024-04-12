// SPDX-License-Identifier: GPL-2.0
/*
 * Linux Security Module infrastructure tests
 * Tests for the lsm_set_self_attr system call
 *
 * Copyright © 2022 Casey Schaufler <casey@schaufler-ca.com>
 */

#define _GNU_SOURCE
#include <linux/lsm.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "../kselftest_harness.h"
#include "common.h"

TEST(ctx_null_lsm_set_self_attr)
{
	ASSERT_EQ(-1, lsm_set_self_attr(LSM_ATTR_CURRENT, NULL,
					sizeof(struct lsm_ctx), 0));
}

TEST(size_too_small_lsm_set_self_attr)
{
	const long page_size = sysconf(_SC_PAGESIZE);
	struct lsm_ctx *ctx = calloc(page_size, 1);
	__u32 size = page_size;

	ASSERT_NE(NULL, ctx);
	if (attr_lsm_count()) {
		ASSERT_LE(1, lsm_get_self_attr(LSM_ATTR_CURRENT, ctx, &size,
					       0));
	}
	ASSERT_EQ(-1, lsm_set_self_attr(LSM_ATTR_CURRENT, ctx, 1, 0));

	free(ctx);
}

TEST(flags_zero_lsm_set_self_attr)
{
	const long page_size = sysconf(_SC_PAGESIZE);
	struct lsm_ctx *ctx = calloc(page_size, 1);
	__u32 size = page_size;

	ASSERT_NE(NULL, ctx);
	if (attr_lsm_count()) {
		ASSERT_LE(1, lsm_get_self_attr(LSM_ATTR_CURRENT, ctx, &size,
					       0));
	}
	ASSERT_EQ(-1, lsm_set_self_attr(LSM_ATTR_CURRENT, ctx, size, 1));

	free(ctx);
}

TEST(flags_overset_lsm_set_self_attr)
{
	const long page_size = sysconf(_SC_PAGESIZE);
	char *ctx = calloc(page_size, 1);
	__u32 size = page_size;
	struct lsm_ctx *tctx = (struct lsm_ctx *)ctx;

	ASSERT_NE(NULL, ctx);
	if (attr_lsm_count()) {
		ASSERT_LE(1, lsm_get_self_attr(LSM_ATTR_CURRENT, tctx, &size,
					       0));
	}
	ASSERT_EQ(-1, lsm_set_self_attr(LSM_ATTR_CURRENT | LSM_ATTR_PREV, tctx,
					size, 0));

	free(ctx);
}

TEST_HARNESS_MAIN
