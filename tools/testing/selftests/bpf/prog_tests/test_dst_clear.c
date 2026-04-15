// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "test_dst_clear.skel.h"

#define IPV4_IFACE_ADDR "1.0.0.1"
#define UDP_TEST_PORT 7777

void test_ns_dst_clear(void)
{
	LIBBPF_OPTS(bpf_tcx_opts, tcx_opts);
	struct test_dst_clear *skel;
	struct sockaddr_in addr;
	struct bpf_link *link;
	socklen_t addrlen;
	char buf[128] = {};
	int sockfd, err;

	skel = test_dst_clear__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;

	SYS(fail, "ip addr add %s/8 dev lo", IPV4_IFACE_ADDR);

	link = bpf_program__attach_tcx(skel->progs.dst_clear,
				       if_nametoindex("lo"), &tcx_opts);
	if (!ASSERT_OK_PTR(link, "attach_tcx"))
		goto fail;
	skel->links.dst_clear = link;

	addrlen = sizeof(addr);
	err = make_sockaddr(AF_INET, IPV4_IFACE_ADDR, UDP_TEST_PORT,
			    (void *)&addr, &addrlen);
	if (!ASSERT_OK(err, "make_sockaddr"))
		goto fail;
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (!ASSERT_NEQ(sockfd, -1, "socket"))
		goto fail;
	err = sendto(sockfd, buf, sizeof(buf), 0, (void *)&addr, addrlen);
	close(sockfd);
	if (!ASSERT_EQ(err, sizeof(buf), "send"))
		goto fail;

	ASSERT_TRUE(skel->bss->had_dst, "had_dst");
	ASSERT_TRUE(skel->bss->dst_cleared, "dst_cleared");

fail:
	test_dst_clear__destroy(skel);
}
