// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 Google LLC */
#include <sys/epoll.h>

#include "test_progs.h"
#include "cgroup_helpers.h"
#include "network_helpers.h"

#include "tcp_autolowat.skel.h"

#define SOL_BPF			0xdeadbeef
#define BPF_TCP_AUTOLOWAT	0x8badf00d

struct rpc_descriptor {
	u32 header_len;
	u32 payload_len;
};

enum rpc_event_type {
	RPC_EVENT_END,
	RPC_EVENT_AUTOLOWAT,
	RPC_EVENT_SEND,
	RPC_EVENT_RECV,
	RPC_EVENT_EPOLL,
	RPC_EVENT_RCVLOWAT,
};

struct rpc_event {
	enum rpc_event_type type;
	union {
		int len;
		int nfds;
		int val;
		int rcvlowat;
	};
};

#define RPC_DESC_SIZE (sizeof(struct rpc_descriptor))

static struct rpc_test_case {
	char data[4096];
	struct rpc_descriptor desc[32];
	struct rpc_event event[32];
} rpc_test_cases[] = {
	{
		.desc = {
			{ .header_len = 100, .payload_len = 150 },
		},
		.event = {
			{ .type = RPC_EVENT_AUTOLOWAT,	.val = 1},
			/* Single full RPC message in skb. */
			{ .type = RPC_EVENT_SEND,	.len = RPC_DESC_SIZE + 100 + 150},
			{ .type = RPC_EVENT_EPOLL,	.nfds = 1},
			{ .type = RPC_EVENT_RCVLOWAT,	.rcvlowat = RPC_DESC_SIZE + 100 + 150},
		},
	},
	{
		.desc = {
			{.header_len = 100, .payload_len = 150},
			{.header_len = 100, .payload_len = 150},
			{.header_len = 100, .payload_len = 150},
		},
		.event = {
			{ .type = RPC_EVENT_AUTOLOWAT,	.val = 1},
			/* Two full RPC messages in skb. */
			{.type = RPC_EVENT_SEND,	.len = (RPC_DESC_SIZE + 100 + 150) * 2},
			{.type = RPC_EVENT_EPOLL,	.nfds = 1},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = (RPC_DESC_SIZE + 100 + 150) * 2},
			/* Single full RPC message in skb. */
			{ .type = RPC_EVENT_SEND,	.len = RPC_DESC_SIZE + 100 + 150},
			{ .type = RPC_EVENT_EPOLL,	.nfds = 1},
			{ .type = RPC_EVENT_RCVLOWAT,	.rcvlowat = (RPC_DESC_SIZE + 100 + 150) * 3},
		},
	},
	{
		.desc = {
			{.header_len = 100, .payload_len = 150},
			{.header_len = 100, .payload_len = 150},
			{.header_len = 100, .payload_len = 150},
		},
		.event = {
			{ .type = RPC_EVENT_AUTOLOWAT,	.val = 1},
			/* Two full RPC messages in skb. */
			{.type = RPC_EVENT_SEND,	.len = (RPC_DESC_SIZE + 100 + 150) * 2},
			{.type = RPC_EVENT_EPOLL,	.nfds = 1},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = (RPC_DESC_SIZE + 100 + 150) * 2},
			/* Only the next descriptor in skb. */
			{ .type = RPC_EVENT_SEND,	.len = RPC_DESC_SIZE},
			{ .type = RPC_EVENT_EPOLL,	.nfds = 1},
			{ .type = RPC_EVENT_RCVLOWAT,	.rcvlowat = (RPC_DESC_SIZE + 100 + 150) * 2},
		},
	},
	{
		.desc = {
			{.header_len = 100, .payload_len = 150},
			{.header_len = 200, .payload_len = 500},
		},
		.event = {
			{ .type = RPC_EVENT_AUTOLOWAT,	.val = 1},
			/* The first descriptor is partial. */
			{.type = RPC_EVENT_SEND,	.len = 1},
			{.type = RPC_EVENT_EPOLL,	.nfds = 0},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = RPC_DESC_SIZE},
			/* The first descriptor is available. */
			{.type = RPC_EVENT_SEND,	.len = RPC_DESC_SIZE - 1},
			{.type = RPC_EVENT_EPOLL,	.nfds = 0},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = RPC_DESC_SIZE + 150 + 100},
			/* The first header is ready. */
			{.type = RPC_EVENT_SEND,	.len = 100},
			{.type = RPC_EVENT_EPOLL,	.nfds = 0},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = RPC_DESC_SIZE + 150 + 100},
			/* skb has the first payload and 1 byte of the next descriptor. */
			{.type = RPC_EVENT_SEND,	.len = 150 + 1},
			{.type = RPC_EVENT_EPOLL,	.nfds = 1},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = RPC_DESC_SIZE + 150 + 100},
			/* After reading the first RPC message, SO_RCVLOWAT should be RPC_DESC_SIZE. */
			{.type = RPC_EVENT_RECV,	.len = RPC_DESC_SIZE + 150 + 100},
			{.type = RPC_EVENT_EPOLL,	.nfds = 0},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = RPC_DESC_SIZE},
			/* The second descriptor is available. */
			{.type = RPC_EVENT_SEND,	.len = RPC_DESC_SIZE - 1},
			{.type = RPC_EVENT_EPOLL,	.nfds = 0},
			{.type = RPC_EVENT_RCVLOWAT,	.rcvlowat = RPC_DESC_SIZE + 200 + 500},
		},
	},
};

struct tcp_autolowat_test_cb {
	int saved_netns;
	union {
		int fd[4];
		struct {
			int server, client, child;
			int epoll;
		};
	};
};

static void tcp_autolowat_teardown_cb(struct tcp_autolowat_test_cb *cb)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(cb->fd); i++) {
		if (cb->fd[i] != -1)
			close(cb->fd[i]);
	}

	if (cb->saved_netns != -1) {
		err = setns(cb->saved_netns, CLONE_NEWNET);
		ASSERT_OK(err, "restore netns");

		close(cb->saved_netns);
	}
}

static int tcp_autolowat_setup_cb(struct tcp_autolowat_test_cb *cb, int family)
{
	struct epoll_event ev = {};
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(cb->fd); i++)
		cb->fd[i] = -1;

	cb->saved_netns = open("/proc/self/ns/net", O_RDONLY);
	if (!ASSERT_OK_FD(cb->saved_netns, "save netns"))
		goto err;

	err = unshare(CLONE_NEWNET);
	if (!ASSERT_OK(err, "unshare"))
		goto err;

	err = system("ip link set dev lo up");
	if (!ASSERT_OK(err, "set up lo"))
		goto err;

	cb->server = start_server(family, SOCK_STREAM, NULL, 0, 0);
	if (!ASSERT_OK_FD(cb->server, "start_server"))
		goto err;

	cb->client = connect_to_fd(cb->server, 0);
	if (!ASSERT_OK_FD(cb->client, "connect_to_fd"))
		goto err;

	cb->child = accept(cb->server, NULL, NULL);
	if (!ASSERT_OK_FD(cb->child, "accept"))
		goto err;

	cb->epoll = epoll_create1(0);
	if (!ASSERT_OK_FD(cb->epoll, "epoll_create"))
		goto err;

	ev.events = EPOLLIN;
	ev.data.fd = cb->child;

	err = epoll_ctl(cb->epoll, EPOLL_CTL_ADD, cb->child, &ev);
	if (!ASSERT_OK(err, "epoll_ctl"))
		goto err;

	return 0;

err:
	tcp_autolowat_teardown_cb(cb);
	return -1;
}

static int tcp_autolowat_build_data(struct rpc_test_case *test_case)
{
	struct rpc_descriptor *desc = test_case->desc;
	char *ptr = test_case->data;
	int rpc_size;

	memset(ptr, 0, sizeof(test_case->data));

	while (desc->header_len + desc->payload_len) {
		rpc_size = sizeof(*desc) + desc->header_len + desc->payload_len;

		if (!ASSERT_LE(ptr + rpc_size - test_case->data,
			       sizeof(test_case->data), "data overflow"))
			return 1;

		memcpy(ptr, desc, sizeof(*desc));
		ptr += rpc_size;
		desc++;
	}

	if (!ASSERT_GT(ptr - test_case->data, 0, "no data"))
		return 1;

	return 0;
}

static void tcp_autolowat_run_rpc_test(struct tcp_autolowat_test_cb *cb,
				       struct rpc_test_case *test_case)
{
	struct rpc_event *event = test_case->event;
	char *ptr = test_case->data;
	struct epoll_event ev;
	socklen_t optlen;
	int err, optval;
	char buf[4096];

	if (tcp_autolowat_build_data(test_case))
		return;

	while (1) {
		switch (event->type) {
		case RPC_EVENT_END:
			return;
		case RPC_EVENT_AUTOLOWAT:
			err = setsockopt(cb->child, SOL_BPF, BPF_TCP_AUTOLOWAT,
					 &event->val, sizeof(event->val));
			if (!ASSERT_OK(err, "setsockopt"))
				return;
			break;
		case RPC_EVENT_SEND:
			err = send(cb->client, ptr, event->len, 0);
			if (!ASSERT_EQ(err, event->len, "send"))
				return;

			ptr += event->len;
			break;
		case RPC_EVENT_RECV:
			err = recv(cb->child, buf, event->len, 0);
			if (!ASSERT_EQ(err, event->len, "recv"))
				return;
			break;
		case RPC_EVENT_EPOLL:
			err = epoll_wait(cb->epoll, &ev, 1, 100);
			if (!ASSERT_EQ(err, event->nfds, "epoll_wait"))
				return;
			break;
		case RPC_EVENT_RCVLOWAT:
			optval = 0;
			optlen = sizeof(optval);

			err = getsockopt(cb->child, SOL_SOCKET, SO_RCVLOWAT, &optval, &optlen);
			if (!ASSERT_OK(err, "getsockopt") ||
			    !ASSERT_EQ(optval, event->rcvlowat, "rcvlowat"))
				return;
			break;
		}

		event++;
	}
}

static void tcp_autolowat_run_rpc_tests(struct tcp_autolowat *skel, int family)
{
	struct tcp_autolowat_test_cb cb;
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(rpc_test_cases); i++) {
		memset(skel->bss->test_name, 0, sizeof(skel->bss->test_name));

		snprintf(skel->bss->test_name, sizeof(skel->bss->test_name),
			 "AF_INET%c rpc_test_cases[%d]",
			 family == AF_INET ? ' ' : '6', i);

		if (!test__start_subtest(skel->bss->test_name))
			continue;

		err = tcp_autolowat_setup_cb(&cb, family);
		if (err)
			continue;

		tcp_autolowat_run_rpc_test(&cb, &rpc_test_cases[i]);
		tcp_autolowat_teardown_cb(&cb);
	}
}

static void tcp_autolowat_run_tests(struct tcp_autolowat *skel)
{
	tcp_autolowat_run_rpc_tests(skel, AF_INET);
	tcp_autolowat_run_rpc_tests(skel, AF_INET6);
}

void test_tcp_autolowat(void)
{
	struct tcp_autolowat *skel;
	struct bpf_link *link[2];
	int cgroup;

	skel = tcp_autolowat__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	cgroup = test__join_cgroup("/tcp_autolowat");
	if (!ASSERT_GE(cgroup, 0, "join_cgroup"))
		goto destroy_skel;

	link[0] = bpf_map__attach_cgroup_opts(skel->maps.tcp_autolowat_ops, cgroup, NULL);
	if (!ASSERT_OK_PTR(link[0], "attach_cgroup(tcp_autolowat_ops)"))
		goto close_cgroup;

	link[1] = bpf_program__attach_cgroup(skel->progs.tcp_autolowat_setsockopt, cgroup);
	if (!ASSERT_OK_PTR(link[1], "attach_cgroup(SETSOCKOPT)"))
		goto destroy_sockops;

	tcp_autolowat_run_tests(skel);

	bpf_link__destroy(link[1]);
destroy_sockops:
	bpf_link__destroy(link[0]);
close_cgroup:
	close(cgroup);
destroy_skel:
	tcp_autolowat__destroy(skel);
}
