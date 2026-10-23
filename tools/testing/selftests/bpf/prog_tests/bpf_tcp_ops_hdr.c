// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <network_helpers.h>
#include "cgroup_helpers.h"
#include "bpf_tcp_ops_hdr.skel.h"

#define CGROUP_PATH	"/bpf_tcp_ops_hdr"
#define TEST_NETNS	"bpf_tcp_ops_hdr"

#define TEST_OPT_D0	0xAB
#define TEST_OPT_D1	0xCD

static void run_hdr_opt(void)
{
	struct bpf_tcp_ops_hdr *skel = NULL;
	struct bpf_link *link = NULL;
	struct netns_obj *ns = NULL;
	int cgroup_fd, lfd = -1, fd = -1;

	cgroup_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops_hdr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	link = bpf_map__attach_cgroup_opts(skel->maps.test_hdr_ops, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link, "attach_cgroup"))
		goto done;

	/*
	 * One direction of data is enough to exercise all hooks: both peers
	 * share the cgroup struct_ops, so the sender runs hdr_opt_len/write
	 * and the receiver runs parse.
	 */
	lfd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (!ASSERT_GE(lfd, 0, "start_server"))
		goto done;

	fd = connect_to_fd(lfd, 0);
	if (!ASSERT_OK_FD(fd, "connect_to_fd"))
		goto done;

	if (!ASSERT_OK(send_recv_data(lfd, fd, 64), "send_recv_data"))
		goto done;

	/* Reserve + write hooks ran while sending. */
	ASSERT_GT(skel->bss->hdr_opt_len_cnt, 0, "hdr_opt_len_cnt");
	ASSERT_GT(skel->bss->write_cnt, 0, "write_cnt");
	/* Parse hook ran and recovered our option on the receive side. */
	ASSERT_GT(skel->bss->parse_cnt, 0, "parse_cnt");
	ASSERT_GT(skel->bss->found_cnt, 0, "found_cnt");
	ASSERT_EQ(skel->bss->found_d0, TEST_OPT_D0, "found_d0");
	ASSERT_EQ(skel->bss->found_d1, TEST_OPT_D1, "found_d1");

done:
	if (fd >= 0)
		close(fd);
	if (lfd >= 0)
		close(lfd);
	bpf_link__destroy(link);
	bpf_tcp_ops_hdr__destroy(skel);
	netns_free(ns);
	close(cgroup_fd);
}

void test_bpf_tcp_ops_hdr(void)
{
	run_hdr_opt();
}
