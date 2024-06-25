// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include "../../kselftest_harness.h"

#define BUF_SZ	32

FIXTURE(msg_oob)
{
	int fd[4];		/* 0: AF_UNIX sender
				 * 1: AF_UNIX receiver
				 * 2: TCP sender
				 * 3: TCP receiver
				 */
	bool tcp_compliant;
};

FIXTURE_VARIANT(msg_oob)
{
	bool peek;
};

FIXTURE_VARIANT_ADD(msg_oob, no_peek)
{
	.peek = false,
};

FIXTURE_VARIANT_ADD(msg_oob, peek)
{
	.peek = true
};

static void create_unix_socketpair(struct __test_metadata *_metadata,
				   FIXTURE_DATA(msg_oob) *self)
{
	int ret;

	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, self->fd);
	ASSERT_EQ(ret, 0);
}

static void create_tcp_socketpair(struct __test_metadata *_metadata,
				  FIXTURE_DATA(msg_oob) *self)
{
	struct sockaddr_in addr;
	socklen_t addrlen;
	int listen_fd;
	int ret;

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(listen_fd, 0);

	ret = listen(listen_fd, -1);
	ASSERT_EQ(ret, 0);

	addrlen = sizeof(addr);
	ret = getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen);
	ASSERT_EQ(ret, 0);

	self->fd[2] = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(self->fd[2], 0);

	ret = connect(self->fd[2], (struct sockaddr *)&addr, addrlen);
	ASSERT_EQ(ret, 0);

	self->fd[3] = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);
	ASSERT_GE(self->fd[3], 0);

	ret = fcntl(self->fd[3], F_SETFL, O_NONBLOCK);
	ASSERT_EQ(ret, 0);
}

static void close_sockets(FIXTURE_DATA(msg_oob) *self)
{
	int i;

	for (i = 0; i < 4; i++)
		close(self->fd[i]);
}

FIXTURE_SETUP(msg_oob)
{
	create_unix_socketpair(_metadata, self);
	create_tcp_socketpair(_metadata, self);

	self->tcp_compliant = true;
}

FIXTURE_TEARDOWN(msg_oob)
{
	close_sockets(self);
}

static void __sendpair(struct __test_metadata *_metadata,
		       FIXTURE_DATA(msg_oob) *self,
		       const void *buf, size_t len, int flags)
{
	int i, ret[2];

	for (i = 0; i < 2; i++)
		ret[i] = send(self->fd[i * 2], buf, len, flags);

	ASSERT_EQ(ret[0], len);
	ASSERT_EQ(ret[0], ret[1]);
}

static void __recvpair(struct __test_metadata *_metadata,
		       FIXTURE_DATA(msg_oob) *self,
		       const void *expected_buf, int expected_len,
		       int buf_len, int flags)
{
	int i, ret[2], recv_errno[2], expected_errno = 0;
	char recv_buf[2][BUF_SZ] = {};
	bool printed = false;

	ASSERT_GE(BUF_SZ, buf_len);

	errno = 0;

	for (i = 0; i < 2; i++) {
		ret[i] = recv(self->fd[i * 2 + 1], recv_buf[i], buf_len, flags);
		recv_errno[i] = errno;
	}

	if (expected_len < 0) {
		expected_errno = -expected_len;
		expected_len = -1;
	}

	if (ret[0] != expected_len || recv_errno[0] != expected_errno) {
		TH_LOG("AF_UNIX :%s", ret[0] < 0 ? strerror(recv_errno[0]) : recv_buf[0]);
		TH_LOG("Expected:%s", expected_errno ? strerror(expected_errno) : expected_buf);

		ASSERT_EQ(ret[0], expected_len);
		ASSERT_EQ(recv_errno[0], expected_errno);
	}

	if (ret[0] != ret[1] || recv_errno[0] != recv_errno[1]) {
		TH_LOG("AF_UNIX :%s", ret[0] < 0 ? strerror(recv_errno[0]) : recv_buf[0]);
		TH_LOG("TCP     :%s", ret[1] < 0 ? strerror(recv_errno[1]) : recv_buf[1]);

		printed = true;

		if (self->tcp_compliant) {
			ASSERT_EQ(ret[0], ret[1]);
			ASSERT_EQ(recv_errno[0], recv_errno[1]);
		}
	}

	if (expected_len >= 0) {
		int cmp;

		cmp = strncmp(expected_buf, recv_buf[0], expected_len);
		if (cmp) {
			TH_LOG("AF_UNIX :%s", ret[0] < 0 ? strerror(recv_errno[0]) : recv_buf[0]);
			TH_LOG("Expected:%s", expected_errno ? strerror(expected_errno) : expected_buf);

			ASSERT_EQ(cmp, 0);
		}

		cmp = strncmp(recv_buf[0], recv_buf[1], expected_len);
		if (cmp) {
			if (!printed) {
				TH_LOG("AF_UNIX :%s", ret[0] < 0 ? strerror(recv_errno[0]) : recv_buf[0]);
				TH_LOG("TCP     :%s", ret[1] < 0 ? strerror(recv_errno[1]) : recv_buf[1]);
			}

			if (self->tcp_compliant)
				ASSERT_EQ(cmp, 0);
		}
	}
}

#define sendpair(buf, len, flags)					\
	__sendpair(_metadata, self, buf, len, flags)

#define recvpair(expected_buf, expected_len, buf_len, flags)		\
	do {								\
		if (variant->peek)					\
			__recvpair(_metadata, self,			\
				   expected_buf, expected_len,		\
				   buf_len, (flags) | MSG_PEEK);	\
		__recvpair(_metadata, self,				\
			   expected_buf, expected_len, buf_len, flags);	\
	} while (0)

#define tcp_incompliant							\
	for (self->tcp_compliant = false;				\
	     self->tcp_compliant == false;				\
	     self->tcp_compliant = true)

TEST_F(msg_oob, non_oob)
{
	sendpair("x", 1, 0);

	recvpair("", -EINVAL, 1, MSG_OOB);
}

TEST_F(msg_oob, oob)
{
	sendpair("x", 1, MSG_OOB);

	recvpair("x", 1, 1, MSG_OOB);
}

TEST_F(msg_oob, oob_drop)
{
	sendpair("x", 1, MSG_OOB);

	recvpair("", -EAGAIN, 1, 0);		/* Drop OOB. */
	recvpair("", -EINVAL, 1, MSG_OOB);
}

TEST_F(msg_oob, oob_ahead)
{
	sendpair("hello", 5, MSG_OOB);

	recvpair("o", 1, 1, MSG_OOB);
	recvpair("hell", 4, 4, 0);
}

TEST_F(msg_oob, oob_break)
{
	sendpair("hello", 5, MSG_OOB);

	recvpair("hell", 4, 5, 0);		/* Break at OOB even with enough buffer. */
	recvpair("o", 1, 1, MSG_OOB);
}

TEST_F(msg_oob, oob_ahead_break)
{
	sendpair("hello", 5, MSG_OOB);
	sendpair("world", 5, 0);

	recvpair("o", 1, 1, MSG_OOB);
	recvpair("hell", 4, 9, 0);		/* Break at OOB even after it's recv()ed. */
	recvpair("world", 5, 5, 0);
}

TEST_F(msg_oob, oob_break_drop)
{
	sendpair("hello", 5, MSG_OOB);
	sendpair("world", 5, 0);

	recvpair("hell", 4, 10, 0);		/* Break at OOB even with enough buffer. */
	recvpair("world", 5, 10, 0);		/* Drop OOB and recv() the next skb. */
	recvpair("", -EINVAL, 1, MSG_OOB);
}

TEST_F(msg_oob, ex_oob_break)
{
	sendpair("hello", 5, MSG_OOB);
	sendpair("wor", 3, MSG_OOB);
	sendpair("ld", 2, 0);

	recvpair("hellowo", 7, 10, 0);		/* Break at OOB but not at ex-OOB. */
	recvpair("r", 1, 1, MSG_OOB);
	recvpair("ld", 2, 2, 0);
}

TEST_F(msg_oob, ex_oob_drop)
{
	sendpair("x", 1, MSG_OOB);
	sendpair("y", 1, MSG_OOB);		/* TCP drops "x" at this moment. */

	tcp_incompliant {
		recvpair("x", 1, 1, 0);		/* TCP drops "y" by passing through it. */
		recvpair("y", 1, 1, MSG_OOB);	/* TCP returns -EINVAL. */
	}
}

TEST_F(msg_oob, ex_oob_drop_2)
{
	sendpair("x", 1, MSG_OOB);
	sendpair("y", 1, MSG_OOB);		/* TCP drops "x" at this moment. */

	recvpair("y", 1, 1, MSG_OOB);

	tcp_incompliant {
		recvpair("x", 1, 1, 0);		/* TCP returns -EAGAIN. */
	}
}

TEST_F(msg_oob, ex_oob_ahead_break)
{
	sendpair("hello", 5, MSG_OOB);
	sendpair("wor", 3, MSG_OOB);

	recvpair("r", 1, 1, MSG_OOB);

	sendpair("ld", 2, MSG_OOB);

	tcp_incompliant {
		recvpair("hellowol", 8, 10, 0);	/* TCP recv()s "helloworl", why "r" ?? */
	}

	recvpair("d", 1, 1, MSG_OOB);
}

TEST_HARNESS_MAIN
