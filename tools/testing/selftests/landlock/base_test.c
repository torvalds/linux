// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Common user space base
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "common.h"

#ifndef O_PATH
#define O_PATH		010000000
#endif

TEST(inconsistent_attr) {
	const long page_size = sysconf(_SC_PAGESIZE);
	char *const buf = malloc(page_size + 1);
	struct landlock_ruleset_attr *const ruleset_attr = (void *)buf;

	ASSERT_NE(NULL, buf);

	/* Checks copy_from_user(). */
	ASSERT_EQ(-1, landlock_create_ruleset(ruleset_attr, 0, 0));
	/* The size if less than sizeof(struct landlock_attr_enforce). */
	ASSERT_EQ(EINVAL, errno);
	ASSERT_EQ(-1, landlock_create_ruleset(ruleset_attr, 1, 0));
	ASSERT_EQ(EINVAL, errno);

	ASSERT_EQ(-1, landlock_create_ruleset(NULL, 1, 0));
	/* The size if less than sizeof(struct landlock_attr_enforce). */
	ASSERT_EQ(EFAULT, errno);

	ASSERT_EQ(-1, landlock_create_ruleset(NULL,
				sizeof(struct landlock_ruleset_attr), 0));
	ASSERT_EQ(EFAULT, errno);

	ASSERT_EQ(-1, landlock_create_ruleset(ruleset_attr, page_size + 1, 0));
	ASSERT_EQ(E2BIG, errno);

	ASSERT_EQ(-1, landlock_create_ruleset(ruleset_attr,
				sizeof(struct landlock_ruleset_attr), 0));
	ASSERT_EQ(ENOMSG, errno);
	ASSERT_EQ(-1, landlock_create_ruleset(ruleset_attr, page_size, 0));
	ASSERT_EQ(ENOMSG, errno);

	/* Checks non-zero value. */
	buf[page_size - 2] = '.';
	ASSERT_EQ(-1, landlock_create_ruleset(ruleset_attr, page_size, 0));
	ASSERT_EQ(E2BIG, errno);

	ASSERT_EQ(-1, landlock_create_ruleset(ruleset_attr, page_size + 1, 0));
	ASSERT_EQ(E2BIG, errno);

	free(buf);
}

TEST(empty_path_beneath_attr) {
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_EXECUTE,
	};
	const int ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);

	ASSERT_LE(0, ruleset_fd);

	/* Similar to struct landlock_path_beneath_attr.parent_fd = 0 */
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				NULL, 0));
	ASSERT_EQ(EFAULT, errno);
	ASSERT_EQ(0, close(ruleset_fd));
}

TEST(inval_fd_enforce) {
	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	ASSERT_EQ(-1, landlock_restrict_self(-1, 0));
	ASSERT_EQ(EBADF, errno);
}

TEST(unpriv_enforce_without_no_new_privs) {
	int err;

	drop_caps(_metadata);
	err = landlock_restrict_self(-1, 0);
	ASSERT_EQ(EPERM, errno);
	ASSERT_EQ(err, -1);
}

TEST(ruleset_fd_io)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE,
	};
	int ruleset_fd;
	char buf;

	drop_caps(_metadata);
	ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(-1, write(ruleset_fd, ".", 1));
	ASSERT_EQ(EINVAL, errno);
	ASSERT_EQ(-1, read(ruleset_fd, &buf, 1));
	ASSERT_EQ(EINVAL, errno);

	ASSERT_EQ(0, close(ruleset_fd));
}

/* Tests enforcement of a ruleset FD transferred through a UNIX socket. */
TEST(ruleset_fd_transfer)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
	};
	struct landlock_path_beneath_attr path_beneath_attr = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_DIR,
	};
	int ruleset_fd_tx, dir_fd;
	union {
		/* Aligned ancillary data buffer. */
		char buf[CMSG_SPACE(sizeof(ruleset_fd_tx))];
		struct cmsghdr _align;
	} cmsg_tx = {};
	char data_tx = '.';
	struct iovec io = {
		.iov_base = &data_tx,
		.iov_len = sizeof(data_tx),
	};
	struct msghdr msg = {
		.msg_iov = &io,
		.msg_iovlen = 1,
		.msg_control = &cmsg_tx.buf,
		.msg_controllen = sizeof(cmsg_tx.buf),
	};
	struct cmsghdr *cmsg;
	int socket_fds[2];
	pid_t child;
	int status;

	drop_caps(_metadata);

	/* Creates a test ruleset with a simple rule. */
	ruleset_fd_tx = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd_tx);
	path_beneath_attr.parent_fd = open("/tmp", O_PATH | O_NOFOLLOW |
			O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, path_beneath_attr.parent_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd_tx, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath_attr, 0));
	ASSERT_EQ(0, close(path_beneath_attr.parent_fd));

	cmsg = CMSG_FIRSTHDR(&msg);
	ASSERT_NE(NULL, cmsg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(ruleset_fd_tx));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memcpy(CMSG_DATA(cmsg), &ruleset_fd_tx, sizeof(ruleset_fd_tx));

	/* Sends the ruleset FD over a socketpair and then close it. */
	ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socket_fds));
	ASSERT_EQ(sizeof(data_tx), sendmsg(socket_fds[0], &msg, 0));
	ASSERT_EQ(0, close(socket_fds[0]));
	ASSERT_EQ(0, close(ruleset_fd_tx));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int ruleset_fd_rx;

		*(char *)msg.msg_iov->iov_base = '\0';
		ASSERT_EQ(sizeof(data_tx), recvmsg(socket_fds[1], &msg, MSG_CMSG_CLOEXEC));
		ASSERT_EQ('.', *(char *)msg.msg_iov->iov_base);
		ASSERT_EQ(0, close(socket_fds[1]));
		cmsg = CMSG_FIRSTHDR(&msg);
		ASSERT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(ruleset_fd_tx)));
		memcpy(&ruleset_fd_rx, CMSG_DATA(cmsg), sizeof(ruleset_fd_tx));

		/* Enforces the received ruleset on the child. */
		ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
		ASSERT_EQ(0, landlock_restrict_self(ruleset_fd_rx, 0));
		ASSERT_EQ(0, close(ruleset_fd_rx));

		/* Checks that the ruleset enforcement. */
		ASSERT_EQ(-1, open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
		ASSERT_EQ(EACCES, errno);
		dir_fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		ASSERT_LE(0, dir_fd);
		ASSERT_EQ(0, close(dir_fd));
		_exit(_metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
		return;
	}

	ASSERT_EQ(0, close(socket_fds[1]));

	/* Checks that the parent is unrestricted. */
	dir_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, dir_fd);
	ASSERT_EQ(0, close(dir_fd));
	dir_fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, dir_fd);
	ASSERT_EQ(0, close(dir_fd));

	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}

TEST_HARNESS_MAIN
