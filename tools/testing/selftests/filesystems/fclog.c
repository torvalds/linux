// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2025 SUSE LLC.
 */

#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>

#include "../kselftest_harness.h"

#define ASSERT_ERRNO(expected, _t, seen)				\
	__EXPECT(expected, #expected,					\
		({__typeof__(seen) _tmp_seen = (seen);			\
		  _tmp_seen >= 0 ? _tmp_seen : -errno; }), #seen, _t, 1)

#define ASSERT_ERRNO_EQ(expected, seen) \
	ASSERT_ERRNO(expected, ==, seen)

#define ASSERT_SUCCESS(seen) \
	ASSERT_ERRNO(0, <=, seen)

FIXTURE(ns)
{
	int host_mntns;
};

FIXTURE_SETUP(ns)
{
	/* Stash the old mntns. */
	self->host_mntns = open("/proc/self/ns/mnt", O_RDONLY|O_CLOEXEC);
	ASSERT_SUCCESS(self->host_mntns);

	/* Create a new mount namespace and make it private. */
	ASSERT_SUCCESS(unshare(CLONE_NEWNS));
	ASSERT_SUCCESS(mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL));
}

FIXTURE_TEARDOWN(ns)
{
	ASSERT_SUCCESS(setns(self->host_mntns, CLONE_NEWNS));
	ASSERT_SUCCESS(close(self->host_mntns));
}

TEST_F(ns, fscontext_log_enodata)
{
	int fsfd = fsopen("tmpfs", FSOPEN_CLOEXEC);
	ASSERT_SUCCESS(fsfd);

	/* A brand new fscontext has no log entries. */
	char buf[128] = {};
	for (int i = 0; i < 16; i++)
		ASSERT_ERRNO_EQ(-ENODATA, read(fsfd, buf, sizeof(buf)));

	ASSERT_SUCCESS(close(fsfd));
}

TEST_F(ns, fscontext_log_errorfc)
{
	int fsfd = fsopen("tmpfs", FSOPEN_CLOEXEC);
	ASSERT_SUCCESS(fsfd);

	ASSERT_ERRNO_EQ(-EINVAL, fsconfig(fsfd, FSCONFIG_SET_STRING, "invalid-arg", "123", 0));

	char buf[128] = {};
	ASSERT_SUCCESS(read(fsfd, buf, sizeof(buf)));
	EXPECT_STREQ("e tmpfs: Unknown parameter 'invalid-arg'\n", buf);

	/* The message has been consumed. */
	ASSERT_ERRNO_EQ(-ENODATA, read(fsfd, buf, sizeof(buf)));
	ASSERT_SUCCESS(close(fsfd));
}

TEST_F(ns, fscontext_log_errorfc_after_fsmount)
{
	int fsfd = fsopen("tmpfs", FSOPEN_CLOEXEC);
	ASSERT_SUCCESS(fsfd);

	ASSERT_ERRNO_EQ(-EINVAL, fsconfig(fsfd, FSCONFIG_SET_STRING, "invalid-arg", "123", 0));

	ASSERT_SUCCESS(fsconfig(fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0));
	int mfd = fsmount(fsfd, FSMOUNT_CLOEXEC, MOUNT_ATTR_NOEXEC | MOUNT_ATTR_NOSUID);
	ASSERT_SUCCESS(mfd);
	ASSERT_SUCCESS(move_mount(mfd, "", AT_FDCWD, "/tmp", MOVE_MOUNT_F_EMPTY_PATH));

	/*
	 * The fscontext log should still contain data even after
	 * FSCONFIG_CMD_CREATE and fsmount().
	 */
	char buf[128] = {};
	ASSERT_SUCCESS(read(fsfd, buf, sizeof(buf)));
	EXPECT_STREQ("e tmpfs: Unknown parameter 'invalid-arg'\n", buf);

	/* The message has been consumed. */
	ASSERT_ERRNO_EQ(-ENODATA, read(fsfd, buf, sizeof(buf)));
	ASSERT_SUCCESS(close(fsfd));
}

TEST_F(ns, fscontext_log_emsgsize)
{
	int fsfd = fsopen("tmpfs", FSOPEN_CLOEXEC);
	ASSERT_SUCCESS(fsfd);

	ASSERT_ERRNO_EQ(-EINVAL, fsconfig(fsfd, FSCONFIG_SET_STRING, "invalid-arg", "123", 0));

	char buf[128] = {};
	/*
	 * Attempting to read a message with too small a buffer should not
	 * result in the message getting consumed.
	 */
	ASSERT_ERRNO_EQ(-EMSGSIZE, read(fsfd, buf, 0));
	ASSERT_ERRNO_EQ(-EMSGSIZE, read(fsfd, buf, 1));
	for (int i = 0; i < 16; i++)
		ASSERT_ERRNO_EQ(-EMSGSIZE, read(fsfd, buf, 16));

	ASSERT_SUCCESS(read(fsfd, buf, sizeof(buf)));
	EXPECT_STREQ("e tmpfs: Unknown parameter 'invalid-arg'\n", buf);

	/* The message has been consumed. */
	ASSERT_ERRNO_EQ(-ENODATA, read(fsfd, buf, sizeof(buf)));
	ASSERT_SUCCESS(close(fsfd));
}

TEST_HARNESS_MAIN
