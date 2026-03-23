// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Tests for empty mount namespace creation via UNSHARE_EMPTY_MNTNS
 *
 * Copyright (c) 2024 Christian Brauner <brauner@kernel.org>
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../utils.h"
#include "../wrappers.h"
#include "empty_mntns.h"
#include "kselftest_harness.h"

static bool unshare_empty_mntns_supported(void)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return false;

	if (pid == 0) {
		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS) && errno == EINVAL)
			_exit(1);
		_exit(0);
	}

	if (waitpid(pid, &status, 0) != pid)
		return false;

	if (!WIFEXITED(status))
		return false;

	return WEXITSTATUS(status) == 0;
}


FIXTURE(empty_mntns) {};

FIXTURE_SETUP(empty_mntns)
{
	if (!unshare_empty_mntns_supported())
		SKIP(return, "UNSHARE_EMPTY_MNTNS not supported");
}

FIXTURE_TEARDOWN(empty_mntns) {}

/* Verify unshare succeeds, produces exactly 1 mount, and root == cwd */
TEST_F(empty_mntns, basic)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t root_id, cwd_id;

		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(2);

		if (count_mounts() != 1)
			_exit(3);

		root_id = get_unique_mnt_id("/");
		cwd_id = get_unique_mnt_id(".");
		if (root_id == 0 || cwd_id == 0)
			_exit(4);

		if (root_id != cwd_id)
			_exit(5);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * UNSHARE_EMPTY_MNTNS combined with CLONE_NEWUSER.
 *
 * The user namespace must be created first so /proc is still accessible
 * for writing uid_map/gid_map.  The empty mount namespace is created
 * afterwards.
 */
TEST_F(empty_mntns, with_clone_newuser)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uid_t uid = getuid();
		gid_t gid = getgid();
		char map[100];

		if (unshare(CLONE_NEWUSER))
			_exit(1);

		snprintf(map, sizeof(map), "0 %d 1", uid);
		if (write_file("/proc/self/uid_map", map))
			_exit(2);

		if (write_file("/proc/self/setgroups", "deny"))
			_exit(3);

		snprintf(map, sizeof(map), "0 %d 1", gid);
		if (write_file("/proc/self/gid_map", map))
			_exit(4);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(5);

		if (count_mounts() != 1)
			_exit(6);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* UNSHARE_EMPTY_MNTNS combined with other namespace flags */
TEST_F(empty_mntns, with_other_ns_flags)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS | CLONE_NEWUTS | CLONE_NEWIPC))
			_exit(2);

		if (count_mounts() != 1)
			_exit(3);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* EPERM without proper capabilities */
TEST_F(empty_mntns, eperm_without_caps)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Skip if already root */
		if (getuid() == 0)
			_exit(0);

		if (unshare(UNSHARE_EMPTY_MNTNS) == 0)
			_exit(1);

		if (errno != EPERM)
			_exit(2);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* Many source mounts still result in exactly 1 mount */
TEST_F(empty_mntns, many_source_mounts)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		char tmpdir[] = "/tmp/empty_mntns_test.XXXXXX";
		int i;

		if (enter_userns())
			_exit(1);

		if (unshare(CLONE_NEWNS))
			_exit(2);

		if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
			_exit(3);

		if (!mkdtemp(tmpdir))
			_exit(4);

		if (mount("tmpfs", tmpdir, "tmpfs", 0, "size=1M"))
			_exit(5);

		for (i = 0; i < 5; i++) {
			char subdir[256];

			snprintf(subdir, sizeof(subdir), "%s/sub%d", tmpdir, i);
			if (mkdir(subdir, 0755) && errno != EEXIST)
				_exit(6);
			if (mount(subdir, subdir, NULL, MS_BIND, NULL))
				_exit(7);
		}

		if (count_mounts() < 5)
			_exit(8);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(9);

		if (count_mounts() != 1)
			_exit(10);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* CWD on a different mount gets reset to root */
TEST_F(empty_mntns, cwd_reset)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		char tmpdir[] = "/tmp/empty_mntns_cwd.XXXXXX";
		uint64_t root_id, cwd_id;
		struct statmount *sm;

		if (enter_userns())
			_exit(1);

		if (unshare(CLONE_NEWNS))
			_exit(2);

		if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
			_exit(3);

		if (!mkdtemp(tmpdir))
			_exit(4);

		if (mount("tmpfs", tmpdir, "tmpfs", 0, "size=1M"))
			_exit(5);

		if (chdir(tmpdir))
			_exit(6);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(7);

		root_id = get_unique_mnt_id("/");
		cwd_id = get_unique_mnt_id(".");
		if (root_id == 0 || cwd_id == 0)
			_exit(8);

		if (root_id != cwd_id)
			_exit(9);

		sm = statmount_alloc(root_id, 0, STATMOUNT_MNT_ROOT | STATMOUNT_MNT_POINT, 0);
		if (!sm)
			_exit(10);

		if (strcmp(sm->str + sm->mnt_point, "/") != 0)
			_exit(11);

		free(sm);
		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* Verify statmount properties of the root mount */
TEST_F(empty_mntns, mount_properties)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct statmount *sm;
		uint64_t root_id;

		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(2);

		root_id = get_unique_mnt_id("/");
		if (!root_id)
			_exit(3);

		sm = statmount_alloc(root_id, 0, STATMOUNT_MNT_BASIC | STATMOUNT_MNT_ROOT |
				     STATMOUNT_MNT_POINT | STATMOUNT_FS_TYPE, 0);
		if (!sm)
			_exit(4);

		if (!(sm->mask & STATMOUNT_MNT_POINT))
			_exit(5);

		if (strcmp(sm->str + sm->mnt_point, "/") != 0)
			_exit(6);

		if (!(sm->mask & STATMOUNT_MNT_BASIC))
			_exit(7);

		if (sm->mnt_id != root_id)
			_exit(8);

		free(sm);
		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* Consecutive UNSHARE_EMPTY_MNTNS calls produce new namespaces */
TEST_F(empty_mntns, repeated_unshare)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t first_root_id, second_root_id;

		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(2);

		if (count_mounts() != 1)
			_exit(3);

		first_root_id = get_unique_mnt_id("/");

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(4);

		if (count_mounts() != 1)
			_exit(5);

		second_root_id = get_unique_mnt_id("/");

		if (first_root_id == second_root_id)
			_exit(6);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* Root mount's parent is itself */
TEST_F(empty_mntns, root_is_own_parent)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct statmount sm;
		uint64_t root_id;

		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(2);

		root_id = get_unique_mnt_id("/");
		if (!root_id)
			_exit(3);

		if (statmount(root_id, 0, 0, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0) < 0)
			_exit(4);

		if (!(sm.mask & STATMOUNT_MNT_BASIC))
			_exit(5);

		if (sm.mnt_parent_id != sm.mnt_id)
			_exit(6);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* Listmount returns only the root mount */
TEST_F(empty_mntns, listmount_single_entry)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t list[16];
		ssize_t nr_mounts;
		uint64_t root_id;

		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(2);

		nr_mounts = listmount(LSMT_ROOT, 0, 0, list, 16, 0);
		if (nr_mounts != 1)
			_exit(3);

		root_id = get_unique_mnt_id("/");
		if (!root_id)
			_exit(4);

		if (list[0] != root_id)
			_exit(5);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Mount tmpfs over nullfs root to build a writable filesystem from scratch.
 * This exercises the intended usage pattern: create an empty mount namespace
 * (which has a nullfs root), then mount a real filesystem over it.
 *
 * Because resolving "/" returns the process root directly (via nd_jump_root)
 * without following overmounts, we use the new mount API (fsopen/fsmount)
 * to obtain a mount fd, then fchdir + chroot to enter the new filesystem.
 */
TEST_F(empty_mntns, overmount_tmpfs)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct statmount *sm;
		uint64_t root_id, cwd_id;
		int fd, fsfd, mntfd;

		if (enter_userns())
			_exit(1);

		if (unshare(UNSHARE_EMPTY_MNTNS))
			_exit(2);

		if (count_mounts() != 1)
			_exit(3);

		root_id = get_unique_mnt_id("/");
		if (!root_id)
			_exit(4);

		/* Verify root is nullfs */
		sm = statmount_alloc(root_id, 0, STATMOUNT_FS_TYPE, 0);
		if (!sm)
			_exit(5);

		if (!(sm->mask & STATMOUNT_FS_TYPE))
			_exit(6);

		if (strcmp(sm->str + sm->fs_type, "nullfs") != 0)
			_exit(7);

		free(sm);

		cwd_id = get_unique_mnt_id(".");
		if (!cwd_id || root_id != cwd_id)
			_exit(8);

		/*
		 * nullfs root is immutable.  open(O_CREAT) returns ENOENT
		 * because empty_dir_lookup() returns -ENOENT before the
		 * IS_IMMUTABLE permission check in may_o_create() is reached.
		 */
		fd = open("/test", O_CREAT | O_RDWR, 0644);
		if (fd >= 0) {
			close(fd);
			_exit(9);
		}
		if (errno != ENOENT)
			_exit(10);

		/*
		 * Use the new mount API to create tmpfs and get a mount fd.
		 * We need the fd because after attaching the tmpfs on top of
		 * "/", path resolution of "/" still returns the process root
		 * (nullfs) without following the overmount.  The mount fd
		 * lets us fchdir + chroot into the tmpfs.
		 */
		fsfd = sys_fsopen("tmpfs", 0);
		if (fsfd < 0)
			_exit(11);

		if (sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "size", "1M", 0)) {
			close(fsfd);
			_exit(12);
		}

		if (sys_fsconfig(fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0)) {
			close(fsfd);
			_exit(13);
		}

		mntfd = sys_fsmount(fsfd, 0, 0);
		close(fsfd);
		if (mntfd < 0)
			_exit(14);

		if (sys_move_mount(mntfd, "", AT_FDCWD, "/",
				   MOVE_MOUNT_F_EMPTY_PATH)) {
			close(mntfd);
			_exit(15);
		}

		if (count_mounts() != 2) {
			close(mntfd);
			_exit(16);
		}

		/* Enter the tmpfs via the mount fd */
		if (fchdir(mntfd)) {
			close(mntfd);
			_exit(17);
		}

		if (chroot(".")) {
			close(mntfd);
			_exit(18);
		}

		close(mntfd);

		/* Verify "/" now resolves to tmpfs */
		root_id = get_unique_mnt_id("/");
		if (!root_id)
			_exit(19);

		sm = statmount_alloc(root_id, 0, STATMOUNT_FS_TYPE, 0);
		if (!sm)
			_exit(20);

		if (!(sm->mask & STATMOUNT_FS_TYPE))
			_exit(21);

		if (strcmp(sm->str + sm->fs_type, "tmpfs") != 0)
			_exit(22);

		free(sm);

		/* Verify tmpfs is writable */
		fd = open("/testfile", O_CREAT | O_RDWR, 0644);
		if (fd < 0)
			_exit(23);

		if (write(fd, "test", 4) != 4) {
			close(fd);
			_exit(24);
		}

		close(fd);

		if (access("/testfile", F_OK))
			_exit(25);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Tests below do not require UNSHARE_EMPTY_MNTNS support.
 */

/* Invalid unshare flags return EINVAL */
TEST(invalid_flags)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		if (enter_userns())
			_exit(1);

		if (unshare(0x80000000) == 0)
			_exit(2);

		if (errno != EINVAL)
			_exit(3);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* Regular CLONE_NEWNS still copies the full mount tree */
TEST(clone_newns_full_copy)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ssize_t nr_mounts_before, nr_mounts_after;
		char tmpdir[] = "/tmp/empty_mntns_regr.XXXXXX";
		int i;

		if (enter_userns())
			_exit(1);

		if (unshare(CLONE_NEWNS))
			_exit(2);

		if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
			_exit(3);

		if (!mkdtemp(tmpdir))
			_exit(4);

		if (mount("tmpfs", tmpdir, "tmpfs", 0, "size=1M"))
			_exit(5);

		for (i = 0; i < 3; i++) {
			char subdir[256];

			snprintf(subdir, sizeof(subdir), "%s/sub%d", tmpdir, i);
			if (mkdir(subdir, 0755) && errno != EEXIST)
				_exit(6);
			if (mount(subdir, subdir, NULL, MS_BIND, NULL))
				_exit(7);
		}

		nr_mounts_before = count_mounts();
		if (nr_mounts_before < 3)
			_exit(8);

		if (unshare(CLONE_NEWNS))
			_exit(9);

		nr_mounts_after = count_mounts();
		if (nr_mounts_after < nr_mounts_before)
			_exit(10);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/* Other namespace unshares are unaffected */
TEST(other_ns_unaffected)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		char hostname[256];

		if (enter_userns())
			_exit(1);

		if (unshare(CLONE_NEWUTS))
			_exit(2);

		if (sethostname("test-empty-mntns", 16))
			_exit(3);

		if (gethostname(hostname, sizeof(hostname)))
			_exit(4);

		if (strcmp(hostname, "test-empty-mntns") != 0)
			_exit(5);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

TEST_HARNESS_MAIN
