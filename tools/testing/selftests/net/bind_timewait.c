// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#include <sys/socket.h>
#include <netinet/in.h>

#include "../kselftest_harness.h"

FIXTURE(bind_timewait)
{
	struct sockaddr_in addr;
	socklen_t addrlen;
};

FIXTURE_VARIANT(bind_timewait)
{
	__u32 addr_const;
};

FIXTURE_VARIANT_ADD(bind_timewait, localhost)
{
	.addr_const = INADDR_LOOPBACK
};

FIXTURE_VARIANT_ADD(bind_timewait, addrany)
{
	.addr_const = INADDR_ANY
};

FIXTURE_SETUP(bind_timewait)
{
	self->addr.sin_family = AF_INET;
	self->addr.sin_port = 0;
	self->addr.sin_addr.s_addr = htonl(variant->addr_const);
	self->addrlen = sizeof(self->addr);
}

FIXTURE_TEARDOWN(bind_timewait)
{
}

void create_timewait_socket(struct __test_metadata *_metadata,
			    FIXTURE_DATA(bind_timewait) *self)
{
	int server_fd, client_fd, child_fd, ret;
	struct sockaddr_in addr;
	socklen_t addrlen;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GT(server_fd, 0);

	ret = bind(server_fd, (struct sockaddr *)&self->addr, self->addrlen);
	ASSERT_EQ(ret, 0);

	ret = listen(server_fd, 1);
	ASSERT_EQ(ret, 0);

	ret = getsockname(server_fd, (struct sockaddr *)&self->addr, &self->addrlen);
	ASSERT_EQ(ret, 0);

	client_fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GT(client_fd, 0);

	ret = connect(client_fd, (struct sockaddr *)&self->addr, self->addrlen);
	ASSERT_EQ(ret, 0);

	addrlen = sizeof(addr);
	child_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
	ASSERT_GT(child_fd, 0);

	close(child_fd);
	close(client_fd);
	close(server_fd);
}

TEST_F(bind_timewait, 1)
{
	int fd, ret;

	create_timewait_socket(_metadata, self);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GT(fd, 0);

	ret = bind(fd, (struct sockaddr *)&self->addr, self->addrlen);
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EADDRINUSE);

	close(fd);
}

TEST_HARNESS_MAIN
