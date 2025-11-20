// SPDX-License-Identifier: GPL-2.0
/* Copyright 2025 Google LLC */

#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>

#include "../../kselftest_harness.h"

FIXTURE(so_peek_off)
{
	int fd[2];	/* 0: sender, 1: receiver */
};

FIXTURE_VARIANT(so_peek_off)
{
	int type;
};

FIXTURE_VARIANT_ADD(so_peek_off, stream)
{
	.type = SOCK_STREAM,
};

FIXTURE_VARIANT_ADD(so_peek_off, dgram)
{
	.type = SOCK_DGRAM,
};

FIXTURE_VARIANT_ADD(so_peek_off, seqpacket)
{
	.type = SOCK_SEQPACKET,
};

FIXTURE_SETUP(so_peek_off)
{
	struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 3000,
	};
	int ret;

	ret = socketpair(AF_UNIX, variant->type, 0, self->fd);
	ASSERT_EQ(0, ret);

	ret = setsockopt(self->fd[1], SOL_SOCKET, SO_RCVTIMEO_NEW,
			 &timeout, sizeof(timeout));
	ASSERT_EQ(0, ret);

	ret = setsockopt(self->fd[1], SOL_SOCKET, SO_PEEK_OFF,
			 &(int){0}, sizeof(int));
	ASSERT_EQ(0, ret);
}

FIXTURE_TEARDOWN(so_peek_off)
{
	close_range(self->fd[0], self->fd[1], 0);
}

#define sendeq(fd, str, flags)					\
	do {							\
		int bytes, len = strlen(str);			\
								\
		bytes = send(fd, str, len, flags);		\
		ASSERT_EQ(len, bytes);				\
	} while (0)

#define recveq(fd, str, buflen, flags)				\
	do {							\
		char buf[(buflen) + 1] = {};			\
		int bytes;					\
								\
		bytes = recv(fd, buf, buflen, flags);		\
		ASSERT_NE(-1, bytes);				\
		ASSERT_STREQ(str, buf);				\
	} while (0)

#define async							\
	for (pid_t pid = (pid = fork(),				\
			  pid < 0 ?				\
			  __TH_LOG("Failed to start async {}"),	\
			  _metadata->exit_code = KSFT_FAIL,	\
			  __bail(1, _metadata),			\
			  0xdead :				\
			  pid);					\
	     !pid; exit(0))

TEST_F(so_peek_off, single_chunk)
{
	sendeq(self->fd[0], "aaaabbbb", 0);

	recveq(self->fd[1], "aaaa", 4, MSG_PEEK);
	recveq(self->fd[1], "bbbb", 100, MSG_PEEK);
}

TEST_F(so_peek_off, two_chunks)
{
	sendeq(self->fd[0], "aaaa", 0);
	sendeq(self->fd[0], "bbbb", 0);

	recveq(self->fd[1], "aaaa", 4, MSG_PEEK);
	recveq(self->fd[1], "bbbb", 100, MSG_PEEK);
}

TEST_F(so_peek_off, two_chunks_blocking)
{
	async {
		usleep(1000);
		sendeq(self->fd[0], "aaaa", 0);
	}

	recveq(self->fd[1], "aaaa", 4, MSG_PEEK);

	async {
		usleep(1000);
		sendeq(self->fd[0], "bbbb", 0);
	}

	/* goto again; -> goto redo; in unix_stream_read_generic(). */
	recveq(self->fd[1], "bbbb", 100, MSG_PEEK);
}

TEST_F(so_peek_off, two_chunks_overlap)
{
	sendeq(self->fd[0], "aaaa", 0);
	recveq(self->fd[1], "aa", 2, MSG_PEEK);

	sendeq(self->fd[0], "bbbb", 0);

	if (variant->type == SOCK_STREAM) {
		/* SOCK_STREAM tries to fill the buffer. */
		recveq(self->fd[1], "aabb", 4, MSG_PEEK);
		recveq(self->fd[1], "bb", 100, MSG_PEEK);
	} else {
		/* SOCK_DGRAM and SOCK_SEQPACKET returns at the skb boundary. */
		recveq(self->fd[1], "aa", 100, MSG_PEEK);
		recveq(self->fd[1], "bbbb", 100, MSG_PEEK);
	}
}

TEST_F(so_peek_off, two_chunks_overlap_blocking)
{
	async {
		usleep(1000);
		sendeq(self->fd[0], "aaaa", 0);
	}

	recveq(self->fd[1], "aa", 2, MSG_PEEK);

	async {
		usleep(1000);
		sendeq(self->fd[0], "bbbb", 0);
	}

	/* Even SOCK_STREAM does not wait if at least one byte is read. */
	recveq(self->fd[1], "aa", 100, MSG_PEEK);

	recveq(self->fd[1], "bbbb", 100, MSG_PEEK);
}

TEST_HARNESS_MAIN
