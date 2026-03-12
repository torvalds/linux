// SPDX-License-Identifier: GPL-2.0
/*
 * Test for OPEN_TREE_NAMESPACE flag.
 *
 * Test that open_tree() with OPEN_TREE_NAMESPACE creates a new mount
 * namespace containing the specified mount tree.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/nsfs.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../wrappers.h"
#include "../statmount/statmount.h"
#include "../utils.h"
#include "../../kselftest_harness.h"

#ifndef OPEN_TREE_NAMESPACE
#define OPEN_TREE_NAMESPACE	(1 << 1)
#endif

static int get_mnt_ns_id(int fd, uint64_t *mnt_ns_id)
{
	if (ioctl(fd, NS_GET_MNTNS_ID, mnt_ns_id) < 0)
		return -errno;
	return 0;
}

static int get_mnt_ns_id_from_path(const char *path, uint64_t *mnt_ns_id)
{
	int fd, ret;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	ret = get_mnt_ns_id(fd, mnt_ns_id);
	close(fd);
	return ret;
}

#define STATMOUNT_BUFSIZE (1 << 15)

static struct statmount *statmount_alloc(uint64_t mnt_id, uint64_t mnt_ns_id, uint64_t mask)
{
	struct statmount *buf;
	size_t bufsize = STATMOUNT_BUFSIZE;
	int ret;

	for (;;) {
		buf = malloc(bufsize);
		if (!buf)
			return NULL;

		ret = statmount(mnt_id, mnt_ns_id, mask, buf, bufsize, 0);
		if (ret == 0)
			return buf;

		free(buf);
		if (errno != EOVERFLOW)
			return NULL;

		bufsize <<= 1;
	}
}

static void log_mount(struct __test_metadata *_metadata, struct statmount *sm)
{
	const char *fs_type = "";
	const char *mnt_root = "";
	const char *mnt_point = "";

	if (sm->mask & STATMOUNT_FS_TYPE)
		fs_type = sm->str + sm->fs_type;
	if (sm->mask & STATMOUNT_MNT_ROOT)
		mnt_root = sm->str + sm->mnt_root;
	if (sm->mask & STATMOUNT_MNT_POINT)
		mnt_point = sm->str + sm->mnt_point;

	TH_LOG("  mnt_id: %llu, parent_id: %llu, fs_type: %s, root: %s, point: %s",
	       (unsigned long long)sm->mnt_id,
	       (unsigned long long)sm->mnt_parent_id,
	       fs_type, mnt_root, mnt_point);
}

static void dump_mounts(struct __test_metadata *_metadata, uint64_t mnt_ns_id)
{
	uint64_t list[256];
	ssize_t nr_mounts;

	nr_mounts = listmount(LSMT_ROOT, mnt_ns_id, 0, list, 256, 0);
	if (nr_mounts < 0) {
		TH_LOG("listmount failed: %s", strerror(errno));
		return;
	}

	TH_LOG("Mount namespace %llu contains %zd mount(s):",
	       (unsigned long long)mnt_ns_id, nr_mounts);

	for (ssize_t i = 0; i < nr_mounts; i++) {
		struct statmount *sm;

		sm = statmount_alloc(list[i], mnt_ns_id,
				     STATMOUNT_MNT_BASIC |
				     STATMOUNT_FS_TYPE |
				     STATMOUNT_MNT_ROOT |
				     STATMOUNT_MNT_POINT);
		if (!sm) {
			TH_LOG("  [%zd] mnt_id %llu: statmount failed: %s",
			       i, (unsigned long long)list[i], strerror(errno));
			continue;
		}

		log_mount(_metadata, sm);
		free(sm);
	}
}

FIXTURE(open_tree_ns)
{
	int fd;
	uint64_t current_ns_id;
};

FIXTURE_VARIANT(open_tree_ns)
{
	const char *path;
	unsigned int flags;
	bool expect_success;
	bool expect_different_ns;
	int min_mounts;
};

FIXTURE_VARIANT_ADD(open_tree_ns, basic_root)
{
	.path = "/",
	.flags = OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = true,
	/*
	 * The empty rootfs is hidden from listmount()/mountinfo,
	 * so we only see the bind mount on top of it.
	 */
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(open_tree_ns, recursive_root)
{
	.path = "/",
	.flags = OPEN_TREE_NAMESPACE | AT_RECURSIVE | OPEN_TREE_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = true,
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(open_tree_ns, subdir_tmp)
{
	.path = "/tmp",
	.flags = OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = true,
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(open_tree_ns, subdir_proc)
{
	.path = "/proc",
	.flags = OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = true,
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(open_tree_ns, recursive_tmp)
{
	.path = "/tmp",
	.flags = OPEN_TREE_NAMESPACE | AT_RECURSIVE | OPEN_TREE_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = true,
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(open_tree_ns, recursive_run)
{
	.path = "/run",
	.flags = OPEN_TREE_NAMESPACE | AT_RECURSIVE | OPEN_TREE_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = true,
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(open_tree_ns, invalid_recursive_alone)
{
	.path = "/",
	.flags = AT_RECURSIVE | OPEN_TREE_CLOEXEC,
	.expect_success = false,
	.expect_different_ns = false,
	.min_mounts = 0,
};

FIXTURE_SETUP(open_tree_ns)
{
	int ret;

	self->fd = -1;

	/* Check if open_tree syscall is supported */
	ret = sys_open_tree(-1, NULL, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "open_tree() syscall not supported");

	/* Check if statmount/listmount are supported */
	ret = statmount(0, 0, 0, NULL, 0, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "statmount() syscall not supported");

	/* Get current mount namespace ID for comparison */
	ret = get_mnt_ns_id_from_path("/proc/self/ns/mnt", &self->current_ns_id);
	if (ret < 0)
		SKIP(return, "Failed to get current mount namespace ID");
}

FIXTURE_TEARDOWN(open_tree_ns)
{
	if (self->fd >= 0)
		close(self->fd);
}

TEST_F(open_tree_ns, create_namespace)
{
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	self->fd = sys_open_tree(AT_FDCWD, variant->path, variant->flags);

	if (!variant->expect_success) {
		ASSERT_LT(self->fd, 0);
		ASSERT_EQ(errno, EINVAL);
		return;
	}

	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	/* Verify we can get the namespace ID */
	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	/* Verify it's a different namespace */
	if (variant->expect_different_ns)
		ASSERT_NE(new_ns_id, self->current_ns_id);

	/* List mounts in the new namespace */
	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 0) {
		TH_LOG("%m - listmount failed");
	}

	/* Verify minimum expected mounts */
	ASSERT_GE(nr_mounts, variant->min_mounts);
	TH_LOG("Namespace contains %zd mounts", nr_mounts);
}

TEST_F(open_tree_ns, setns_into_namespace)
{
	uint64_t new_ns_id;
	pid_t pid;
	int status;
	int ret;

	/* Only test with basic flags */
	if (!(variant->flags & OPEN_TREE_NAMESPACE))
		SKIP(return, "setns test only for basic / case");

	self->fd = sys_open_tree(AT_FDCWD, variant->path, variant->flags);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	/* Get namespace ID and dump all mounts */
	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	dump_mounts(_metadata, new_ns_id);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* Child: try to enter the namespace */
		if (setns(self->fd, CLONE_NEWNS) < 0)
			_exit(1);
		_exit(0);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);
}

TEST_F(open_tree_ns, verify_mount_properties)
{
	struct statmount sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	/* Only test with basic flags on root */
	if (variant->flags != (OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC) ||
	    strcmp(variant->path, "/") != 0)
		SKIP(return, "mount properties test only for basic / case");

	self->fd = sys_open_tree(AT_FDCWD, "/", OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	/* Get info about the root mount (the bind mount, rootfs is hidden) */
	ret = statmount(list[0], new_ns_id, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0);
	ASSERT_EQ(ret, 0);

	ASSERT_NE(sm.mnt_id, sm.mnt_parent_id);

	TH_LOG("Root mount id: %llu, parent: %llu",
	       (unsigned long long)sm.mnt_id,
	       (unsigned long long)sm.mnt_parent_id);
}

FIXTURE(open_tree_ns_caps)
{
	bool has_caps;
};

FIXTURE_SETUP(open_tree_ns_caps)
{
	int ret;

	/* Check if open_tree syscall is supported */
	ret = sys_open_tree(-1, NULL, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "open_tree() syscall not supported");

	self->has_caps = (geteuid() == 0);
}

FIXTURE_TEARDOWN(open_tree_ns_caps)
{
}

TEST_F(open_tree_ns_caps, requires_cap_sys_admin)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;

		/* Child: drop privileges using utils.h helper */
		if (enter_userns() != 0)
			_exit(2);

		/* Drop all caps using utils.h helper */
		if (caps_down() == 0)
			_exit(3);

		fd = sys_open_tree(AT_FDCWD, "/",
				   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
		if (fd >= 0) {
			close(fd);
			/* Should have failed without caps */
			_exit(1);
		}

		if (errno == EPERM)
			_exit(0);

		/* EINVAL means OPEN_TREE_NAMESPACE not supported */
		if (errno == EINVAL)
			_exit(4);

		/* Unexpected error */
		_exit(5);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));

	switch (WEXITSTATUS(status)) {
	case 0:
		/* Expected: EPERM without caps */
		break;
	case 1:
		ASSERT_FALSE(true) TH_LOG("OPEN_TREE_NAMESPACE succeeded without caps");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 3:
		SKIP(return, "caps_down failed");
		break;
	case 4:
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

FIXTURE(open_tree_ns_userns)
{
	int fd;
};

FIXTURE_SETUP(open_tree_ns_userns)
{
	int ret;

	self->fd = -1;

	/* Check if open_tree syscall is supported */
	ret = sys_open_tree(-1, NULL, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "open_tree() syscall not supported");

	/* Check if statmount/listmount are supported */
	ret = statmount(0, 0, 0, NULL, 0, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "statmount() syscall not supported");
}

FIXTURE_TEARDOWN(open_tree_ns_userns)
{
	if (self->fd >= 0)
		close(self->fd);
}

TEST_F(open_tree_ns_userns, create_in_userns)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		uint64_t list[256];
		ssize_t nr_mounts;
		int fd;

		/* Create new user namespace (also creates mount namespace) */
		if (enter_userns() != 0)
			_exit(2);

		/* Now we have CAP_SYS_ADMIN in the user namespace */
		fd = sys_open_tree(AT_FDCWD, "/",
				   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
		if (fd < 0) {
			if (errno == EINVAL)
				_exit(4); /* OPEN_TREE_NAMESPACE not supported */
			_exit(1);
		}

		/* Verify we can get the namespace ID */
		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(5);

		/* Verify we can list mounts in the new namespace */
		nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
		if (nr_mounts < 0)
			_exit(6);

		/* Should have at least 1 mount */
		if (nr_mounts < 1)
			_exit(7);

		close(fd);
		_exit(0);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));

	switch (WEXITSTATUS(status)) {
	case 0:
		/* Success */
		break;
	case 1:
		ASSERT_FALSE(true) TH_LOG("open_tree(OPEN_TREE_NAMESPACE) failed in userns");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 4:
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");
		break;
	case 5:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 6:
		ASSERT_FALSE(true) TH_LOG("listmount failed in new namespace");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("New namespace has no mounts");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

TEST_F(open_tree_ns_userns, setns_in_userns)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		int fd;
		pid_t inner_pid;
		int inner_status;

		/* Create new user namespace */
		if (enter_userns() != 0)
			_exit(2);

		fd = sys_open_tree(AT_FDCWD, "/",
				   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
		if (fd < 0) {
			if (errno == EINVAL)
				_exit(4);
			_exit(1);
		}

		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(5);

		/* Fork again to test setns into the new namespace */
		inner_pid = fork();
		if (inner_pid < 0)
			_exit(8);

		if (inner_pid == 0) {
			/* Inner child: enter the new namespace */
			if (setns(fd, CLONE_NEWNS) < 0)
				_exit(1);
			_exit(0);
		}

		if (waitpid(inner_pid, &inner_status, 0) != inner_pid)
			_exit(9);

		if (!WIFEXITED(inner_status) || WEXITSTATUS(inner_status) != 0)
			_exit(10);

		close(fd);
		_exit(0);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));

	switch (WEXITSTATUS(status)) {
	case 0:
		/* Success */
		break;
	case 1:
		ASSERT_FALSE(true) TH_LOG("open_tree or setns failed in userns");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 4:
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");
		break;
	case 5:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 8:
		ASSERT_FALSE(true) TH_LOG("Inner fork failed");
		break;
	case 9:
		ASSERT_FALSE(true) TH_LOG("Inner waitpid failed");
		break;
	case 10:
		ASSERT_FALSE(true) TH_LOG("setns into new namespace failed");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

TEST_F(open_tree_ns_userns, recursive_in_userns)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		uint64_t list[256];
		ssize_t nr_mounts;
		int fd;

		/* Create new user namespace */
		if (enter_userns() != 0)
			_exit(2);

		/* Test recursive flag in userns */
		fd = sys_open_tree(AT_FDCWD, "/",
				   OPEN_TREE_NAMESPACE | AT_RECURSIVE | OPEN_TREE_CLOEXEC);
		if (fd < 0) {
			if (errno == EINVAL)
				_exit(4);
			_exit(1);
		}

		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(5);

		nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
		if (nr_mounts < 0)
			_exit(6);

		/* Recursive should copy submounts too */
		if (nr_mounts < 1)
			_exit(7);

		close(fd);
		_exit(0);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));

	switch (WEXITSTATUS(status)) {
	case 0:
		/* Success */
		break;
	case 1:
		ASSERT_FALSE(true) TH_LOG("open_tree(OPEN_TREE_NAMESPACE|AT_RECURSIVE) failed in userns");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 4:
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");
		break;
	case 5:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 6:
		ASSERT_FALSE(true) TH_LOG("listmount failed in new namespace");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("New namespace has no mounts");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

TEST_F(open_tree_ns_userns, umount_fails_einval)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		uint64_t list[256];
		ssize_t nr_mounts;
		int fd;
		ssize_t i;

		/* Create new user namespace */
		if (enter_userns() != 0)
			_exit(2);

		fd = sys_open_tree(AT_FDCWD, "/",
				   OPEN_TREE_NAMESPACE | AT_RECURSIVE | OPEN_TREE_CLOEXEC);
		if (fd < 0) {
			if (errno == EINVAL)
				_exit(4);
			_exit(1);
		}

		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(5);

		/* Get all mounts in the new namespace */
		nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, LISTMOUNT_REVERSE);
		if (nr_mounts < 0)
			_exit(9);

		if (nr_mounts < 1)
			_exit(10);

		/* Enter the new namespace */
		if (setns(fd, CLONE_NEWNS) < 0)
			_exit(6);

		for (i = 0; i < nr_mounts; i++) {
			struct statmount *sm;
			const char *mnt_point;

			sm = statmount_alloc(list[i], new_ns_id,
					     STATMOUNT_MNT_POINT);
			if (!sm)
				_exit(11);

			mnt_point = sm->str + sm->mnt_point;

			TH_LOG("Trying to umount %s", mnt_point);
			if (umount2(mnt_point, MNT_DETACH) == 0) {
				free(sm);
				_exit(7);
			}

			if (errno != EINVAL) {
				/* Wrong error */
				free(sm);
				_exit(8);
			}

			free(sm);
		}

		close(fd);
		_exit(0);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));

	switch (WEXITSTATUS(status)) {
	case 0:
		break;
	case 1:
		ASSERT_FALSE(true) TH_LOG("open_tree(OPEN_TREE_NAMESPACE) failed");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 4:
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");
		break;
	case 5:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 6:
		ASSERT_FALSE(true) TH_LOG("setns into new namespace failed");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("umount succeeded but should have failed with EINVAL");
		break;
	case 8:
		ASSERT_FALSE(true) TH_LOG("umount failed with wrong error (expected EINVAL)");
		break;
	case 9:
		ASSERT_FALSE(true) TH_LOG("listmount failed");
		break;
	case 10:
		ASSERT_FALSE(true) TH_LOG("No mounts in new namespace");
		break;
	case 11:
		ASSERT_FALSE(true) TH_LOG("statmount_alloc failed");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

TEST_F(open_tree_ns_userns, umount_succeeds)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		uint64_t list[256];
		ssize_t nr_mounts;
		int fd;
		ssize_t i;

		if (unshare(CLONE_NEWNS))
			_exit(1);

		if (sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) != 0)
			_exit(1);

		fd = sys_open_tree(AT_FDCWD, "/",
				   OPEN_TREE_NAMESPACE | AT_RECURSIVE | OPEN_TREE_CLOEXEC);
		if (fd < 0) {
			if (errno == EINVAL)
				_exit(4);
			_exit(1);
		}

		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(5);

		/* Get all mounts in the new namespace */
		nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, LISTMOUNT_REVERSE);
		if (nr_mounts < 0)
			_exit(9);

		if (nr_mounts < 1)
			_exit(10);

		/* Enter the new namespace */
		if (setns(fd, CLONE_NEWNS) < 0)
			_exit(6);

		for (i = 0; i < nr_mounts; i++) {
			struct statmount *sm;
			const char *mnt_point;

			sm = statmount_alloc(list[i], new_ns_id,
					     STATMOUNT_MNT_POINT);
			if (!sm)
				_exit(11);

			mnt_point = sm->str + sm->mnt_point;

			TH_LOG("Trying to umount %s", mnt_point);
			if (umount2(mnt_point, MNT_DETACH) != 0) {
				free(sm);
				_exit(7);
			}

			free(sm);
		}

		close(fd);
		_exit(0);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));

	switch (WEXITSTATUS(status)) {
	case 0:
		break;
	case 1:
		ASSERT_FALSE(true) TH_LOG("open_tree(OPEN_TREE_NAMESPACE) failed");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 4:
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");
		break;
	case 5:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 6:
		ASSERT_FALSE(true) TH_LOG("setns into new namespace failed");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("umount succeeded but should have failed with EINVAL");
		break;
	case 9:
		ASSERT_FALSE(true) TH_LOG("listmount failed");
		break;
	case 10:
		ASSERT_FALSE(true) TH_LOG("No mounts in new namespace");
		break;
	case 11:
		ASSERT_FALSE(true) TH_LOG("statmount_alloc failed");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

FIXTURE(open_tree_ns_unbindable)
{
	char tmpdir[PATH_MAX];
	bool mounted;
};

FIXTURE_SETUP(open_tree_ns_unbindable)
{
	int ret;

	self->mounted = false;

	/* Check if open_tree syscall is supported */
	ret = sys_open_tree(-1, NULL, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "open_tree() syscall not supported");

	/* Create a temporary directory for the test mount */
	snprintf(self->tmpdir, sizeof(self->tmpdir),
		 "/tmp/open_tree_ns_test.XXXXXX");
	ASSERT_NE(mkdtemp(self->tmpdir), NULL);

	/* Mount tmpfs there */
	ret = mount("tmpfs", self->tmpdir, "tmpfs", 0, NULL);
	if (ret < 0) {
		rmdir(self->tmpdir);
		SKIP(return, "Failed to mount tmpfs");
	}
	self->mounted = true;

	ret = mount(NULL, self->tmpdir, NULL, MS_UNBINDABLE, NULL);
	if (ret < 0) {
		rmdir(self->tmpdir);
		SKIP(return, "Failed to make tmpfs unbindable");
	}
}

FIXTURE_TEARDOWN(open_tree_ns_unbindable)
{
	if (self->mounted)
		umount2(self->tmpdir, MNT_DETACH);
	rmdir(self->tmpdir);
}

TEST_F(open_tree_ns_unbindable, fails_on_unbindable)
{
	int fd;

	fd = sys_open_tree(AT_FDCWD, self->tmpdir,
			   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
	ASSERT_LT(fd, 0);
}

TEST_F(open_tree_ns_unbindable, recursive_skips_on_unbindable)
{
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int fd;
	ssize_t i;
	bool found_unbindable = false;

	fd = sys_open_tree(AT_FDCWD, "/",
			   OPEN_TREE_NAMESPACE | AT_RECURSIVE | OPEN_TREE_CLOEXEC);
	ASSERT_GT(fd, 0);

	ASSERT_EQ(get_mnt_ns_id(fd, &new_ns_id), 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 0) {
		TH_LOG("listmount failed: %m");
	}

	/*
	 * Iterate through all mounts in the new namespace and verify
	 * the unbindable tmpfs mount was silently dropped.
	 */
	for (i = 0; i < nr_mounts; i++) {
		struct statmount *sm;
		const char *mnt_point;

		sm = statmount_alloc(list[i], new_ns_id, STATMOUNT_MNT_POINT);
		ASSERT_NE(sm, NULL) {
			TH_LOG("statmount_alloc failed for mnt_id %llu",
			       (unsigned long long)list[i]);
		}

		mnt_point = sm->str + sm->mnt_point;

		if (strcmp(mnt_point, self->tmpdir) == 0) {
			TH_LOG("Found unbindable mount at %s (should have been dropped)",
			       mnt_point);
			found_unbindable = true;
		}

		free(sm);
	}

	ASSERT_FALSE(found_unbindable) {
		TH_LOG("Unbindable mount at %s was not dropped", self->tmpdir);
	}

	close(fd);
}

TEST_HARNESS_MAIN
