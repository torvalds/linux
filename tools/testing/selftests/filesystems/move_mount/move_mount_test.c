// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Christian Brauner <brauner@kernel.org>

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "../wrappers.h"
#include "../utils.h"
#include "../statmount/statmount.h"
#include "../../kselftest_harness.h"

#include <linux/stat.h>

#ifndef MOVE_MOUNT_BENEATH
#define MOVE_MOUNT_BENEATH 0x00000200
#endif

static uint64_t get_unique_mnt_id_fd(int fd)
{
	struct statx sx;
	int ret;

	ret = statx(fd, "", AT_EMPTY_PATH, STATX_MNT_ID_UNIQUE, &sx);
	if (ret)
		return 0;

	if (!(sx.stx_mask & STATX_MNT_ID_UNIQUE))
		return 0;

	return sx.stx_mnt_id;
}

/*
 * Create a locked overmount stack at /mnt_dir for testing MNT_LOCKED
 * transfer on non-rootfs mounts.
 *
 * Mounts tmpfs A at /mnt_dir, overmounts with tmpfs B, then enters a
 * new user+mount namespace where both become locked. Returns the exit
 * code to use on failure, or 0 on success.
 */
static int setup_locked_overmount(void)
{
	/* Isolate so mounts don't leak. */
	if (unshare(CLONE_NEWNS))
		return 1;
	if (mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL))
		return 2;

	/*
	 * Create mounts while still in the initial user namespace so
	 * they become locked after the subsequent user namespace
	 * unshare.
	 */
	rmdir("/mnt_dir");
	if (mkdir("/mnt_dir", 0755))
		return 3;

	/* Mount tmpfs A */
	if (mount("tmpfs", "/mnt_dir", "tmpfs", 0, NULL))
		return 4;

	/* Overmount with tmpfs B */
	if (mount("tmpfs", "/mnt_dir", "tmpfs", 0, NULL))
		return 5;

	/*
	 * Create user+mount namespace. Mounts A and B become locked
	 * because they might be covering something that is not supposed
	 * to be revealed.
	 */
	if (setup_userns())
		return 6;

	/* Sanity check: B must be locked */
	if (!umount2("/mnt_dir", MNT_DETACH) || errno != EINVAL)
		return 7;

	return 0;
}

/*
 * Create a detached tmpfs mount and return its fd, or -1 on failure.
 */
static int create_detached_tmpfs(void)
{
	int fs_fd, mnt_fd;

	fs_fd = sys_fsopen("tmpfs", FSOPEN_CLOEXEC);
	if (fs_fd < 0)
		return -1;

	if (sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0)) {
		close(fs_fd);
		return -1;
	}

	mnt_fd = sys_fsmount(fs_fd, FSMOUNT_CLOEXEC, 0);
	close(fs_fd);
	return mnt_fd;
}

FIXTURE(move_mount) {
	uint64_t orig_root_id;
};

FIXTURE_SETUP(move_mount)
{
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);

	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	self->orig_root_id = get_unique_mnt_id("/");
	ASSERT_NE(self->orig_root_id, 0);
}

FIXTURE_TEARDOWN(move_mount)
{
}

/*
 * Test successful MOVE_MOUNT_BENEATH on the rootfs.
 * Mount a clone beneath /, fchdir to the clone, chroot to switch root,
 * then detach the old root.
 */
TEST_F(move_mount, beneath_rootfs_success)
{
	int fd_tree, ret;
	uint64_t clone_id, root_id;

	fd_tree = sys_open_tree(AT_FDCWD, "/",
				OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(fd_tree, 0);

	clone_id = get_unique_mnt_id_fd(fd_tree);
	ASSERT_NE(clone_id, 0);
	ASSERT_NE(clone_id, self->orig_root_id);

	ASSERT_EQ(fchdir(fd_tree), 0);

	ret = sys_move_mount(fd_tree, "", AT_FDCWD, "/",
			     MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, 0);

	close(fd_tree);

	/* Switch root to the clone */
	ASSERT_EQ(chroot("."), 0);

	/* Verify "/" is now the clone */
	root_id = get_unique_mnt_id("/");
	ASSERT_NE(root_id, 0);
	ASSERT_EQ(root_id, clone_id);

	/* Detach old root */
	ASSERT_EQ(umount2(".", MNT_DETACH), 0);
}

/*
 * Test that after MOVE_MOUNT_BENEATH on the rootfs the old root is
 * stacked on top of the clone. Verify via statmount that the old
 * root's parent is the clone.
 */
TEST_F(move_mount, beneath_rootfs_old_root_stacked)
{
	int fd_tree, ret;
	uint64_t clone_id;
	struct statmount sm;

	fd_tree = sys_open_tree(AT_FDCWD, "/",
				OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(fd_tree, 0);

	clone_id = get_unique_mnt_id_fd(fd_tree);
	ASSERT_NE(clone_id, 0);
	ASSERT_NE(clone_id, self->orig_root_id);

	ASSERT_EQ(fchdir(fd_tree), 0);

	ret = sys_move_mount(fd_tree, "", AT_FDCWD, "/",
			     MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, 0);

	close(fd_tree);

	ASSERT_EQ(chroot("."), 0);

	/* Old root's parent should now be the clone */
	ASSERT_EQ(statmount(self->orig_root_id, 0, 0,
			     STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0), 0);
	ASSERT_EQ(sm.mnt_parent_id, clone_id);

	ASSERT_EQ(umount2(".", MNT_DETACH), 0);
}

/*
 * Test that MOVE_MOUNT_BENEATH on rootfs fails when chroot'd into a
 * subdirectory of the same mount. The caller's fs->root.dentry doesn't
 * match mnt->mnt_root so the kernel rejects it.
 */
TEST_F(move_mount, beneath_rootfs_in_chroot_fail)
{
	int fd_tree, ret;
	uint64_t chroot_id, clone_id;

	rmdir("/chroot_dir");
	ASSERT_EQ(mkdir("/chroot_dir", 0755), 0);

	chroot_id = get_unique_mnt_id("/chroot_dir");
	ASSERT_NE(chroot_id, 0);
	ASSERT_EQ(self->orig_root_id, chroot_id);

	ASSERT_EQ(chdir("/chroot_dir"), 0);
	ASSERT_EQ(chroot("."), 0);

	fd_tree = sys_open_tree(AT_FDCWD, "/",
				OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(fd_tree, 0);

	clone_id = get_unique_mnt_id_fd(fd_tree);
	ASSERT_NE(clone_id, 0);
	ASSERT_NE(clone_id, chroot_id);

	ASSERT_EQ(fchdir(fd_tree), 0);

	/*
	 * Should fail: fs->root.dentry (/chroot_dir) doesn't match
	 * the mount's mnt_root (/).
	 */
	ret = sys_move_mount(fd_tree, "", AT_FDCWD, "/",
			     MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EINVAL);

	close(fd_tree);
}

/*
 * Test that MOVE_MOUNT_BENEATH on rootfs succeeds when chroot'd into a
 * separate tmpfs mount. The caller's root dentry matches the mount's
 * mnt_root since it's a dedicated mount.
 */
TEST_F(move_mount, beneath_rootfs_in_chroot_success)
{
	int fd_tree, ret;
	uint64_t chroot_id, clone_id, root_id;
	struct statmount sm;

	rmdir("/chroot_dir");
	ASSERT_EQ(mkdir("/chroot_dir", 0755), 0);
	ASSERT_EQ(mount("tmpfs", "/chroot_dir", "tmpfs", 0, NULL), 0);

	chroot_id = get_unique_mnt_id("/chroot_dir");
	ASSERT_NE(chroot_id, 0);

	ASSERT_EQ(chdir("/chroot_dir"), 0);
	ASSERT_EQ(chroot("."), 0);

	ASSERT_EQ(get_unique_mnt_id("/"), chroot_id);

	fd_tree = sys_open_tree(AT_FDCWD, "/",
				OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(fd_tree, 0);

	clone_id = get_unique_mnt_id_fd(fd_tree);
	ASSERT_NE(clone_id, 0);
	ASSERT_NE(clone_id, chroot_id);

	ASSERT_EQ(fchdir(fd_tree), 0);

	ret = sys_move_mount(fd_tree, "", AT_FDCWD, "/",
			     MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, 0);

	close(fd_tree);

	ASSERT_EQ(chroot("."), 0);

	root_id = get_unique_mnt_id("/");
	ASSERT_NE(root_id, 0);
	ASSERT_EQ(root_id, clone_id);

	ASSERT_EQ(statmount(chroot_id, 0, 0,
			     STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0), 0);
	ASSERT_EQ(sm.mnt_parent_id, clone_id);

	ASSERT_EQ(umount2(".", MNT_DETACH), 0);
}

/*
 * Test MNT_LOCKED transfer when mounting beneath rootfs in a user+mount
 * namespace. After mount-beneath the new root gets MNT_LOCKED and the
 * old root has MNT_LOCKED cleared so it can be unmounted.
 */
TEST_F(move_mount, beneath_rootfs_locked_transfer)
{
	int fd_tree, ret;
	uint64_t clone_id, root_id;

	ASSERT_EQ(setup_userns(), 0);

	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	fd_tree = sys_open_tree(AT_FDCWD, "/",
				OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC |
				AT_RECURSIVE);
	ASSERT_GE(fd_tree, 0);

	clone_id = get_unique_mnt_id_fd(fd_tree);
	ASSERT_NE(clone_id, 0);

	ASSERT_EQ(fchdir(fd_tree), 0);

	ret = sys_move_mount(fd_tree, "", AT_FDCWD, "/",
			     MOVE_MOUNT_F_EMPTY_PATH |
			     MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, 0);

	close(fd_tree);

	ASSERT_EQ(chroot("."), 0);

	root_id = get_unique_mnt_id("/");
	ASSERT_EQ(root_id, clone_id);

	/*
	 * The old root should be unmountable (MNT_LOCKED was
	 * transferred to the clone). If MNT_LOCKED wasn't
	 * cleared, this would fail with EINVAL.
	 */
	ASSERT_EQ(umount2(".", MNT_DETACH), 0);

	/* Verify "/" is still the clone after detaching old root */
	root_id = get_unique_mnt_id("/");
	ASSERT_EQ(root_id, clone_id);
}

/*
 * Test containment invariant: after mount-beneath rootfs in a user+mount
 * namespace, the new root must be MNT_LOCKED. The lock transfer from the
 * old root preserves containment -- the process cannot unmount the new root
 * to escape the namespace.
 */
TEST_F(move_mount, beneath_rootfs_locked_containment)
{
	int fd_tree, ret;
	uint64_t clone_id, root_id;

	ASSERT_EQ(setup_userns(), 0);

	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	/* Sanity: rootfs must be locked in the new userns */
	ASSERT_EQ(umount2("/", MNT_DETACH), -1);
	ASSERT_EQ(errno, EINVAL);

	fd_tree = sys_open_tree(AT_FDCWD, "/",
				OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC |
				AT_RECURSIVE);
	ASSERT_GE(fd_tree, 0);

	clone_id = get_unique_mnt_id_fd(fd_tree);
	ASSERT_NE(clone_id, 0);

	ASSERT_EQ(fchdir(fd_tree), 0);

	ret = sys_move_mount(fd_tree, "", AT_FDCWD, "/",
			     MOVE_MOUNT_F_EMPTY_PATH |
			     MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, 0);

	close(fd_tree);

	ASSERT_EQ(chroot("."), 0);

	root_id = get_unique_mnt_id("/");
	ASSERT_EQ(root_id, clone_id);

	/* Detach old root (MNT_LOCKED was cleared from it) */
	ASSERT_EQ(umount2(".", MNT_DETACH), 0);

	/* Verify "/" is still the clone after detaching old root */
	root_id = get_unique_mnt_id("/");
	ASSERT_EQ(root_id, clone_id);

	/*
	 * The new root must be locked (MNT_LOCKED was transferred
	 * from the old root). Attempting to unmount it must fail
	 * with EINVAL, preserving the containment invariant.
	 */
	ASSERT_EQ(umount2("/", MNT_DETACH), -1);
	ASSERT_EQ(errno, EINVAL);
}

/*
 * Test MNT_LOCKED transfer when mounting beneath a non-rootfs locked mount.
 * Mounts created before unshare(CLONE_NEWUSER | CLONE_NEWNS) become locked
 * in the new namespace. Mount-beneath transfers the lock from the displaced
 * mount to the new mount, so the displaced mount can be unmounted.
 */
TEST_F(move_mount, beneath_non_rootfs_locked_transfer)
{
	int mnt_fd, ret;
	uint64_t mnt_new_id, mnt_visible_id;

	ASSERT_EQ(setup_locked_overmount(), 0);

	mnt_fd = create_detached_tmpfs();
	ASSERT_GE(mnt_fd, 0);

	mnt_new_id = get_unique_mnt_id_fd(mnt_fd);
	ASSERT_NE(mnt_new_id, 0);

	/* Move mount beneath B (which is locked) */
	ret = sys_move_mount(mnt_fd, "", AT_FDCWD, "/mnt_dir",
			     MOVE_MOUNT_F_EMPTY_PATH |
			     MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, 0);

	close(mnt_fd);

	/*
	 * B should now be unmountable (MNT_LOCKED was transferred
	 * to the new mount beneath it). If MNT_LOCKED wasn't
	 * cleared from B, this would fail with EINVAL.
	 */
	ASSERT_EQ(umount2("/mnt_dir", MNT_DETACH), 0);

	/* Verify the new mount is now visible */
	mnt_visible_id = get_unique_mnt_id("/mnt_dir");
	ASSERT_EQ(mnt_visible_id, mnt_new_id);
}

/*
 * Test MNT_LOCKED containment when mounting beneath a non-rootfs mount
 * that was locked during unshare(CLONE_NEWUSER | CLONE_NEWNS).
 * Mounts created before unshare become locked in the new namespace.
 * Mount-beneath transfers the lock, preserving containment: the new
 * mount cannot be unmounted, but the displaced mount can.
 */
TEST_F(move_mount, beneath_non_rootfs_locked_containment)
{
	int mnt_fd, ret;
	uint64_t mnt_new_id, mnt_visible_id;

	ASSERT_EQ(setup_locked_overmount(), 0);

	mnt_fd = create_detached_tmpfs();
	ASSERT_GE(mnt_fd, 0);

	mnt_new_id = get_unique_mnt_id_fd(mnt_fd);
	ASSERT_NE(mnt_new_id, 0);

	/*
	 * Move new tmpfs beneath B at /mnt_dir.
	 * Stack becomes: A -> new -> B
	 * Lock transfers from B to new.
	 */
	ret = sys_move_mount(mnt_fd, "", AT_FDCWD, "/mnt_dir",
			     MOVE_MOUNT_F_EMPTY_PATH |
			     MOVE_MOUNT_BENEATH);
	ASSERT_EQ(ret, 0);

	close(mnt_fd);

	/*
	 * B lost MNT_LOCKED -- unmounting it must succeed.
	 * This reveals the new mount at /mnt_dir.
	 */
	ASSERT_EQ(umount2("/mnt_dir", MNT_DETACH), 0);

	/* Verify the new mount is now visible */
	mnt_visible_id = get_unique_mnt_id("/mnt_dir");
	ASSERT_EQ(mnt_visible_id, mnt_new_id);

	/*
	 * The new mount gained MNT_LOCKED -- unmounting it must
	 * fail with EINVAL, preserving the containment invariant.
	 */
	ASSERT_EQ(umount2("/mnt_dir", MNT_DETACH), -1);
	ASSERT_EQ(errno, EINVAL);
}

TEST_HARNESS_MAIN
