// SPDX-License-Identifier: GPL-2.0
/*
 * A synchronous umount first unmounts the shrinkable submounts of the
 * victim that aren't busy. Every one of them has to be checked right
 * before it is unmounted: unmounting one can slide a busy mount to where
 * the propagated copy of the next one is looked up.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fanotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/vfs.h>
#include <linux/magic.h>

#include "../../kselftest_harness.h"

#ifndef FAN_REPORT_MNT
#define FAN_REPORT_MNT		0x00004000
#endif
#ifndef FAN_MARK_MNTNS
#define FAN_MARK_MNTNS		0x00000110
#endif
#ifndef FAN_MNT_ATTACH
#define FAN_MNT_ATTACH		0x01000000
#endif
#ifndef FAN_MNT_DETACH
#define FAN_MNT_DETACH		0x02000000
#endif

#define DIR_LEN 64
#define PATH_LEN 128

FIXTURE(shrink_submounts) {
	char base[DIR_LEN];
	char automount[PATH_LEN];
	bool mounted;
	int fan;
};

/*
 * Shrinkable mounts come from an automount. The tracefs mount below debugfs
 * is one and bind mounts inherit the flag.
 */
FIXTURE_SETUP(shrink_submounts)
{
	struct stat st;
	char p[PATH_LEN];

	self->mounted = false;
	self->fan = -1;

	if (geteuid() != 0)
		SKIP(return, "test requires CAP_SYS_ADMIN");

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	snprintf(self->base, sizeof(self->base), "/tmp/shrink_submounts.XXXXXX");
	ASSERT_NE(mkdtemp(self->base), NULL);
	ASSERT_EQ(mount("tmpfs", self->base, "tmpfs", 0, NULL), 0);
	self->mounted = true;
	ASSERT_EQ(mount(NULL, self->base, NULL, MS_PRIVATE, NULL), 0);

	snprintf(p, sizeof(p), "%s/dbg", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	if (mount("debugfs", p, "debugfs", 0, NULL)) {
		umount2(self->base, MNT_DETACH);
		rmdir(self->base);
		SKIP(return, "test requires debugfs");
	}
	snprintf(self->automount, sizeof(self->automount), "%s/dbg/tracing",
		 self->base);
	snprintf(p, sizeof(p), "%s/dbg/tracing/.", self->base);
	if (stat(p, &st)) {
		umount2(self->base, MNT_DETACH);
		rmdir(self->base);
		SKIP(return, "test requires the tracefs automount");
	}
}

FIXTURE_TEARDOWN(shrink_submounts)
{
	if (self->fan >= 0)
		close(self->fan);
	chdir("/");
	if (self->mounted)
		umount2(self->base, MNT_DETACH);
	rmdir(self->base);
}

static bool mounted_tmpfs(const char *path)
{
	struct statfs st;

	return !statfs(path, &st) && st.f_type == TMPFS_MAGIC;
}

/*
 * P is a shared bind mount of the automount, P1 a slave of P that is shared
 * in turn and P2 its peer. B, another bind mount of the automount, goes on
 * P/options and propagates copies Bc1 and Bc2 onto P1/options and
 * P2/options. R, a tmpfs and our working directory, sits on top of Bc2 which
 * is made private first. P, with B on it, is moved to P1/instances.
 *
 * A synchronous umount of P1 unmounts the shrinkable submounts Bc1 and B
 * first. Unmounting Bc1 takes Bc2 along and slides R to P2/options where
 * the propagated copy of B is looked up next. R is busy, so B has to stay
 * and the umount fails with EBUSY.
 */
TEST_F(shrink_submounts, busy_mount_moved_into_reach)
{
	char p[PATH_LEN], p1[PATH_LEN], p2[PATH_LEN], r[PATH_LEN], cwd[PATH_LEN];
	int nsfd;

	snprintf(p, sizeof(p), "%s/p", self->base);
	snprintf(p1, sizeof(p1), "%s/p1", self->base);
	snprintf(p2, sizeof(p2), "%s/p2", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	ASSERT_EQ(mkdir(p1, 0755), 0);
	ASSERT_EQ(mkdir(p2, 0755), 0);

	/* watch the mount namespace so that the detached mounts get queued */
	self->fan = fanotify_init(FAN_REPORT_MNT, O_RDONLY);
	if (self->fan >= 0) {
		nsfd = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
		ASSERT_GE(nsfd, 0);
		EXPECT_EQ(fanotify_mark(self->fan, FAN_MARK_ADD | FAN_MARK_MNTNS,
					FAN_MNT_ATTACH | FAN_MNT_DETACH, nsfd, NULL), 0);
		close(nsfd);
	}

	ASSERT_EQ(mount(self->automount, p, NULL, MS_BIND, NULL), 0);
	ASSERT_EQ(mount(NULL, p, NULL, MS_SHARED, NULL), 0);
	ASSERT_EQ(mount(p, p1, NULL, MS_BIND, NULL), 0);
	ASSERT_EQ(mount(NULL, p1, NULL, MS_SLAVE, NULL), 0);
	ASSERT_EQ(mount(NULL, p1, NULL, MS_SHARED, NULL), 0);
	ASSERT_EQ(mount(p1, p2, NULL, MS_BIND, NULL), 0);

	/* B on P/options, copies on P1/options and P2/options */
	snprintf(r, sizeof(r), "%s/p/options", self->base);
	ASSERT_EQ(mount(self->automount, r, NULL, MS_BIND, NULL), 0);

	/* R on top of Bc2 */
	snprintf(r, sizeof(r), "%s/p2/options", self->base);
	ASSERT_EQ(mount(NULL, r, NULL, MS_PRIVATE, NULL), 0);
	ASSERT_EQ(mount("R", r, "tmpfs", 0, NULL), 0);
	ASSERT_EQ(chdir(r), 0);

	/* P, with B on it, below the victim */
	snprintf(cwd, sizeof(cwd), "%s/p1/instances", self->base);
	ASSERT_EQ(mount(p, cwd, NULL, MS_MOVE, NULL), 0);

	ASSERT_TRUE(mounted_tmpfs(r));
	ASSERT_EQ(umount2(p1, 0), -1);
	EXPECT_EQ(errno, EBUSY);

	/* R is still mounted and still our working directory */
	EXPECT_TRUE(mounted_tmpfs(r));
	ASSERT_NE(getcwd(cwd, sizeof(cwd)), NULL);
	EXPECT_STREQ(cwd, r);
}

/*
 * T is shared and Q, a slave of T, has T moved into it, so Q receives
 * propagation from its own child. M, another bind mount of the automount, is
 * on T/options and that is the dentry T sits on in Q. Unmounting M makes T
 * the propagated victim at that dentry in Q and takes T along. The shrink
 * walk of V has just unmounted M and continues in the children of T.
 */
TEST_F(shrink_submounts, parent_goes_with_child)
{
	char v[PATH_LEN], t[PATH_LEN], q[PATH_LEN], p[PATH_LEN];
	struct stat before, after;

	snprintf(v, sizeof(v), "%s/v", self->base);
	snprintf(t, sizeof(t), "%s/v/t", self->base);
	snprintf(q, sizeof(q), "%s/v/q", self->base);
	ASSERT_EQ(mkdir(v, 0755), 0);
	ASSERT_EQ(mount("V", v, "tmpfs", 0, NULL), 0);
	ASSERT_EQ(mount(NULL, v, NULL, MS_PRIVATE, NULL), 0);
	ASSERT_EQ(stat(v, &before), 0);
	ASSERT_EQ(mkdir(t, 0755), 0);
	ASSERT_EQ(mkdir(q, 0755), 0);

	/* T shared, M on T/options before anything receives from T */
	ASSERT_EQ(mount(self->automount, t, NULL, MS_BIND, NULL), 0);
	ASSERT_EQ(mount(NULL, t, NULL, MS_SHARED, NULL), 0);
	snprintf(p, sizeof(p), "%s/v/t/options", self->base);
	ASSERT_EQ(mount(self->automount, p, NULL, MS_BIND, NULL), 0);

	/* Q, a slave of T, and T moved into Q at the dentry M sits on */
	ASSERT_EQ(mount(t, q, NULL, MS_BIND, NULL), 0);
	ASSERT_EQ(mount(NULL, q, NULL, MS_SLAVE, NULL), 0);
	snprintf(p, sizeof(p), "%s/v/q/options", self->base);
	ASSERT_EQ(mount(t, p, NULL, MS_MOVE, NULL), 0);

	/* M, T and then Q go, V is empty and can be unmounted */
	ASSERT_EQ(umount2(v, 0), 0);
	ASSERT_EQ(stat(v, &after), 0);
	EXPECT_NE(before.st_dev, after.st_dev);
}

TEST_HARNESS_MAIN
