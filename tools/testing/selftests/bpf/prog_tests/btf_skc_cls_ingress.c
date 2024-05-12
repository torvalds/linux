// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#define _GNU_SOURCE
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <net/if.h>
#include <linux/compiler.h>
#include <bpf/libbpf.h>

#include "network_helpers.h"
#include "test_progs.h"
#include "test_btf_skc_cls_ingress.skel.h"

static struct test_btf_skc_cls_ingress *skel;
static struct sockaddr_in6 srv_sa6;
static __u32 duration;

static int prepare_netns(void)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_lo, .attach_point = BPF_TC_INGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_attach,
		    .prog_fd = bpf_program__fd(skel->progs.cls_ingress));

	if (CHECK(unshare(CLONE_NEWNET), "create netns",
		  "unshare(CLONE_NEWNET): %s (%d)",
		  strerror(errno), errno))
		return -1;

	if (CHECK(system("ip link set dev lo up"),
		  "ip link set dev lo up", "failed\n"))
		return -1;

	qdisc_lo.ifindex = if_nametoindex("lo");
	if (!ASSERT_OK(bpf_tc_hook_create(&qdisc_lo), "qdisc add dev lo clsact"))
		return -1;

	if (!ASSERT_OK(bpf_tc_attach(&qdisc_lo, &tc_attach),
		       "filter add dev lo ingress"))
		return -1;

	/* Ensure 20 bytes options (i.e. in total 40 bytes tcp header) for the
	 * bpf_tcp_gen_syncookie() helper.
	 */
	if (write_sysctl("/proc/sys/net/ipv4/tcp_window_scaling", "1") ||
	    write_sysctl("/proc/sys/net/ipv4/tcp_timestamps", "1") ||
	    write_sysctl("/proc/sys/net/ipv4/tcp_sack", "1"))
		return -1;

	return 0;
}

static void reset_test(void)
{
	memset(&skel->bss->srv_sa6, 0, sizeof(skel->bss->srv_sa6));
	skel->bss->listen_tp_sport = 0;
	skel->bss->req_sk_sport = 0;
	skel->bss->recv_cookie = 0;
	skel->bss->gen_cookie = 0;
	skel->bss->linum = 0;
}

static void print_err_line(void)
{
	if (skel->bss->linum)
		printf("bpf prog error at line %u\n", skel->bss->linum);
}

static void test_conn(void)
{
	int listen_fd = -1, cli_fd = -1, srv_fd = -1, err;
	socklen_t addrlen = sizeof(srv_sa6);
	int srv_port;

	if (write_sysctl("/proc/sys/net/ipv4/tcp_syncookies", "1"))
		return;

	listen_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (CHECK_FAIL(listen_fd == -1))
		return;

	err = getsockname(listen_fd, (struct sockaddr *)&srv_sa6, &addrlen);
	if (CHECK(err, "getsockname(listen_fd)", "err:%d errno:%d\n", err,
		  errno))
		goto done;
	memcpy(&skel->bss->srv_sa6, &srv_sa6, sizeof(srv_sa6));
	srv_port = ntohs(srv_sa6.sin6_port);

	cli_fd = connect_to_fd(listen_fd, 0);
	if (CHECK_FAIL(cli_fd == -1))
		goto done;

	srv_fd = accept(listen_fd, NULL, NULL);
	if (CHECK_FAIL(srv_fd == -1))
		goto done;

	if (CHECK(skel->bss->listen_tp_sport != srv_port ||
		  skel->bss->req_sk_sport != srv_port,
		  "Unexpected sk src port",
		  "listen_tp_sport:%u req_sk_sport:%u expected:%u\n",
		  skel->bss->listen_tp_sport, skel->bss->req_sk_sport,
		  srv_port))
		goto done;

	if (CHECK(skel->bss->gen_cookie || skel->bss->recv_cookie,
		  "Unexpected syncookie states",
		  "gen_cookie:%u recv_cookie:%u\n",
		  skel->bss->gen_cookie, skel->bss->recv_cookie))
		goto done;

	CHECK(skel->bss->linum, "bpf prog detected error", "at line %u\n",
	      skel->bss->linum);

done:
	if (listen_fd != -1)
		close(listen_fd);
	if (cli_fd != -1)
		close(cli_fd);
	if (srv_fd != -1)
		close(srv_fd);
}

static void test_syncookie(void)
{
	int listen_fd = -1, cli_fd = -1, srv_fd = -1, err;
	socklen_t addrlen = sizeof(srv_sa6);
	int srv_port;

	/* Enforce syncookie mode */
	if (write_sysctl("/proc/sys/net/ipv4/tcp_syncookies", "2"))
		return;

	listen_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (CHECK_FAIL(listen_fd == -1))
		return;

	err = getsockname(listen_fd, (struct sockaddr *)&srv_sa6, &addrlen);
	if (CHECK(err, "getsockname(listen_fd)", "err:%d errno:%d\n", err,
		  errno))
		goto done;
	memcpy(&skel->bss->srv_sa6, &srv_sa6, sizeof(srv_sa6));
	srv_port = ntohs(srv_sa6.sin6_port);

	cli_fd = connect_to_fd(listen_fd, 0);
	if (CHECK_FAIL(cli_fd == -1))
		goto done;

	srv_fd = accept(listen_fd, NULL, NULL);
	if (CHECK_FAIL(srv_fd == -1))
		goto done;

	if (CHECK(skel->bss->listen_tp_sport != srv_port,
		  "Unexpected tp src port",
		  "listen_tp_sport:%u expected:%u\n",
		  skel->bss->listen_tp_sport, srv_port))
		goto done;

	if (CHECK(skel->bss->req_sk_sport,
		  "Unexpected req_sk src port",
		  "req_sk_sport:%u expected:0\n",
		   skel->bss->req_sk_sport))
		goto done;

	if (CHECK(!skel->bss->gen_cookie ||
		  skel->bss->gen_cookie != skel->bss->recv_cookie,
		  "Unexpected syncookie states",
		  "gen_cookie:%u recv_cookie:%u\n",
		  skel->bss->gen_cookie, skel->bss->recv_cookie))
		goto done;

	CHECK(skel->bss->linum, "bpf prog detected error", "at line %u\n",
	      skel->bss->linum);

done:
	if (listen_fd != -1)
		close(listen_fd);
	if (cli_fd != -1)
		close(cli_fd);
	if (srv_fd != -1)
		close(srv_fd);
}

struct test {
	const char *desc;
	void (*run)(void);
};

#define DEF_TEST(name) { #name, test_##name }
static struct test tests[] = {
	DEF_TEST(conn),
	DEF_TEST(syncookie),
};

void test_btf_skc_cls_ingress(void)
{
	int i;

	skel = test_btf_skc_cls_ingress__open_and_load();
	if (CHECK(!skel, "test_btf_skc_cls_ingress__open_and_load", "failed\n"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (!test__start_subtest(tests[i].desc))
			continue;

		if (prepare_netns())
			break;

		tests[i].run();

		print_err_line();
		reset_test();
	}

	test_btf_skc_cls_ingress__destroy(skel);
}
