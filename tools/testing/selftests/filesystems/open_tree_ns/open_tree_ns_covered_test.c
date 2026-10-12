// SPDX-License-Identifier: GPL-2.0
/*
 * open_tree(OPEN_TREE_NAMESPACE) by a caller that isn't privileged over the
 * mount namespace it copies from must not reveal what the mounts below the
 * copied mount cover. Without AT_RECURSIVE the copy is refused when there's
 * anything mounted below the requested directory. With AT_RECURSIVE
 * unbindable mounts are copied as well.
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

#include "../wrappers.h"
#include "../../kselftest_harness.h"

#ifndef OPEN_TREE_NAMESPACE
#define OPEN_TREE_NAMESPACE	(1 << 1)
#endif

#define DIR_LEN 64
#define PATH_LEN 128

/* Child exit codes. */
enum {
	CHILD_OK,
	CHILD_USERNS,		/* could not create the child user namespace */
	CHILD_NONREC_ALLOWED,	/* the non-recursive copy was not refused */
	CHILD_NONREC_ERRNO,	/* it was refused with the wrong error */
	CHILD_REC_REFUSED,	/* the recursive copy failed */
	CHILD_SETNS,		/* could not enter the new mount namespace */
	CHILD_NO_COVER,		/* the covering mount is missing in the copy */
	CHILD_REVEALED,		/* the covered file is visible in the copy */
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

/* Become root in a new user namespace, the uid @uid is mapped to 0. */
static int enter_userns(uid_t uid, gid_t gid)
{
	char map[32];

	if (unshare(CLONE_NEWUSER))
		return -1;
	if (write_file("/proc/self/setgroups", "deny") && errno != ENOENT)
		return -1;
	snprintf(map, sizeof(map), "0 %d 1", uid);
	if (write_file("/proc/self/uid_map", map))
		return -1;
	snprintf(map, sizeof(map), "0 %d 1", gid);
	if (write_file("/proc/self/gid_map", map))
		return -1;
	return setgid(0) || setuid(0) ? -1 : 0;
}

FIXTURE(open_tree_ns_covered) {
	char dir[DIR_LEN];
	char cover[PATH_LEN];
};

FIXTURE_VARIANT(open_tree_ns_covered) {
	int propagation;
};

FIXTURE_VARIANT_ADD(open_tree_ns_covered, private_cover) {
	.propagation = MS_PRIVATE,
};

FIXTURE_VARIANT_ADD(open_tree_ns_covered, unbindable_cover) {
	.propagation = MS_UNBINDABLE,
};

/*
 * Root in a user namespace owns a private mount namespace with a tmpfs
 * on @dir and a second tmpfs covering @dir/covered/under.txt.
 */
FIXTURE_SETUP(open_tree_ns_covered)
{
	char p[PATH_LEN];
	int fd;

	snprintf(self->dir, sizeof(self->dir), "/tmp/open_tree_ns_covered.XXXXXX");
	ASSERT_NE(mkdtemp(self->dir), NULL);
	if (enter_userns(getuid(), getgid()) || unshare(CLONE_NEWNS)) {
		rmdir(self->dir);
		SKIP(return, "test requires user namespaces");
	}
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);
	ASSERT_EQ(mount("tmpfs", self->dir, "tmpfs", 0, NULL), 0);

	/* the flag has to exist before its rules can be checked */
	fd = sys_open_tree(AT_FDCWD, self->dir,
			   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
	if (fd < 0 && (errno == EINVAL || errno == ENOSYS)) {
		umount2(self->dir, MNT_DETACH);
		rmdir(self->dir);
		SKIP(return, "OPEN_TREE_NAMESPACE not supported");
	}
	ASSERT_GE(fd, 0);
	close(fd);

	snprintf(self->cover, sizeof(self->cover), "%s/covered", self->dir);
	ASSERT_EQ(mkdir(self->cover, 0755), 0);
	snprintf(p, sizeof(p), "%s/covered/under.txt", self->dir);
	ASSERT_EQ(create_file(p, "hidden"), 0);
	ASSERT_EQ(mount("tmpfs", self->cover, "tmpfs", 0, NULL), 0);
	ASSERT_EQ(mount(NULL, self->cover, NULL, variant->propagation, NULL), 0);
}

FIXTURE_TEARDOWN(open_tree_ns_covered)
{
	umount2(self->dir, MNT_DETACH);
	rmdir(self->dir);
}

/* A caller in a new user namespace that doesn't own the mount namespace. */
static int foreign_child(const char *dir)
{
	struct stat st;
	int fd;

	if (enter_userns(0, 0))
		return CHILD_USERNS;

	fd = sys_open_tree(AT_FDCWD, dir, OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
	if (fd >= 0)
		return CHILD_NONREC_ALLOWED;
	if (errno != EINVAL)
		return CHILD_NONREC_ERRNO;

	fd = sys_open_tree(AT_FDCWD, dir,
			   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC | AT_RECURSIVE);
	if (fd < 0)
		return CHILD_REC_REFUSED;
	if (setns(fd, CLONE_NEWNS))
		return CHILD_SETNS;
	if (stat("/covered", &st))
		return CHILD_NO_COVER;
	if (!access("/covered/under.txt", F_OK) || errno != ENOENT)
		return CHILD_REVEALED;
	return CHILD_OK;
}

TEST_F(open_tree_ns_covered, foreign_user_namespace)
{
	int status, fd;
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(foreign_child(self->dir));
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), CHILD_OK);

	/* the owner of the mount namespace keeps bind mount semantics */
	fd = sys_open_tree(AT_FDCWD, self->dir,
			   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC);
	ASSERT_GE(fd, 0);
	close(fd);
}

TEST_HARNESS_MAIN
