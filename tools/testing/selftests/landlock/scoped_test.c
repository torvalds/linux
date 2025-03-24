// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Common scope restriction
 *
 * Copyright © 2024 Tahera Fahimi <fahimitahera@gmail.com>
 */

#define _GNU_SOURCE
#include <errno.h>
#include <linux/landlock.h>
#include <sys/prctl.h>

#include "common.h"

#define ACCESS_LAST LANDLOCK_SCOPE_SIGNAL

TEST(ruleset_with_unknown_scope)
{
	__u64 scoped_mask;

	for (scoped_mask = 1ULL << 63; scoped_mask != ACCESS_LAST;
	     scoped_mask >>= 1) {
		struct landlock_ruleset_attr ruleset_attr = {
			.scoped = scoped_mask,
		};

		ASSERT_EQ(-1, landlock_create_ruleset(&ruleset_attr,
						      sizeof(ruleset_attr), 0));
		ASSERT_EQ(EINVAL, errno);
	}
}

TEST_HARNESS_MAIN
