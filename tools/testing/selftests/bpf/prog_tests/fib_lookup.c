// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <linux/rtnetlink.h>
#include <sys/types.h>
#include <net/if.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "fib_lookup.skel.h"

#define NS_TEST			"fib_lookup_ns"
#define IPV6_IFACE_ADDR		"face::face"
#define IPV6_IFACE_ADDR_SEC	"cafe::cafe"
#define IPV6_ADDR_DST		"face::3"
#define IPV6_NUD_FAILED_ADDR	"face::1"
#define IPV6_NUD_STALE_ADDR	"face::2"
#define IPV4_IFACE_ADDR		"10.0.0.254"
#define IPV4_IFACE_ADDR_SEC	"10.1.0.254"
#define IPV4_ADDR_DST		"10.2.0.254"
#define IPV4_NUD_FAILED_ADDR	"10.0.0.1"
#define IPV4_NUD_STALE_ADDR	"10.0.0.2"
#define IPV4_TBID_ADDR		"172.0.0.254"
#define IPV4_TBID_NET		"172.0.0.0"
#define IPV4_TBID_DST		"172.0.0.2"
#define IPV6_TBID_ADDR		"fd00::FFFF"
#define IPV6_TBID_NET		"fd00::"
#define IPV6_TBID_DST		"fd00::2"
#define MARK_NO_POLICY		33
#define MARK			42
#define MARK_TABLE		"200"
#define IPV4_REMOTE_DST		"1.2.3.4"
#define IPV4_LOCAL		"10.4.0.3"
#define IPV4_GW1		"10.4.0.1"
#define IPV4_GW2		"10.4.0.2"
#define IPV6_REMOTE_DST		"be:ef::b0:10"
#define IPV6_LOCAL		"fd01::3"
#define IPV6_GW1		"fd01::1"
#define IPV6_GW2		"fd01::2"
#define DMAC			"11:11:11:11:11:11"
#define DMAC_INIT { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, }
#define DMAC2			"01:01:01:01:01:01"
#define DMAC_INIT2 { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, }

struct fib_lookup_test {
	const char *desc;
	const char *daddr;
	int expected_ret;
	const char *expected_src;
	const char *expected_dst;
	int lookup_flags;
	__u32 tbid;
	__u8 dmac[6];
	__u32 mark;
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
	{ .desc = "IPv4 TBID lookup failure",
	  .daddr = IPV4_TBID_DST, .expected_ret = BPF_FIB_LKUP_RET_NOT_FWDED,
	  .lookup_flags = BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_TBID,
	  .tbid = RT_TABLE_MAIN, },
	{ .desc = "IPv4 TBID lookup success",
	  .daddr = IPV4_TBID_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_TBID, .tbid = 100,
	  .dmac = DMAC_INIT2, },
	{ .desc = "IPv6 TBID lookup failure",
	  .daddr = IPV6_TBID_DST, .expected_ret = BPF_FIB_LKUP_RET_NOT_FWDED,
	  .lookup_flags = BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_TBID,
	  .tbid = RT_TABLE_MAIN, },
	{ .desc = "IPv6 TBID lookup success",
	  .daddr = IPV6_TBID_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_TBID, .tbid = 100,
	  .dmac = DMAC_INIT2, },
	{ .desc = "IPv4 set src addr from netdev",
	  .daddr = IPV4_NUD_FAILED_ADDR, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_src = IPV4_IFACE_ADDR,
	  .lookup_flags = BPF_FIB_LOOKUP_SRC | BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv6 set src addr from netdev",
	  .daddr = IPV6_NUD_FAILED_ADDR, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_src = IPV6_IFACE_ADDR,
	  .lookup_flags = BPF_FIB_LOOKUP_SRC | BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv4 set prefsrc addr from route",
	  .daddr = IPV4_ADDR_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_src = IPV4_IFACE_ADDR_SEC,
	  .lookup_flags = BPF_FIB_LOOKUP_SRC | BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv6 set prefsrc addr route",
	  .daddr = IPV6_ADDR_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_src = IPV6_IFACE_ADDR_SEC,
	  .lookup_flags = BPF_FIB_LOOKUP_SRC | BPF_FIB_LOOKUP_SKIP_NEIGH, },
	/* policy routing */
	{ .desc = "IPv4 policy routing, default",
	  .daddr = IPV4_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_MARK | BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv4 policy routing, mark doesn't point to a policy",
	  .daddr = IPV4_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_MARK | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .mark = MARK_NO_POLICY, },
	{ .desc = "IPv4 policy routing, mark points to a policy",
	  .daddr = IPV4_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_GW2,
	  .lookup_flags = BPF_FIB_LOOKUP_MARK | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .mark = MARK, },
	{ .desc = "IPv4 policy routing, mark points to a policy, but no flag",
	  .daddr = IPV4_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .mark = MARK, },
	{ .desc = "IPv6 policy routing, default",
	  .daddr = IPV6_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV6_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_MARK | BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv6 policy routing, mark doesn't point to a policy",
	  .daddr = IPV6_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV6_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_MARK | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .mark = MARK_NO_POLICY, },
	{ .desc = "IPv6 policy routing, mark points to a policy",
	  .daddr = IPV6_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV6_GW2,
	  .lookup_flags = BPF_FIB_LOOKUP_MARK | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .mark = MARK, },
	{ .desc = "IPv6 policy routing, mark points to a policy, but no flag",
	  .daddr = IPV6_REMOTE_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV6_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .mark = MARK, },
};

static int setup_netns(void)
{
	int err;

	SYS(fail, "ip link add veth1 type veth peer name veth2");
	SYS(fail, "ip link set dev veth1 up");
	SYS(fail, "ip link set dev veth2 up");

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

	/* Setup for prefsrc IP addr selection */
	SYS(fail, "ip addr add %s/24 dev veth1", IPV4_IFACE_ADDR_SEC);
	SYS(fail, "ip route add %s/32 dev veth1 src %s", IPV4_ADDR_DST, IPV4_IFACE_ADDR_SEC);

	SYS(fail, "ip addr add %s/64 dev veth1 nodad", IPV6_IFACE_ADDR_SEC);
	SYS(fail, "ip route add %s/128 dev veth1 src %s", IPV6_ADDR_DST, IPV6_IFACE_ADDR_SEC);

	/* Setup for tbid lookup tests */
	SYS(fail, "ip addr add %s/24 dev veth2", IPV4_TBID_ADDR);
	SYS(fail, "ip route del %s/24 dev veth2", IPV4_TBID_NET);
	SYS(fail, "ip route add table 100 %s/24 dev veth2", IPV4_TBID_NET);
	SYS(fail, "ip neigh add %s dev veth2 lladdr %s nud stale", IPV4_TBID_DST, DMAC2);

	SYS(fail, "ip addr add %s/64 dev veth2", IPV6_TBID_ADDR);
	SYS(fail, "ip -6 route del %s/64 dev veth2", IPV6_TBID_NET);
	SYS(fail, "ip -6 route add table 100 %s/64 dev veth2", IPV6_TBID_NET);
	SYS(fail, "ip neigh add %s dev veth2 lladdr %s nud stale", IPV6_TBID_DST, DMAC2);

	err = write_sysctl("/proc/sys/net/ipv4/conf/veth1/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.conf.veth1.forwarding)"))
		goto fail;

	err = write_sysctl("/proc/sys/net/ipv6/conf/veth1/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv6.conf.veth1.forwarding)"))
		goto fail;

	/* Setup for policy routing tests */
	SYS(fail, "ip addr add %s/24 dev veth1", IPV4_LOCAL);
	SYS(fail, "ip addr add %s/64 dev veth1 nodad", IPV6_LOCAL);
	SYS(fail, "ip route add %s/32 via %s", IPV4_REMOTE_DST, IPV4_GW1);
	SYS(fail, "ip route add %s/32 via %s table %s", IPV4_REMOTE_DST, IPV4_GW2, MARK_TABLE);
	SYS(fail, "ip -6 route add %s/128 via %s", IPV6_REMOTE_DST, IPV6_GW1);
	SYS(fail, "ip -6 route add %s/128 via %s table %s", IPV6_REMOTE_DST, IPV6_GW2, MARK_TABLE);
	SYS(fail, "ip rule add prio 2 fwmark %d lookup %s", MARK, MARK_TABLE);
	SYS(fail, "ip -6 rule add prio 2 fwmark %d lookup %s", MARK, MARK_TABLE);

	return 0;
fail:
	return -1;
}

static int set_lookup_params(struct bpf_fib_lookup *params,
			     const struct fib_lookup_test *test,
			     int ifindex)
{
	int ret;

	memset(params, 0, sizeof(*params));

	params->l4_protocol = IPPROTO_TCP;
	params->ifindex = ifindex;
	params->tbid = test->tbid;
	params->mark = test->mark;

	if (inet_pton(AF_INET6, test->daddr, params->ipv6_dst) == 1) {
		params->family = AF_INET6;
		if (!(test->lookup_flags & BPF_FIB_LOOKUP_SRC)) {
			ret = inet_pton(AF_INET6, IPV6_IFACE_ADDR, params->ipv6_src);
			if (!ASSERT_EQ(ret, 1, "inet_pton(IPV6_IFACE_ADDR)"))
				return -1;
		}

		return 0;
	}

	ret = inet_pton(AF_INET, test->daddr, &params->ipv4_dst);
	if (!ASSERT_EQ(ret, 1, "convert IP[46] address"))
		return -1;
	params->family = AF_INET;

	if (!(test->lookup_flags & BPF_FIB_LOOKUP_SRC)) {
		ret = inet_pton(AF_INET, IPV4_IFACE_ADDR, &params->ipv4_src);
		if (!ASSERT_EQ(ret, 1, "inet_pton(IPV4_IFACE_ADDR)"))
			return -1;
	}

	return 0;
}

static void mac_str(char *b, const __u8 *mac)
{
	sprintf(b, "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void assert_ip_address(int family, void *addr, const char *expected_str)
{
	char str[INET6_ADDRSTRLEN];
	u8 expected_addr[16];
	int addr_len = 0;
	int ret;

	switch (family) {
	case AF_INET6:
		ret = inet_pton(AF_INET6, expected_str, expected_addr);
		ASSERT_EQ(ret, 1, "inet_pton(AF_INET6, expected_str)");
		addr_len = 16;
		break;
	case AF_INET:
		ret = inet_pton(AF_INET, expected_str, expected_addr);
		ASSERT_EQ(ret, 1, "inet_pton(AF_INET, expected_str)");
		addr_len = 4;
		break;
	default:
		PRINT_FAIL("invalid address family: %d", family);
		break;
	}

	if (memcmp(addr, expected_addr, addr_len)) {
		inet_ntop(family, addr, str, sizeof(str));
		PRINT_FAIL("expected %s actual %s ", expected_str, str);
	}
}

static void assert_src_ip(struct bpf_fib_lookup *params, const char *expected)
{
	assert_ip_address(params->family, params->ipv6_src, expected);
}

static void assert_dst_ip(struct bpf_fib_lookup *params, const char *expected)
{
	assert_ip_address(params->family, params->ipv6_dst, expected);
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

	skb.ifindex = if_nametoindex("veth1");
	if (!ASSERT_NEQ(skb.ifindex, 0, "if_nametoindex(veth1)"))
		goto fail;

	fib_params = &skel->bss->fib_params;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		printf("Testing %s ", tests[i].desc);

		if (set_lookup_params(fib_params, &tests[i], skb.ifindex))
			continue;

		skel->bss->fib_lookup_ret = -1;
		skel->bss->lookup_flags = tests[i].lookup_flags;

		err = bpf_prog_test_run_opts(prog_fd, &run_opts);
		if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
			continue;

		ASSERT_EQ(skel->bss->fib_lookup_ret, tests[i].expected_ret,
			  "fib_lookup_ret");

		if (tests[i].expected_src)
			assert_src_ip(fib_params, tests[i].expected_src);

		if (tests[i].expected_dst)
			assert_dst_ip(fib_params, tests[i].expected_dst);

		ret = memcmp(tests[i].dmac, fib_params->dmac, sizeof(tests[i].dmac));
		if (!ASSERT_EQ(ret, 0, "dmac not match")) {
			char expected[18], actual[18];

			mac_str(expected, tests[i].dmac);
			mac_str(actual, fib_params->dmac);
			printf("dmac expected %s actual %s ", expected, actual);
		}

		// ensure tbid is zero'd out after fib lookup.
		if (tests[i].lookup_flags & BPF_FIB_LOOKUP_DIRECT) {
			if (!ASSERT_EQ(skel->bss->fib_params.tbid, 0,
					"expected fib_params.tbid to be zero"))
				goto fail;
		}
	}

fail:
	if (nstoken)
		close_netns(nstoken);
	SYS_NOFAIL("ip netns del " NS_TEST);
	fib_lookup__destroy(skel);
}
