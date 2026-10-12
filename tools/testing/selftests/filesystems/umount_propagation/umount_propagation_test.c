// SPDX-License-Identifier: GPL-2.0
/*
 * A synchronous umount fails with EBUSY when a mount it would pull out by
 * propagation is still in use.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/mount.h>
#include <linux/stat.h>

#include "../../kselftest_harness.h"

#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE		1
#endif
#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC	O_CLOEXEC
#endif
#ifndef AT_RECURSIVE
#define AT_RECURSIVE		0x8000
#endif
#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH	0x00000004
#endif
#ifndef MOVE_MOUNT_BENEATH
#define MOVE_MOUNT_BENEATH	0x00000200
#endif
#ifndef STATX_MNT_ID
#define STATX_MNT_ID		0x00001000U
#endif

static int sys_open_tree(int dfd, const char *filename, unsigned int flags)
{
	return syscall(__NR_open_tree, dfd, filename, flags);
}

static int sys_move_mount(int from_dfd, const char *from_pathname,
			  int to_dfd, const char *to_pathname,
			  unsigned int flags)
{
	return syscall(__NR_move_mount, from_dfd, from_pathname, to_dfd,
		       to_pathname, flags);
}

/* Child exit codes. */
enum {
	CHILD_OK,
	CHILD_UNSHARE,		/* could not set up the slave namespace */
	CHILD_OPEN_TREE,	/* open_tree() failed */
	CHILD_MOVE_MOUNT,	/* move_mount() failed */
	CHILD_STATX,		/* statx() failed */
	CHILD_PIPE,		/* the parent went away */
};

/* Messages between parent and child. */
enum {
	MSG_READY = 'r',	/* child: the copy is mounted and referenced */
	MSG_CHECK = 'c',	/* parent: check that the copy is still attached */
	MSG_ATTACHED = 'a',	/* child: it is */
	MSG_DETACHED = 'd',	/* child: it is not */
	MSG_CLOSE = 'x',	/* parent: drop the reference */
	MSG_CLOSED = 'y',	/* child: dropped */
	MSG_EXIT = 'e',		/* parent: done */
};

FIXTURE(umount_propagation)
{
	char base[64];
	char victim[80];
	bool mounted;
};

FIXTURE_SETUP(umount_propagation)
{
	self->mounted = false;

	if (geteuid() != 0)
		SKIP(return, "test requires CAP_SYS_ADMIN");

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	snprintf(self->base, sizeof(self->base), "/tmp/umount_propagation.XXXXXX");
	ASSERT_NE(mkdtemp(self->base), NULL);
	ASSERT_EQ(mount("tmpfs", self->base, "tmpfs", 0, NULL), 0);
	self->mounted = true;
	ASSERT_EQ(mount(NULL, self->base, NULL, MS_SHARED, NULL), 0);

	snprintf(self->victim, sizeof(self->victim), "%s/victim", self->base);
	ASSERT_EQ(mkdir(self->victim, 0755), 0);
	ASSERT_EQ(mount("tmpfs", self->victim, "tmpfs", 0, NULL), 0);
}

FIXTURE_TEARDOWN(umount_propagation)
{
	if (self->mounted)
		umount2(self->base, MNT_DETACH);
	rmdir(self->base);
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

/* Is the mount with id @mnt_id attached in this mount namespace? */
static bool mount_attached(__u64 mnt_id)
{
	char line[4096];
	bool found = false;
	FILE *f;

	f = fopen("/proc/self/mountinfo", "re");
	if (!f)
		return false;

	while (fgets(line, sizeof(line), f)) {
		if (strtoull(line, NULL, 10) == mnt_id) {
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

/*
 * The slave namespace: take a detached copy of the shared tree and move it
 * beneath the propagated copy of the victim, keeping the open_tree()
 * descriptor as a reference on it.
 */
static int slave_child(const char *base, const char *victim, int to_parent,
		       int from_parent)
{
	struct statx stx;
	int fd;

	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	if (mount("", "/", NULL, MS_REC | MS_SLAVE, NULL))
		return CHILD_UNSHARE;

	fd = sys_open_tree(AT_FDCWD, base,
			   OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_RECURSIVE);
	if (fd < 0)
		return CHILD_OPEN_TREE;
	if (sys_move_mount(fd, "", AT_FDCWD, victim,
			   MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_BENEATH))
		return CHILD_MOVE_MOUNT;
	if (statx(fd, "", AT_EMPTY_PATH, STATX_MNT_ID, &stx))
		return CHILD_STATX;

	if (send_msg(to_parent, MSG_READY) || recv_msg(from_parent) != MSG_CHECK)
		return CHILD_PIPE;
	if (send_msg(to_parent, mount_attached(stx.stx_mnt_id) ?
				MSG_ATTACHED : MSG_DETACHED))
		return CHILD_PIPE;

	if (recv_msg(from_parent) != MSG_CLOSE)
		return CHILD_PIPE;
	close(fd);
	if (send_msg(to_parent, MSG_CLOSED) || recv_msg(from_parent) != MSG_EXIT)
		return CHILD_PIPE;
	return CHILD_OK;
}

TEST_F(umount_propagation, busy_copy_pulled_out)
{
	int to_child[2], to_parent[2];
	int status;
	pid_t pid;

	ASSERT_EQ(pipe(to_child), 0);
	ASSERT_EQ(pipe(to_parent), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		close(to_child[1]);
		close(to_parent[0]);
		_exit(slave_child(self->base, self->victim, to_parent[1],
				  to_child[0]));
	}
	close(to_child[0]);
	close(to_parent[1]);

	ASSERT_EQ(recv_msg(to_parent[0]), MSG_READY);

	/* the copy in the slave namespace is in use */
	ASSERT_EQ(umount2(self->victim, 0), -1);
	ASSERT_EQ(errno, EBUSY);

	ASSERT_EQ(send_msg(to_child[1], MSG_CHECK), 0);
	ASSERT_EQ(recv_msg(to_parent[0]), MSG_ATTACHED);

	/* and once it is not, the umount goes through */
	ASSERT_EQ(send_msg(to_child[1], MSG_CLOSE), 0);
	ASSERT_EQ(recv_msg(to_parent[0]), MSG_CLOSED);
	ASSERT_EQ(umount2(self->victim, 0), 0);

	ASSERT_EQ(send_msg(to_child[1], MSG_EXIT), 0);
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), CHILD_OK);
}

TEST_HARNESS_MAIN
