// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <net/if.h>

#include "test_progs.h"
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "test_tcp_custom_syncookie.skel.h"

static struct test_tcp_custom_syncookie_case {
	int family, type;
	char addr[16];
	char name[10];
} test_cases[] = {
	{
		.name = "IPv4 TCP",
		.family = AF_INET,
		.type = SOCK_STREAM,
		.addr = "127.0.0.1",
	},
	{
		.name = "IPv6 TCP",
		.family = AF_INET6,
		.type = SOCK_STREAM,
		.addr = "::1",
	},
};

static int setup_netns(void)
{
	if (!ASSERT_OK(unshare(CLONE_NEWNET), "create netns"))
		return -1;

	if (!ASSERT_OK(system("ip link set dev lo up"), "ip"))
		goto err;

	if (!ASSERT_OK(write_sysctl("/proc/sys/net/ipv4/tcp_ecn", "1"),
		       "write_sysctl"))
		goto err;

	return 0;
err:
	return -1;
}

static int setup_tc(struct test_tcp_custom_syncookie *skel)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_lo, .attach_point = BPF_TC_INGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_attach,
		    .prog_fd = bpf_program__fd(skel->progs.tcp_custom_syncookie));

	qdisc_lo.ifindex = if_nametoindex("lo");
	if (!ASSERT_OK(bpf_tc_hook_create(&qdisc_lo), "qdisc add dev lo clsact"))
		goto err;

	if (!ASSERT_OK(bpf_tc_attach(&qdisc_lo, &tc_attach),
		       "filter add dev lo ingress"))
		goto err;

	return 0;
err:
	return -1;
}

#define msg "Hello World"
#define msglen 11

static void transfer_message(int sender, int receiver)
{
	char buf[msglen];
	int ret;

	ret = send(sender, msg, msglen, 0);
	if (!ASSERT_EQ(ret, msglen, "send"))
		return;

	memset(buf, 0, sizeof(buf));

	ret = recv(receiver, buf, msglen, 0);
	if (!ASSERT_EQ(ret, msglen, "recv"))
		return;

	ret = strncmp(buf, msg, msglen);
	if (!ASSERT_EQ(ret, 0, "strncmp"))
		return;
}

static void create_connection(struct test_tcp_custom_syncookie_case *test_case)
{
	int server, client, child;

	server = start_server(test_case->family, test_case->type, test_case->addr, 0, 0);
	if (!ASSERT_NEQ(server, -1, "start_server"))
		return;

	client = connect_to_fd(server, 0);
	if (!ASSERT_NEQ(client, -1, "connect_to_fd"))
		goto close_server;

	child = accept(server, NULL, 0);
	if (!ASSERT_NEQ(child, -1, "accept"))
		goto close_client;

	transfer_message(client, child);
	transfer_message(child, client);

	close(child);
close_client:
	close(client);
close_server:
	close(server);
}

void test_tcp_custom_syncookie(void)
{
	struct test_tcp_custom_syncookie *skel;
	int i;

	if (setup_netns())
		return;

	skel = test_tcp_custom_syncookie__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	if (setup_tc(skel))
		goto destroy_skel;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		if (!test__start_subtest(test_cases[i].name))
			continue;

		skel->bss->handled_syn = false;
		skel->bss->handled_ack = false;

		create_connection(&test_cases[i]);

		ASSERT_EQ(skel->bss->handled_syn, true, "SYN is not handled at tc.");
		ASSERT_EQ(skel->bss->handled_ack, true, "ACK is not handled at tc");
	}

destroy_skel:
	system("tc qdisc del dev lo clsact");

	test_tcp_custom_syncookie__destroy(skel);
}
