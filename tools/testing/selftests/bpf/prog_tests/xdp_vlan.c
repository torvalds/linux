// SPDX-License-Identifier: GPL-2.0

/*
 * Network topology:
 *  -----------        -----------
 *  |  NS1    |        |   NS2   |
 *  | veth0  -|--------|- veth0  |
 *  -----------        -----------
 *
 */

#define _GNU_SOURCE
#include <net/if.h>
#include <uapi/linux/if_link.h>

#include "network_helpers.h"
#include "test_progs.h"
#include "test_xdp_vlan.skel.h"


#define VETH_NAME	"veth0"
#define NS_MAX_SIZE	32
#define NS1_NAME	"ns-xdp-vlan-1-"
#define NS2_NAME	"ns-xdp-vlan-2-"
#define NS1_IP_ADDR	"100.64.10.1"
#define NS2_IP_ADDR	"100.64.10.2"
#define VLAN_ID		4011

static int setup_network(char *ns1, char *ns2)
{
	if (!ASSERT_OK(append_tid(ns1, NS_MAX_SIZE), "create ns1 name"))
		goto fail;
	if (!ASSERT_OK(append_tid(ns2, NS_MAX_SIZE), "create ns2 name"))
		goto fail;

	SYS(fail, "ip netns add %s", ns1);
	SYS(fail, "ip netns add %s", ns2);
	SYS(fail, "ip -n %s link add %s type veth peer name %s netns %s",
	    ns1, VETH_NAME, VETH_NAME, ns2);

	/* NOTICE: XDP require VLAN header inside packet payload
	 *  - Thus, disable VLAN offloading driver features
	 */
	SYS(fail, "ip netns exec %s ethtool -K %s rxvlan off txvlan off", ns1, VETH_NAME);
	SYS(fail, "ip netns exec %s ethtool -K %s rxvlan off txvlan off", ns2, VETH_NAME);

	/* NS1 configuration */
	SYS(fail, "ip -n %s addr add %s/24 dev %s", ns1, NS1_IP_ADDR, VETH_NAME);
	SYS(fail, "ip -n %s link set %s up", ns1, VETH_NAME);

	/* NS2 configuration */
	SYS(fail, "ip -n %s link add link %s name %s.%d type vlan id %d",
	    ns2, VETH_NAME, VETH_NAME, VLAN_ID, VLAN_ID);
	SYS(fail, "ip -n %s addr add %s/24 dev %s.%d", ns2, NS2_IP_ADDR, VETH_NAME, VLAN_ID);
	SYS(fail, "ip -n %s link set %s up", ns2, VETH_NAME);
	SYS(fail, "ip -n %s link set %s.%d up", ns2, VETH_NAME, VLAN_ID);

	/* At this point ping should fail because VLAN tags are only used by NS2 */
	return !SYS_NOFAIL("ip netns exec %s ping -W 1 -c1 %s", ns2, NS1_IP_ADDR);

fail:
	return -1;
}

static void cleanup_network(const char *ns1, const char *ns2)
{
	SYS_NOFAIL("ip netns del %s", ns1);
	SYS_NOFAIL("ip netns del %s", ns2);
}

static void xdp_vlan(struct bpf_program *xdp, struct bpf_program *tc, u32 flags)
{
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .attach_point = BPF_TC_EGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 1);
	char ns1[NS_MAX_SIZE] = NS1_NAME;
	char ns2[NS_MAX_SIZE] = NS2_NAME;
	struct nstoken *nstoken = NULL;
	int interface;
	int ret;

	if (!ASSERT_OK(setup_network(ns1, ns2), "setup network"))
		goto cleanup;

	nstoken = open_netns(ns1);
	if (!ASSERT_OK_PTR(nstoken, "open NS1"))
		goto cleanup;

	interface = if_nametoindex(VETH_NAME);
	if (!ASSERT_NEQ(interface, 0, "get interface index"))
		goto cleanup;

	ret = bpf_xdp_attach(interface, bpf_program__fd(xdp), flags, NULL);
	if (!ASSERT_OK(ret, "attach xdp_vlan_change"))
		goto cleanup;

	tc_hook.ifindex = interface;
	ret = bpf_tc_hook_create(&tc_hook);
	if (!ASSERT_OK(ret, "bpf_tc_hook_create"))
		goto detach_xdp;

	/* Now we'll use BPF programs to pop/push the VLAN tags */
	tc_opts.prog_fd = bpf_program__fd(tc);
	ret = bpf_tc_attach(&tc_hook, &tc_opts);
	if (!ASSERT_OK(ret, "bpf_tc_attach"))
		goto detach_xdp;

	close_netns(nstoken);
	nstoken = NULL;

	/* Now the namespaces can reach each-other, test with pings */
	SYS(detach_tc, "ip netns exec %s ping -i 0.2 -W 2 -c 2 %s > /dev/null", ns1, NS2_IP_ADDR);
	SYS(detach_tc, "ip netns exec %s ping -i 0.2 -W 2 -c 2 %s > /dev/null", ns2, NS1_IP_ADDR);


detach_tc:
	bpf_tc_detach(&tc_hook, &tc_opts);
detach_xdp:
	bpf_xdp_detach(interface, flags, NULL);
cleanup:
	close_netns(nstoken);
	cleanup_network(ns1, ns2);
}

/* First test: Remove VLAN by setting VLAN ID 0, using "xdp_vlan_change"
 * egress use TC to add back VLAN tag 4011
 */
void test_xdp_vlan_change(void)
{
	struct test_xdp_vlan *skel;

	skel = test_xdp_vlan__open_and_load();
	if (!ASSERT_OK_PTR(skel, "xdp_vlan__open_and_load"))
		return;

	if (test__start_subtest("0"))
		xdp_vlan(skel->progs.xdp_vlan_change, skel->progs.tc_vlan_push, 0);

	if (test__start_subtest("DRV_MODE"))
		xdp_vlan(skel->progs.xdp_vlan_change, skel->progs.tc_vlan_push,
			 XDP_FLAGS_DRV_MODE);

	if (test__start_subtest("SKB_MODE"))
		xdp_vlan(skel->progs.xdp_vlan_change, skel->progs.tc_vlan_push,
			 XDP_FLAGS_SKB_MODE);

	test_xdp_vlan__destroy(skel);
}

/* Second test: XDP prog fully remove vlan header
 *
 * Catch kernel bug for generic-XDP, that doesn't allow us to
 * remove a VLAN header, because skb->protocol still contain VLAN
 * ETH_P_8021Q indication, and this cause overwriting of our changes.
 */
void test_xdp_vlan_remove(void)
{
	struct test_xdp_vlan *skel;

	skel = test_xdp_vlan__open_and_load();
	if (!ASSERT_OK_PTR(skel, "xdp_vlan__open_and_load"))
		return;

	if (test__start_subtest("0"))
		xdp_vlan(skel->progs.xdp_vlan_remove_outer2, skel->progs.tc_vlan_push, 0);

	if (test__start_subtest("DRV_MODE"))
		xdp_vlan(skel->progs.xdp_vlan_remove_outer2, skel->progs.tc_vlan_push,
			 XDP_FLAGS_DRV_MODE);

	if (test__start_subtest("SKB_MODE"))
		xdp_vlan(skel->progs.xdp_vlan_remove_outer2, skel->progs.tc_vlan_push,
			 XDP_FLAGS_SKB_MODE);

	test_xdp_vlan__destroy(skel);
}
