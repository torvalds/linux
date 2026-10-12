// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The root of an empty mount namespace is a nullfs mount. Its superblock is
 * kernel-internal and shared by every mount namespace. It can't be
 * reconfigured, neither through fspick() nor through mount(MS_REMOUNT) nor
 * through umount() of the root which remounts it read-only.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../utils.h"
#include "../wrappers.h"
#include "empty_mntns.h"
#include "kselftest_harness.h"

#ifndef __NR_fspick
#define __NR_fspick 433
#endif

static int sys_fspick(int dfd, const char *path, unsigned int flags)
{
	return syscall(__NR_fspick, dfd, path, flags);
}

/* Child exit codes. */
enum {
	CHILD_OK,
	CHILD_USERNS,		/* could not create the user namespace */
	CHILD_UNSHARE,		/* could not create the empty mount namespace */
	CHILD_FSPICK,		/* fspick() of the root was not refused with EINVAL */
	CHILD_REMOUNT,		/* mount(MS_REMOUNT) was not refused with EINVAL */
	CHILD_UMOUNT,		/* umount() of the root succeeded */
	CHILD_STATFS,		/* statfs() of the root failed */
	CHILD_RDONLY,		/* the root ended up read-only */
};

static int empty_mntns_child(void)
{
	struct statfs st;

	if (enter_userns())
		return CHILD_USERNS;
	if (unshare(UNSHARE_EMPTY_MNTNS))
		return CHILD_UNSHARE;

	if (sys_fspick(AT_FDCWD, "/", 0) >= 0 || errno != EINVAL)
		return CHILD_FSPICK;
	if (!mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) ||
	    errno != EINVAL)
		return CHILD_REMOUNT;
	if (!umount2("/", 0))
		return CHILD_UMOUNT;
	if (statfs("/", &st))
		return CHILD_STATFS;
	if (st.f_flags & ST_RDONLY)
		return CHILD_RDONLY;
	return CHILD_OK;
}

FIXTURE(internal_sb_reconfigure) {};

FIXTURE_SETUP(internal_sb_reconfigure)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		if (enter_userns())
			_exit(1);
		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(1);
		_exit(0);
	}
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		SKIP(return, "UNSHARE_EMPTY_MNTNS not supported");
}

FIXTURE_TEARDOWN(internal_sb_reconfigure) {}

TEST_F(internal_sb_reconfigure, nullfs_root)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(empty_mntns_child());
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), CHILD_OK);
}

TEST_HARNESS_MAIN
