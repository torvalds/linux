// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "decap_sanity.skel.h"

#define NS_TEST "decap_sanity_ns"
#define IPV6_IFACE_ADDR "face::1"
#define UDP_TEST_PORT 7777

void test_decap_sanity(void)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_hook, .attach_point = BPF_TC_EGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_attach);
	struct nstoken *nstoken = NULL;
	struct decap_sanity *skel;
	struct sockaddr_in6 addr;
	socklen_t addrlen;
	char buf[128] = {};
	int sockfd, err;

	skel = decap_sanity__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;

	SYS(fail, "ip netns add %s", NS_TEST);
	SYS(fail, "ip -net %s -6 addr add %s/128 dev lo nodad", NS_TEST, IPV6_IFACE_ADDR);
	SYS(fail, "ip -net %s link set dev lo up", NS_TEST);

	nstoken = open_netns(NS_TEST);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto fail;

	qdisc_hook.ifindex = if_nametoindex("lo");
	if (!ASSERT_GT(qdisc_hook.ifindex, 0, "if_nametoindex lo"))
		goto fail;

	err = bpf_tc_hook_create(&qdisc_hook);
	if (!ASSERT_OK(err, "create qdisc hook"))
		goto fail;

	tc_attach.prog_fd = bpf_program__fd(skel->progs.decap_sanity);
	err = bpf_tc_attach(&qdisc_hook, &tc_attach);
	if (!ASSERT_OK(err, "attach filter"))
		goto fail;

	addrlen = sizeof(addr);
	err = make_sockaddr(AF_INET6, IPV6_IFACE_ADDR, UDP_TEST_PORT,
			    (void *)&addr, &addrlen);
	if (!ASSERT_OK(err, "make_sockaddr"))
		goto fail;
	sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (!ASSERT_NEQ(sockfd, -1, "socket"))
		goto fail;
	err = sendto(sockfd, buf, sizeof(buf), 0, (void *)&addr, addrlen);
	close(sockfd);
	if (!ASSERT_EQ(err, sizeof(buf), "send"))
		goto fail;

	ASSERT_TRUE(skel->bss->init_csum_partial, "init_csum_partial");
	ASSERT_TRUE(skel->bss->final_csum_none, "final_csum_none");
	ASSERT_FALSE(skel->bss->broken_csum_start, "broken_csum_start");

fail:
	if (nstoken) {
		bpf_tc_hook_destroy(&qdisc_hook);
		close_netns(nstoken);
	}
	SYS_NOFAIL("ip netns del " NS_TEST);
	decap_sanity__destroy(skel);
}
