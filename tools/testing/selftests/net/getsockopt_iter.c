// SPDX-License-Identifier: GPL-2.0
/*
 * Quick test for getsockopt{_iter} tests.
 *
 * Each fixture targets one converted protocol and pins down the
 * returned-length / errno semantics across buffer-size variations,
 * an unknown optname and a bogus level.
 *
 * - netlink: NETLINK_PKTINFO covers the flag-style int path; the
 *   NETLINK_LIST_MEMBERSHIPS cases cover the size-discovery path
 *   that always reports the required buffer length back via optlen,
 *   even when the user buffer is too small to receive any group bits.
 * - vsock:   SO_VM_SOCKETS_BUFFER_SIZE covers the u64 path.
 * - raw:     ICMP_FILTER covers a fixed-size struct payload that clamps
 *            the length down on a short buffer instead of failing.
 *
 * Author: Breno Leitao <leitao@debian.org>
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/time_types.h>
#include <linux/vm_sockets.h>
#include <linux/icmp.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/tls.h>
#include "kselftest_harness.h"

#ifndef AF_VSOCK
#define AF_VSOCK 40
#endif
#ifndef SOL_RAW
#define SOL_RAW 255
#endif
#ifndef ICMP_FILTER
#define ICMP_FILTER 1
#endif
#ifndef IPV6_HDRINCL
#define IPV6_HDRINCL 36
#endif
#ifndef IPV6_CHECKSUM
#define IPV6_CHECKSUM 7
#endif
#ifndef SOL_TLS
#define SOL_TLS 282
#endif
#ifndef TCP_ULP
#define TCP_ULP 31
#endif

/* ---------- netlink ---------- */

FIXTURE(netlink)
{
	int fd;
};

FIXTURE_SETUP(netlink)
{
	int group = RTNLGRP_LINK;

	self->fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (self->fd < 0)
		SKIP(return, "AF_NETLINK socket: %s", strerror(errno));

	/* Joining a multicast group grows nlk->ngroups so the
	 * NETLINK_LIST_MEMBERSHIPS path has a non-zero size to report.
	 */
	if (setsockopt(self->fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
		       &group, sizeof(group)) < 0)
		SKIP(return, "NETLINK_ADD_MEMBERSHIP: %s", strerror(errno));
}

FIXTURE_TEARDOWN(netlink)
{
	if (self->fd >= 0)
		close(self->fd);
}

TEST_F(netlink, pktinfo_exact)
{
	socklen_t optlen;
	int val = -1;

	optlen = sizeof(val);

	ASSERT_EQ(0, getsockopt(self->fd, SOL_NETLINK, NETLINK_PKTINFO,
				&val, &optlen));
	ASSERT_EQ(sizeof(int), optlen);
	ASSERT_TRUE(val == 0 || val == 1);
}

TEST_F(netlink, pktinfo_oversize_clamped)
{
	char buf[16] = {};
	socklen_t optlen;

	optlen = sizeof(buf);

	ASSERT_EQ(0, getsockopt(self->fd, SOL_NETLINK, NETLINK_PKTINFO,
				buf, &optlen));
	ASSERT_EQ(sizeof(int), optlen);
}

TEST_F(netlink, pktinfo_undersize)
{
	char buf[2] = {};
	socklen_t optlen;

	optlen = sizeof(buf);

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_NETLINK, NETLINK_PKTINFO,
				 buf, &optlen));
	ASSERT_EQ(EINVAL, errno);
	ASSERT_EQ(sizeof(buf), optlen);
}

TEST_F(netlink, list_memberships_size_discovery)
{
	socklen_t optlen = 0;
	char dummy;

	ASSERT_EQ(0, getsockopt(self->fd, SOL_NETLINK,
				NETLINK_LIST_MEMBERSHIPS,
				&dummy, &optlen));
	ASSERT_GT(optlen, 0);
	ASSERT_EQ(0, optlen % sizeof(__u32));
}

TEST_F(netlink, list_memberships_full_read)
{
	__u32 buf[64] = {};
	socklen_t optlen;

	optlen = sizeof(buf);

	ASSERT_EQ(0, getsockopt(self->fd, SOL_NETLINK,
				NETLINK_LIST_MEMBERSHIPS,
				buf, &optlen));
	ASSERT_GT(optlen, 0);
	ASSERT_LE(optlen, sizeof(buf));
	ASSERT_EQ(0, optlen % sizeof(__u32));
}

TEST_F(netlink, bad_level)
{
	socklen_t optlen;
	int val;

	optlen = sizeof(val);

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_SOCKET + 1, NETLINK_PKTINFO,
				 &val, &optlen));
	ASSERT_EQ(ENOPROTOOPT, errno);
	ASSERT_EQ(sizeof(val), optlen);
}

TEST_F(netlink, bad_optname)
{
	socklen_t optlen;
	int val;

	optlen = sizeof(val);

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_NETLINK, 0x7fff,
				 &val, &optlen));
	ASSERT_EQ(ENOPROTOOPT, errno);
	ASSERT_EQ(sizeof(val), optlen);
}

/* ---------- vsock ---------- */

FIXTURE(vsock)
{
	int fd;
};

FIXTURE_SETUP(vsock)
{
	self->fd = socket(AF_VSOCK, SOCK_STREAM, 0);
	if (self->fd < 0)
		SKIP(return, "AF_VSOCK socket: %s", strerror(errno));
}

FIXTURE_TEARDOWN(vsock)
{
	if (self->fd >= 0)
		close(self->fd);
}

TEST_F(vsock, buffer_size_exact)
{
	socklen_t optlen;
	uint64_t val = 0;

	optlen = sizeof(val);

	ASSERT_EQ(0, getsockopt(self->fd, AF_VSOCK,
				SO_VM_SOCKETS_BUFFER_SIZE,
				&val, &optlen));
	ASSERT_EQ(sizeof(uint64_t), optlen);
	ASSERT_GT(val, 0);
}

TEST_F(vsock, buffer_size_oversize_clamped)
{
	char buf[16] = {};
	socklen_t optlen;

	optlen = sizeof(buf);

	ASSERT_EQ(0, getsockopt(self->fd, AF_VSOCK,
				SO_VM_SOCKETS_BUFFER_SIZE,
				buf, &optlen));
	ASSERT_EQ(sizeof(uint64_t), optlen);
}

TEST_F(vsock, buffer_size_undersize)
{
	char buf[4] = {};
	socklen_t optlen;

	optlen = sizeof(buf);

	ASSERT_EQ(-1, getsockopt(self->fd, AF_VSOCK,
				 SO_VM_SOCKETS_BUFFER_SIZE,
				 buf, &optlen));
	ASSERT_EQ(EINVAL, errno);
	ASSERT_EQ(sizeof(buf), optlen);
}

TEST_F(vsock, bad_level)
{
	socklen_t optlen;
	uint64_t val;

	optlen = sizeof(val);

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_SOCKET + 1,
				 SO_VM_SOCKETS_BUFFER_SIZE,
				 &val, &optlen));
	ASSERT_EQ(ENOPROTOOPT, errno);
	ASSERT_EQ(sizeof(val), optlen);
}

TEST_F(vsock, bad_optname)
{
	socklen_t optlen;
	uint64_t val;

	optlen = sizeof(val);

	ASSERT_EQ(-1, getsockopt(self->fd, AF_VSOCK, 0x7fff,
				 &val, &optlen));
	ASSERT_EQ(ENOPROTOOPT, errno);
	ASSERT_EQ(sizeof(val), optlen);
}

/* SO_VM_SOCKETS_CONNECT_TIMEOUT_{NEW,OLD} return a sock_timeval-shaped
 * payload, which is wider than u64 on 64-bit. They exercise the path
 * where the protocol's reported lv (16 bytes) is larger than the
 * common 8-byte u64 case covered above.
 */
TEST_F(vsock, connect_timeout_new_exact)
{
	struct __kernel_sock_timeval tv = {};
	socklen_t optlen;

	optlen = sizeof(tv);

	ASSERT_EQ(0, getsockopt(self->fd, AF_VSOCK,
				SO_VM_SOCKETS_CONNECT_TIMEOUT_NEW,
				&tv, &optlen));
	ASSERT_EQ(sizeof(tv), optlen);
}

TEST_F(vsock, connect_timeout_new_oversize_clamped)
{
	char buf[sizeof(struct __kernel_sock_timeval) * 2] = {};
	socklen_t optlen;

	optlen = sizeof(buf);

	ASSERT_EQ(0, getsockopt(self->fd, AF_VSOCK,
				SO_VM_SOCKETS_CONNECT_TIMEOUT_NEW,
				buf, &optlen));
	ASSERT_EQ(sizeof(struct __kernel_sock_timeval), optlen);
}

TEST_F(vsock, connect_timeout_new_undersize)
{
	socklen_t optlen;
	uint64_t val;

	optlen = sizeof(val);

	ASSERT_EQ(-1, getsockopt(self->fd, AF_VSOCK,
				 SO_VM_SOCKETS_CONNECT_TIMEOUT_NEW,
				 &val, &optlen));
	ASSERT_EQ(EINVAL, errno);
	ASSERT_EQ(sizeof(val), optlen);
}

TEST_F(vsock, connect_timeout_old_exact)
{
	struct __kernel_old_timeval tv = {};
	socklen_t optlen;

	optlen = sizeof(tv);

	ASSERT_EQ(0, getsockopt(self->fd, AF_VSOCK,
				SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD,
				&tv, &optlen));
	ASSERT_EQ(sizeof(tv), optlen);
}

/* ---------- raw (ipv4) ---------- */

FIXTURE(raw)
{
	int fd;
};

FIXTURE_SETUP(raw)
{
	struct icmp_filter filt = { .data = 0xdeadbeef };

	self->fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (self->fd < 0)
		SKIP(return, "SOCK_RAW/ICMP socket: %s", strerror(errno));

	if (setsockopt(self->fd, SOL_RAW, ICMP_FILTER, &filt, sizeof(filt)) < 0)
		SKIP(return, "set ICMP_FILTER: %s", strerror(errno));
}

FIXTURE_TEARDOWN(raw)
{
	if (self->fd >= 0)
		close(self->fd);
}

TEST_F(raw, icmpfilter_exact)
{
	struct icmp_filter filt = {};
	socklen_t optlen = sizeof(filt);

	ASSERT_EQ(0, getsockopt(self->fd, SOL_RAW, ICMP_FILTER,
				&filt, &optlen));
	ASSERT_EQ(sizeof(filt), optlen);
	ASSERT_EQ(0xdeadbeef, filt.data);
}

TEST_F(raw, icmpfilter_oversize_clamped)
{
	char buf[16] = {};
	socklen_t optlen = sizeof(buf);

	ASSERT_EQ(0, getsockopt(self->fd, SOL_RAW, ICMP_FILTER,
				buf, &optlen));
	ASSERT_EQ(sizeof(struct icmp_filter), optlen);
}

/* Unlike the int/u64 options above, ICMP_FILTER clamps the length down
 * to the user buffer instead of returning EINVAL: a short buffer
 * succeeds and reports the truncated length back via optlen.
 */
TEST_F(raw, icmpfilter_undersize_clamped)
{
	char buf[2] = {};
	socklen_t optlen = sizeof(buf);

	ASSERT_EQ(0, getsockopt(self->fd, SOL_RAW, ICMP_FILTER,
				buf, &optlen));
	ASSERT_EQ(sizeof(buf), optlen);
}

TEST_F(raw, icmpfilter_wrong_proto)
{
	struct icmp_filter filt;
	socklen_t optlen = sizeof(filt);
	int fd;

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
	if (fd < 0)
		SKIP(return, "SOCK_RAW/UDP socket: %s", strerror(errno));

	ASSERT_EQ(-1, getsockopt(fd, SOL_RAW, ICMP_FILTER, &filt, &optlen));
	ASSERT_EQ(EOPNOTSUPP, errno);
	close(fd);
}

TEST_F(raw, bad_optname)
{
	socklen_t optlen;
	int val;

	optlen = sizeof(val);

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_RAW, 0x7fff, &val, &optlen));
	ASSERT_EQ(ENOPROTOOPT, errno);
	ASSERT_EQ(sizeof(val), optlen);
}

/* ---------- raw (ipv6) ---------- */

FIXTURE(rawv6)
{
	int fd;
};

FIXTURE_SETUP(rawv6)
{
	self->fd = socket(AF_INET6, SOCK_RAW, IPPROTO_UDP);
	if (self->fd < 0)
		SKIP(return, "SOCK_RAW/IPv6 socket: %s", strerror(errno));
}

FIXTURE_TEARDOWN(rawv6)
{
	if (self->fd >= 0)
		close(self->fd);
}

TEST_F(rawv6, hdrincl_exact)
{
	socklen_t optlen;
	int val = -1;

	optlen = sizeof(val);

	ASSERT_EQ(0, getsockopt(self->fd, IPPROTO_IPV6, IPV6_HDRINCL,
				&val, &optlen));
	ASSERT_EQ(sizeof(int), optlen);
	ASSERT_TRUE(val == 0 || val == 1);
}

TEST_F(rawv6, hdrincl_oversize_clamped)
{
	char buf[16] = {};
	socklen_t optlen = sizeof(buf);

	ASSERT_EQ(0, getsockopt(self->fd, IPPROTO_IPV6, IPV6_HDRINCL,
				buf, &optlen));
	ASSERT_EQ(sizeof(int), optlen);
}

/* Raw int options clamp the reported length down to the user buffer
 * instead of returning EINVAL on a short buffer.
 */
TEST_F(rawv6, hdrincl_undersize_clamped)
{
	socklen_t optlen = 2;
	int val = 0;

	ASSERT_EQ(0, getsockopt(self->fd, IPPROTO_IPV6, IPV6_HDRINCL,
				&val, &optlen));
	ASSERT_EQ(2, optlen);
}

TEST_F(rawv6, checksum_default)
{
	socklen_t optlen;
	int val = 0;

	optlen = sizeof(val);

	/* A non-ICMPv6 raw socket has the checksum disabled, reported as -1. */
	ASSERT_EQ(0, getsockopt(self->fd, IPPROTO_IPV6, IPV6_CHECKSUM,
				&val, &optlen));
	ASSERT_EQ(sizeof(int), optlen);
	ASSERT_EQ(-1, val);
}

TEST_F(rawv6, bad_optname)
{
	socklen_t optlen;
	int val;

	optlen = sizeof(val);

	/* SOL_RAW reaches do_rawv6_getsockopt() directly. */
	ASSERT_EQ(-1, getsockopt(self->fd, SOL_RAW, 0x7fff, &val, &optlen));
	ASSERT_EQ(ENOPROTOOPT, errno);
	ASSERT_EQ(sizeof(val), optlen);
}

/* ---------- tls ---------- */

FIXTURE(tls)
{
	int fd;
	int sfd;
};

FIXTURE_SETUP(tls)
{
	struct sockaddr_in a = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	socklen_t alen = sizeof(a);
	int lfd;

	self->fd = -1;
	self->sfd = -1;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0)
		SKIP(return, "TCP socket: %s", strerror(errno));
	if (bind(lfd, (struct sockaddr *)&a, sizeof(a)) || listen(lfd, 1) ||
	    getsockname(lfd, (struct sockaddr *)&a, &alen)) {
		close(lfd);
		SKIP(return, "listener setup: %s", strerror(errno));
	}
	self->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (self->fd < 0) {
		close(lfd);
		SKIP(return, "TCP socket: %s", strerror(errno));
	}
	if (connect(self->fd, (struct sockaddr *)&a, sizeof(a))) {
		close(lfd);
		SKIP(return, "connect: %s", strerror(errno));
	}
	self->sfd = accept(lfd, NULL, NULL);
	close(lfd);
	if (setsockopt(self->fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls")))
		SKIP(return, "TCP_ULP=tls: %s (built without TLS?)",
		     strerror(errno));
}

FIXTURE_TEARDOWN(tls)
{
	if (self->fd >= 0)
		close(self->fd);
	if (self->sfd >= 0)
		close(self->sfd);
}

/* do_tls_getsockopt_tx_zc(): fixed-size int, exact length required. */
TEST_F(tls, tx_zerocopy_exact)
{
	socklen_t optlen = sizeof(int);
	int val = -1;

	ASSERT_EQ(0, getsockopt(self->fd, SOL_TLS, TLS_TX_ZEROCOPY_RO,
				&val, &optlen));
	ASSERT_EQ(sizeof(int), optlen);
	ASSERT_TRUE(val == 0 || val == 1);
}

TEST_F(tls, tx_zerocopy_wrong_len)
{
	socklen_t optlen = 2;
	int val;

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_TLS, TLS_TX_ZEROCOPY_RO,
				 &val, &optlen));
	ASSERT_EQ(EINVAL, errno);
}

/* do_tls_getsockopt_conf(): NULL optval still yields EINVAL -- the
 * converted code tests opt->iter_out.ubuf in place of optval.
 */
TEST_F(tls, conf_null_optval)
{
	socklen_t optlen = 64;

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_TLS, TLS_TX, NULL, &optlen));
	ASSERT_EQ(EINVAL, errno);
}

TEST_F(tls, conf_short)
{
	socklen_t optlen = 2;
	char buf[2];

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_TLS, TLS_TX, buf, &optlen));
	ASSERT_EQ(EINVAL, errno);
}

/* TLS_TX before crypto is set reports not-ready. */
TEST_F(tls, conf_not_ready)
{
	struct tls_crypto_info info;
	socklen_t optlen = sizeof(info);

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_TLS, TLS_TX, &info, &optlen));
	ASSERT_EQ(EBUSY, errno);
}

/* Set TX crypto, then read it back at the base and full sizes, exercising
 * both copy_to_iter() branches. SKIP if AES-GCM is unavailable.
 */
TEST_F(tls, conf_crypto_roundtrip)
{
	struct tls12_crypto_info_aes_gcm_128 tx = {
		.info.version = TLS_1_2_VERSION,
		.info.cipher_type = TLS_CIPHER_AES_GCM_128,
	};
	struct tls12_crypto_info_aes_gcm_128 full;
	struct tls_crypto_info base;
	socklen_t optlen;

	if (setsockopt(self->fd, SOL_TLS, TLS_TX, &tx, sizeof(tx)))
		SKIP(return, "set TLS_TX aes_gcm_128: %s", strerror(errno));

	optlen = sizeof(base);
	ASSERT_EQ(0, getsockopt(self->fd, SOL_TLS, TLS_TX, &base, &optlen));
	ASSERT_EQ(sizeof(base), optlen);
	ASSERT_EQ(TLS_1_2_VERSION, base.version);
	ASSERT_EQ(TLS_CIPHER_AES_GCM_128, base.cipher_type);

	optlen = sizeof(full);
	ASSERT_EQ(0, getsockopt(self->fd, SOL_TLS, TLS_TX, &full, &optlen));
	ASSERT_EQ(sizeof(full), optlen);
	ASSERT_EQ(TLS_CIPHER_AES_GCM_128, full.info.cipher_type);
}

TEST_F(tls, bad_optname)
{
	socklen_t optlen = sizeof(int);
	int val;

	ASSERT_EQ(-1, getsockopt(self->fd, SOL_TLS, 0x7fff, &val, &optlen));
	ASSERT_EQ(ENOPROTOOPT, errno);
}

TEST_HARNESS_MAIN
