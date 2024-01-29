// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2023 Cloudflare

/* Test IP_LOCAL_PORT_RANGE socket option: IPv4 + IPv6, TCP + UDP.
 *
 * Tests assume that net.ipv4.ip_local_port_range is [40000, 49999].
 * Don't run these directly but with ip_local_port_range.sh script.
 */

#include <fcntl.h>
#include <netinet/ip.h>

#include "../kselftest_harness.h"

#ifndef IP_LOCAL_PORT_RANGE
#define IP_LOCAL_PORT_RANGE 51
#endif

static __u32 pack_port_range(__u16 lo, __u16 hi)
{
	return (hi << 16) | (lo << 0);
}

static void unpack_port_range(__u32 range, __u16 *lo, __u16 *hi)
{
	*lo = range & 0xffff;
	*hi = range >> 16;
}

static int get_so_domain(int fd)
{
	int domain, err;
	socklen_t len;

	len = sizeof(domain);
	err = getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &len);
	if (err)
		return -1;

	return domain;
}

static int bind_to_loopback_any_port(int fd)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in v4;
		struct sockaddr_in6 v6;
	} addr;
	socklen_t addr_len;

	memset(&addr, 0, sizeof(addr));
	switch (get_so_domain(fd)) {
	case AF_INET:
		addr.v4.sin_family = AF_INET;
		addr.v4.sin_port = htons(0);
		addr.v4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr_len = sizeof(addr.v4);
		break;
	case AF_INET6:
		addr.v6.sin6_family = AF_INET6;
		addr.v6.sin6_port = htons(0);
		addr.v6.sin6_addr = in6addr_loopback;
		addr_len = sizeof(addr.v6);
		break;
	default:
		return -1;
	}

	return bind(fd, &addr.sa, addr_len);
}

static int get_sock_port(int fd)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in v4;
		struct sockaddr_in6 v6;
	} addr;
	socklen_t addr_len;
	int err;

	addr_len = sizeof(addr);
	memset(&addr, 0, sizeof(addr));
	err = getsockname(fd, &addr.sa, &addr_len);
	if (err)
		return -1;

	switch (addr.sa.sa_family) {
	case AF_INET:
		return ntohs(addr.v4.sin_port);
	case AF_INET6:
		return ntohs(addr.v6.sin6_port);
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}
}

static int get_ip_local_port_range(int fd, __u32 *range)
{
	socklen_t len;
	__u32 val;
	int err;

	len = sizeof(val);
	err = getsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &val, &len);
	if (err)
		return -1;

	*range = val;
	return 0;
}

FIXTURE(ip_local_port_range) {};

FIXTURE_SETUP(ip_local_port_range)
{
}

FIXTURE_TEARDOWN(ip_local_port_range)
{
}

FIXTURE_VARIANT(ip_local_port_range) {
	int so_domain;
	int so_type;
	int so_protocol;
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip4_tcp) {
	.so_domain	= AF_INET,
	.so_type	= SOCK_STREAM,
	.so_protocol	= 0,
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip4_udp) {
	.so_domain	= AF_INET,
	.so_type	= SOCK_DGRAM,
	.so_protocol	= 0,
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip4_stcp) {
	.so_domain	= AF_INET,
	.so_type	= SOCK_STREAM,
	.so_protocol	= IPPROTO_SCTP,
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip4_mptcp) {
	.so_domain	= AF_INET,
	.so_type	= SOCK_STREAM,
	.so_protocol	= IPPROTO_MPTCP,
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip6_tcp) {
	.so_domain	= AF_INET6,
	.so_type	= SOCK_STREAM,
	.so_protocol	= 0,
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip6_udp) {
	.so_domain	= AF_INET6,
	.so_type	= SOCK_DGRAM,
	.so_protocol	= 0,
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip6_stcp) {
	.so_domain	= AF_INET6,
	.so_type	= SOCK_STREAM,
	.so_protocol	= IPPROTO_SCTP,
};

FIXTURE_VARIANT_ADD(ip_local_port_range, ip6_mptcp) {
	.so_domain	= AF_INET6,
	.so_type	= SOCK_STREAM,
	.so_protocol	= IPPROTO_MPTCP,
};

TEST_F(ip_local_port_range, invalid_option_value)
{
	__u16 val16;
	__u32 val32;
	__u64 val64;
	int fd, err;

	fd = socket(variant->so_domain, variant->so_type, variant->so_protocol);
	ASSERT_GE(fd, 0) TH_LOG("socket failed");

	/* Too few bytes */
	val16 = 40000;
	err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &val16, sizeof(val16));
	EXPECT_TRUE(err) TH_LOG("expected setsockopt(IP_LOCAL_PORT_RANGE) to fail");
	EXPECT_EQ(errno, EINVAL);

	/* Empty range: low port > high port */
	val32 = pack_port_range(40222, 40111);
	err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &val32, sizeof(val32));
	EXPECT_TRUE(err) TH_LOG("expected setsockopt(IP_LOCAL_PORT_RANGE) to fail");
	EXPECT_EQ(errno, EINVAL);

	/* Too many bytes */
	val64 = pack_port_range(40333, 40444);
	err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &val64, sizeof(val64));
	EXPECT_TRUE(err) TH_LOG("expected setsockopt(IP_LOCAL_PORT_RANGE) to fail");
	EXPECT_EQ(errno, EINVAL);

	err = close(fd);
	ASSERT_TRUE(!err) TH_LOG("close failed");
}

TEST_F(ip_local_port_range, port_range_out_of_netns_range)
{
	const struct test {
		__u16 range_lo;
		__u16 range_hi;
	} tests[] = {
		{ 30000, 39999 }, /* socket range below netns range */
		{ 50000, 59999 }, /* socket range above netns range */
	};
	const struct test *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		/* Bind a couple of sockets, not just one, to check
		 * that the range wasn't clamped to a single port from
		 * the netns range. That is [40000, 40000] or [49999,
		 * 49999], respectively for each test case.
		 */
		int fds[2], i;

		TH_LOG("lo %5hu, hi %5hu", t->range_lo, t->range_hi);

		for (i = 0; i < ARRAY_SIZE(fds); i++) {
			int fd, err, port;
			__u32 range;

			fd = socket(variant->so_domain, variant->so_type, variant->so_protocol);
			ASSERT_GE(fd, 0) TH_LOG("#%d: socket failed", i);

			range = pack_port_range(t->range_lo, t->range_hi);
			err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &range, sizeof(range));
			ASSERT_TRUE(!err) TH_LOG("#%d: setsockopt(IP_LOCAL_PORT_RANGE) failed", i);

			err = bind_to_loopback_any_port(fd);
			ASSERT_TRUE(!err) TH_LOG("#%d: bind failed", i);

			/* Check that socket port range outside of ephemeral range is ignored */
			port = get_sock_port(fd);
			ASSERT_GE(port, 40000) TH_LOG("#%d: expected port within netns range", i);
			ASSERT_LE(port, 49999) TH_LOG("#%d: expected port within netns range", i);

			fds[i] = fd;
		}

		for (i = 0; i < ARRAY_SIZE(fds); i++)
			ASSERT_TRUE(close(fds[i]) == 0) TH_LOG("#%d: close failed", i);
	}
}

TEST_F(ip_local_port_range, single_port_range)
{
	const struct test {
		__u16 range_lo;
		__u16 range_hi;
		__u16 expected;
	} tests[] = {
		/* single port range within ephemeral range */
		{ 45000, 45000, 45000 },
		/* first port in the ephemeral range (clamp from above) */
		{ 0, 40000, 40000 },
		/* last port in the ephemeral range (clamp from below)  */
		{ 49999, 0, 49999 },
	};
	const struct test *t;

	for (t = tests; t < tests + ARRAY_SIZE(tests); t++) {
		int fd, err, port;
		__u32 range;

		TH_LOG("lo %5hu, hi %5hu, expected %5hu",
		       t->range_lo, t->range_hi, t->expected);

		fd = socket(variant->so_domain, variant->so_type, variant->so_protocol);
		ASSERT_GE(fd, 0) TH_LOG("socket failed");

		range = pack_port_range(t->range_lo, t->range_hi);
		err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &range, sizeof(range));
		ASSERT_TRUE(!err) TH_LOG("setsockopt(IP_LOCAL_PORT_RANGE) failed");

		err = bind_to_loopback_any_port(fd);
		ASSERT_TRUE(!err) TH_LOG("bind failed");

		port = get_sock_port(fd);
		ASSERT_EQ(port, t->expected) TH_LOG("unexpected local port");

		err = close(fd);
		ASSERT_TRUE(!err) TH_LOG("close failed");
	}
}

TEST_F(ip_local_port_range, exhaust_8_port_range)
{
	__u8 port_set = 0;
	int i, fd, err;
	__u32 range;
	__u16 port;
	int fds[8];

	for (i = 0; i < ARRAY_SIZE(fds); i++) {
		fd = socket(variant->so_domain, variant->so_type, variant->so_protocol);
		ASSERT_GE(fd, 0) TH_LOG("socket failed");

		range = pack_port_range(40000, 40007);
		err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &range, sizeof(range));
		ASSERT_TRUE(!err) TH_LOG("setsockopt(IP_LOCAL_PORT_RANGE) failed");

		err = bind_to_loopback_any_port(fd);
		ASSERT_TRUE(!err) TH_LOG("bind failed");

		port = get_sock_port(fd);
		ASSERT_GE(port, 40000) TH_LOG("expected port within sockopt range");
		ASSERT_LE(port, 40007) TH_LOG("expected port within sockopt range");

		port_set |= 1 << (port - 40000);
		fds[i] = fd;
	}

	/* Check that all every port from the test range is in use */
	ASSERT_EQ(port_set, 0xff) TH_LOG("expected all ports to be busy");

	/* Check that bind() fails because the whole range is busy */
	fd = socket(variant->so_domain, variant->so_type, variant->so_protocol);
	ASSERT_GE(fd, 0) TH_LOG("socket failed");

	range = pack_port_range(40000, 40007);
	err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &range, sizeof(range));
	ASSERT_TRUE(!err) TH_LOG("setsockopt(IP_LOCAL_PORT_RANGE) failed");

	err = bind_to_loopback_any_port(fd);
	ASSERT_TRUE(err) TH_LOG("expected bind to fail");
	ASSERT_EQ(errno, EADDRINUSE);

	err = close(fd);
	ASSERT_TRUE(!err) TH_LOG("close failed");

	for (i = 0; i < ARRAY_SIZE(fds); i++) {
		err = close(fds[i]);
		ASSERT_TRUE(!err) TH_LOG("close failed");
	}
}

TEST_F(ip_local_port_range, late_bind)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in v4;
		struct sockaddr_in6 v6;
	} addr;
	socklen_t addr_len;
	const int one = 1;
	int fd, err;
	__u32 range;
	__u16 port;

	if (variant->so_protocol == IPPROTO_SCTP)
		SKIP(return, "SCTP doesn't support IP_BIND_ADDRESS_NO_PORT");

	fd = socket(variant->so_domain, variant->so_type, 0);
	ASSERT_GE(fd, 0) TH_LOG("socket failed");

	range = pack_port_range(40100, 40199);
	err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &range, sizeof(range));
	ASSERT_TRUE(!err) TH_LOG("setsockopt(IP_LOCAL_PORT_RANGE) failed");

	err = setsockopt(fd, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &one, sizeof(one));
	ASSERT_TRUE(!err) TH_LOG("setsockopt(IP_BIND_ADDRESS_NO_PORT) failed");

	err = bind_to_loopback_any_port(fd);
	ASSERT_TRUE(!err) TH_LOG("bind failed");

	port = get_sock_port(fd);
	ASSERT_EQ(port, 0) TH_LOG("getsockname failed");

	/* Invalid destination */
	memset(&addr, 0, sizeof(addr));
	switch (variant->so_domain) {
	case AF_INET:
		addr.v4.sin_family = AF_INET;
		addr.v4.sin_port = htons(0);
		addr.v4.sin_addr.s_addr = htonl(INADDR_ANY);
		addr_len = sizeof(addr.v4);
		break;
	case AF_INET6:
		addr.v6.sin6_family = AF_INET6;
		addr.v6.sin6_port = htons(0);
		addr.v6.sin6_addr = in6addr_any;
		addr_len = sizeof(addr.v6);
		break;
	default:
		ASSERT_TRUE(false) TH_LOG("unsupported socket domain");
	}

	/* connect() doesn't need to succeed for late bind to happen */
	connect(fd, &addr.sa, addr_len);

	port = get_sock_port(fd);
	ASSERT_GE(port, 40100);
	ASSERT_LE(port, 40199);

	err = close(fd);
	ASSERT_TRUE(!err) TH_LOG("close failed");
}

TEST_F(ip_local_port_range, get_port_range)
{
	__u16 lo, hi;
	__u32 range;
	int fd, err;

	fd = socket(variant->so_domain, variant->so_type, variant->so_protocol);
	ASSERT_GE(fd, 0) TH_LOG("socket failed");

	/* Get range before it will be set */
	err = get_ip_local_port_range(fd, &range);
	ASSERT_TRUE(!err) TH_LOG("getsockopt(IP_LOCAL_PORT_RANGE) failed");

	unpack_port_range(range, &lo, &hi);
	ASSERT_EQ(lo, 0) TH_LOG("unexpected low port");
	ASSERT_EQ(hi, 0) TH_LOG("unexpected high port");

	range = pack_port_range(12345, 54321);
	err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &range, sizeof(range));
	ASSERT_TRUE(!err) TH_LOG("setsockopt(IP_LOCAL_PORT_RANGE) failed");

	/* Get range after it has been set */
	err = get_ip_local_port_range(fd, &range);
	ASSERT_TRUE(!err) TH_LOG("getsockopt(IP_LOCAL_PORT_RANGE) failed");

	unpack_port_range(range, &lo, &hi);
	ASSERT_EQ(lo, 12345) TH_LOG("unexpected low port");
	ASSERT_EQ(hi, 54321) TH_LOG("unexpected high port");

	/* Unset the port range  */
	range = pack_port_range(0, 0);
	err = setsockopt(fd, SOL_IP, IP_LOCAL_PORT_RANGE, &range, sizeof(range));
	ASSERT_TRUE(!err) TH_LOG("setsockopt(IP_LOCAL_PORT_RANGE) failed");

	/* Get range after it has been unset */
	err = get_ip_local_port_range(fd, &range);
	ASSERT_TRUE(!err) TH_LOG("getsockopt(IP_LOCAL_PORT_RANGE) failed");

	unpack_port_range(range, &lo, &hi);
	ASSERT_EQ(lo, 0) TH_LOG("unexpected low port");
	ASSERT_EQ(hi, 0) TH_LOG("unexpected high port");

	err = close(fd);
	ASSERT_TRUE(!err) TH_LOG("close failed");
}

TEST_HARNESS_MAIN
