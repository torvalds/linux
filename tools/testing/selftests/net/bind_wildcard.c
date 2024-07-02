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

#define NR_SOCKETS 8

FIXTURE(bind_wildcard)
{
	int fd[NR_SOCKETS];
	socklen_t addrlen[NR_SOCKETS];
	union {
		struct sockaddr addr;
		struct sockaddr_in addr4;
		struct sockaddr_in6 addr6;
	} addr[NR_SOCKETS];
};

FIXTURE_VARIANT(bind_wildcard)
{
	sa_family_t family[2];
	const void *addr[2];
	bool ipv6_only[2];

	/* 6 bind() calls below follow two bind() for the defined 2 addresses:
	 *
	 *   0.0.0.0
	 *   127.0.0.1
	 *   ::
	 *   ::1
	 *   ::ffff:0.0.0.0
	 *   ::ffff:127.0.0.1
	 */
	int expected_errno[NR_SOCKETS];
	int expected_reuse_errno[NR_SOCKETS];
};

/* (IPv4, IPv4) */
FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v4_local)
{
	.family = {AF_INET, AF_INET},
	.addr = {&in4addr_any, &in4addr_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v4_any)
{
	.family = {AF_INET, AF_INET},
	.addr = {&in4addr_loopback, &in4addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

/* (IPv4, IPv6) */
FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_any_only)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_any},
	.ipv6_only = {false, true},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_loopback},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_v4mapped_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_v4mapped_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_any_v6_v4mapped_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_any, &in6addr_v4mapped_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_any_only)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_any},
	.ipv6_only = {false, true},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_loopback},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_v4mapped_any)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_v4mapped_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v4_local_v6_v4mapped_local)
{
	.family = {AF_INET, AF_INET6},
	.addr = {&in4addr_loopback, &in6addr_v4mapped_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

/* (IPv6, IPv4) */
FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_any, &in4addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_only_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_any, &in4addr_any},
	.ipv6_only = {true, false},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_any, &in4addr_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_only_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_any, &in4addr_loopback},
	.ipv6_only = {true, false},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_loopback, &in4addr_any},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_loopback, &in4addr_loopback},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_any, &in4addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_any, &in4addr_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_local_v4_any)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_loopback, &in4addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_local_v4_local)
{
	.family = {AF_INET6, AF_INET},
	.addr = {&in6addr_v4mapped_loopback, &in4addr_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

/* (IPv6, IPv6) */
FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v6_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_only_v6_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_any},
	.ipv6_only = {true, false},
	.expected_errno = {0, EADDRINUSE,
			   0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v6_any_only)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_any},
	.ipv6_only = {false, true},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_only_v6_any_only)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_any},
	.ipv6_only = {true, true},
	.expected_errno = {0, EADDRINUSE,
			   0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 0, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v6_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_only_v6_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_loopback},
	.ipv6_only = {true, false},
	.expected_errno = {0, EADDRINUSE,
			   0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 0, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v6_v4mapped_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_v4mapped_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_only_v6_v4mapped_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_v4mapped_any},
	.ipv6_only = {true, false},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_v6_v4mapped_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_v4mapped_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_any_only_v6_v4mapped_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_any, &in6addr_v4mapped_loopback},
	.ipv6_only = {true, false},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v6_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_loopback, &in6addr_any},
	.expected_errno = {0, EADDRINUSE,
			   0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v6_any_only)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_loopback, &in6addr_any},
	.ipv6_only = {false, true},
	.expected_errno = {0, EADDRINUSE,
			   0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 0, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v6_v4mapped_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_loopback, &in6addr_v4mapped_any},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_local_v6_v4mapped_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_loopback, &in6addr_v4mapped_loopback},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v6_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_any, &in6addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v6_any_only)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_any, &in6addr_any},
	.ipv6_only = {false, true},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v6_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_any, &in6addr_loopback},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_any_v6_v4mapped_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_any, &in6addr_v4mapped_loopback},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_loopback_v6_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_loopback, &in6addr_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_loopback_v6_any_only)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_loopback, &in6addr_any},
	.ipv6_only = {false, true},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_loopback_v6_local)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_loopback, &in6addr_loopback},
	.expected_errno = {0, 0,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, EADDRINUSE},
};

FIXTURE_VARIANT_ADD(bind_wildcard, v6_v4mapped_loopback_v6_v4mapped_any)
{
	.family = {AF_INET6, AF_INET6},
	.addr = {&in6addr_v4mapped_loopback, &in6addr_v4mapped_any},
	.expected_errno = {0, EADDRINUSE,
			   EADDRINUSE, EADDRINUSE,
			   EADDRINUSE, 0,
			   EADDRINUSE, EADDRINUSE},
	.expected_reuse_errno = {0, 0,
				 EADDRINUSE, EADDRINUSE,
				 EADDRINUSE, 0,
				 EADDRINUSE, EADDRINUSE},
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

	setup_addr(self, 2, AF_INET, &in4addr_any);
	setup_addr(self, 3, AF_INET, &in4addr_loopback);

	setup_addr(self, 4, AF_INET6, &in6addr_any);
	setup_addr(self, 5, AF_INET6, &in6addr_loopback);
	setup_addr(self, 6, AF_INET6, &in6addr_v4mapped_any);
	setup_addr(self, 7, AF_INET6, &in6addr_v4mapped_loopback);
}

FIXTURE_TEARDOWN(bind_wildcard)
{
	int i;

	for (i = 0; i < NR_SOCKETS; i++)
		close(self->fd[i]);
}

void bind_socket(struct __test_metadata *_metadata,
		 FIXTURE_DATA(bind_wildcard) *self,
		 const FIXTURE_VARIANT(bind_wildcard) *variant,
		 int i, int reuse)
{
	int ret;

	self->fd[i] = socket(self->addr[i].addr.sa_family, SOCK_STREAM, 0);
	ASSERT_GT(self->fd[i], 0);

	if (i < 2 && variant->ipv6_only[i]) {
		ret = setsockopt(self->fd[i], SOL_IPV6, IPV6_V6ONLY, &(int){1}, sizeof(int));
		ASSERT_EQ(ret, 0);
	}

	if (i < 2 && reuse) {
		ret = setsockopt(self->fd[i], SOL_SOCKET, reuse, &(int){1}, sizeof(int));
		ASSERT_EQ(ret, 0);
	}

	self->addr[i].addr4.sin_port = self->addr[0].addr4.sin_port;

	ret = bind(self->fd[i], &self->addr[i].addr, self->addrlen[i]);

	if (reuse) {
		if (variant->expected_reuse_errno[i]) {
			ASSERT_EQ(ret, -1);
			ASSERT_EQ(errno, variant->expected_reuse_errno[i]);
		} else {
			ASSERT_EQ(ret, 0);
		}
	} else {
		if (variant->expected_errno[i]) {
			ASSERT_EQ(ret, -1);
			ASSERT_EQ(errno, variant->expected_errno[i]);
		} else {
			ASSERT_EQ(ret, 0);
		}
	}

	if (i == 0) {
		ret = getsockname(self->fd[0], &self->addr[0].addr, &self->addrlen[0]);
		ASSERT_EQ(ret, 0);
	}
}

TEST_F(bind_wildcard, plain)
{
	int i;

	for (i = 0; i < NR_SOCKETS; i++)
		bind_socket(_metadata, self, variant, i, 0);
}

TEST_F(bind_wildcard, reuseaddr)
{
	int i;

	for (i = 0; i < NR_SOCKETS; i++)
		bind_socket(_metadata, self, variant, i, SO_REUSEADDR);
}

TEST_F(bind_wildcard, reuseport)
{
	int i;

	for (i = 0; i < NR_SOCKETS; i++)
		bind_socket(_metadata, self, variant, i, SO_REUSEPORT);
}

TEST_HARNESS_MAIN
