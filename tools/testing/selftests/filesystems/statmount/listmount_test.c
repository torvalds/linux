// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Christian Brauner <brauner@kernel.org>

#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>

#include "statmount.h"
#include "../../kselftest_harness.h"

#ifndef LISTMOUNT_REVERSE
#define LISTMOUNT_REVERSE    (1 << 0) /* List later mounts first */
#endif

#define LISTMNT_BUFFER 10

/* Check that all mount ids are in increasing order. */
TEST(listmount_forward)
{
	uint64_t list[LISTMNT_BUFFER], last_mnt_id = 0;

	for (;;) {
		ssize_t nr_mounts;

		nr_mounts = listmount(LSMT_ROOT, 0, last_mnt_id,
				      list, LISTMNT_BUFFER, 0);
		ASSERT_GE(nr_mounts, 0);
		if (nr_mounts == 0)
			break;

		for (size_t cur = 0; cur < nr_mounts; cur++) {
			if (cur < nr_mounts - 1)
				ASSERT_LT(list[cur], list[cur + 1]);
			last_mnt_id = list[cur];
		}
	}
}

/* Check that all mount ids are in decreasing order. */
TEST(listmount_backward)
{
	uint64_t list[LISTMNT_BUFFER], last_mnt_id = 0;

	for (;;) {
		ssize_t nr_mounts;

		nr_mounts = listmount(LSMT_ROOT, 0, last_mnt_id,
				      list, LISTMNT_BUFFER, LISTMOUNT_REVERSE);
		ASSERT_GE(nr_mounts, 0);
		if (nr_mounts == 0)
			break;

		for (size_t cur = 0; cur < nr_mounts; cur++) {
			if (cur < nr_mounts - 1)
				ASSERT_GT(list[cur], list[cur + 1]);
			last_mnt_id = list[cur];
		}
	}
}

TEST_HARNESS_MAIN
