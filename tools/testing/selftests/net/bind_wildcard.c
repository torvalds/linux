// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#include <sys/socket.h>
#include <netinet/in.h>

#include "../kselftest_harness.h"

static const __u32 in4addr_any = INADDR_ANY;
static const __u32 in4addr_loopback = INADDR_LOOPBACK;
static const struct in6_addr in6addr_v4mapped_any = {
	.s6_addr = {
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 255, 255,
		0, 0, 0, 0
	}
};
static const struct in6_addr in6addr_v4mapped_loopback = {
	.s6_addr = {
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 255, 255,
		127, 0, 0, 1
	}
};

FIXTURE(bind_wildcard)
{
	socklen_t addrlen[2];
	union {
		struct sockaddr addr;
		struct sockaddr_in addr4;
		struct sockaddr_in6 addr6;
	} addr[2];
};

FIXTURE_VARIANT(bind_wildcard)
{
	sa_family_t family[2];
	const void *addr[2];
	int expected_errno;
};

/* (IPv4, IPv6) */
FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_any},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_loopback},
	.expected_errno = 0,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_v4mapped_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_v4mapped_any},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_v4mapped_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_v4mapped_loopback},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_any},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_loopback},
	.expected_errno = 0,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_v4mapped_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_v4mapped_any},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_v4mapped_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_v4mapped_loopback},
	.expected_errno = EADDRINUSE,
};

/* (IPv6, IPv4) */
FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_any, &in4addr_any},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_any, &in4addr_loopback},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_loopback, &in4addr_any},
	.expected_errno = 0,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_loopback, &in4addr_loopback},
	.expected_errno = 0,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_any, &in4addr_any},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_any, &in4addr_loopback},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_local_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_loopback, &in4addr_any},
	.expected_errno = EADDRINUSE,
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_local_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_loopback, &in4addr_loopback},
	.expected_errno = EADDRINUSE,
};

static void setup_addr(FIXTURE_DATA(bind_wildcard) *self, int i,
		       int family, const void *addr_const)
{
	if (family == AF_INET) {
		struct sockaddr_in *addr4 = &self->addr[i].addr4;
		const __u32 *addr4_const = addr_const;

		addr4->sin_family = AF_INET;
		addr4->sin_port = htons(0);
		addr4->sin_addr.s_addr = htonl(*addr4_const);

		self->addrlen[i] = sizeof(struct sockaddr_in);
	} else {
		struct sockaddr_in6 *addr6 = &self->addr[i].addr6;
		const struct in6_addr *addr6_const = addr_const;

		addr6->sin6_family = AF_INET6;
		addr6->sin6_port = htons(0);
		addr6->sin6_addr = *addr6_const;

		self->addrlen[i] = sizeof(struct sockaddr_in6);
	}
}

FIXTURE_SETUP(bind_wildcard)
{
	setup_addr(self, 0, variant->family[0], variant->addr[0]);
	setup_addr(self, 1, variant->family[1], variant->addr[1]);
}

FIXTURE_TEARDOWN(bind_wildcard)
{
}

void bind_sockets(struct __test_metadata *_metadata,
		  FIXTURE_DATA(bind_wildcard) *self,
		  int expected_errno,
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
	if (expected_errno) {
		ASSERT_EQ(ret, -1);
		ASSERT_EQ(errno, expected_errno);
	} else {
		ASSERT_EQ(ret, 0);
	}

	close(fd[1]);
	close(fd[0]);
}

TEST_F(bind_wildcard, plain)
{
	bind_sockets(_metadata, self, variant->expected_errno,
		     &self->addr[0].addr, self->addrlen[0],
		     &self->addr[1].addr, self->addrlen[1]);
}

TEST_HARNESS_MAIN
