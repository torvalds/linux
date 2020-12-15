// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2020 Cloudflare

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <string.h>

#include <linux/pkt_cls.h>

#include <test_progs.h>

#include "progs/test_cls_redirect.h"
#include "test_cls_redirect.skel.h"

#define ENCAP_IP INADDR_LOOPBACK
#define ENCAP_PORT (1234)

struct addr_port {
	in_port_t port;
	union {
		struct in_addr in_addr;
		struct in6_addr in6_addr;
	};
};

struct tuple {
	int family;
	struct addr_port src;
	struct addr_port dst;
};

static int start_server(const struct sockaddr *addr, socklen_t len, int type)
{
	int fd = socket(addr->sa_family, type, 0);
	if (CHECK_FAIL(fd == -1))
		return -1;
	if (CHECK_FAIL(bind(fd, addr, len) == -1))
		goto err;
	if (type == SOCK_STREAM && CHECK_FAIL(listen(fd, 128) == -1))
		goto err;

	return fd;

err:
	close(fd);
	return -1;
}

static int connect_to_server(const struct sockaddr *addr, socklen_t len,
			     int type)
{
	int fd = socket(addr->sa_family, type, 0);
	if (CHECK_FAIL(fd == -1))
		return -1;
	if (CHECK_FAIL(connect(fd, addr, len)))
		goto err;

	return fd;

err:
	close(fd);
	return -1;
}

static bool fill_addr_port(const struct sockaddr *sa, struct addr_port *ap)
{
	const struct sockaddr_in6 *in6;
	const struct sockaddr_in *in;

	switch (sa->sa_family) {
	case AF_INET:
		in = (const struct sockaddr_in *)sa;
		ap->in_addr = in->sin_addr;
		ap->port = in->sin_port;
		return true;

	case AF_INET6:
		in6 = (const struct sockaddr_in6 *)sa;
		ap->in6_addr = in6->sin6_addr;
		ap->port = in6->sin6_port;
		return true;

	default:
		return false;
	}
}

static bool set_up_conn(const struct sockaddr *addr, socklen_t len, int type,
			int *server, int *conn, struct tuple *tuple)
{
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	struct sockaddr *sa = (struct sockaddr *)&ss;

	*server = start_server(addr, len, type);
	if (*server < 0)
		return false;

	if (CHECK_FAIL(getsockname(*server, sa, &slen)))
		goto close_server;

	*conn = connect_to_server(sa, slen, type);
	if (*conn < 0)
		goto close_server;

	/* We want to simulate packets arriving at conn, so we have to
	 * swap src and dst.
	 */
	slen = sizeof(ss);
	if (CHECK_FAIL(getsockname(*conn, sa, &slen)))
		goto close_conn;

	if (CHECK_FAIL(!fill_addr_port(sa, &tuple->dst)))
		goto close_conn;

	slen = sizeof(ss);
	if (CHECK_FAIL(getpeername(*conn, sa, &slen)))
		goto close_conn;

	if (CHECK_FAIL(!fill_addr_port(sa, &tuple->src)))
		goto close_conn;

	tuple->family = ss.ss_family;
	return true;

close_conn:
	close(*conn);
	*conn = -1;
close_server:
	close(*server);
	*server = -1;
	return false;
}

static socklen_t prepare_addr(struct sockaddr_storage *addr, int family)
{
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;

	switch (family) {
	case AF_INET:
		addr4 = (struct sockaddr_in *)addr;
		memset(addr4, 0, sizeof(*addr4));
		addr4->sin_family = family;
		addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		return sizeof(*addr4);
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)addr;
		memset(addr6, 0, sizeof(*addr6));
		addr6->sin6_family = family;
		addr6->sin6_addr = in6addr_loopback;
		return sizeof(*addr6);
	default:
		fprintf(stderr, "Invalid family %d", family);
		return 0;
	}
}

static bool was_decapsulated(struct bpf_prog_test_run_attr *tattr)
{
	return tattr->data_size_out < tattr->data_size_in;
}

enum type {
	UDP,
	TCP,
	__NR_KIND,
};

enum hops {
	NO_HOPS,
	ONE_HOP,
};

enum flags {
	NONE,
	SYN,
	ACK,
};

enum conn {
	KNOWN_CONN,
	UNKNOWN_CONN,
};

enum result {
	ACCEPT,
	FORWARD,
};

struct test_cfg {
	enum type type;
	enum result result;
	enum conn conn;
	enum hops hops;
	enum flags flags;
};

static int test_str(void *buf, size_t len, const struct test_cfg *test,
		    int family)
{
	const char *family_str, *type, *conn, *hops, *result, *flags;

	family_str = "IPv4";
	if (family == AF_INET6)
		family_str = "IPv6";

	type = "TCP";
	if (test->type == UDP)
		type = "UDP";

	conn = "known";
	if (test->conn == UNKNOWN_CONN)
		conn = "unknown";

	hops = "no hops";
	if (test->hops == ONE_HOP)
		hops = "one hop";

	result = "accept";
	if (test->result == FORWARD)
		result = "forward";

	flags = "none";
	if (test->flags == SYN)
		flags = "SYN";
	else if (test->flags == ACK)
		flags = "ACK";

	return snprintf(buf, len, "%s %s %s %s (%s, flags: %s)", family_str,
			type, result, conn, hops, flags);
}

static struct test_cfg tests[] = {
	{ TCP, ACCEPT, UNKNOWN_CONN, NO_HOPS, SYN },
	{ TCP, ACCEPT, UNKNOWN_CONN, NO_HOPS, ACK },
	{ TCP, FORWARD, UNKNOWN_CONN, ONE_HOP, ACK },
	{ TCP, ACCEPT, KNOWN_CONN, ONE_HOP, ACK },
	{ UDP, ACCEPT, UNKNOWN_CONN, NO_HOPS, NONE },
	{ UDP, FORWARD, UNKNOWN_CONN, ONE_HOP, NONE },
	{ UDP, ACCEPT, KNOWN_CONN, ONE_HOP, NONE },
};

static void encap_init(encap_headers_t *encap, uint8_t hop_count, uint8_t proto)
{
	const uint8_t hlen =
		(sizeof(struct guehdr) / sizeof(uint32_t)) + hop_count;
	*encap = (encap_headers_t){
		.eth = { .h_proto = htons(ETH_P_IP) },
		.ip = {
			.ihl = 5,
			.version = 4,
			.ttl = IPDEFTTL,
			.protocol = IPPROTO_UDP,
			.daddr = htonl(ENCAP_IP)
		},
		.udp = {
			.dest = htons(ENCAP_PORT),
		},
		.gue = {
			.hlen = hlen,
			.proto_ctype = proto
		},
		.unigue = {
			.hop_count = hop_count
		},
	};
}

static size_t build_input(const struct test_cfg *test, void *const buf,
			  const struct tuple *tuple)
{
	in_port_t sport = tuple->src.port;
	encap_headers_t encap;
	struct iphdr ip;
	struct ipv6hdr ipv6;
	struct tcphdr tcp;
	struct udphdr udp;
	struct in_addr next_hop;
	uint8_t *p = buf;
	int proto;

	proto = IPPROTO_IPIP;
	if (tuple->family == AF_INET6)
		proto = IPPROTO_IPV6;

	encap_init(&encap, test->hops == ONE_HOP ? 1 : 0, proto);
	p = mempcpy(p, &encap, sizeof(encap));

	if (test->hops == ONE_HOP) {
		next_hop = (struct in_addr){ .s_addr = htonl(0x7f000002) };
		p = mempcpy(p, &next_hop, sizeof(next_hop));
	}

	proto = IPPROTO_TCP;
	if (test->type == UDP)
		proto = IPPROTO_UDP;

	switch (tuple->family) {
	case AF_INET:
		ip = (struct iphdr){
			.ihl = 5,
			.version = 4,
			.ttl = IPDEFTTL,
			.protocol = proto,
			.saddr = tuple->src.in_addr.s_addr,
			.daddr = tuple->dst.in_addr.s_addr,
		};
		p = mempcpy(p, &ip, sizeof(ip));
		break;
	case AF_INET6:
		ipv6 = (struct ipv6hdr){
			.version = 6,
			.hop_limit = IPDEFTTL,
			.nexthdr = proto,
			.saddr = tuple->src.in6_addr,
			.daddr = tuple->dst.in6_addr,
		};
		p = mempcpy(p, &ipv6, sizeof(ipv6));
		break;
	default:
		return 0;
	}

	if (test->conn == UNKNOWN_CONN)
		sport--;

	switch (test->type) {
	case TCP:
		tcp = (struct tcphdr){
			.source = sport,
			.dest = tuple->dst.port,
		};
		if (test->flags == SYN)
			tcp.syn = true;
		if (test->flags == ACK)
			tcp.ack = true;
		p = mempcpy(p, &tcp, sizeof(tcp));
		break;
	case UDP:
		udp = (struct udphdr){
			.source = sport,
			.dest = tuple->dst.port,
		};
		p = mempcpy(p, &udp, sizeof(udp));
		break;
	default:
		return 0;
	}

	return (void *)p - buf;
}

static void close_fds(int *fds, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (fds[i] > 0)
			close(fds[i]);
}

void test_cls_redirect(void)
{
	struct test_cls_redirect *skel = NULL;
	struct bpf_prog_test_run_attr tattr = {};
	int families[] = { AF_INET, AF_INET6 };
	struct sockaddr_storage ss;
	struct sockaddr *addr;
	socklen_t slen;
	int i, j, err;

	int servers[__NR_KIND][ARRAY_SIZE(families)] = {};
	int conns[__NR_KIND][ARRAY_SIZE(families)] = {};
	struct tuple tuples[__NR_KIND][ARRAY_SIZE(families)];

	skel = test_cls_redirect__open();
	if (CHECK_FAIL(!skel))
		return;

	skel->rodata->ENCAPSULATION_IP = htonl(ENCAP_IP);
	skel->rodata->ENCAPSULATION_PORT = htons(ENCAP_PORT);

	if (CHECK_FAIL(test_cls_redirect__load(skel)))
		goto cleanup;

	addr = (struct sockaddr *)&ss;
	for (i = 0; i < ARRAY_SIZE(families); i++) {
		slen = prepare_addr(&ss, families[i]);
		if (CHECK_FAIL(!slen))
			goto cleanup;

		if (CHECK_FAIL(!set_up_conn(addr, slen, SOCK_DGRAM,
					    &servers[UDP][i], &conns[UDP][i],
					    &tuples[UDP][i])))
			goto cleanup;

		if (CHECK_FAIL(!set_up_conn(addr, slen, SOCK_STREAM,
					    &servers[TCP][i], &conns[TCP][i],
					    &tuples[TCP][i])))
			goto cleanup;
	}

	tattr.prog_fd = bpf_program__fd(skel->progs.cls_redirect);
	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		struct test_cfg *test = &tests[i];

		for (j = 0; j < ARRAY_SIZE(families); j++) {
			struct tuple *tuple = &tuples[test->type][j];
			char input[256];
			char tmp[256];

			test_str(tmp, sizeof(tmp), test, tuple->family);
			if (!test__start_subtest(tmp))
				continue;

			tattr.data_out = tmp;
			tattr.data_size_out = sizeof(tmp);

			tattr.data_in = input;
			tattr.data_size_in = build_input(test, input, tuple);
			if (CHECK_FAIL(!tattr.data_size_in))
				continue;

			err = bpf_prog_test_run_xattr(&tattr);
			if (CHECK_FAIL(err))
				continue;

			if (tattr.retval != TC_ACT_REDIRECT) {
				PRINT_FAIL("expected TC_ACT_REDIRECT, got %d\n",
					   tattr.retval);
				continue;
			}

			switch (test->result) {
			case ACCEPT:
				if (CHECK_FAIL(!was_decapsulated(&tattr)))
					continue;
				break;
			case FORWARD:
				if (CHECK_FAIL(was_decapsulated(&tattr)))
					continue;
				break;
			default:
				PRINT_FAIL("unknown result %d\n", test->result);
				continue;
			}
		}
	}

cleanup:
	test_cls_redirect__destroy(skel);
	close_fds((int *)servers, sizeof(servers) / sizeof(servers[0][0]));
	close_fds((int *)conns, sizeof(conns) / sizeof(conns[0][0]));
}
