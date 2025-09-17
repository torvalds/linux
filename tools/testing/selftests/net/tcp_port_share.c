// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2025 Cloudflare, Inc.

/* Tests for TCP port sharing (bind bucket reuse). */

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>

#include "../kselftest_harness.h"

#define DST_PORT 30000
#define SRC_PORT 40000

struct sockaddr_inet {
	union {
		struct sockaddr_storage ss;
		struct sockaddr_in6 v6;
		struct sockaddr_in v4;
		struct sockaddr sa;
	};
	socklen_t len;
	char str[INET6_ADDRSTRLEN + __builtin_strlen("[]:65535") + 1];
};

const int one = 1;

static int disconnect(int fd)
{
	return connect(fd, &(struct sockaddr){ AF_UNSPEC }, sizeof(struct sockaddr));
}

static int getsockname_port(int fd)
{
	struct sockaddr_inet addr = {};
	int err;

	addr.len = sizeof(addr);
	err = getsockname(fd, &addr.sa, &addr.len);
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

static void make_inet_addr(int af, const char *ip, __u16 port,
			   struct sockaddr_inet *addr)
{
	const char *fmt = "";

	memset(addr, 0, sizeof(*addr));

	switch (af) {
	case AF_INET:
		addr->len = sizeof(addr->v4);
		addr->v4.sin_family = af;
		addr->v4.sin_port = htons(port);
		inet_pton(af, ip, &addr->v4.sin_addr);
		fmt = "%s:%hu";
		break;
	case AF_INET6:
		addr->len = sizeof(addr->v6);
		addr->v6.sin6_family = af;
		addr->v6.sin6_port = htons(port);
		inet_pton(af, ip, &addr->v6.sin6_addr);
		fmt = "[%s]:%hu";
		break;
	}

	snprintf(addr->str, sizeof(addr->str), fmt, ip, port);
}

FIXTURE(tcp_port_share) {};

FIXTURE_VARIANT(tcp_port_share) {
	int domain;
	/* IP to listen on and connect to */
	const char *dst_ip;
	/* Primary IP to connect from */
	const char *src1_ip;
	/* Secondary IP to connect from */
	const char *src2_ip;
	/* IP to bind to in order to block the source port */
	const char *bind_ip;
};

FIXTURE_VARIANT_ADD(tcp_port_share, ipv4) {
	.domain = AF_INET,
	.dst_ip = "127.0.0.1",
	.src1_ip = "127.1.1.1",
	.src2_ip = "127.2.2.2",
	.bind_ip = "127.3.3.3",
};

FIXTURE_VARIANT_ADD(tcp_port_share, ipv6) {
	.domain = AF_INET6,
	.dst_ip = "::1",
	.src1_ip = "2001:db8::1",
	.src2_ip = "2001:db8::2",
	.bind_ip = "2001:db8::3",
};

FIXTURE_SETUP(tcp_port_share)
{
	int sc;

	ASSERT_EQ(unshare(CLONE_NEWNET), 0);
	ASSERT_EQ(system("ip link set dev lo up"), 0);
	ASSERT_EQ(system("ip addr add dev lo 2001:db8::1/32 nodad"), 0);
	ASSERT_EQ(system("ip addr add dev lo 2001:db8::2/32 nodad"), 0);
	ASSERT_EQ(system("ip addr add dev lo 2001:db8::3/32 nodad"), 0);

	sc = open("/proc/sys/net/ipv4/ip_local_port_range", O_WRONLY);
	ASSERT_GE(sc, 0);
	ASSERT_GT(dprintf(sc, "%hu %hu\n", SRC_PORT, SRC_PORT), 0);
	ASSERT_EQ(close(sc), 0);
}

FIXTURE_TEARDOWN(tcp_port_share) {}

/* Verify that an ephemeral port becomes available again after the socket
 * bound to it and blocking it from reuse is closed.
 */
TEST_F(tcp_port_share, can_reuse_port_after_bind_and_close)
{
	const typeof(variant) v = variant;
	struct sockaddr_inet addr;
	int c1, c2, ln, pb;

	/* Listen on <dst_ip>:<DST_PORT> */
	ln = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(ln, 0) TH_LOG("socket(): %m");
	ASSERT_EQ(setsockopt(ln, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->dst_ip, DST_PORT, &addr);
	ASSERT_EQ(bind(ln, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);
	ASSERT_EQ(listen(ln, 2), 0);

	/* Connect from <src1_ip>:<SRC_PORT> */
	c1 = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(c1, 0) TH_LOG("socket(): %m");
	ASSERT_EQ(setsockopt(c1, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->src1_ip, 0, &addr);
	ASSERT_EQ(bind(c1, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);

	make_inet_addr(v->domain, v->dst_ip, DST_PORT, &addr);
	ASSERT_EQ(connect(c1, &addr.sa, addr.len), 0) TH_LOG("connect(%s): %m", addr.str);
	ASSERT_EQ(getsockname_port(c1), SRC_PORT);

	/* Bind to <bind_ip>:<SRC_PORT>. Block the port from reuse. */
	pb = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(pb, 0) TH_LOG("socket(): %m");
	ASSERT_EQ(setsockopt(pb, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->bind_ip, SRC_PORT, &addr);
	ASSERT_EQ(bind(pb, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);

	/* Try to connect from <src2_ip>:<SRC_PORT>. Expect failure. */
	c2 = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(c2, 0) TH_LOG("socket");
	ASSERT_EQ(setsockopt(c2, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->src2_ip, 0, &addr);
	ASSERT_EQ(bind(c2, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);

	make_inet_addr(v->domain, v->dst_ip, DST_PORT, &addr);
	ASSERT_EQ(connect(c2, &addr.sa, addr.len), -1) TH_LOG("connect(%s)", addr.str);
	ASSERT_EQ(errno, EADDRNOTAVAIL) TH_LOG("%m");

	/* Unbind from <bind_ip>:<SRC_PORT>. Unblock the port for reuse. */
	ASSERT_EQ(close(pb), 0);

	/* Connect again from <src2_ip>:<SRC_PORT> */
	EXPECT_EQ(connect(c2, &addr.sa, addr.len), 0) TH_LOG("connect(%s): %m", addr.str);
	EXPECT_EQ(getsockname_port(c2), SRC_PORT);

	ASSERT_EQ(close(c2), 0);
	ASSERT_EQ(close(c1), 0);
	ASSERT_EQ(close(ln), 0);
}

/* Verify that a socket auto-bound during connect() blocks port reuse after
 * disconnect (connect(AF_UNSPEC)) followed by an explicit port bind().
 */
TEST_F(tcp_port_share, port_block_after_disconnect)
{
	const typeof(variant) v = variant;
	struct sockaddr_inet addr;
	int c1, c2, ln, pb;

	/* Listen on <dst_ip>:<DST_PORT> */
	ln = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(ln, 0) TH_LOG("socket(): %m");
	ASSERT_EQ(setsockopt(ln, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->dst_ip, DST_PORT, &addr);
	ASSERT_EQ(bind(ln, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);
	ASSERT_EQ(listen(ln, 2), 0);

	/* Connect from <src1_ip>:<SRC_PORT> */
	c1 = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(c1, 0) TH_LOG("socket(): %m");
	ASSERT_EQ(setsockopt(c1, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->src1_ip, 0, &addr);
	ASSERT_EQ(bind(c1, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);

	make_inet_addr(v->domain, v->dst_ip, DST_PORT, &addr);
	ASSERT_EQ(connect(c1, &addr.sa, addr.len), 0) TH_LOG("connect(%s): %m", addr.str);
	ASSERT_EQ(getsockname_port(c1), SRC_PORT);

	/* Disconnect the socket and bind it to <bind_ip>:<SRC_PORT> to block the port */
	ASSERT_EQ(disconnect(c1), 0) TH_LOG("disconnect: %m");
	ASSERT_EQ(setsockopt(c1, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->bind_ip, SRC_PORT, &addr);
	ASSERT_EQ(bind(c1, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);

	/* Trigger port-addr bucket state update with another bind() and close() */
	pb = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(pb, 0) TH_LOG("socket(): %m");
	ASSERT_EQ(setsockopt(pb, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->bind_ip, SRC_PORT, &addr);
	ASSERT_EQ(bind(pb, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);

	ASSERT_EQ(close(pb), 0);

	/* Connect from <src2_ip>:<SRC_PORT>. Expect failure. */
	c2 = socket(v->domain, SOCK_STREAM, 0);
	ASSERT_GE(c2, 0) TH_LOG("socket: %m");
	ASSERT_EQ(setsockopt(c2, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &one, sizeof(one)), 0);

	make_inet_addr(v->domain, v->src2_ip, 0, &addr);
	ASSERT_EQ(bind(c2, &addr.sa, addr.len), 0) TH_LOG("bind(%s): %m", addr.str);

	make_inet_addr(v->domain, v->dst_ip, DST_PORT, &addr);
	EXPECT_EQ(connect(c2, &addr.sa, addr.len), -1) TH_LOG("connect(%s)", addr.str);
	EXPECT_EQ(errno, EADDRNOTAVAIL) TH_LOG("%m");

	ASSERT_EQ(close(c2), 0);
	ASSERT_EQ(close(c1), 0);
	ASSERT_EQ(close(ln), 0);
}

TEST_HARNESS_MAIN
