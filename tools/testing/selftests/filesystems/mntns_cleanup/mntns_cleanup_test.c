// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../kselftest_harness.h"

FIXTURE(mntns_cleanup) {
};

FIXTURE_SETUP(mntns_cleanup)
{
	if (geteuid() != 0)
		SKIP(return, "test requires CAP_SYS_ADMIN");

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	rmdir("/mnt_dir");
	ASSERT_EQ(mkdir("/mnt_dir", 0755), 0);
	ASSERT_EQ(mount("tmpfs", "/mnt_dir", "tmpfs", 0, NULL), 0);
	ASSERT_EQ(mkdir("/mnt_dir/hidden", 0755), 0);
	ASSERT_EQ(mkdir("/mnt_dir/hidden/secret", 0755), 0);
	ASSERT_EQ(mount("tmpfs", "/mnt_dir/hidden", "tmpfs", 0, NULL), 0);
}

FIXTURE_TEARDOWN(mntns_cleanup)
{
}

/* Mounts must stay connected when a mount namespace is cleaned up. */
TEST_F(mntns_cleanup, keeps_mounts_connected)
{
	int fd, sfd, err;

	fd = open("/mnt_dir", O_PATH | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	/* Destroy the namespace; the fd keeps /mnt_dir alive. */
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);

	sfd = openat(fd, "hidden/secret", O_RDONLY);
	err = errno;
	if (sfd >= 0)
		close(sfd);
	close(fd);

	ASSERT_LT(sfd, 0)
		TH_LOG("mount namespace teardown revealed what the overmount covered");
	ASSERT_EQ(err, ENOENT);
}

TEST_HARNESS_MAIN
