// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Christian Brauner <brauner@kernel.org>
 *
 * Test that completing a filesystem context from another user namespace
 * doesn't warn.
 *
 * fsopen() records the caller's user namespace in fc->user_ns and hands
 * back an ordinary file descriptor. The task that issues
 * FSCONFIG_CMD_CREATE need not be the one that created the context: the fd
 * is inherited across fork() and exec() and it can be passed over a unix
 * socket. vfs_cmd_create() authorizes the create with mount_capable(),
 * which for FS_USERNS_MOUNT checks ns_capable(fc->user_ns, CAP_SYS_ADMIN),
 * and that succeeds for a task holding CAP_SYS_ADMIN in an ancestor of
 * fc->user_ns.
 *
 * binfmt_misc and overlayfs used to WARN_ON() that mismatch, which let an
 * unprivileged user taint the kernel, flood the log and panic a kernel
 * booted with panic_on_warn. The mount must still be refused, but it must
 * not warn.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../wrappers.h"
#include "../utils.h"
#include "../../kselftest_harness.h"

#ifndef FSCONFIG_CMD_CREATE
#define FSCONFIG_CMD_CREATE	6
#endif

/* TAINT_WARN, i.e. bit 9 of /proc/sys/kernel/tainted. */
#define TAINT_WARN_BIT		9

static bool taint_warn_set(void)
{
	unsigned long taint = 0;
	FILE *f;

	f = fopen("/proc/sys/kernel/tainted", "r");
	if (!f)
		return false;
	if (fscanf(f, "%lu", &taint) != 1)
		taint = 0;
	fclose(f);

	return taint & (1UL << TAINT_WARN_BIT);
}

static int send_fd(int sock, int fd)
{
	char cmsgbuf[CMSG_SPACE(sizeof(int))] = {};
	char b[1] = { 'x' };
	struct iovec iov = { .iov_base = b, .iov_len = sizeof(b) };
	struct msghdr msg = {
		.msg_iov	= &iov,
		.msg_iovlen	= 1,
		.msg_control	= cmsgbuf,
		.msg_controllen	= sizeof(cmsgbuf),
	};
	struct cmsghdr *cmsg;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

	return sendmsg(sock, &msg, 0) < 0 ? -1 : 0;
}

static int recv_fd(int sock)
{
	char cmsgbuf[CMSG_SPACE(sizeof(int))] = {};
	char b[1];
	struct iovec iov = { .iov_base = b, .iov_len = sizeof(b) };
	struct msghdr msg = {
		.msg_iov	= &iov,
		.msg_iovlen	= 1,
		.msg_control	= cmsgbuf,
		.msg_controllen	= sizeof(cmsgbuf),
	};
	struct cmsghdr *cmsg;
	int fd = -1;

	if (recvmsg(sock, &msg, 0) <= 0)
		return -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS)
		return -1;
	memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));

	return fd;
}

/*
 * Create a context for @fsname in a child and complete it here. With @nest
 * the child first creates its own user namespace, so that the context is
 * created in a descendant of the namespace completing it. The child needs a
 * mount namespace of its own as well: fsopen() gates on may_mount(), which
 * asks for CAP_SYS_ADMIN in the user namespace owning the caller's mount
 * namespace.
 *
 * Returns the result of FSCONFIG_CMD_CREATE with errno set, or -ENODATA if
 * the child could not create the context at all.
 */
static int create_from_child(const char *fsname, bool nest)
{
	int sock[2], fd, ret, status;
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock))
		return -ENODATA;

	pid = fork();
	if (pid < 0) {
		close(sock[0]);
		close(sock[1]);
		return -ENODATA;
	}

	if (pid == 0) {
		close(sock[0]);

		if (nest && unshare(CLONE_NEWUSER | CLONE_NEWNS))
			_exit(1);

		fd = sys_fsopen(fsname, 0);
		if (fd < 0)
			_exit(1);
		if (send_fd(sock[1], fd))
			_exit(1);
		_exit(0);
	}

	close(sock[1]);
	fd = recv_fd(sock[0]);
	close(sock[0]);
	wait_for_pid(pid);
	waitpid(pid, &status, WNOHANG);

	if (fd < 0)
		return -ENODATA;

	errno = 0;
	ret = sys_fsconfig(fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0);
	status = errno;
	close(fd);
	errno = status;

	return ret;
}

FIXTURE(fscontext_ns) {
	bool warn_before;
};

FIXTURE_SETUP(fscontext_ns)
{
	self->warn_before = taint_warn_set();

	if (setup_userns() != 0)
		SKIP(return, "setup_userns failed");
}

FIXTURE_TEARDOWN(fscontext_ns)
{
}

/*
 * The condition the kernel used to WARN about. It has to be refused, and it
 * has to be refused quietly: an unprivileged task reaches this.
 */
FIXTURE_VARIANT(fscontext_ns) {
	const char *fsname;
	int expected_errno;
};

FIXTURE_VARIANT_ADD(fscontext_ns, binfmt_misc) {
	.fsname = "binfmt_misc",
	.expected_errno = EINVAL,
};

FIXTURE_VARIANT_ADD(fscontext_ns, overlay) {
	.fsname = "overlay",
	.expected_errno = EIO,
};

TEST_F(fscontext_ns, create_from_descendant_userns)
{
	int ret;

	ret = create_from_child(variant->fsname, true);
	if (ret == -ENODATA)
		SKIP(return, "%s unavailable", variant->fsname);

	ASSERT_EQ(-1, ret);
	ASSERT_EQ(variant->expected_errno, errno);

	/*
	 * Only meaningful if nothing had warned before us. Note that an
	 * unrelated warning racing this test would look like a failure.
	 */
	if (self->warn_before)
		TH_LOG("TAINT_WARN already set, not checking for a new warning");
	else
		ASSERT_FALSE(taint_warn_set());
}

/*
 * The same handover within one user namespace is a supported thing to do and
 * has to keep working. binfmt_misc takes no options, so the create succeeds
 * outright and this also shows the test really drives the create path.
 */
TEST(create_from_same_userns)
{
	int ret;

	if (setup_userns() != 0)
		SKIP(return, "setup_userns failed");

	ret = create_from_child("binfmt_misc", false);
	if (ret == -ENODATA)
		SKIP(return, "binfmt_misc unavailable");

	ASSERT_EQ(0, ret);
}

TEST_HARNESS_MAIN
