// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook
// Copyright (c) 2019 Cloudflare
// Copyright (c) 2020 Isovalent, Inc.
/*
 * Test that the socket assign program is able to redirect traffic towards a
 * socket, regardless of whether the port or address destination of the traffic
 * matches the port.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "test_progs.h"

#define BIND_PORT 1234
#define CONNECT_PORT 4321
#define TEST_DADDR (0xC0A80203)
#define NS_SELF "/proc/self/ns/net"
#define SERVER_MAP_PATH "/sys/fs/bpf/tc/globals/server_map"

static const struct timeval timeo_sec = { .tv_sec = 3 };
static const size_t timeo_optlen = sizeof(timeo_sec);
static int stop, duration;

static bool
configure_stack(void)
{
	char tc_version[128];
	char tc_cmd[BUFSIZ];
	char *prog;
	FILE *tc;

	/* Check whether tc is built with libbpf. */
	tc = popen("tc -V", "r");
	if (CHECK_FAIL(!tc))
		return false;
	if (CHECK_FAIL(!fgets(tc_version, sizeof(tc_version), tc)))
		return false;
	if (strstr(tc_version, ", libbpf "))
		prog = "test_sk_assign_libbpf.bpf.o";
	else
		prog = "test_sk_assign.bpf.o";
	if (CHECK_FAIL(pclose(tc)))
		return false;

	/* Move to a new networking namespace */
	if (CHECK_FAIL(unshare(CLONE_NEWNET)))
		return false;

	/* Configure necessary links, routes */
	if (CHECK_FAIL(system("ip link set dev lo up")))
		return false;
	if (CHECK_FAIL(system("ip route add local default dev lo")))
		return false;
	if (CHECK_FAIL(system("ip -6 route add local default dev lo")))
		return false;

	/* Load qdisc, BPF program */
	if (CHECK_FAIL(system("tc qdisc add dev lo clsact")))
		return false;
	sprintf(tc_cmd, "%s %s %s %s %s", "tc filter add dev lo ingress bpf",
		       "direct-action object-file", prog,
		       "section tc",
		       (env.verbosity < VERBOSE_VERY) ? " 2>/dev/null" : "verbose");
	if (CHECK(system(tc_cmd), "BPF load failed;",
		  "run with -vv for more info\n"))
		return false;

	return true;
}

static int
start_server(const struct sockaddr *addr, socklen_t len, int type)
{
	int fd;

	fd = socket(addr->sa_family, type, 0);
	if (CHECK_FAIL(fd == -1))
		goto out;
	if (CHECK_FAIL(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeo_sec,
				  timeo_optlen)))
		goto close_out;
	if (CHECK_FAIL(bind(fd, addr, len) == -1))
		goto close_out;
	if (type == SOCK_STREAM && CHECK_FAIL(listen(fd, 128) == -1))
		goto close_out;

	goto out;
close_out:
	close(fd);
	fd = -1;
out:
	return fd;
}

static int
connect_to_server(const struct sockaddr *addr, socklen_t len, int type)
{
	int fd = -1;

	fd = socket(addr->sa_family, type, 0);
	if (CHECK_FAIL(fd == -1))
		goto out;
	if (CHECK_FAIL(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeo_sec,
				  timeo_optlen)))
		goto close_out;
	if (CHECK_FAIL(connect(fd, addr, len)))
		goto close_out;

	goto out;
close_out:
	close(fd);
	fd = -1;
out:
	return fd;
}

static in_port_t
get_port(int fd)
{
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	in_port_t port = 0;

	if (CHECK_FAIL(getsockname(fd, (struct sockaddr *)&ss, &slen)))
		return port;

	switch (ss.ss_family) {
	case AF_INET:
		port = ((struct sockaddr_in *)&ss)->sin_port;
		break;
	case AF_INET6:
		port = ((struct sockaddr_in6 *)&ss)->sin6_port;
		break;
	default:
		CHECK(1, "Invalid address family", "%d\n", ss.ss_family);
	}
	return port;
}

static ssize_t
rcv_msg(int srv_client, int type)
{
	char buf[BUFSIZ];

	if (type == SOCK_STREAM)
		return read(srv_client, &buf, sizeof(buf));
	else
		return recvfrom(srv_client, &buf, sizeof(buf), 0, NULL, NULL);
}

static int
run_test(int server_fd, const struct sockaddr *addr, socklen_t len, int type)
{
	int client = -1, srv_client = -1;
	char buf[] = "testing";
	in_port_t port;
	int ret = 1;

	client = connect_to_server(addr, len, type);
	if (client == -1) {
		perror("Cannot connect to server");
		goto out;
	}

	if (type == SOCK_STREAM) {
		srv_client = accept(server_fd, NULL, NULL);
		if (CHECK_FAIL(srv_client == -1)) {
			perror("Can't accept connection");
			goto out;
		}
	} else {
		srv_client = server_fd;
	}
	if (CHECK_FAIL(write(client, buf, sizeof(buf)) != sizeof(buf))) {
		perror("Can't write on client");
		goto out;
	}
	if (CHECK_FAIL(rcv_msg(srv_client, type) != sizeof(buf))) {
		perror("Can't read on server");
		goto out;
	}

	port = get_port(srv_client);
	if (CHECK_FAIL(!port))
		goto out;
	/* SOCK_STREAM is connected via accept(), so the server's local address
	 * will be the CONNECT_PORT rather than the BIND port that corresponds
	 * to the listen socket. SOCK_DGRAM on the other hand is connectionless
	 * so we can't really do the same check there; the server doesn't ever
	 * create a socket with CONNECT_PORT.
	 */
	if (type == SOCK_STREAM &&
	    CHECK(port != htons(CONNECT_PORT), "Expected", "port %u but got %u",
		  CONNECT_PORT, ntohs(port)))
		goto out;
	else if (type == SOCK_DGRAM &&
		 CHECK(port != htons(BIND_PORT), "Expected",
		       "port %u but got %u", BIND_PORT, ntohs(port)))
		goto out;

	ret = 0;
out:
	close(client);
	if (srv_client != server_fd)
		close(srv_client);
	if (ret)
		WRITE_ONCE(stop, 1);
	return ret;
}

static void
prepare_addr(struct sockaddr *addr, int family, __u16 port, bool rewrite_addr)
{
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;

	switch (family) {
	case AF_INET:
		addr4 = (struct sockaddr_in *)addr;
		memset(addr4, 0, sizeof(*addr4));
		addr4->sin_family = family;
		addr4->sin_port = htons(port);
		if (rewrite_addr)
			addr4->sin_addr.s_addr = htonl(TEST_DADDR);
		else
			addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		break;
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)addr;
		memset(addr6, 0, sizeof(*addr6));
		addr6->sin6_family = family;
		addr6->sin6_port = htons(port);
		addr6->sin6_addr = in6addr_loopback;
		if (rewrite_addr)
			addr6->sin6_addr.s6_addr32[3] = htonl(TEST_DADDR);
		break;
	default:
		fprintf(stderr, "Invalid family %d", family);
	}
}

struct test_sk_cfg {
	const char *name;
	int family;
	struct sockaddr *addr;
	socklen_t len;
	int type;
	bool rewrite_addr;
};

#define TEST(NAME, FAMILY, TYPE, REWRITE)				\
{									\
	.name = NAME,							\
	.family = FAMILY,						\
	.addr = (FAMILY == AF_INET) ? (struct sockaddr *)&addr4		\
				    : (struct sockaddr *)&addr6,	\
	.len = (FAMILY == AF_INET) ? sizeof(addr4) : sizeof(addr6),	\
	.type = TYPE,							\
	.rewrite_addr = REWRITE,					\
}

void test_sk_assign(void)
{
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct test_sk_cfg tests[] = {
		TEST("ipv4 tcp port redir", AF_INET, SOCK_STREAM, false),
		TEST("ipv4 tcp addr redir", AF_INET, SOCK_STREAM, true),
		TEST("ipv6 tcp port redir", AF_INET6, SOCK_STREAM, false),
		TEST("ipv6 tcp addr redir", AF_INET6, SOCK_STREAM, true),
		TEST("ipv4 udp port redir", AF_INET, SOCK_DGRAM, false),
		TEST("ipv4 udp addr redir", AF_INET, SOCK_DGRAM, true),
		TEST("ipv6 udp port redir", AF_INET6, SOCK_DGRAM, false),
		TEST("ipv6 udp addr redir", AF_INET6, SOCK_DGRAM, true),
	};
	__s64 server = -1;
	int server_map;
	int self_net;
	int i;

	self_net = open(NS_SELF, O_RDONLY);
	if (CHECK_FAIL(self_net < 0)) {
		perror("Unable to open "NS_SELF);
		return;
	}

	if (!configure_stack()) {
		perror("configure_stack");
		goto cleanup;
	}

	server_map = bpf_obj_get(SERVER_MAP_PATH);
	if (CHECK_FAIL(server_map < 0)) {
		perror("Unable to open " SERVER_MAP_PATH);
		goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(tests) && !READ_ONCE(stop); i++) {
		struct test_sk_cfg *test = &tests[i];
		const struct sockaddr *addr;
		const int zero = 0;
		int err;

		if (!test__start_subtest(test->name))
			continue;
		prepare_addr(test->addr, test->family, BIND_PORT, false);
		addr = (const struct sockaddr *)test->addr;
		server = start_server(addr, test->len, test->type);
		if (server == -1)
			goto close;

		err = bpf_map_update_elem(server_map, &zero, &server, BPF_ANY);
		if (CHECK_FAIL(err)) {
			perror("Unable to update server_map");
			goto close;
		}

		/* connect to unbound ports */
		prepare_addr(test->addr, test->family, CONNECT_PORT,
			     test->rewrite_addr);
		if (run_test(server, addr, test->len, test->type))
			goto close;

		close(server);
		server = -1;
	}

close:
	close(server);
	close(server_map);
cleanup:
	if (CHECK_FAIL(unlink(SERVER_MAP_PATH)))
		perror("Unable to unlink " SERVER_MAP_PATH);
	if (CHECK_FAIL(setns(self_net, CLONE_NEWNET)))
		perror("Failed to setns("NS_SELF")");
	close(self_net);
}
