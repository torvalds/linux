// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Christian Brauner <brauner@kernel.org>
 *
 * Test for FSMOUNT_NAMESPACE flag.
 *
 * Test that fsmount() with FSMOUNT_NAMESPACE creates a new mount
 * namespace containing the specified mount.
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

#ifndef FSMOUNT_NAMESPACE
#define FSMOUNT_NAMESPACE	0x00000002
#endif

#ifndef FSMOUNT_CLOEXEC
#define FSMOUNT_CLOEXEC		0x00000001
#endif

#ifndef FSCONFIG_CMD_CREATE
#define FSCONFIG_CMD_CREATE	6
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
				     STATMOUNT_MNT_POINT, 0);
		if (!sm) {
			TH_LOG("  [%zd] mnt_id %llu: statmount failed: %s",
			       i, (unsigned long long)list[i], strerror(errno));
			continue;
		}

		log_mount(_metadata, sm);
		free(sm);
	}
}

static int create_tmpfs_fd(void)
{
	int fs_fd, ret;

	fs_fd = sys_fsopen("tmpfs", FSOPEN_CLOEXEC);
	if (fs_fd < 0)
		return -errno;

	ret = sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0);
	if (ret < 0) {
		close(fs_fd);
		return -errno;
	}

	return fs_fd;
}

FIXTURE(fsmount_ns)
{
	int fd;
	int fs_fd;
	uint64_t current_ns_id;
};

FIXTURE_VARIANT(fsmount_ns)
{
	const char *fstype;
	unsigned int flags;
	bool expect_success;
	bool expect_different_ns;
	int min_mounts;
};

FIXTURE_VARIANT_ADD(fsmount_ns, basic_tmpfs)
{
	.fstype = "tmpfs",
	.flags = FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = true,
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(fsmount_ns, cloexec_only)
{
	.fstype = "tmpfs",
	.flags = FSMOUNT_CLOEXEC,
	.expect_success = true,
	.expect_different_ns = false,
	.min_mounts = 1,
};

FIXTURE_VARIANT_ADD(fsmount_ns, namespace_only)
{
	.fstype = "tmpfs",
	.flags = FSMOUNT_NAMESPACE,
	.expect_success = true,
	.expect_different_ns = true,
	.min_mounts = 1,
};

FIXTURE_SETUP(fsmount_ns)
{
	int ret;

	self->fd = -1;
	self->fs_fd = -1;

	/* Check if fsopen syscall is supported */
	ret = sys_fsopen("tmpfs", 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "fsopen() syscall not supported");
	if (ret >= 0)
		close(ret);

	/* Check if statmount/listmount are supported */
	ret = statmount(0, 0, 0, 0, NULL, 0, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "statmount() syscall not supported");

	/* Get current mount namespace ID for comparison */
	ret = get_mnt_ns_id_from_path("/proc/self/ns/mnt", &self->current_ns_id);
	if (ret < 0)
		SKIP(return, "Failed to get current mount namespace ID");
}

FIXTURE_TEARDOWN(fsmount_ns)
{
	if (self->fd >= 0)
		close(self->fd);
	if (self->fs_fd >= 0)
		close(self->fs_fd);
}

TEST_F(fsmount_ns, create_namespace)
{
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, variant->flags, 0);

	if (!variant->expect_success) {
		ASSERT_LT(self->fd, 0);
		return;
	}

	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	if (variant->expect_different_ns) {
		/* Verify we can get the namespace ID from the fd */
		ret = get_mnt_ns_id(self->fd, &new_ns_id);
		ASSERT_EQ(ret, 0);

		/* Verify it's a different namespace */
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
}

TEST_F(fsmount_ns, setns_into_namespace)
{
	uint64_t new_ns_id;
	pid_t pid;
	int status;
	int ret;

	/* Only test with FSMOUNT_NAMESPACE flag */
	if (!(variant->flags & FSMOUNT_NAMESPACE))
		SKIP(return, "setns test only for FSMOUNT_NAMESPACE case");

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, variant->flags, 0);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

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

TEST_F(fsmount_ns, verify_mount_properties)
{
	struct statmount sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	/* Only test with basic FSMOUNT_NAMESPACE flags */
	if (variant->flags != (FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC))
		SKIP(return, "mount properties test only for basic case");

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC, 0);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	/* Get info about the root mount */
	ret = statmount(list[0], new_ns_id, 0, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0);
	ASSERT_EQ(ret, 0);

	TH_LOG("Root mount id: %llu, parent: %llu",
	       (unsigned long long)sm.mnt_id,
	       (unsigned long long)sm.mnt_parent_id);
}

TEST_F(fsmount_ns, verify_tmpfs_type)
{
	struct statmount *sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	const char *fs_type;
	int ret;

	/* Only test with basic FSMOUNT_NAMESPACE flags */
	if (variant->flags != (FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC))
		SKIP(return, "fs type test only for basic case");

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC, 0);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	sm = statmount_alloc(list[0], new_ns_id, STATMOUNT_FS_TYPE, 0);
	ASSERT_NE(sm, NULL);

	fs_type = sm->str + sm->fs_type;
	ASSERT_STREQ(fs_type, "tmpfs");

	free(sm);
}

FIXTURE(fsmount_ns_caps)
{
	bool has_caps;
};

FIXTURE_SETUP(fsmount_ns_caps)
{
	int ret;

	/* Check if fsopen syscall is supported */
	ret = sys_fsopen("tmpfs", 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "fsopen() syscall not supported");
	if (ret >= 0)
		close(ret);

	self->has_caps = (geteuid() == 0);
}

FIXTURE_TEARDOWN(fsmount_ns_caps)
{
}

TEST_F(fsmount_ns_caps, requires_cap_sys_admin)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fs_fd, fd;

		/* Child: drop privileges using utils.h helper */
		if (enter_userns() != 0)
			_exit(2);

		/* Drop all caps using utils.h helper */
		if (caps_down() == 0)
			_exit(3);

		fs_fd = sys_fsopen("tmpfs", FSOPEN_CLOEXEC);
		if (fs_fd < 0)
			_exit(4);

		if (sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
			close(fs_fd);
			_exit(5);
		}

		fd = sys_fsmount(fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC, 0);
		close(fs_fd);

		if (fd >= 0) {
			close(fd);
			/* Should have failed without caps */
			_exit(1);
		}

		if (errno == EPERM)
			_exit(0);

		/* EINVAL means FSMOUNT_NAMESPACE not supported */
		if (errno == EINVAL)
			_exit(6);

		/* Unexpected error */
		_exit(7);
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));

	switch (WEXITSTATUS(status)) {
	case 0:
		/* Expected: EPERM without caps */
		break;
	case 1:
		ASSERT_FALSE(true) TH_LOG("FSMOUNT_NAMESPACE succeeded without caps");
		break;
	case 2:
		SKIP(return, "enter_userns failed");
		break;
	case 3:
		SKIP(return, "caps_down failed");
		break;
	case 4:
		SKIP(return, "fsopen failed in userns");
		break;
	case 5:
		SKIP(return, "fsconfig CMD_CREATE failed in userns");
		break;
	case 6:
		SKIP(return, "FSMOUNT_NAMESPACE not supported");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

FIXTURE(fsmount_ns_userns)
{
	int fd;
	int fs_fd;
};

FIXTURE_SETUP(fsmount_ns_userns)
{
	int ret;

	self->fd = -1;
	self->fs_fd = -1;

	/* Check if fsopen syscall is supported */
	ret = sys_fsopen("tmpfs", 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "fsopen() syscall not supported");
	if (ret >= 0)
		close(ret);

	/* Check if statmount/listmount are supported */
	ret = statmount(0, 0, 0, 0, NULL, 0, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "statmount() syscall not supported");
}

FIXTURE_TEARDOWN(fsmount_ns_userns)
{
	if (self->fd >= 0)
		close(self->fd);
	if (self->fs_fd >= 0)
		close(self->fs_fd);
}

TEST_F(fsmount_ns_userns, create_in_userns)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		uint64_t list[256];
		ssize_t nr_mounts;
		int fs_fd, fd;

		/* Create new user namespace (also creates mount namespace) */
		if (setup_userns() != 0)
			_exit(2);

		/* Now we have CAP_SYS_ADMIN in the user namespace */
		fs_fd = sys_fsopen("tmpfs", FSOPEN_CLOEXEC);
		if (fs_fd < 0)
			_exit(3);

		if (sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
			close(fs_fd);
			_exit(4);
		}

		fd = sys_fsmount(fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC, 0);
		close(fs_fd);

		if (fd < 0) {
			if (errno == EINVAL)
				_exit(6); /* FSMOUNT_NAMESPACE not supported */
			_exit(1);
		}

		/* Verify we can get the namespace ID */
		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(7);

		/* Verify we can list mounts in the new namespace */
		nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
		if (nr_mounts < 0)
			_exit(8);

		/* Should have at least 1 mount (the tmpfs) */
		if (nr_mounts < 1)
			_exit(9);

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
		ASSERT_FALSE(true) TH_LOG("fsmount(FSMOUNT_NAMESPACE) failed in userns");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 3:
		SKIP(return, "fsopen failed in userns");
		break;
	case 4:
		SKIP(return, "fsconfig CMD_CREATE failed in userns");
		break;
	case 6:
		SKIP(return, "FSMOUNT_NAMESPACE not supported");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 8:
		ASSERT_FALSE(true) TH_LOG("listmount failed in new namespace");
		break;
	case 9:
		ASSERT_FALSE(true) TH_LOG("New namespace has no mounts");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

TEST_F(fsmount_ns_userns, setns_in_userns)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		int fs_fd, fd;
		pid_t inner_pid;
		int inner_status;

		/* Create new user namespace */
		if (setup_userns() != 0)
			_exit(2);

		fs_fd = sys_fsopen("tmpfs", FSOPEN_CLOEXEC);
		if (fs_fd < 0)
			_exit(3);

		if (sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
			close(fs_fd);
			_exit(4);
		}

		fd = sys_fsmount(fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC, 0);
		close(fs_fd);

		if (fd < 0) {
			if (errno == EINVAL)
				_exit(6);
			_exit(1);
		}

		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(7);

		/* Fork again to test setns into the new namespace */
		inner_pid = fork();
		if (inner_pid < 0)
			_exit(10);

		if (inner_pid == 0) {
			/* Inner child: enter the new namespace */
			if (setns(fd, CLONE_NEWNS) < 0)
				_exit(1);
			_exit(0);
		}

		if (waitpid(inner_pid, &inner_status, 0) != inner_pid)
			_exit(11);

		if (!WIFEXITED(inner_status) || WEXITSTATUS(inner_status) != 0)
			_exit(12);

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
		ASSERT_FALSE(true) TH_LOG("fsmount or setns failed in userns");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 3:
		SKIP(return, "fsopen failed in userns");
		break;
	case 4:
		SKIP(return, "fsconfig CMD_CREATE failed in userns");
		break;
	case 6:
		SKIP(return, "FSMOUNT_NAMESPACE not supported");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 10:
		ASSERT_FALSE(true) TH_LOG("Inner fork failed");
		break;
	case 11:
		ASSERT_FALSE(true) TH_LOG("Inner waitpid failed");
		break;
	case 12:
		ASSERT_FALSE(true) TH_LOG("setns into new namespace failed");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

TEST_F(fsmount_ns_userns, umount_fails_einval)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		uint64_t list[256];
		ssize_t nr_mounts;
		int fs_fd, fd;
		ssize_t i;

		/* Create new user namespace */
		if (setup_userns() != 0)
			_exit(2);

		fs_fd = sys_fsopen("tmpfs", FSOPEN_CLOEXEC);
		if (fs_fd < 0)
			_exit(3);

		if (sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
			close(fs_fd);
			_exit(4);
		}

		fd = sys_fsmount(fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC, 0);
		close(fs_fd);

		if (fd < 0) {
			if (errno == EINVAL)
				_exit(6);
			_exit(1);
		}

		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(7);

		/* Get all mounts in the new namespace */
		nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, LISTMOUNT_REVERSE);
		if (nr_mounts < 0)
			_exit(13);

		if (nr_mounts < 1)
			_exit(14);

		/* Enter the new namespace */
		if (setns(fd, CLONE_NEWNS) < 0)
			_exit(8);

		for (i = 0; i < nr_mounts; i++) {
			struct statmount *sm;
			const char *mnt_point;

			sm = statmount_alloc(list[i], new_ns_id,
					     STATMOUNT_MNT_POINT, 0);
			if (!sm)
				_exit(15);

			mnt_point = sm->str + sm->mnt_point;

			if (umount2(mnt_point, MNT_DETACH) == 0) {
				free(sm);
				_exit(9);
			}

			if (errno != EINVAL) {
				/* Wrong error */
				free(sm);
				_exit(10);
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
		ASSERT_FALSE(true) TH_LOG("fsmount(FSMOUNT_NAMESPACE) failed");
		break;
	case 2:
		SKIP(return, "setup_userns failed");
		break;
	case 3:
		SKIP(return, "fsopen failed in userns");
		break;
	case 4:
		SKIP(return, "fsconfig CMD_CREATE failed in userns");
		break;
	case 6:
		SKIP(return, "FSMOUNT_NAMESPACE not supported");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 8:
		ASSERT_FALSE(true) TH_LOG("setns into new namespace failed");
		break;
	case 9:
		ASSERT_FALSE(true) TH_LOG("umount succeeded but should have failed with EINVAL");
		break;
	case 10:
		ASSERT_FALSE(true) TH_LOG("umount failed with wrong error (expected EINVAL)");
		break;
	case 13:
		ASSERT_FALSE(true) TH_LOG("listmount failed");
		break;
	case 14:
		ASSERT_FALSE(true) TH_LOG("No mounts in new namespace");
		break;
	case 15:
		ASSERT_FALSE(true) TH_LOG("statmount_alloc failed");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

TEST_F(fsmount_ns_userns, umount_succeeds)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		uint64_t new_ns_id;
		uint64_t list[256];
		ssize_t nr_mounts;
		int fs_fd, fd;
		ssize_t i;

		if (unshare(CLONE_NEWNS))
			_exit(1);

		if (sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) != 0)
			_exit(1);

		fs_fd = sys_fsopen("tmpfs", FSOPEN_CLOEXEC);
		if (fs_fd < 0)
			_exit(3);

		if (sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
			close(fs_fd);
			_exit(4);
		}

		fd = sys_fsmount(fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC, 0);
		close(fs_fd);

		if (fd < 0) {
			if (errno == EINVAL)
				_exit(6);
			_exit(1);
		}

		if (get_mnt_ns_id(fd, &new_ns_id) != 0)
			_exit(7);

		/* Get all mounts in the new namespace */
		nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, LISTMOUNT_REVERSE);
		if (nr_mounts < 0)
			_exit(13);

		if (nr_mounts < 1)
			_exit(14);

		/* Enter the new namespace */
		if (setns(fd, CLONE_NEWNS) < 0)
			_exit(8);

		for (i = 0; i < nr_mounts; i++) {
			struct statmount *sm;
			const char *mnt_point;

			sm = statmount_alloc(list[i], new_ns_id,
					     STATMOUNT_MNT_POINT, 0);
			if (!sm)
				_exit(15);

			mnt_point = sm->str + sm->mnt_point;

			if (umount2(mnt_point, MNT_DETACH) != 0) {
				free(sm);
				_exit(9);
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
		ASSERT_FALSE(true) TH_LOG("fsmount(FSMOUNT_NAMESPACE) failed or unshare failed");
		break;
	case 3:
		SKIP(return, "fsopen failed");
		break;
	case 4:
		SKIP(return, "fsconfig CMD_CREATE failed");
		break;
	case 6:
		SKIP(return, "FSMOUNT_NAMESPACE not supported");
		break;
	case 7:
		ASSERT_FALSE(true) TH_LOG("Failed to get mount namespace ID");
		break;
	case 8:
		ASSERT_FALSE(true) TH_LOG("setns into new namespace failed");
		break;
	case 9:
		ASSERT_FALSE(true) TH_LOG("umount failed but should have succeeded");
		break;
	case 13:
		ASSERT_FALSE(true) TH_LOG("listmount failed");
		break;
	case 14:
		ASSERT_FALSE(true) TH_LOG("No mounts in new namespace");
		break;
	case 15:
		ASSERT_FALSE(true) TH_LOG("statmount_alloc failed");
		break;
	default:
		ASSERT_FALSE(true) TH_LOG("Unexpected error in child (exit %d)",
					  WEXITSTATUS(status));
		break;
	}
}

FIXTURE(fsmount_ns_mount_attrs)
{
	int fd;
	int fs_fd;
};

FIXTURE_SETUP(fsmount_ns_mount_attrs)
{
	int ret;

	self->fd = -1;
	self->fs_fd = -1;

	/* Check if fsopen syscall is supported */
	ret = sys_fsopen("tmpfs", 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "fsopen() syscall not supported");
	if (ret >= 0)
		close(ret);

	/* Check if statmount/listmount are supported */
	ret = statmount(0, 0, 0, 0, NULL, 0, 0);
	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "statmount() syscall not supported");
}

FIXTURE_TEARDOWN(fsmount_ns_mount_attrs)
{
	if (self->fd >= 0)
		close(self->fd);
	if (self->fs_fd >= 0)
		close(self->fs_fd);
}

TEST_F(fsmount_ns_mount_attrs, readonly)
{
	struct statmount sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC,
			       MOUNT_ATTR_RDONLY);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	ret = statmount(list[0], new_ns_id, 0, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0);
	ASSERT_EQ(ret, 0);

	/* Verify the mount is read-only */
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_RDONLY);
}

TEST_F(fsmount_ns_mount_attrs, noexec)
{
	struct statmount sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC,
			       MOUNT_ATTR_NOEXEC);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	ret = statmount(list[0], new_ns_id, 0, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0);
	ASSERT_EQ(ret, 0);

	/* Verify the mount is noexec */
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_NOEXEC);
}

TEST_F(fsmount_ns_mount_attrs, nosuid)
{
	struct statmount sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC,
			       MOUNT_ATTR_NOSUID);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	ret = statmount(list[0], new_ns_id, 0, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0);
	ASSERT_EQ(ret, 0);

	/* Verify the mount is nosuid */
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_NOSUID);
}

TEST_F(fsmount_ns_mount_attrs, noatime)
{
	struct statmount sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC,
			       MOUNT_ATTR_NOATIME);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	ret = statmount(list[0], new_ns_id, 0, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0);
	ASSERT_EQ(ret, 0);

	/* Verify the mount is noatime */
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_NOATIME);
}

TEST_F(fsmount_ns_mount_attrs, combined)
{
	struct statmount sm;
	uint64_t new_ns_id;
	uint64_t list[256];
	ssize_t nr_mounts;
	int ret;

	self->fs_fd = create_tmpfs_fd();
	ASSERT_GE(self->fs_fd, 0);

	self->fd = sys_fsmount(self->fs_fd, FSMOUNT_NAMESPACE | FSMOUNT_CLOEXEC,
			       MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOEXEC |
			       MOUNT_ATTR_NOSUID | MOUNT_ATTR_NOATIME);
	if (self->fd < 0 && errno == EINVAL)
		SKIP(return, "FSMOUNT_NAMESPACE not supported");

	ASSERT_GE(self->fd, 0);

	ret = get_mnt_ns_id(self->fd, &new_ns_id);
	ASSERT_EQ(ret, 0);

	nr_mounts = listmount(LSMT_ROOT, new_ns_id, 0, list, 256, 0);
	ASSERT_GE(nr_mounts, 1);

	ret = statmount(list[0], new_ns_id, 0, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0);
	ASSERT_EQ(ret, 0);

	/* Verify all attributes are set */
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_RDONLY);
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_NOEXEC);
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_NOSUID);
	ASSERT_TRUE(sm.mnt_attr & MOUNT_ATTR_NOATIME);
}

TEST_HARNESS_MAIN
