// SPDX-License-Identifier: GPL-2.0
/*
 * A mount namespace file bind-mounted on top of the mount that is moved
 * onto a shared mount isn't copied to the peers and slaves. The mount that
 * already sits at the destination in a slave has to end up on top of the
 * propagated copy, not below the root of the mount namespace file where no
 * path walk ever finds it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "../wrappers.h"
#include "../../kselftest_harness.h"

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

static int create_file(const char *path, const char *s)
{
	ssize_t n = -1;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd >= 0) {
		n = write(fd, s, strlen(s));
		close(fd);
	}
	return n == (ssize_t)strlen(s) ? 0 : -1;
}

/* the first bytes of the file at @path, "" if it can't be read */
static const char *read_file(const char *path, char *buf, size_t len)
{
	ssize_t n = -1;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		n = read(fd, buf, len - 1);
		close(fd);
	}
	buf[n > 0 ? n : 0] = '\0';
	return buf;
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

FIXTURE(overmount_ns_file) {
	char dir[DIR_LEN];
	pid_t child;
};

FIXTURE_SETUP(overmount_ns_file)
{
	self->child = -1;
	snprintf(self->dir, sizeof(self->dir), "/tmp/overmount_ns_file.XXXXXX");
	ASSERT_NE(mkdtemp(self->dir), NULL);
	if (enter_userns()) {
		rmdir(self->dir);
		SKIP(return, "test requires user namespaces");
	}
	ASSERT_EQ(mount("tmpfs", self->dir, "tmpfs", 0, NULL), 0);
}

FIXTURE_TEARDOWN(overmount_ns_file)
{
	if (self->child > 0) {
		kill(self->child, SIGKILL);
		waitpid(self->child, NULL, 0);
	}
	umount2(self->dir, MNT_DETACH);
	rmdir(self->dir);
}

/*
 * A is a shared tmpfs and B its slave with Q, a bind mount of a file, on
 * B/file. S is a bind mount of a file with N, a bind mount of a newer mount
 * namespace's file, on top of it. S is moved onto A/file. Its copy S' lands
 * on B/file below Q, without N. B/file keeps reading Q and once Q is
 * unmounted it reads S'.
 */
TEST_F(overmount_ns_file, existing_mount_stays_on_top)
{
	char a[PATH_LEN], b[PATH_LEN], s[PATH_LEN], p[PATH_LEN], buf[16];
	int fd, pfd[2];
	char c;

	snprintf(a, sizeof(a), "%s/A", self->dir);
	snprintf(b, sizeof(b), "%s/B", self->dir);
	snprintf(s, sizeof(s), "%s/s", self->dir);
	ASSERT_EQ(mkdir(a, 0755), 0);
	ASSERT_EQ(mkdir(b, 0755), 0);
	ASSERT_EQ(mkdir(s, 0755), 0);

	/* A shared, B its slave, Q on B/file */
	ASSERT_EQ(mount("tmpfs", a, "tmpfs", 0, NULL), 0);
	ASSERT_EQ(mount(NULL, a, NULL, MS_SHARED, NULL), 0);
	snprintf(p, sizeof(p), "%s/A/file", self->dir);
	ASSERT_EQ(create_file(p, "A"), 0);
	ASSERT_EQ(mount(a, b, NULL, MS_BIND, NULL), 0);
	ASSERT_EQ(mount(NULL, b, NULL, MS_SLAVE, NULL), 0);
	snprintf(p, sizeof(p), "%s/Q", self->dir);
	ASSERT_EQ(create_file(p, "Q"), 0);
	snprintf(b, sizeof(b), "%s/B/file", self->dir);
	ASSERT_EQ(mount(p, b, NULL, MS_BIND, NULL), 0);

	/* S on s/f, pinned by a file descriptor before N goes on top */
	ASSERT_EQ(mount("tmpfs", s, "tmpfs", 0, NULL), 0);
	snprintf(p, sizeof(p), "%s/s/f", self->dir);
	ASSERT_EQ(create_file(p, "f"), 0);
	snprintf(s, sizeof(s), "%s/s/S", self->dir);
	ASSERT_EQ(create_file(s, "S"), 0);
	ASSERT_EQ(mount(s, p, NULL, MS_BIND, NULL), 0);
	fd = open(p, O_PATH | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	/* a newer mount namespace whose file can be bound */
	ASSERT_EQ(pipe(pfd), 0);
	self->child = fork();
	ASSERT_GE(self->child, 0);
	if (self->child == 0) {
		close(pfd[0]);
		if (unshare(CLONE_NEWNS) || write(pfd[1], "r", 1) != 1)
			_exit(1);
		pause();
		_exit(0);
	}
	close(pfd[1]);
	ASSERT_EQ(read(pfd[0], &c, 1), 1);
	close(pfd[0]);
	snprintf(s, sizeof(s), "/proc/%d/ns/mnt", self->child);
	ASSERT_EQ(mount(s, p, NULL, MS_BIND, NULL), 0);

	ASSERT_STREQ(read_file(b, buf, sizeof(buf)), "Q");

	snprintf(a, sizeof(a), "%s/A/file", self->dir);
	ASSERT_EQ(sys_move_mount(fd, "", AT_FDCWD, a, MOVE_MOUNT_F_EMPTY_PATH), 0);
	close(fd);

	/* Q is still on top of the copy in B and can be unmounted */
	EXPECT_STREQ(read_file(b, buf, sizeof(buf)), "Q");
	EXPECT_EQ(umount2(b, 0), 0);
	EXPECT_STREQ(read_file(b, buf, sizeof(buf)), "S");
}

TEST_HARNESS_MAIN
