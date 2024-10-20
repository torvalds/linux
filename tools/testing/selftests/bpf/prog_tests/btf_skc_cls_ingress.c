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

#define TEST_NS "skc_cls_ingress"

static struct netns_obj *prepare_netns(struct test_btf_skc_cls_ingress *skel)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_lo, .attach_point = BPF_TC_INGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_attach,
		    .prog_fd = bpf_program__fd(skel->progs.cls_ingress));
	struct netns_obj *ns = NULL;

	ns = netns_new(TEST_NS, true);
	if (!ASSERT_OK_PTR(ns, "create and join netns"))
		return ns;

	qdisc_lo.ifindex = if_nametoindex("lo");
	if (!ASSERT_OK(bpf_tc_hook_create(&qdisc_lo), "qdisc add dev lo clsact"))
		goto free_ns;

	if (!ASSERT_OK(bpf_tc_attach(&qdisc_lo, &tc_attach),
		       "filter add dev lo ingress"))
		goto free_ns;

	/* Ensure 20 bytes options (i.e. in total 40 bytes tcp header) for the
	 * bpf_tcp_gen_syncookie() helper.
	 */
	if (write_sysctl("/proc/sys/net/ipv4/tcp_window_scaling", "1") ||
	    write_sysctl("/proc/sys/net/ipv4/tcp_timestamps", "1") ||
	    write_sysctl("/proc/sys/net/ipv4/tcp_sack", "1"))
		goto free_ns;

	return ns;

free_ns:
	netns_free(ns);
	return NULL;
}

static void reset_test(struct test_btf_skc_cls_ingress *skel)
{
	memset(&skel->bss->srv_sa6, 0, sizeof(skel->bss->srv_sa6));
	skel->bss->listen_tp_sport = 0;
	skel->bss->req_sk_sport = 0;
	skel->bss->recv_cookie = 0;
	skel->bss->gen_cookie = 0;
	skel->bss->linum = 0;
}

static void print_err_line(struct test_btf_skc_cls_ingress *skel)
{
	if (skel->bss->linum)
		printf("bpf prog error at line %u\n", skel->bss->linum);
}

static void run_test(struct test_btf_skc_cls_ingress *skel, bool gen_cookies)
{
	const char *tcp_syncookies = gen_cookies ? "2" : "1";
	int listen_fd = -1, cli_fd = -1, srv_fd = -1, err;
	struct sockaddr_in6 srv_sa6;
	socklen_t addrlen = sizeof(srv_sa6);
	int srv_port;

	if (write_sysctl("/proc/sys/net/ipv4/tcp_syncookies", tcp_syncookies))
		return;

	listen_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (!ASSERT_OK_FD(listen_fd, "start server"))
		return;

	err = getsockname(listen_fd, (struct sockaddr *)&srv_sa6, &addrlen);
	if (!ASSERT_OK(err, "getsockname(listen_fd)"))
		goto done;
	memcpy(&skel->bss->srv_sa6, &srv_sa6, sizeof(srv_sa6));
	srv_port = ntohs(srv_sa6.sin6_port);

	cli_fd = connect_to_fd(listen_fd, 0);
	if (!ASSERT_OK_FD(cli_fd, "connect client"))
		goto done;

	srv_fd = accept(listen_fd, NULL, NULL);
	if (!ASSERT_OK_FD(srv_fd, "accept connection"))
		goto done;

	ASSERT_EQ(skel->bss->listen_tp_sport, srv_port, "listen tp src port");

	if (!gen_cookies) {
		ASSERT_EQ(skel->bss->req_sk_sport, srv_port,
			  "request socket source port with syncookies disabled");
		ASSERT_EQ(skel->bss->gen_cookie, 0,
			  "generated syncookie with syncookies disabled");
		ASSERT_EQ(skel->bss->recv_cookie, 0,
			  "received syncookie with syncookies disabled");
	} else {
		ASSERT_EQ(skel->bss->req_sk_sport, 0,
			  "request socket source port with syncookies enabled");
		ASSERT_NEQ(skel->bss->gen_cookie, 0,
			   "syncookie properly generated");
		ASSERT_EQ(skel->bss->gen_cookie, skel->bss->recv_cookie,
			  "matching syncookies on client and server");
	}

done:
	if (listen_fd != -1)
		close(listen_fd);
	if (cli_fd != -1)
		close(cli_fd);
	if (srv_fd != -1)
		close(srv_fd);
}

static void test_conn(struct test_btf_skc_cls_ingress *skel)
{
	run_test(skel, false);
}

static void test_syncookie(struct test_btf_skc_cls_ingress *skel)
{
	run_test(skel, true);
}

struct test {
	const char *desc;
	void (*run)(struct test_btf_skc_cls_ingress *skel);
};

#define DEF_TEST(name) { #name, test_##name }
static struct test tests[] = {
	DEF_TEST(conn),
	DEF_TEST(syncookie),
};

void test_btf_skc_cls_ingress(void)
{
	struct test_btf_skc_cls_ingress *skel;
	struct netns_obj *ns;
	int i;

	skel = test_btf_skc_cls_ingress__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_btf_skc_cls_ingress__open_and_load"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (!test__start_subtest(tests[i].desc))
			continue;

		ns = prepare_netns(skel);
		if (!ns)
			break;

		tests[i].run(skel);

		print_err_line(skel);
		reset_test(skel);
		netns_free(ns);
	}

	test_btf_skc_cls_ingress__destroy(skel);
}
