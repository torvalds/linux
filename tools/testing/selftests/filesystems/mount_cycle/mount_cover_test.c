// SPDX-License-Identifier: GPL-2.0
/*
 * An unmounted mount that would have stayed attached to its unmounted parent
 * leaves a cover behind instead. A lookup on the parent at the mountpoint
 * finds knullfs: an empty read-only directory that is shared by every cover
 * and every kernel thread, so it can't be watched or locked and nothing can
 * be mounted on it. The mount itself is a root from then on. Where a file
 * was mounted, the stand-in is an empty regular file of the same instance.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fanotify.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <sys/wait.h>

#include "../../kselftest_harness.h"
#include "../readdir_hold.h"

#ifndef NULL_FS_MAGIC
#define NULL_FS_MAGIC 0x4E554C4C
#endif

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

#ifndef F_SETDELEG
#define F_SETDELEG	(F_SETLEASE + 16)
#endif

/* struct delegation of <linux/fcntl.h>, which doesn't mix with <fcntl.h> */
struct delegation_req {
	uint32_t d_flags;
	uint16_t d_type;
	uint16_t __pad;
};

#define DIR_LEN 64
#define PATH_LEN 128

/* what the parent asks the child to do */
#define CMD_RMDIR	'r'
#define CMD_CLOSE_T	't'
#define CMD_QUIT	'q'

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

static int touch(const char *path)
{
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fd < 0)
		return -1;
	close(fd);
	return 0;
}

/* Become root in a new user namespace with a private mount namespace. */
static int enter_userns(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();
	char map[32];

	prctl(PR_SET_DUMPABLE, 1);
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

/*
 * The child mounts P on @base/p, C on P/covered and the file P/src on P/file
 * in a mount namespace of its own, binds P a second time at @base/q and hands
 * out descriptors on P and on C. rmdir() of @base/p from here unmounts P
 * together with C and the file bind. P and C are held by the descriptors and
 * the unmounted children leave their covers behind. On request the child
 * removes C's mountpoint through the bind.
 *
 * It also mounts T on @base/t with a child on T/covered, binds T at @base/u
 * with a second child on the same dentry and hands out descriptors on T and
 * U. rmdir() of both from here leaves two covers on one mountpoint. On request
 * the child lets go of T.
 */
static int cover_child(const char *base, int to_parent, int from_parent)
{
	char p[PATH_LEN], c[PATH_LEN], q[PATH_LEN], qc[PATH_LEN];
	char f[PATH_LEN], src[PATH_LEN], t[PATH_LEN], u[PATH_LEN], tc[PATH_LEN];
	int fds[4], ret;
	char cmd;

	if (unshare(CLONE_NEWNS) || mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL))
		return 1;
	snprintf(p, sizeof(p), "%s/p", base);
	if (mkdir(p, 0755) || mount("tmpfs", p, "tmpfs", 0, NULL))
		return 2;
	snprintf(c, sizeof(c), "%s/p/covered", base);
	if (mkdir(c, 0755) || mount("tmpfs", c, "tmpfs", 0, NULL))
		return 3;
	snprintf(f, sizeof(f), "%s/p/file", base);
	snprintf(src, sizeof(src), "%s/p/src", base);
	if (touch(src) || touch(f) || mount(src, f, NULL, MS_BIND, NULL))
		return 4;
	snprintf(q, sizeof(q), "%s/q", base);
	if (mkdir(q, 0755) || mount(p, q, NULL, MS_BIND, NULL))
		return 5;
	snprintf(t, sizeof(t), "%s/t", base);
	if (mkdir(t, 0755) || mount("tmpfs", t, "tmpfs", 0, NULL))
		return 6;
	snprintf(tc, sizeof(tc), "%s/t/covered", base);
	if (mkdir(tc, 0755) || mount("tmpfs", tc, "tmpfs", 0, NULL))
		return 7;
	snprintf(u, sizeof(u), "%s/u", base);
	if (mkdir(u, 0755) || mount(t, u, NULL, MS_BIND, NULL))
		return 8;
	snprintf(tc, sizeof(tc), "%s/u/covered", base);
	if (mount("tmpfs", tc, "tmpfs", 0, NULL))
		return 9;
	fds[0] = open(p, O_PATH | O_DIRECTORY | O_CLOEXEC);
	fds[1] = open(c, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	fds[2] = open(t, O_PATH | O_DIRECTORY | O_CLOEXEC);
	fds[3] = open(u, O_PATH | O_DIRECTORY | O_CLOEXEC);
	if (fds[0] < 0 || fds[1] < 0 || fds[2] < 0 || fds[3] < 0)
		return 10;
	if (write(to_parent, fds, sizeof(fds)) != sizeof(fds))
		return 11;

	snprintf(qc, sizeof(qc), "%s/q/covered", base);
	for (;;) {
		if (read(from_parent, &cmd, 1) != 1)
			return 12;
		switch (cmd) {
		case CMD_RMDIR:
			ret = rmdir(qc) ? errno : 0;
			if (write(to_parent, &ret, sizeof(ret)) != sizeof(ret))
				return 13;
			break;
		case CMD_CLOSE_T:
			ret = close(fds[2]) ? errno : 0;
			if (write(to_parent, &ret, sizeof(ret)) != sizeof(ret))
				return 13;
			break;
		case CMD_QUIT:
			return 0;
		default:
			return 14;
		}
	}
}

FIXTURE(mount_cover) {
	char base[DIR_LEN];
	pid_t child;
	int to_child;
	int from_child;
	int dfd;	/* P, unmounted, held */
	int cfd;	/* C, unmounted, held */
	int fd;		/* what a lookup on P finds at C's mountpoint */
	int tfd;	/* T, unmounted, held */
	int ufd;	/* U, a bind of T, unmounted, held */
	int fan;	/* fanotify group from before the user namespace, or -1 */
	struct readdir_hold hold;	/* likewise from before, uffd -1 without */
};

/*
 * Everything the setup has made. The harness does not run the teardown
 * when an assertion of the setup fails, so the setup calls this itself.
 */
static void cover_cleanup(FIXTURE_DATA(mount_cover) *self)
{
	char cmd = CMD_QUIT;
	int status;

	if (self->fd >= 0)
		close(self->fd);
	if (self->cfd >= 0)
		close(self->cfd);
	if (self->dfd >= 0)
		close(self->dfd);
	if (self->tfd >= 0)
		close(self->tfd);
	if (self->ufd >= 0)
		close(self->ufd);
	if (self->fan >= 0)
		close(self->fan);
	readdir_hold_destroy(&self->hold);
	if (self->child > 0) {
		if (write(self->to_child, &cmd, 1) != 1)
			kill(self->child, SIGKILL);
		waitpid(self->child, &status, 0);
	}
	if (self->to_child >= 0)
		close(self->to_child);
	if (self->from_child >= 0)
		close(self->from_child);
	umount2(self->base, MNT_DETACH);
	rmdir(self->base);
}

static int same_file(int fd1, int fd2)
{
	struct stat st1, st2;

	if (fstat(fd1, &st1) || fstat(fd2, &st2))
		return 0;
	return st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino;
}

FIXTURE_SETUP(mount_cover)
{
	int to_parent[2], to_child[2], fds[4], pidfd, status;
	char dir[PATH_LEN];
	struct statfs sf;

	self->child = 0;
	self->to_child = self->from_child = -1;
	self->dfd = self->cfd = self->fd = self->tfd = self->ufd = -1;

	/*
	 * A group for plain events takes CAP_SYS_ADMIN in the initial user
	 * namespace, so get one before that is gone. An inode mark can be
	 * added to it from anywhere.
	 */
	self->fan = fanotify_init(FAN_CLASS_NOTIF | FAN_CLOEXEC, O_RDONLY);
	/* same for the userfaultfd that holds a readdir in its fault */
	readdir_hold_init(&self->hold);

	snprintf(self->base, sizeof(self->base), "/tmp/mount_cover.XXXXXX");
	ASSERT_NE(mkdtemp(self->base), NULL);
	if (enter_userns()) {
		cover_cleanup(self);
		SKIP(return, "test requires user namespaces");
	}
	ASSERT_EQ(mount("tmpfs", self->base, "tmpfs", 0, NULL), 0)
		cover_cleanup(self);

	snprintf(dir, sizeof(dir), "%s/p", self->base);
	ASSERT_EQ(pipe(to_parent), 0)
		cover_cleanup(self);
	ASSERT_EQ(pipe(to_child), 0)
		cover_cleanup(self);
	self->child = fork();
	ASSERT_GE(self->child, 0)
		cover_cleanup(self);
	if (self->child == 0) {
		close(to_parent[0]);
		close(to_child[1]);
		_exit(cover_child(self->base, to_parent[1], to_child[0]));
	}
	close(to_parent[1]);
	close(to_child[0]);
	self->to_child = to_child[1];
	self->from_child = to_parent[0];
	if (read(self->from_child, fds, sizeof(fds)) != sizeof(fds)) {
		pid_t pid = self->child;

		waitpid(pid, &status, 0);
		self->child = 0;
		cover_cleanup(self);
		ASSERT_TRUE(false)
			TH_LOG("child failed to set up: %s %d",
			       WIFEXITED(status) ? "step" : "signal",
			       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	}

	pidfd = syscall(__NR_pidfd_open, self->child, 0);
	ASSERT_GE(pidfd, 0)
		cover_cleanup(self);
	self->dfd = syscall(__NR_pidfd_getfd, pidfd, fds[0], 0);
	self->cfd = syscall(__NR_pidfd_getfd, pidfd, fds[1], 0);
	self->tfd = syscall(__NR_pidfd_getfd, pidfd, fds[2], 0);
	self->ufd = syscall(__NR_pidfd_getfd, pidfd, fds[3], 0);
	close(pidfd);
	ASSERT_GE(self->dfd, 0)
		cover_cleanup(self);
	ASSERT_GE(self->cfd, 0)
		cover_cleanup(self);
	ASSERT_GE(self->tfd, 0)
		cover_cleanup(self);
	ASSERT_GE(self->ufd, 0)
		cover_cleanup(self);

	/* unmounts P and C, C leaves its cover behind */
	ASSERT_EQ(rmdir(dir), 0)
		cover_cleanup(self);
	/* unmounts T and U with their children, two covers on one mountpoint */
	snprintf(dir, sizeof(dir), "%s/t", self->base);
	ASSERT_EQ(rmdir(dir), 0)
		cover_cleanup(self);
	snprintf(dir, sizeof(dir), "%s/u", self->base);
	ASSERT_EQ(rmdir(dir), 0)
		cover_cleanup(self);

	self->fd = openat(self->dfd, "covered", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(self->fd, 0)
		cover_cleanup(self);
	ASSERT_EQ(fstatfs(self->fd, &sf), 0)
		cover_cleanup(self);
	ASSERT_EQ(sf.f_type, NULL_FS_MAGIC)
		cover_cleanup(self);
}

FIXTURE_TEARDOWN(mount_cover)
{
	cover_cleanup(self);
}

TEST_F(mount_cover, not_watchable)
{
	char p[PATH_LEN];
	int ifd;

	snprintf(p, sizeof(p), "/proc/self/fd/%d", self->fd);

	ifd = inotify_init1(IN_CLOEXEC);
	if (ifd < 0 && errno == ENOSYS) {
		TH_LOG("no inotify in this kernel, skipping that part");
	} else {
		ASSERT_GE(ifd, 0);
		EXPECT_EQ(inotify_add_watch(ifd, p, IN_OPEN), -1);
		EXPECT_EQ(errno, EINVAL);
		close(ifd);
	}

	/* dnotify ends up at the same place */
	EXPECT_EQ(fcntl(self->fd, F_NOTIFY, DN_ACCESS), -1);
	EXPECT_EQ(errno, EINVAL);

	if (self->fan < 0) {
		TH_LOG("no fanotify group without CAP_SYS_ADMIN in the initial user namespace, skipping the fanotify part");
		return;
	}
	EXPECT_EQ(fanotify_mark(self->fan, FAN_MARK_ADD, FAN_OPEN, self->fd, NULL), -1);
	EXPECT_EQ(errno, EINVAL);
}

TEST_F(mount_cover, not_lockable)
{
	struct flock fl = {
		.l_type = F_RDLCK,
		.l_whence = SEEK_SET,
	};

	EXPECT_EQ(flock(self->fd, LOCK_EX | LOCK_NB), -1);
	EXPECT_EQ(errno, ENOLCK);
	EXPECT_EQ(fcntl(self->fd, F_SETLK, &fl), -1);
	EXPECT_EQ(errno, ENOLCK);
	EXPECT_EQ(fcntl(self->fd, F_GETLK, &fl), -1);
	EXPECT_EQ(errno, ENOLCK);
}

/* a lease is refused for the owner and for everybody else alike */
TEST_F(mount_cover, not_leasable)
{
	struct delegation_req deleg = {
		.d_type = F_RDLCK,
	};

	EXPECT_EQ(fcntl(self->fd, F_SETLEASE, F_RDLCK), -1);
	EXPECT_TRUE(errno == EINVAL || errno == EACCES);
	EXPECT_EQ(fcntl(self->fd, F_SETDELEG, &deleg), -1);
	EXPECT_TRUE(errno == EINVAL || errno == EACCES);
}

TEST_F(mount_cover, not_mountable)
{
	char p[PATH_LEN];

	snprintf(p, sizeof(p), "/proc/self/fd/%d", self->fd);
	EXPECT_EQ(mount("tmpfs", p, "tmpfs", 0, NULL), -1);
	EXPECT_EQ(errno, ENOENT);
}

TEST_F(mount_cover, read_only)
{
	struct statvfs sv;

	EXPECT_EQ(mkdirat(self->fd, "x", 0755), -1);
	EXPECT_EQ(errno, ENOENT);
	EXPECT_EQ(fchmod(self->fd, 0777), -1);
	EXPECT_EQ(errno, EROFS);
	/* the immutable inode is checked before the read-only mount */
	EXPECT_EQ(faccessat(self->fd, ".", W_OK, 0), -1);
	EXPECT_EQ(errno, EPERM);
	ASSERT_EQ(fstatvfs(self->fd, &sv), 0);
	EXPECT_TRUE(sv.f_flag & ST_RDONLY);
}

/* the stand-in is a root of its own, ".." stays put */
TEST_F(mount_cover, island)
{
	int fd;

	fd = openat(self->fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	EXPECT_TRUE(same_file(fd, self->fd));
	close(fd);
}

/*
 * C is alive for as long as the child holds it, but it can't be reached
 * through P anymore and it's a root of its own as well.
 */
TEST_F(mount_cover, held_child_detached)
{
	struct statfs sf;
	int fd;

	ASSERT_EQ(fstatfs(self->cfd, &sf), 0);
	EXPECT_EQ(sf.f_type, TMPFS_MAGIC);

	fd = openat(self->cfd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	EXPECT_TRUE(same_file(fd, self->cfd));
	close(fd);

	fd = openat(self->dfd, "covered", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(fstatfs(fd, &sf), 0);
	EXPECT_EQ(sf.f_type, NULL_FS_MAGIC);
	close(fd);
}

/*
 * The cover goes with its mountpoint: once the child has removed C's
 * mountpoint through the bind of P, the name is gone from P as well.
 * What was opened through the cover before stays open.
 */
TEST_F(mount_cover, cover_goes_with_mountpoint)
{
	char cmd = CMD_RMDIR;
	struct statfs sf;
	int ret;

	ASSERT_EQ(write(self->to_child, &cmd, 1), 1);
	ASSERT_EQ(read(self->from_child, &ret, sizeof(ret)), sizeof(ret));
	ASSERT_EQ(ret, 0);

	EXPECT_EQ(openat(self->dfd, "covered", O_RDONLY | O_DIRECTORY | O_CLOEXEC), -1);
	EXPECT_EQ(errno, ENOENT);
	ASSERT_EQ(fstatfs(self->fd, &sf), 0);
	EXPECT_EQ(sf.f_type, NULL_FS_MAGIC);
}

/*
 * A readdir of the stand-in holds nothing that others wait for: one holder
 * sticks in the page fault of its buffer, and a create and a lookup of
 * another come back meanwhile.
 */
TEST_F(mount_cover, readdir_blocks_nobody)
{
	bool stalled;

	if (self->hold.uffd < 0)
		SKIP(return, "test requires userfaultfd");
	ASSERT_EQ(readdir_hold_check(&self->hold, self->fd, &stalled), 0);
	EXPECT_FALSE(stalled);
}

/* where a file was mounted, the stand-in is an empty regular file */
TEST_F(mount_cover, file_stand_in)
{
	char src[PATH_LEN], p[PATH_LEN];
	struct statfs sf;
	struct stat st;
	char c;
	int fd;

	fd = openat(self->dfd, "file", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(fstat(fd, &st), 0);
	EXPECT_TRUE(S_ISREG(st.st_mode));
	ASSERT_EQ(fstatfs(fd, &sf), 0);
	EXPECT_EQ(sf.f_type, NULL_FS_MAGIC);
	/* the two stand-ins are two inodes */
	EXPECT_FALSE(same_file(fd, self->fd));
	EXPECT_EQ(read(fd, &c, 1), 0);
	EXPECT_EQ(flock(fd, LOCK_EX | LOCK_NB), -1);
	EXPECT_EQ(errno, ENOLCK);

	snprintf(src, sizeof(src), "%s/src", self->base);
	ASSERT_EQ(touch(src), 0);
	snprintf(p, sizeof(p), "/proc/self/fd/%d", fd);
	EXPECT_EQ(mount(src, p, NULL, MS_BIND, NULL), -1);
	EXPECT_EQ(errno, ENOENT);
	close(fd);

	/* writes and truncates go nowhere, like /dev/null */
	fd = openat(self->dfd, "file", O_WRONLY | O_TRUNC | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	EXPECT_EQ(write(fd, "xy", 2), 2);
	EXPECT_EQ(ftruncate(fd, 4), 0);
	ASSERT_EQ(fstat(fd, &st), 0);
	EXPECT_EQ(st.st_size, 0);
	/* the shared inode keeps its identity */
	EXPECT_EQ(fchmod(fd, 0600), -1);
	EXPECT_EQ(errno, EPERM);
	close(fd);
	fd = openat(self->dfd, "file", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	EXPECT_EQ(read(fd, &c, 1), 0);
	close(fd);

	EXPECT_EQ(openat(self->dfd, "file", O_RDONLY | O_DIRECTORY | O_CLOEXEC), -1);
	EXPECT_EQ(errno, ENOTDIR);
}

/*
 * Two unmounted parents left covers on the same dentry. A lookup on
 * either finds a stand-in, and U's cover stays when T goes.
 */
TEST_F(mount_cover, shared_mountpoint)
{
	char cmd = CMD_CLOSE_T;
	struct statfs sf;
	int fd, ret;

	fd = openat(self->tfd, "covered", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(fstatfs(fd, &sf), 0);
	EXPECT_EQ(sf.f_type, NULL_FS_MAGIC);
	close(fd);

	fd = openat(self->ufd, "covered", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(fstatfs(fd, &sf), 0);
	EXPECT_EQ(sf.f_type, NULL_FS_MAGIC);
	close(fd);

	/* the last references to T go, with T its cover */
	ASSERT_EQ(write(self->to_child, &cmd, 1), 1);
	ASSERT_EQ(read(self->from_child, &ret, sizeof(ret)), sizeof(ret));
	ASSERT_EQ(ret, 0);
	close(self->tfd);
	self->tfd = -1;

	fd = openat(self->ufd, "covered", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(fstatfs(fd, &sf), 0);
	EXPECT_EQ(sf.f_type, NULL_FS_MAGIC);
	close(fd);
}

TEST_HARNESS_MAIN
