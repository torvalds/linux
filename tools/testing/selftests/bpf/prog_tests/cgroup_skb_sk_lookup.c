// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <test_progs.h>

#include "network_helpers.h"
#include "cgroup_skb_sk_lookup_kern.skel.h"

static void run_lookup_test(__u16 *g_serv_port, int out_sk)
{
	int serv_sk = -1, in_sk = -1, serv_in_sk = -1, err;
	struct sockaddr_in6 addr = {};
	socklen_t addr_len = sizeof(addr);
	__u32 duration = 0;

	serv_sk = start_server(AF_INET6, SOCK_STREAM, NULL, 0, 0);
	if (CHECK(serv_sk < 0, "start_server", "failed to start server\n"))
		return;

	err = getsockname(serv_sk, (struct sockaddr *)&addr, &addr_len);
	if (CHECK(err, "getsockname", "errno %d\n", errno))
		goto cleanup;

	*g_serv_port = addr.sin6_port;

	/* Client outside of test cgroup should fail to connect by timeout. */
	err = connect_fd_to_fd(out_sk, serv_sk, 1000);
	if (CHECK(!err || errno != EINPROGRESS, "connect_fd_to_fd",
		  "unexpected result err %d errno %d\n", err, errno))
		goto cleanup;

	/* Client inside test cgroup should connect just fine. */
	in_sk = connect_to_fd(serv_sk, 0);
	if (CHECK(in_sk < 0, "connect_to_fd", "errno %d\n", errno))
		goto cleanup;

	serv_in_sk = accept(serv_sk, NULL, NULL);
	if (CHECK(serv_in_sk < 0, "accept", "errno %d\n", errno))
		goto cleanup;

cleanup:
	close(serv_in_sk);
	close(in_sk);
	close(serv_sk);
}

static void run_cgroup_bpf_test(const char *cg_path, int out_sk)
{
	struct cgroup_skb_sk_lookup_kern *skel;
	struct bpf_link *link;
	__u32 duration = 0;
	int cgfd = -1;

	skel = cgroup_skb_sk_lookup_kern__open_and_load();
	if (CHECK(!skel, "skel_open_load", "open_load failed\n"))
		return;

	cgfd = test__join_cgroup(cg_path);
	if (CHECK(cgfd < 0, "cgroup_join", "cgroup setup failed\n"))
		goto cleanup;

	link = bpf_program__attach_cgroup(skel->progs.ingress_lookup, cgfd);
	if (CHECK(IS_ERR(link), "cgroup_attach", "err: %ld\n", PTR_ERR(link)))
		goto cleanup;

	run_lookup_test(&skel->bss->g_serv_port, out_sk);

	bpf_link__destroy(link);

cleanup:
	close(cgfd);
	cgroup_skb_sk_lookup_kern__destroy(skel);
}

void test_cgroup_skb_sk_lookup(void)
{
	const char *cg_path = "/foo";
	int out_sk;

	/* Create a socket before joining testing cgroup so that its cgroup id
	 * differs from that of testing cgroup. Moving selftests process to
	 * testing cgroup won't change cgroup id of an already created socket.
	 */
	out_sk = socket(AF_INET6, SOCK_STREAM, 0);
	if (CHECK_FAIL(out_sk < 0))
		return;

	run_cgroup_bpf_test(cg_path, out_sk);

	close(out_sk);
}
