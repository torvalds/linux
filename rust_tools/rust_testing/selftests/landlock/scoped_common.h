/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock scope test helpers
 *
 * Copyright © 2024 Tahera Fahimi <fahimitahera@gmail.com>
 */

#define _GNU_SOURCE

#include <sys/types.h>

static void create_scoped_domain(struct __test_metadata *const _metadata,
				 const __u16 scope)
{
	int ruleset_fd;
	const struct landlock_ruleset_attr ruleset_attr = {
		.scoped = scope,
	};

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd)
	{
		TH_LOG("Failed to create a ruleset: %s", strerror(errno));
	}
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));
}
