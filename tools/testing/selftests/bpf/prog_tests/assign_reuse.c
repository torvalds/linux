// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */
#include <uapi/linux/if_link.h>
#include <test_progs.h>

#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "network_helpers.h"
#include "test_assign_reuse.skel.h"

#define NS_TEST "assign_reuse"
#define LOOPBACK 1
#define PORT 4443

static int attach_reuseport(int sock_fd, int prog_fd)
{
	return setsockopt(sock_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF,
			  &prog_fd, sizeof(prog_fd));
}

static __u64 cookie(int fd)
{
	__u64 cookie = 0;
	socklen_t cookie_len = sizeof(cookie);
	int ret;

	ret = getsockopt(fd, SOL_SOCKET, SO_COOKIE, &cookie, &cookie_len);
	ASSERT_OK(ret, "cookie");
	ASSERT_GT(cookie, 0, "cookie_invalid");

	return cookie;
}

static int echo_test_udp(int fd_sv)
{
	struct sockaddr_storage addr = {};
	socklen_t len = sizeof(addr);
	char buff[1] = {};
	int fd_cl = -1, ret;

	fd_cl = connect_to_fd(fd_sv, 100);
	ASSERT_GT(fd_cl, 0, "create_client");
	ASSERT_EQ(getsockname(fd_cl, (void *)&addr, &len), 0, "getsockname");

	ASSERT_EQ(send(fd_cl, buff, sizeof(buff), 0), 1, "send_client");

	ret = recv(fd_sv, buff, sizeof(buff), 0);
	if (ret < 0) {
		close(fd_cl);
		return errno;
	}

	ASSERT_EQ(ret, 1, "recv_server");
	ASSERT_EQ(sendto(fd_sv, buff, sizeof(buff), 0, (void *)&addr, len), 1, "send_server");
	ASSERT_EQ(recv(fd_cl, buff, sizeof(buff), 0), 1, "recv_client");
	close(fd_cl);
	return 0;
}

static int echo_test_tcp(int fd_sv)
{
	char buff[1] = {};
	int fd_cl = -1, fd_sv_cl = -1;

	fd_cl = connect_to_fd(fd_sv, 100);
	if (fd_cl < 0)
		return errno;

	fd_sv_cl = accept(fd_sv, NULL, NULL);
	ASSERT_GE(fd_sv_cl, 0, "accept_fd");

	ASSERT_EQ(send(fd_cl, buff, sizeof(buff), 0), 1, "send_client");
	ASSERT_EQ(recv(fd_sv_cl, buff, sizeof(buff), 0), 1, "recv_server");
	ASSERT_EQ(send(fd_sv_cl, buff, sizeof(buff), 0), 1, "send_server");
	ASSERT_EQ(recv(fd_cl, buff, sizeof(buff), 0), 1, "recv_client");
	close(fd_sv_cl);
	close(fd_cl);
	return 0;
}

void run_assign_reuse(int family, int sotype, const char *ip, __u16 port)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, tc_hook,
		.ifindex = LOOPBACK,
		.attach_point = BPF_TC_INGRESS,
	);
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, tc_opts,
		.handle = 1,
		.priority = 1,
	);
	bool hook_created = false, tc_attached = false;
	int ret, fd_tc, fd_accept, fd_drop, fd_map;
	int *fd_sv = NULL;
	__u64 fd_val;
	struct test_assign_reuse *skel;
	const int zero = 0;

	skel = test_assign_reuse__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	skel->rodata->dest_port = port;

	ret = test_assign_reuse__load(skel);
	if (!ASSERT_OK(ret, "skel_load"))
		goto cleanup;

	ASSERT_EQ(skel->bss->sk_cookie_seen, 0, "cookie_init");

	fd_tc = bpf_program__fd(skel->progs.tc_main);
	fd_accept = bpf_program__fd(skel->progs.reuse_accept);
	fd_drop = bpf_program__fd(skel->progs.reuse_drop);
	fd_map = bpf_map__fd(skel->maps.sk_map);

	fd_sv = start_reuseport_server(family, sotype, ip, port, 100, 1);
	if (!ASSERT_NEQ(fd_sv, NULL, "start_reuseport_server"))
		goto cleanup;

	ret = attach_reuseport(*fd_sv, fd_drop);
	if (!ASSERT_OK(ret, "attach_reuseport"))
		goto cleanup;

	fd_val = *fd_sv;
	ret = bpf_map_update_elem(fd_map, &zero, &fd_val, BPF_NOEXIST);
	if (!ASSERT_OK(ret, "bpf_sk_map"))
		goto cleanup;

	ret = bpf_tc_hook_create(&tc_hook);
	if (ret == 0)
		hook_created = true;
	ret = ret == -EEXIST ? 0 : ret;
	if (!ASSERT_OK(ret, "bpf_tc_hook_create"))
		goto cleanup;

	tc_opts.prog_fd = fd_tc;
	ret = bpf_tc_attach(&tc_hook, &tc_opts);
	if (!ASSERT_OK(ret, "bpf_tc_attach"))
		goto cleanup;
	tc_attached = true;

	if (sotype == SOCK_STREAM)
		ASSERT_EQ(echo_test_tcp(*fd_sv), ECONNREFUSED, "drop_tcp");
	else
		ASSERT_EQ(echo_test_udp(*fd_sv), EAGAIN, "drop_udp");
	ASSERT_EQ(skel->bss->reuseport_executed, 1, "program executed once");

	skel->bss->sk_cookie_seen = 0;
	skel->bss->reuseport_executed = 0;
	ASSERT_OK(attach_reuseport(*fd_sv, fd_accept), "attach_reuseport(accept)");

	if (sotype == SOCK_STREAM)
		ASSERT_EQ(echo_test_tcp(*fd_sv), 0, "echo_tcp");
	else
		ASSERT_EQ(echo_test_udp(*fd_sv), 0, "echo_udp");

	ASSERT_EQ(skel->bss->sk_cookie_seen, cookie(*fd_sv),
		  "cookie_mismatch");
	ASSERT_EQ(skel->bss->reuseport_executed, 1, "program executed once");
cleanup:
	if (tc_attached) {
		tc_opts.flags = tc_opts.prog_fd = tc_opts.prog_id = 0;
		ret = bpf_tc_detach(&tc_hook, &tc_opts);
		ASSERT_OK(ret, "bpf_tc_detach");
	}
	if (hook_created) {
		tc_hook.attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS;
		bpf_tc_hook_destroy(&tc_hook);
	}
	test_assign_reuse__destroy(skel);
	free_fds(fd_sv, 1);
}

void test_assign_reuse(void)
{
	struct nstoken *tok = NULL;

	SYS(out, "ip netns add %s", NS_TEST);
	SYS(cleanup, "ip -net %s link set dev lo up", NS_TEST);

	tok = open_netns(NS_TEST);
	if (!ASSERT_OK_PTR(tok, "netns token"))
		return;

	if (test__start_subtest("tcpv4"))
		run_assign_reuse(AF_INET, SOCK_STREAM, "127.0.0.1", PORT);
	if (test__start_subtest("tcpv6"))
		run_assign_reuse(AF_INET6, SOCK_STREAM, "::1", PORT);
	if (test__start_subtest("udpv4"))
		run_assign_reuse(AF_INET, SOCK_DGRAM, "127.0.0.1", PORT);
	if (test__start_subtest("udpv6"))
		run_assign_reuse(AF_INET6, SOCK_DGRAM, "::1", PORT);

cleanup:
	close_netns(tok);
	SYS_NOFAIL("ip netns delete %s", NS_TEST);
out:
	return;
}
