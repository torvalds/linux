// SPDX-License-Identifier: GPL-2.0
/*
 * propagate_umount() slides a surviving overmount off a mount that is being
 * unmounted and unmounts that mount right after. If a file descriptor keeps
 * the unmounted mount alive its ->overmount is left pointing at the moved
 * mount, which is freed on its own schedule. move_mount(MOVE_MOUNT_BENEATH)
 * with such a file descriptor as the target walks ->overmount in
 * topmost_overmount() before it checks that the target is still mounted and
 * must not step into the freed mount.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "../wrappers.h"
#include "../../kselftest_harness.h"

#ifndef MOVE_MOUNT_BENEATH
#define MOVE_MOUNT_BENEATH 0x00000200
#endif

#ifndef FSCONFIG_CMD_CREATE
#define FSCONFIG_CMD_CREATE 6
#endif

#define DIR_LEN 64
#define PATH_LEN 128

static int write_file(const char *path, const char *s)
{
	ssize_t n = -1;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		n = write(fd, s, strlen(s));
		close(fd);
	}
	return n == (ssize_t)strlen(s) ? 0 : -1;
}

/* Become root in a new user namespace with a private mount namespace. */
static int enter_userns(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();
	char map[32];

	if (unshare(CLONE_NEWUSER | CLONE_NEWNS))
		return -1;
	if (write_file("/proc/self/setgroups", "deny") && errno != ENOENT)
		return -1;
	snprintf(map, sizeof(map), "0 %d 1", uid);
	if (write_file("/proc/self/uid_map", map))
		return -1;
	snprintf(map, sizeof(map), "0 %d 1", gid);
	if (write_file("/proc/self/gid_map", map))
		return -1;
	if (setgid(0) || setuid(0))
		return -1;
	return mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL);
}

/* A detached tmpfs mount to serve as the source of a move. */
static int detached_tmpfs(void)
{
	int sfd, mfd;

	sfd = sys_fsopen("tmpfs", 0);
	if (sfd < 0)
		return -1;
	if (sys_fsconfig(sfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0)) {
		close(sfd);
		return -1;
	}
	mfd = sys_fsmount(sfd, 0, 0);
	close(sfd);
	return mfd;
}

FIXTURE(overmount_reparent) {
	char dir[DIR_LEN];
};

FIXTURE_SETUP(overmount_reparent)
{
	snprintf(self->dir, sizeof(self->dir), "/tmp/overmount_reparent.XXXXXX");
	ASSERT_NE(mkdtemp(self->dir), NULL);
	if (enter_userns()) {
		rmdir(self->dir);
		SKIP(return, "test requires user namespaces");
	}
}

FIXTURE_TEARDOWN(overmount_reparent)
{
	char p[PATH_LEN];

	snprintf(p, sizeof(p), "%s/b", self->dir);
	umount2(p, MNT_DETACH);
	snprintf(p, sizeof(p), "%s/a", self->dir);
	umount2(p, MNT_DETACH);
}

/*
 * A shared mount base_a is bind-mounted to base_b as its peer. A mount X on
 * base_a/mp propagates a copy X_b onto base_b/mp. A file descriptor pins X_b,
 * then X_b is made private and an overmount is stacked on its root.
 *
 * A lazy unmount of X propagates to X_b: X_b is committed to the unmount while
 * its overmount survives, so propagate_umount() reparents the overmount onto
 * base_b and unmounts X_b, which the file descriptor keeps alive. Unmounting
 * the reparented overmount frees it. X_b->overmount now dangles unless
 * mnt_change_mountpoint() reset it.
 *
 * move_mount(MOVE_MOUNT_BENEATH) through the file descriptor makes
 * do_lock_mount() follow X_b->overmount in topmost_overmount() before it
 * notices that X_b is no longer mounted. With the pointer reset the move fails
 * cleanly with ENOENT; otherwise it reads the freed overmount.
 */
TEST_F(overmount_reparent, dead_overmount_holder_not_followed)
{
	char a[PATH_LEN], b[PATH_LEN], amp[PATH_LEN], bmp[PATH_LEN];
	int fd, mfd, ret;

	snprintf(a, sizeof(a), "%s/a", self->dir);
	snprintf(b, sizeof(b), "%s/b", self->dir);
	ASSERT_EQ(mkdir(a, 0755), 0);
	ASSERT_EQ(mkdir(b, 0755), 0);

	ASSERT_EQ(mount("tmpfs", a, "tmpfs", 0, NULL), 0);
	ASSERT_EQ(mount(NULL, a, NULL, MS_SHARED, NULL), 0);
	ASSERT_EQ(mount(a, b, NULL, MS_BIND, NULL), 0);

	snprintf(amp, sizeof(amp), "%s/a/mp", self->dir);
	snprintf(bmp, sizeof(bmp), "%s/b/mp", self->dir);
	/* base_a and base_b share one superblock, so this dir is in both. */
	ASSERT_EQ(mkdir(amp, 0755), 0);

	/* X on base_a/mp propagates a copy X_b onto base_b/mp. */
	ASSERT_EQ(mount("tmpfs", amp, "tmpfs", 0, NULL), 0);

	/* Pin X_b before anything is stacked on top of it. */
	fd = open(bmp, O_PATH | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	/* Keep the overmount local to X_b. */
	ASSERT_EQ(mount(NULL, bmp, NULL, MS_PRIVATE, NULL), 0);

	/* The overmount on X_b's root: X_b->overmount points at it. */
	ASSERT_EQ(mount("tmpfs", bmp, "tmpfs", 0, NULL), 0);

	/* Reparents the overmount onto base_b and unmounts X_b. */
	ASSERT_EQ(umount2(amp, MNT_DETACH), 0);

	/* Free the reparented overmount. */
	ASSERT_EQ(umount2(bmp, MNT_DETACH), 0);
	usleep(100000);

	mfd = detached_tmpfs();
	ASSERT_GE(mfd, 0);

	ret = sys_move_mount(mfd, "", fd, "",
			     MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_T_EMPTY_PATH |
			     MOVE_MOUNT_BENEATH);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);

	close(mfd);
	close(fd);
}

TEST_HARNESS_MAIN
