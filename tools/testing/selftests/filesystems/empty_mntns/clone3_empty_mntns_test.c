// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Tests for empty mount namespace creation via clone3() CLONE_EMPTY_MNTNS
 *
 * These tests exercise the clone3() code path for creating empty mount
 * namespaces, which is distinct from the unshare() path tested in
 * empty_mntns_test.c.  With clone3(), CLONE_EMPTY_MNTNS (0x2000000000ULL)
 * is a 64-bit flag that implies CLONE_NEWNS.  The implication happens in
 * kernel_clone() before copy_process(), unlike unshare() where it goes
 * through UNSHARE_EMPTY_MNTNS -> CLONE_EMPTY_MNTNS conversion in
 * unshare_nsproxy_namespaces().
 *
 * Copyright (c) 2024 Christian Brauner <brauner@kernel.org>
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../utils.h"
#include "../wrappers.h"
#include "clone3/clone3_selftests.h"
#include "empty_mntns.h"
#include "kselftest_harness.h"

static pid_t clone3_empty_mntns(uint64_t extra_flags)
{
	struct __clone_args args = {
		.flags		= CLONE_EMPTY_MNTNS | extra_flags,
		.exit_signal	= SIGCHLD,
	};

	return sys_clone3(&args, sizeof(args));
}

static bool clone3_empty_mntns_supported(void)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return false;

	if (pid == 0) {
		if (enter_userns())
			_exit(1);

		pid = clone3_empty_mntns(0);
		if (pid < 0)
			_exit(1);

		if (pid == 0)
			_exit(0);

		_exit(wait_for_pid(pid) != 0);
	}

	if (waitpid(pid, &status, 0) != pid)
		return false;

	if (!WIFEXITED(status))
		return false;

	return WEXITSTATUS(status) == 0;
}

FIXTURE(clone3_empty_mntns) {};

FIXTURE_SETUP(clone3_empty_mntns)
{
	if (!clone3_empty_mntns_supported())
		SKIP(return, "CLONE_EMPTY_MNTNS via clone3 not supported");
}

FIXTURE_TEARDOWN(clone3_empty_mntns) {}

/*
 * Basic clone3() with CLONE_EMPTY_MNTNS: child gets empty mount namespace
 * with exactly 1 mount and root == cwd.
 */
TEST_F(clone3_empty_mntns, basic)
{
	pid_t pid, inner;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		if (enter_userns())
			_exit(1);

		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(2);

		if (inner == 0) {
			uint64_t root_id, cwd_id;

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

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * CLONE_EMPTY_MNTNS implies CLONE_NEWNS.  Verify that it works without
 * explicitly setting CLONE_NEWNS (tests fork.c:2627-2630).
 */
TEST_F(clone3_empty_mntns, implies_newns)
{
	pid_t pid, inner;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ssize_t parent_mounts;

		if (enter_userns())
			_exit(1);

		/* Verify we have mounts in our current namespace. */
		parent_mounts = count_mounts();
		if (parent_mounts < 1)
			_exit(2);

		/* Only CLONE_EMPTY_MNTNS, no explicit CLONE_NEWNS. */
		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(3);

		if (inner == 0) {
			if (count_mounts() != 1)
				_exit(4);

			_exit(0);
		}

		/* Parent still has its mounts. */
		if (count_mounts() != parent_mounts)
			_exit(5);

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Helper macro: generate a test that clones with CLONE_EMPTY_MNTNS |
 * @extra_flags and verifies the child has exactly one mount.
 */
#define TEST_CLONE3_FLAGS(test_name, extra_flags)			\
TEST_F(clone3_empty_mntns, test_name)					\
{									\
	pid_t pid, inner;						\
									\
	pid = fork();							\
	ASSERT_GE(pid, 0);						\
									\
	if (pid == 0) {							\
		if (enter_userns())					\
			_exit(1);					\
									\
		inner = clone3_empty_mntns(extra_flags);		\
		if (inner < 0)						\
			_exit(2);					\
									\
		if (inner == 0) {					\
			if (count_mounts() != 1)			\
				_exit(3);				\
			_exit(0);					\
		}							\
									\
		_exit(wait_for_pid(inner));				\
	}								\
									\
	ASSERT_EQ(wait_for_pid(pid), 0);				\
}

/* Redundant CLONE_NEWNS | CLONE_EMPTY_MNTNS should succeed. */
TEST_CLONE3_FLAGS(with_explicit_newns, CLONE_NEWNS)

/* CLONE_EMPTY_MNTNS combined with CLONE_NEWUSER. */
TEST_CLONE3_FLAGS(with_newuser, CLONE_NEWUSER)

/* CLONE_EMPTY_MNTNS combined with other namespace flags. */
TEST_CLONE3_FLAGS(with_other_ns_flags, CLONE_NEWUTS | CLONE_NEWIPC)

/*
 * CLONE_EMPTY_MNTNS combined with CLONE_NEWPID.
 */
TEST_F(clone3_empty_mntns, with_newpid)
{
	pid_t pid, inner;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		if (enter_userns())
			_exit(1);

		inner = clone3_empty_mntns(CLONE_NEWPID);
		if (inner < 0)
			_exit(2);

		if (inner == 0) {
			if (count_mounts() != 1)
				_exit(3);

			/* In a new PID namespace, getpid() returns 1. */
			if (getpid() != 1)
				_exit(4);

			_exit(0);
		}

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * CLONE_EMPTY_MNTNS | CLONE_FS must fail because the implied CLONE_NEWNS
 * and CLONE_FS are mutually exclusive (fork.c:1981).
 */
TEST_F(clone3_empty_mntns, with_clone_fs_fails)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct __clone_args args = {
			.flags		= CLONE_EMPTY_MNTNS | CLONE_FS,
			.exit_signal	= SIGCHLD,
		};
		pid_t ret;

		if (enter_userns())
			_exit(1);

		ret = sys_clone3(&args, sizeof(args));
		if (ret >= 0) {
			if (ret == 0)
				_exit(0);
			wait_for_pid(ret);
			_exit(2);
		}

		if (errno != EINVAL)
			_exit(3);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * CLONE_EMPTY_MNTNS combined with CLONE_PIDFD returns a valid pidfd.
 */
TEST_F(clone3_empty_mntns, with_pidfd)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct __clone_args args = {
			.flags		= CLONE_EMPTY_MNTNS | CLONE_PIDFD,
			.exit_signal	= SIGCHLD,
		};
		int pidfd = -1;
		pid_t inner;

		if (enter_userns())
			_exit(1);

		args.pidfd = (uintptr_t)&pidfd;

		inner = sys_clone3(&args, sizeof(args));
		if (inner < 0)
			_exit(2);

		if (inner == 0) {
			if (count_mounts() != 1)
				_exit(3);

			_exit(0);
		}

		/* Verify we got a valid pidfd. */
		if (pidfd < 0)
			_exit(4);

		close(pidfd);
		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * clone3 without CAP_SYS_ADMIN must fail with EPERM.
 */
TEST_F(clone3_empty_mntns, eperm_without_caps)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pid_t ret;

		/* Skip if already root. */
		if (getuid() == 0)
			_exit(0);

		ret = clone3_empty_mntns(0);
		if (ret >= 0) {
			if (ret == 0)
				_exit(0);
			wait_for_pid(ret);
			_exit(1);
		}

		if (errno != EPERM)
			_exit(2);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Parent's mount namespace is unaffected after clone3 with CLONE_EMPTY_MNTNS.
 */
TEST_F(clone3_empty_mntns, parent_unchanged)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ssize_t nr_before, nr_after;
		pid_t inner;

		if (enter_userns())
			_exit(1);

		nr_before = count_mounts();
		if (nr_before < 1)
			_exit(2);

		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(3);

		if (inner == 0)
			_exit(0);

		if (wait_for_pid(inner) != 0)
			_exit(4);

		nr_after = count_mounts();
		if (nr_after != nr_before)
			_exit(5);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Parent with many mounts: child still gets exactly 1 mount.
 */
TEST_F(clone3_empty_mntns, many_parent_mounts)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		char tmpdir[] = "/tmp/clone3_mntns_test.XXXXXX";
		pid_t inner;
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

		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(9);

		if (inner == 0) {
			if (count_mounts() != 1)
				_exit(10);

			_exit(0);
		}

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Verify the child's root mount is nullfs with expected statmount properties.
 */
TEST_F(clone3_empty_mntns, mount_properties)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pid_t inner;

		if (enter_userns())
			_exit(1);

		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(2);

		if (inner == 0) {
			struct statmount *sm;
			uint64_t root_id;

			root_id = get_unique_mnt_id("/");
			if (!root_id)
				_exit(3);

			sm = statmount_alloc(root_id, 0,
					     STATMOUNT_MNT_BASIC |
					     STATMOUNT_MNT_POINT |
					     STATMOUNT_FS_TYPE, 0);
			if (!sm)
				_exit(4);

			/* Root mount point is "/". */
			if (!(sm->mask & STATMOUNT_MNT_POINT))
				_exit(5);
			if (strcmp(sm->str + sm->mnt_point, "/") != 0)
				_exit(6);

			/* Filesystem type is nullfs. */
			if (!(sm->mask & STATMOUNT_FS_TYPE))
				_exit(7);
			if (strcmp(sm->str + sm->fs_type, "nullfs") != 0)
				_exit(8);

			/* Root mount is its own parent. */
			if (!(sm->mask & STATMOUNT_MNT_BASIC))
				_exit(9);
			if (sm->mnt_parent_id != sm->mnt_id)
				_exit(10);

			free(sm);
			_exit(0);
		}

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Listmount returns only the root mount in the child's empty namespace.
 */
TEST_F(clone3_empty_mntns, listmount_single_entry)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pid_t inner;

		if (enter_userns())
			_exit(1);

		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(2);

		if (inner == 0) {
			uint64_t list[16];
			ssize_t nr_mounts;
			uint64_t root_id;

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

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Child can mount tmpfs over nullfs root (the primary container use case).
 *
 * Uses the new mount API (fsopen/fsmount/move_mount) because resolving
 * "/" returns the process root directly without following overmounts.
 * The mount fd from fsmount lets us fchdir + chroot into the new tmpfs.
 */
TEST_F(clone3_empty_mntns, child_overmount_tmpfs)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pid_t inner;

		if (enter_userns())
			_exit(1);

		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(2);

		if (inner == 0) {
			struct statmount *sm;
			uint64_t root_id;
			int fd, fsfd, mntfd;

			if (count_mounts() != 1)
				_exit(3);

			/* Verify root is nullfs. */
			root_id = get_unique_mnt_id("/");
			if (!root_id)
				_exit(4);

			sm = statmount_alloc(root_id, 0, STATMOUNT_FS_TYPE, 0);
			if (!sm)
				_exit(5);
			if (!(sm->mask & STATMOUNT_FS_TYPE))
				_exit(6);
			if (strcmp(sm->str + sm->fs_type, "nullfs") != 0)
				_exit(7);
			free(sm);

			/* Create tmpfs via the new mount API. */
			fsfd = sys_fsopen("tmpfs", 0);
			if (fsfd < 0)
				_exit(8);

			if (sys_fsconfig(fsfd, FSCONFIG_SET_STRING,
					 "size", "1M", 0)) {
				close(fsfd);
				_exit(9);
			}

			if (sys_fsconfig(fsfd, FSCONFIG_CMD_CREATE,
					 NULL, NULL, 0)) {
				close(fsfd);
				_exit(10);
			}

			mntfd = sys_fsmount(fsfd, 0, 0);
			close(fsfd);
			if (mntfd < 0)
				_exit(11);

			/* Attach tmpfs to "/". */
			if (sys_move_mount(mntfd, "", AT_FDCWD, "/",
					   MOVE_MOUNT_F_EMPTY_PATH)) {
				close(mntfd);
				_exit(12);
			}

			if (count_mounts() != 2) {
				close(mntfd);
				_exit(13);
			}

			/* Enter the tmpfs. */
			if (fchdir(mntfd)) {
				close(mntfd);
				_exit(14);
			}

			if (chroot(".")) {
				close(mntfd);
				_exit(15);
			}

			close(mntfd);

			/* Verify "/" is now tmpfs. */
			root_id = get_unique_mnt_id("/");
			if (!root_id)
				_exit(16);

			sm = statmount_alloc(root_id, 0, STATMOUNT_FS_TYPE, 0);
			if (!sm)
				_exit(17);
			if (!(sm->mask & STATMOUNT_FS_TYPE))
				_exit(18);
			if (strcmp(sm->str + sm->fs_type, "tmpfs") != 0)
				_exit(19);
			free(sm);

			/* Verify tmpfs is writable. */
			fd = open("/testfile", O_CREAT | O_RDWR, 0644);
			if (fd < 0)
				_exit(20);

			if (write(fd, "test", 4) != 4) {
				close(fd);
				_exit(21);
			}
			close(fd);

			if (access("/testfile", F_OK))
				_exit(22);

			_exit(0);
		}

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Multiple clone3 calls with CLONE_EMPTY_MNTNS produce children with
 * distinct mount namespace root mount IDs.
 */
TEST_F(clone3_empty_mntns, repeated)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int pipe1[2], pipe2[2];
		uint64_t id1 = 0, id2 = 0;
		pid_t inner1, inner2;

		if (enter_userns())
			_exit(1);

		if (pipe(pipe1) || pipe(pipe2))
			_exit(2);

		inner1 = clone3_empty_mntns(0);
		if (inner1 < 0)
			_exit(3);

		if (inner1 == 0) {
			uint64_t root_id;

			close(pipe1[0]);
			root_id = get_unique_mnt_id("/");
			if (write(pipe1[1], &root_id, sizeof(root_id)) != sizeof(root_id))
				_exit(1);
			close(pipe1[1]);
			_exit(0);
		}

		inner2 = clone3_empty_mntns(0);
		if (inner2 < 0)
			_exit(4);

		if (inner2 == 0) {
			uint64_t root_id;

			close(pipe2[0]);
			root_id = get_unique_mnt_id("/");
			if (write(pipe2[1], &root_id, sizeof(root_id)) != sizeof(root_id))
				_exit(1);
			close(pipe2[1]);
			_exit(0);
		}

		close(pipe1[1]);
		close(pipe2[1]);

		if (read(pipe1[0], &id1, sizeof(id1)) != sizeof(id1))
			_exit(5);
		if (read(pipe2[0], &id2, sizeof(id2)) != sizeof(id2))
			_exit(6);

		close(pipe1[0]);
		close(pipe2[0]);

		if (wait_for_pid(inner1) || wait_for_pid(inner2))
			_exit(7);

		/* Each child must have a distinct root mount ID. */
		if (id1 == 0 || id2 == 0)
			_exit(8);
		if (id1 == id2)
			_exit(9);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Verify setns() into a child's empty mount namespace works.
 */
TEST_F(clone3_empty_mntns, setns_into_child_mntns)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int pipe_fd[2];
		pid_t inner;
		char c;

		if (enter_userns())
			_exit(1);

		if (pipe(pipe_fd))
			_exit(2);

		inner = clone3_empty_mntns(0);
		if (inner < 0)
			_exit(3);

		if (inner == 0) {
			/* Signal parent we're ready. */
			close(pipe_fd[0]);
			if (write(pipe_fd[1], "r", 1) != 1)
				_exit(1);

			/*
			 * Wait for parent to finish.  Reading from our
			 * write end will block until the parent closes
			 * its read end, giving us an implicit barrier.
			 */
			if (read(pipe_fd[1], &c, 1) < 0)
				;
			close(pipe_fd[1]);
			_exit(0);
		}

		close(pipe_fd[1]);

		/* Wait for child to be ready. */
		if (read(pipe_fd[0], &c, 1) != 1)
			_exit(4);

		/* Open child's mount namespace. */
		{
			char path[64];
			int mntns_fd;

			snprintf(path, sizeof(path), "/proc/%d/ns/mnt", inner);
			mntns_fd = open(path, O_RDONLY);
			if (mntns_fd < 0)
				_exit(5);

			if (setns(mntns_fd, CLONE_NEWNS))
				_exit(6);

			close(mntns_fd);
		}

		/* Now we should be in the child's empty mntns. */
		if (count_mounts() != 1)
			_exit(7);

		close(pipe_fd[0]);
		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Tests below do not require CLONE_EMPTY_MNTNS support.
 */

/*
 * Unknown 64-bit flags beyond the known set are rejected.
 */
TEST(unknown_flags_rejected)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct __clone_args args = {
			.flags		= 0x800000000ULL,
			.exit_signal	= SIGCHLD,
		};
		pid_t ret;

		ret = sys_clone3(&args, sizeof(args));
		if (ret >= 0) {
			if (ret == 0)
				_exit(0);
			wait_for_pid(ret);
			_exit(1);
		}

		if (errno != EINVAL)
			_exit(2);

		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

/*
 * Regular clone3 with CLONE_NEWNS (without CLONE_EMPTY_MNTNS) still
 * copies the full mount tree.
 */
TEST(clone3_newns_full_copy)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct __clone_args args = {
			.flags		= CLONE_NEWNS,
			.exit_signal	= SIGCHLD,
		};
		ssize_t parent_mounts;
		pid_t inner;

		if (enter_userns())
			_exit(1);

		parent_mounts = count_mounts();
		if (parent_mounts < 1)
			_exit(2);

		inner = sys_clone3(&args, sizeof(args));
		if (inner < 0)
			_exit(3);

		if (inner == 0) {
			/* Full copy should have at least as many mounts. */
			if (count_mounts() < parent_mounts)
				_exit(1);

			_exit(0);
		}

		_exit(wait_for_pid(inner));
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

TEST_HARNESS_MAIN
