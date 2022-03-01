// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
/*
 * Tests for sockmap/sockhash holding kTLS sockets.
 */

#include "test_progs.h"

#define MAX_TEST_NAME 80
#define TCP_ULP 31

static int tcp_server(int family)
{
	int err, s;

	s = socket(family, SOCK_STREAM, 0);
	if (CHECK_FAIL(s == -1)) {
		perror("socket");
		return -1;
	}

	err = listen(s, SOMAXCONN);
	if (CHECK_FAIL(err)) {
		perror("listen");
		return -1;
	}

	return s;
}

static int disconnect(int fd)
{
	struct sockaddr unspec = { AF_UNSPEC };

	return connect(fd, &unspec, sizeof(unspec));
}

/* Disconnect (unhash) a kTLS socket after removing it from sockmap. */
static void test_sockmap_ktls_disconnect_after_delete(int family, int map)
{
	struct sockaddr_storage addr = {0};
	socklen_t len = sizeof(addr);
	int err, cli, srv, zero = 0;

	srv = tcp_server(family);
	if (srv == -1)
		return;

	err = getsockname(srv, (struct sockaddr *)&addr, &len);
	if (CHECK_FAIL(err)) {
		perror("getsockopt");
		goto close_srv;
	}

	cli = socket(family, SOCK_STREAM, 0);
	if (CHECK_FAIL(cli == -1)) {
		perror("socket");
		goto close_srv;
	}

	err = connect(cli, (struct sockaddr *)&addr, len);
	if (CHECK_FAIL(err)) {
		perror("connect");
		goto close_cli;
	}

	err = bpf_map_update_elem(map, &zero, &cli, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_map_update_elem");
		goto close_cli;
	}

	err = setsockopt(cli, IPPROTO_TCP, TCP_ULP, "tls", strlen("tls"));
	if (CHECK_FAIL(err)) {
		perror("setsockopt(TCP_ULP)");
		goto close_cli;
	}

	err = bpf_map_delete_elem(map, &zero);
	if (CHECK_FAIL(err)) {
		perror("bpf_map_delete_elem");
		goto close_cli;
	}

	err = disconnect(cli);
	if (CHECK_FAIL(err))
		perror("disconnect");

close_cli:
	close(cli);
close_srv:
	close(srv);
}

static void run_tests(int family, enum bpf_map_type map_type)
{
	char test_name[MAX_TEST_NAME];
	int map;

	map = bpf_map_create(map_type, NULL, sizeof(int), sizeof(int), 1, NULL);
	if (CHECK_FAIL(map < 0)) {
		perror("bpf_map_create");
		return;
	}

	snprintf(test_name, MAX_TEST_NAME,
		 "sockmap_ktls disconnect_after_delete %s %s",
		 family == AF_INET ? "IPv4" : "IPv6",
		 map_type == BPF_MAP_TYPE_SOCKMAP ? "SOCKMAP" : "SOCKHASH");
	if (!test__start_subtest(test_name))
		return;

	test_sockmap_ktls_disconnect_after_delete(family, map);

	close(map);
}

void test_sockmap_ktls(void)
{
	run_tests(AF_INET, BPF_MAP_TYPE_SOCKMAP);
	run_tests(AF_INET, BPF_MAP_TYPE_SOCKHASH);
	run_tests(AF_INET6, BPF_MAP_TYPE_SOCKMAP);
	run_tests(AF_INET6, BPF_MAP_TYPE_SOCKHASH);
}
