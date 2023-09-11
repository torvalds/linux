// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#include <sys/socket.h>
#include <netinet/in.h>

#include "../kselftest_harness.h"

FIXTURE(bind_wildcard)
{
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	int expected_errno;
};

FIXTURE_VARIANT(bind_wildcard)
{
	const __u32 addr4_const;
	const struct in6_addr *addr6_const;
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_any)
{
	.addr4_const = INADDR_ANY,
	.addr6_const = &in6addr_any,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_local)
{
	.addr4_const = INADDR_ANY,
	.addr6_const = &in6addr_loopback,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_any)
{
	.addr4_const = INADDR_LOOPBACK,
	.addr6_const = &in6addr_any,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_local)
{
	.addr4_const = INADDR_LOOPBACK,
	.addr6_const = &in6addr_loopback,
};

FIXTURE_SETUP(bind_wildcard)
{
	self->addr4.sin_family = AF_INET;
	self->addr4.sin_port = htons(0);
	self->addr4.sin_addr.s_addr = htonl(variant->addr4_const);

	self->addr6.sin6_family = AF_INET6;
	self->addr6.sin6_port = htons(0);
	self->addr6.sin6_addr = *variant->addr6_const;

	if (variant->addr6_const == &in6addr_any)
		self->expected_errno = EADDRINUSE;
	else
		self->expected_errno = 0;
}

FIXTURE_TEARDOWN(bind_wildcard)
{
}

void bind_sockets(struct __test_metadata *_metadata,
		  FIXTURE_DATA(bind_wildcard) *self,
		  struct sockaddr *addr1, socklen_t addrlen1,
		  struct sockaddr *addr2, socklen_t addrlen2)
{
	int fd[2];
	int ret;

	fd[0] = socket(addr1->sa_family, SOCK_STREAM, 0);
	ASSERT_GT(fd[0], 0);

	ret = bind(fd[0], addr1, addrlen1);
	ASSERT_EQ(ret, 0);

	ret = getsockname(fd[0], addr1, &addrlen1);
	ASSERT_EQ(ret, 0);

	((struct sockaddr_in *)addr2)->sin_port = ((struct sockaddr_in *)addr1)->sin_port;

	fd[1] = socket(addr2->sa_family, SOCK_STREAM, 0);
	ASSERT_GT(fd[1], 0);

	ret = bind(fd[1], addr2, addrlen2);
	if (self->expected_errno) {
		ASSERT_EQ(ret, -1);
		ASSERT_EQ(errno, self->expected_errno);
	} else {
		ASSERT_EQ(ret, 0);
	}

	close(fd[1]);
	close(fd[0]);
}

TEST_F(bind_wildcard, v4_v6)
{
	bind_sockets(_metadata, self,
		     (struct sockaddr *)&self->addr4, sizeof(self->addr4),
		     (struct sockaddr *)&self->addr6, sizeof(self->addr6));
}

TEST_F(bind_wildcard, v6_v4)
{
	bind_sockets(_metadata, self,
		     (struct sockaddr *)&self->addr6, sizeof(self->addr6),
		     (struct sockaddr *)&self->addr4, sizeof(self->addr4));
}

TEST_HARNESS_MAIN
