// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <sched.h>
#include <linux/socket.h>
#include <net/if.h>

#include "test_progs.h"
#include "cgroup_helpers.h"
#include "network_helpers.h"

#include "setget_sockopt.skel.h"

#define CG_NAME "/setget-sockopt-test"

static const char addr4_str[] = "127.0.0.1";
static const char addr6_str[] = "::1";
static struct setget_sockopt *skel;
static int cg_fd;

static int create_netns(void)
{
	if (!ASSERT_OK(unshare(CLONE_NEWNET), "create netns"))
		return -1;

	if (!ASSERT_OK(system("ip link set dev lo up"), "set lo up"))
		return -1;

	if (!ASSERT_OK(system("ip link add dev binddevtest1 type veth peer name binddevtest2"),
		       "add veth"))
		return -1;

	if (!ASSERT_OK(system("ip link set dev binddevtest1 up"),
		       "bring veth up"))
		return -1;

	return 0;
}

static void test_tcp(int family)
{
	struct setget_sockopt__bss *bss = skel->bss;
	int sfd, cfd;

	memset(bss, 0, sizeof(*bss));

	sfd = start_server(family, SOCK_STREAM,
			   family == AF_INET6 ? addr6_str : addr4_str, 0, 0);
	if (!ASSERT_GE(sfd, 0, "start_server"))
		return;

	cfd = connect_to_fd(sfd, 0);
	if (!ASSERT_GE(cfd, 0, "connect_to_fd_server")) {
		close(sfd);
		return;
	}
	close(sfd);
	close(cfd);

	ASSERT_EQ(bss->nr_listen, 1, "nr_listen");
	ASSERT_EQ(bss->nr_connect, 1, "nr_connect");
	ASSERT_EQ(bss->nr_active, 1, "nr_active");
	ASSERT_EQ(bss->nr_passive, 1, "nr_passive");
	ASSERT_EQ(bss->nr_socket_post_create, 2, "nr_socket_post_create");
	ASSERT_EQ(bss->nr_binddev, 2, "nr_bind");
}

static void test_udp(int family)
{
	struct setget_sockopt__bss *bss = skel->bss;
	int sfd;

	memset(bss, 0, sizeof(*bss));

	sfd = start_server(family, SOCK_DGRAM,
			   family == AF_INET6 ? addr6_str : addr4_str, 0, 0);
	if (!ASSERT_GE(sfd, 0, "start_server"))
		return;
	close(sfd);

	ASSERT_GE(bss->nr_socket_post_create, 1, "nr_socket_post_create");
	ASSERT_EQ(bss->nr_binddev, 1, "nr_bind");
}

void test_setget_sockopt(void)
{
	cg_fd = test__join_cgroup(CG_NAME);
	if (cg_fd < 0)
		return;

	if (create_netns())
		goto done;

	skel = setget_sockopt__open();
	if (!ASSERT_OK_PTR(skel, "open skel"))
		goto done;

	strcpy(skel->rodata->veth, "binddevtest1");
	skel->rodata->veth_ifindex = if_nametoindex("binddevtest1");
	if (!ASSERT_GT(skel->rodata->veth_ifindex, 0, "if_nametoindex"))
		goto done;

	if (!ASSERT_OK(setget_sockopt__load(skel), "load skel"))
		goto done;

	skel->links.skops_sockopt =
		bpf_program__attach_cgroup(skel->progs.skops_sockopt, cg_fd);
	if (!ASSERT_OK_PTR(skel->links.skops_sockopt, "attach cgroup"))
		goto done;

	skel->links.socket_post_create =
		bpf_program__attach_cgroup(skel->progs.socket_post_create, cg_fd);
	if (!ASSERT_OK_PTR(skel->links.socket_post_create, "attach_cgroup"))
		goto done;

	test_tcp(AF_INET6);
	test_tcp(AF_INET);
	test_udp(AF_INET6);
	test_udp(AF_INET);

done:
	setget_sockopt__destroy(skel);
	close(cg_fd);
}
