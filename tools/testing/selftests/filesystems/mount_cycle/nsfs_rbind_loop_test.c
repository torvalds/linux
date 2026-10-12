// SPDX-License-Identifier: GPL-2.0
/*
 * A recursive bind mount of a namespace file that lives in another mount
 * namespace copies whatever is stacked on top of it there. If that includes
 * the file of the caller's own mount namespace, or of an older one, the copy
 * would pin the namespace it is put in forever. The bind mount has to be
 * refused, a plain bind mount of the file itself still works.
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
#include <sys/wait.h>

#include "../../kselftest_harness.h"

#define DIR_LEN 64
#define PATH_LEN 128

/* Child exit codes. */
enum {
	CHILD_OK,
	CHILD_UNSHARE,		/* could not create the newer mount namespace */
	CHILD_PIPE,		/* the parent went away */
	CHILD_TMPFS,		/* could not mount the tmpfs in the new namespace */
	CHILD_REC_ALLOWED,	/* the recursive bind mount was not refused */
	CHILD_REC_ERRNO,	/* it was refused with the wrong error */
	CHILD_PLAIN_REFUSED,	/* the plain bind mount of the file was refused */
};

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

static int create_file(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);

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

static int send_msg(int fd, char msg)
{
	return write(fd, &msg, 1) == 1 ? 0 : -1;
}

static char recv_msg(int fd)
{
	char msg;

	if (read(fd, &msg, 1) != 1)
		return 0;
	return msg;
}

FIXTURE(nsfs_rbind_loop) {
	char dir[DIR_LEN];
	char x[PATH_LEN];
};

FIXTURE_SETUP(nsfs_rbind_loop)
{
	snprintf(self->dir, sizeof(self->dir), "/tmp/nsfs_rbind_loop.XXXXXX");
	ASSERT_NE(mkdtemp(self->dir), NULL);
	if (enter_userns()) {
		rmdir(self->dir);
		SKIP(return, "test requires user namespaces");
	}
	ASSERT_EQ(mount("tmpfs", self->dir, "tmpfs", 0, NULL), 0);
	snprintf(self->x, sizeof(self->x), "%s/x", self->dir);
	ASSERT_EQ(create_file(self->x), 0);
}

FIXTURE_TEARDOWN(nsfs_rbind_loop)
{
	umount2(self->dir, MNT_DETACH);
	rmdir(self->dir);
}

/*
 * The child in the newer mount namespace binds the network namespace file
 * mount of the parent through @fd. Recursively that would copy the mount of
 * its own mount namespace file that the parent stacked on top.
 */
static int newer_ns_child(const char *dir, int fd, int to_parent, int from_parent)
{
	char src[32], y[PATH_LEN];

	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	if (send_msg(to_parent, 'r') || recv_msg(from_parent) != 'g')
		return CHILD_PIPE;

	snprintf(y, sizeof(y), "%s/y", dir);
	if (mount("tmpfs", y, "tmpfs", 0, NULL))
		return CHILD_TMPFS;
	snprintf(src, sizeof(src), "/proc/self/fd/%d", fd);
	snprintf(y, sizeof(y), "%s/y/f", dir);
	if (create_file(y))
		return CHILD_TMPFS;

	if (!mount(src, y, NULL, MS_BIND | MS_REC, NULL))
		return CHILD_REC_ALLOWED;
	if (errno != EINVAL)
		return CHILD_REC_ERRNO;
	if (mount(src, y, NULL, MS_BIND, NULL))
		return CHILD_PLAIN_REFUSED;
	umount2(y, MNT_DETACH);
	return CHILD_OK;
}

TEST_F(nsfs_rbind_loop, own_ns_file_below_foreign_source)
{
	int to_child[2], to_parent[2], fd, status;
	char p[PATH_LEN];
	pid_t pid;

	snprintf(p, sizeof(p), "%s/y", self->dir);
	ASSERT_EQ(mkdir(p, 0755), 0);

	/* M, a mount of our network namespace file, held by a descriptor */
	ASSERT_EQ(mount("/proc/self/ns/net", self->x, NULL, MS_BIND, NULL), 0);
	fd = open(self->x, O_PATH | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	ASSERT_EQ(pipe(to_child), 0);
	ASSERT_EQ(pipe(to_parent), 0);
	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		close(to_child[1]);
		close(to_parent[0]);
		_exit(newer_ns_child(self->dir, fd, to_parent[1], to_child[0]));
	}
	close(to_child[0]);
	close(to_parent[1]);
	ASSERT_EQ(recv_msg(to_parent[0]), 'r');

	/* the child's mount namespace file on top of M */
	snprintf(p, sizeof(p), "/proc/%d/ns/mnt", pid);
	ASSERT_EQ(mount(p, self->x, NULL, MS_BIND, NULL), 0);

	ASSERT_EQ(send_msg(to_child[1], 'g'), 0);
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), CHILD_OK);

	close(fd);
	ASSERT_EQ(umount2(self->x, MNT_DETACH), 0);
	ASSERT_EQ(umount2(self->x, MNT_DETACH), 0);
}

TEST_HARNESS_MAIN
