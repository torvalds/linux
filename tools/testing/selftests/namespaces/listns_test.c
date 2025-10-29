// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/nsfs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"
#include "wrappers.h"

/*
 * Test basic listns() functionality with the unified namespace tree.
 * List all active namespaces globally.
 */
TEST(listns_basic_unified)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,  /* All types */
		.spare2 = 0,
		.user_ns_id = 0,  /* Global listing */
	};
	__u64 ns_ids[100];
	ssize_t ret;

	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		ASSERT_TRUE(false);
	}

	/* Should find at least the initial namespaces */
	ASSERT_GT(ret, 0);
	TH_LOG("Found %zd active namespaces", ret);

	/* Verify all returned IDs are non-zero */
	for (ssize_t i = 0; i < ret; i++) {
		ASSERT_NE(ns_ids[i], 0);
		TH_LOG("  [%zd] ns_id: %llu", i, (unsigned long long)ns_ids[i]);
	}
}

TEST_HARNESS_MAIN
