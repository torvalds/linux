// SPDX-License-Identifier: GPL-2.0
/*
 * Reading the root of an empty mount namespace holds nothing that anybody
 * else waits for: the directory never has an entry, so a readdir stuck in
 * the page fault of its buffer stalls neither a create nor a lookup in it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../kselftest_harness.h"
#include "../readdir_hold.h"
#include "empty_mntns.h"

TEST(readdir_blocks_nobody)
{
	struct readdir_hold hold;
	bool stalled;
	int dfd;

	if (geteuid())
		SKIP(return, "test requires root");
	if (readdir_hold_init(&hold))
		SKIP(return, "test requires userfaultfd");
	if (unshare(UNSHARE_EMPTY_MNTNS)) {
		readdir_hold_destroy(&hold);
		if (errno == EINVAL)
			SKIP(return, "UNSHARE_EMPTY_MNTNS not supported");
		ASSERT_TRUE(false)
			TH_LOG("unshare(UNSHARE_EMPTY_MNTNS): %m");
	}
	dfd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(dfd, 0);
	ASSERT_EQ(readdir_hold_check(&hold, dfd, &stalled), 0);
	/* the lookup came back while the readdir was stuck in its fault */
	EXPECT_FALSE(stalled);
	close(dfd);
	readdir_hold_destroy(&hold);
}

TEST_HARNESS_MAIN
