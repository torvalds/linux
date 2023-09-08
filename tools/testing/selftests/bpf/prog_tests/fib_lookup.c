// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <sys/types.h>
#include <net/if.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "fib_lookup.skel.h"

#define NS_TEST			"fib_lookup_ns"
#define IPV6_IFACE_ADDR		"face::face"
#define IPV6_NUD_FAILED_ADDR	"face::1"
#define IPV6_NUD_STALE_ADDR	"face::2"
#define IPV4_IFACE_ADDR		"10.0.0.254"
#define IPV4_NUD_FAILED_ADDR	"10.0.0.1"
#define IPV4_NUD_STALE_ADDR	"10.0.0.2"
#define DMAC			"11:11:11:11:11:11"
#define DMAC_INIT { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, }

struct fib_lookup_test {
	const char *desc;
	const char *daddr;
	int expected_ret;
	int lookup_flags;
	__u8 dmac[6];
};

static const struct fib_lookup_test tests[] = {
	{ .desc = "IPv6 failed neigh",
	  .daddr = IPV6_NUD_FAILED_ADDR, .expected_ret = BPF_FIB_LKUP_RET_NO_NEIGH, },
	{ .desc = "IPv6 stale neigh",
	  .daddr = IPV6_NUD_STALE_ADDR, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .dmac = DMAC_INIT, },
	{ .desc = "IPv6 skip neigh",
	  .daddr = IPV6_NUD_FAILED_ADDR, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv4 failed neigh",
	  .daddr = IPV4_NUD_FAILED_ADDR, .expected_ret = BPF_FIB_LKUP_RET_NO_NEIGH, },
	{ .desc = "IPv4 stale neigh",
	  .daddr = IPV4_NUD_STALE_ADDR, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .dmac = DMAC_INIT, },
	{ .desc = "IPv4 skip neigh",
	  .daddr = IPV4_NUD_FAILED_ADDR, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH, },
};

static int ifindex;

static int setup_netns(void)
{
	int err;

	SYS(fail, "ip link add veth1 type veth peer name veth2");
	SYS(fail, "ip link set dev veth1 up");

	err = write_sysctl("/proc/sys/net/ipv4/neigh/veth1/gc_stale_time", "900");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.neigh.veth1.gc_stale_time)"))
		goto fail;

	err = write_sysctl("/proc/sys/net/ipv6/neigh/veth1/gc_stale_time", "900");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv6.neigh.veth1.gc_stale_time)"))
		goto fail;

	SYS(fail, "ip addr add %s/64 dev veth1 nodad", IPV6_IFACE_ADDR);
	SYS(fail, "ip neigh add %s dev veth1 nud failed", IPV6_NUD_FAILED_ADDR);
	SYS(fail, "ip neigh add %s dev veth1 lladdr %s nud stale", IPV6_NUD_STALE_ADDR, DMAC);

	SYS(fail, "ip addr add %s/24 dev veth1", IPV4_IFACE_ADDR);
	SYS(fail, "ip neigh add %s dev veth1 nud failed", IPV4_NUD_FAILED_ADDR);
	SYS(fail, "ip neigh add %s dev veth1 lladdr %s nud stale", IPV4_NUD_STALE_ADDR, DMAC);

	err = write_sysctl("/proc/sys/net/ipv4/conf/veth1/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.conf.veth1.forwarding)"))
		goto fail;

	err = write_sysctl("/proc/sys/net/ipv6/conf/veth1/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv6.conf.veth1.forwarding)"))
		goto fail;

	return 0;
fail:
	return -1;
}

static int set_lookup_params(struct bpf_fib_lookup *params, const char *daddr)
{
	int ret;

	memset(params, 0, sizeof(*params));

	params->l4_protocol = IPPROTO_TCP;
	params->ifindex = ifindex;

	if (inet_pton(AF_INET6, daddr, params->ipv6_dst) == 1) {
		params->family = AF_INET6;
		ret = inet_pton(AF_INET6, IPV6_IFACE_ADDR, params->ipv6_src);
		if (!ASSERT_EQ(ret, 1, "inet_pton(IPV6_IFACE_ADDR)"))
			return -1;
		return 0;
	}

	ret = inet_pton(AF_INET, daddr, &params->ipv4_dst);
	if (!ASSERT_EQ(ret, 1, "convert IP[46] address"))
		return -1;
	params->family = AF_INET;
	ret = inet_pton(AF_INET, IPV4_IFACE_ADDR, &params->ipv4_src);
	if (!ASSERT_EQ(ret, 1, "inet_pton(IPV4_IFACE_ADDR)"))
		return -1;

	return 0;
}

static void mac_str(char *b, const __u8 *mac)
{
	sprintf(b, "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void test_fib_lookup(void)
{
	struct bpf_fib_lookup *fib_params;
	struct nstoken *nstoken = NULL;
	struct __sk_buff skb = { };
	struct fib_lookup *skel;
	int prog_fd, err, ret, i;

	/* The test does not use the skb->data, so
	 * use pkt_v6 for both v6 and v4 test.
	 */
	LIBBPF_OPTS(bpf_test_run_opts, run_opts,
		    .data_in = &pkt_v6,
		    .data_size_in = sizeof(pkt_v6),
		    .ctx_in = &skb,
		    .ctx_size_in = sizeof(skb),
	);

	skel = fib_lookup__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;
	prog_fd = bpf_program__fd(skel->progs.fib_lookup);

	SYS(fail, "ip netns add %s", NS_TEST);

	nstoken = open_netns(NS_TEST);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto fail;

	if (setup_netns())
		goto fail;

	ifindex = if_nametoindex("veth1");
	skb.ifindex = ifindex;
	fib_params = &skel->bss->fib_params;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		printf("Testing %s\n", tests[i].desc);

		if (set_lookup_params(fib_params, tests[i].daddr))
			continue;
		skel->bss->fib_lookup_ret = -1;
		skel->bss->lookup_flags = BPF_FIB_LOOKUP_OUTPUT |
			tests[i].lookup_flags;

		err = bpf_prog_test_run_opts(prog_fd, &run_opts);
		if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
			continue;

		ASSERT_EQ(skel->bss->fib_lookup_ret, tests[i].expected_ret,
			  "fib_lookup_ret");

		ret = memcmp(tests[i].dmac, fib_params->dmac, sizeof(tests[i].dmac));
		if (!ASSERT_EQ(ret, 0, "dmac not match")) {
			char expected[18], actual[18];

			mac_str(expected, tests[i].dmac);
			mac_str(actual, fib_params->dmac);
			printf("dmac expected %s actual %s\n", expected, actual);
		}
	}

fail:
	if (nstoken)
		close_netns(nstoken);
	SYS_NOFAIL("ip netns del " NS_TEST " &> /dev/null");
	fib_lookup__destroy(skel);
}
