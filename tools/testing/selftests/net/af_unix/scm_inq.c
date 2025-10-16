// SPDX-License-Identifier: GPL-2.0
/* Copyright 2025 Google LLC */

#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "../../kselftest_harness.h"

#define NR_CHUNKS	100
#define MSG_LEN		256

FIXTURE(scm_inq)
{
	int fd[2];
};

FIXTURE_VARIANT(scm_inq)
{
	int type;
};

FIXTURE_VARIANT_ADD(scm_inq, stream)
{
	.type = SOCK_STREAM,
};

FIXTURE_VARIANT_ADD(scm_inq, dgram)
{
	.type = SOCK_DGRAM,
};

FIXTURE_VARIANT_ADD(scm_inq, seqpacket)
{
	.type = SOCK_SEQPACKET,
};

FIXTURE_SETUP(scm_inq)
{
	int err;

	err = socketpair(AF_UNIX, variant->type | SOCK_NONBLOCK, 0, self->fd);
	ASSERT_EQ(0, err);
}

FIXTURE_TEARDOWN(scm_inq)
{
	close(self->fd[0]);
	close(self->fd[1]);
}

static void send_chunks(struct __test_metadata *_metadata,
			FIXTURE_DATA(scm_inq) *self)
{
	char buf[MSG_LEN] = {};
	int i, ret;

	for (i = 0; i < NR_CHUNKS; i++) {
		ret = send(self->fd[0], buf, sizeof(buf), 0);
		ASSERT_EQ(sizeof(buf), ret);
	}
}

static void recv_chunks(struct __test_metadata *_metadata,
			FIXTURE_DATA(scm_inq) *self)
{
	char cmsg_buf[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {};
	struct iovec iov = {};
	struct cmsghdr *cmsg;
	char buf[MSG_LEN];
	int i, ret;
	int inq;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);

	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);

	for (i = 0; i < NR_CHUNKS; i++) {
		memset(buf, 0, sizeof(buf));
		memset(cmsg_buf, 0, sizeof(cmsg_buf));

		ret = recvmsg(self->fd[1], &msg, 0);
		ASSERT_EQ(MSG_LEN, ret);

		cmsg = CMSG_FIRSTHDR(&msg);
		ASSERT_NE(NULL, cmsg);
		ASSERT_EQ(CMSG_LEN(sizeof(int)), cmsg->cmsg_len);
		ASSERT_EQ(SOL_SOCKET, cmsg->cmsg_level);
		ASSERT_EQ(SCM_INQ, cmsg->cmsg_type);

		ret = ioctl(self->fd[1], SIOCINQ, &inq);
		ASSERT_EQ(0, ret);
		ASSERT_EQ(*(int *)CMSG_DATA(cmsg), inq);
	}
}

TEST_F(scm_inq, basic)
{
	int err, inq;

	err = setsockopt(self->fd[1], SOL_SOCKET, SO_INQ, &(int){1}, sizeof(int));
	if (variant->type != SOCK_STREAM) {
		ASSERT_EQ(-ENOPROTOOPT, -errno);
		return;
	}

	ASSERT_EQ(0, err);

	err = ioctl(self->fd[1], SIOCINQ, &inq);
	ASSERT_EQ(0, err);
	ASSERT_EQ(0, inq);

	send_chunks(_metadata, self);
	recv_chunks(_metadata, self);
}

TEST_HARNESS_MAIN
