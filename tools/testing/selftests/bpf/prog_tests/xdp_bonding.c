// SPDX-License-Identifier: GPL-2.0

/**
 * Test XDP bonding support
 *
 * Sets up two bonded veth pairs between two fresh namespaces
 * and verifies that XDP_TX program loaded on a bond device
 * are correctly loaded onto the slave devices and XDP_TX'd
 * packets are balanced using bonding.
 */

#define _GNU_SOURCE
#include <sched.h>
#include <net/if.h>
#include <linux/if_link.h>
#include "test_progs.h"
#include "network_helpers.h"
#include <linux/if_bonding.h>
#include <linux/limits.h>
#include <linux/udp.h>

#include "xdp_dummy.skel.h"
#include "xdp_redirect_multi_kern.skel.h"
#include "xdp_tx.skel.h"

#define BOND1_MAC {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}
#define BOND1_MAC_STR "00:11:22:33:44:55"
#define BOND2_MAC {0x00, 0x22, 0x33, 0x44, 0x55, 0x66}
#define BOND2_MAC_STR "00:22:33:44:55:66"
#define NPACKETS 100

static int root_netns_fd = -1;

static void restore_root_netns(void)
{
	ASSERT_OK(setns(root_netns_fd, CLONE_NEWNET), "restore_root_netns");
}

static int setns_by_name(char *name)
{
	int nsfd, err;
	char nspath[PATH_MAX];

	snprintf(nspath, sizeof(nspath), "%s/%s", "/var/run/netns", name);
	nsfd = open(nspath, O_RDONLY | O_CLOEXEC);
	if (nsfd < 0)
		return -1;

	err = setns(nsfd, CLONE_NEWNET);
	close(nsfd);
	return err;
}

static int get_rx_packets(const char *iface)
{
	FILE *f;
	char line[512];
	int iface_len = strlen(iface);

	f = fopen("/proc/net/dev", "r");
	if (!f)
		return -1;

	while (fgets(line, sizeof(line), f)) {
		char *p = line;

		while (*p == ' ')
			p++; /* skip whitespace */
		if (!strncmp(p, iface, iface_len)) {
			p += iface_len;
			if (*p++ != ':')
				continue;
			while (*p == ' ')
				p++; /* skip whitespace */
			while (*p && *p != ' ')
				p++; /* skip rx bytes */
			while (*p == ' ')
				p++; /* skip whitespace */
			fclose(f);
			return atoi(p);
		}
	}
	fclose(f);
	return -1;
}

#define MAX_BPF_LINKS 8

struct skeletons {
	struct xdp_dummy *xdp_dummy;
	struct xdp_tx *xdp_tx;
	struct xdp_redirect_multi_kern *xdp_redirect_multi_kern;

	int nlinks;
	struct bpf_link *links[MAX_BPF_LINKS];
};

static int xdp_attach(struct skeletons *skeletons, struct bpf_program *prog, char *iface)
{
	struct bpf_link *link;
	int ifindex;

	ifindex = if_nametoindex(iface);
	if (!ASSERT_GT(ifindex, 0, "get ifindex"))
		return -1;

	if (!ASSERT_LE(skeletons->nlinks+1, MAX_BPF_LINKS, "too many XDP programs attached"))
		return -1;

	link = bpf_program__attach_xdp(prog, ifindex);
	if (!ASSERT_OK_PTR(link, "attach xdp program"))
		return -1;

	skeletons->links[skeletons->nlinks++] = link;
	return 0;
}

enum {
	BOND_ONE_NO_ATTACH = 0,
	BOND_BOTH_AND_ATTACH,
};

static const char * const mode_names[] = {
	[BOND_MODE_ROUNDROBIN]   = "balance-rr",
	[BOND_MODE_ACTIVEBACKUP] = "active-backup",
	[BOND_MODE_XOR]          = "balance-xor",
	[BOND_MODE_BROADCAST]    = "broadcast",
	[BOND_MODE_8023AD]       = "802.3ad",
	[BOND_MODE_TLB]          = "balance-tlb",
	[BOND_MODE_ALB]          = "balance-alb",
};

static const char * const xmit_policy_names[] = {
	[BOND_XMIT_POLICY_LAYER2]       = "layer2",
	[BOND_XMIT_POLICY_LAYER34]      = "layer3+4",
	[BOND_XMIT_POLICY_LAYER23]      = "layer2+3",
	[BOND_XMIT_POLICY_ENCAP23]      = "encap2+3",
	[BOND_XMIT_POLICY_ENCAP34]      = "encap3+4",
};

static int bonding_setup(struct skeletons *skeletons, int mode, int xmit_policy,
			 int bond_both_attach)
{
	SYS(fail, "ip netns add ns_dst");
	SYS(fail, "ip link add veth1_1 type veth peer name veth2_1 netns ns_dst");
	SYS(fail, "ip link add veth1_2 type veth peer name veth2_2 netns ns_dst");

	SYS(fail, "ip link add bond1 type bond mode %s xmit_hash_policy %s",
	    mode_names[mode], xmit_policy_names[xmit_policy]);
	SYS(fail, "ip link set bond1 up address " BOND1_MAC_STR " addrgenmode none");
	SYS(fail, "ip -netns ns_dst link add bond2 type bond mode %s xmit_hash_policy %s",
	    mode_names[mode], xmit_policy_names[xmit_policy]);
	SYS(fail, "ip -netns ns_dst link set bond2 up address " BOND2_MAC_STR " addrgenmode none");

	SYS(fail, "ip link set veth1_1 master bond1");
	if (bond_both_attach == BOND_BOTH_AND_ATTACH) {
		SYS(fail, "ip link set veth1_2 master bond1");
	} else {
		SYS(fail, "ip link set veth1_2 up addrgenmode none");

		if (xdp_attach(skeletons, skeletons->xdp_dummy->progs.xdp_dummy_prog, "veth1_2"))
			return -1;
	}

	SYS(fail, "ip -netns ns_dst link set veth2_1 master bond2");

	if (bond_both_attach == BOND_BOTH_AND_ATTACH)
		SYS(fail, "ip -netns ns_dst link set veth2_2 master bond2");
	else
		SYS(fail, "ip -netns ns_dst link set veth2_2 up addrgenmode none");

	/* Load a dummy program on sending side as with veth peer needs to have a
	 * XDP program loaded as well.
	 */
	if (xdp_attach(skeletons, skeletons->xdp_dummy->progs.xdp_dummy_prog, "bond1"))
		return -1;

	if (bond_both_attach == BOND_BOTH_AND_ATTACH) {
		if (!ASSERT_OK(setns_by_name("ns_dst"), "set netns to ns_dst"))
			return -1;

		if (xdp_attach(skeletons, skeletons->xdp_tx->progs.xdp_tx, "bond2"))
			return -1;

		restore_root_netns();
	}

	return 0;
fail:
	return -1;
}

static void bonding_cleanup(struct skeletons *skeletons)
{
	restore_root_netns();
	while (skeletons->nlinks) {
		skeletons->nlinks--;
		bpf_link__destroy(skeletons->links[skeletons->nlinks]);
	}
	ASSERT_OK(system("ip link delete bond1"), "delete bond1");
	ASSERT_OK(system("ip link delete veth1_1"), "delete veth1_1");
	ASSERT_OK(system("ip link delete veth1_2"), "delete veth1_2");
	ASSERT_OK(system("ip netns delete ns_dst"), "delete ns_dst");
}

static int send_udp_packets(int vary_dst_ip)
{
	struct ethhdr eh = {
		.h_source = BOND1_MAC,
		.h_dest = BOND2_MAC,
		.h_proto = htons(ETH_P_IP),
	};
	struct iphdr iph = {};
	struct udphdr uh = {};
	uint8_t buf[128];
	int i, s = -1;
	int ifindex;

	s = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW);
	if (!ASSERT_GE(s, 0, "socket"))
		goto err;

	ifindex = if_nametoindex("bond1");
	if (!ASSERT_GT(ifindex, 0, "get bond1 ifindex"))
		goto err;

	iph.ihl = 5;
	iph.version = 4;
	iph.tos = 16;
	iph.id = 1;
	iph.ttl = 64;
	iph.protocol = IPPROTO_UDP;
	iph.saddr = 1;
	iph.daddr = 2;
	iph.tot_len = htons(sizeof(buf) - ETH_HLEN);
	iph.check = 0;

	for (i = 1; i <= NPACKETS; i++) {
		int n;
		struct sockaddr_ll saddr_ll = {
			.sll_ifindex = ifindex,
			.sll_halen = ETH_ALEN,
			.sll_addr = BOND2_MAC,
		};

		/* vary the UDP destination port for even distribution with roundrobin/xor modes */
		uh.dest++;

		if (vary_dst_ip)
			iph.daddr++;

		/* construct a packet */
		memcpy(buf, &eh, sizeof(eh));
		memcpy(buf + sizeof(eh), &iph, sizeof(iph));
		memcpy(buf + sizeof(eh) + sizeof(iph), &uh, sizeof(uh));

		n = sendto(s, buf, sizeof(buf), 0, (struct sockaddr *)&saddr_ll, sizeof(saddr_ll));
		if (!ASSERT_EQ(n, sizeof(buf), "sendto"))
			goto err;
	}

	return 0;

err:
	if (s >= 0)
		close(s);
	return -1;
}

static void test_xdp_bonding_with_mode(struct skeletons *skeletons, int mode, int xmit_policy)
{
	int bond1_rx;

	if (bonding_setup(skeletons, mode, xmit_policy, BOND_BOTH_AND_ATTACH))
		goto out;

	if (send_udp_packets(xmit_policy != BOND_XMIT_POLICY_LAYER34))
		goto out;

	bond1_rx = get_rx_packets("bond1");
	ASSERT_EQ(bond1_rx, NPACKETS, "expected more received packets");

	switch (mode) {
	case BOND_MODE_ROUNDROBIN:
	case BOND_MODE_XOR: {
		int veth1_rx = get_rx_packets("veth1_1");
		int veth2_rx = get_rx_packets("veth1_2");
		int diff = abs(veth1_rx - veth2_rx);

		ASSERT_GE(veth1_rx + veth2_rx, NPACKETS, "expected more packets");

		switch (xmit_policy) {
		case BOND_XMIT_POLICY_LAYER2:
			ASSERT_GE(diff, NPACKETS,
				  "expected packets on only one of the interfaces");
			break;
		case BOND_XMIT_POLICY_LAYER23:
		case BOND_XMIT_POLICY_LAYER34:
			ASSERT_LT(diff, NPACKETS/2,
				  "expected even distribution of packets");
			break;
		default:
			PRINT_FAIL("Unimplemented xmit_policy=%d\n", xmit_policy);
			break;
		}
		break;
	}
	case BOND_MODE_ACTIVEBACKUP: {
		int veth1_rx = get_rx_packets("veth1_1");
		int veth2_rx = get_rx_packets("veth1_2");
		int diff = abs(veth1_rx - veth2_rx);

		ASSERT_GE(diff, NPACKETS,
			  "expected packets on only one of the interfaces");
		break;
	}
	default:
		PRINT_FAIL("Unimplemented xmit_policy=%d\n", xmit_policy);
		break;
	}

out:
	bonding_cleanup(skeletons);
}

/* Test the broadcast redirection using xdp_redirect_map_multi_prog and adding
 * all the interfaces to it and checking that broadcasting won't send the packet
 * to neither the ingress bond device (bond2) or its slave (veth2_1).
 */
static void test_xdp_bonding_redirect_multi(struct skeletons *skeletons)
{
	static const char * const ifaces[] = {"bond2", "veth2_1", "veth2_2"};
	int veth1_1_rx, veth1_2_rx;
	int err;

	if (bonding_setup(skeletons, BOND_MODE_ROUNDROBIN, BOND_XMIT_POLICY_LAYER23,
			  BOND_ONE_NO_ATTACH))
		goto out;


	if (!ASSERT_OK(setns_by_name("ns_dst"), "could not set netns to ns_dst"))
		goto out;

	/* populate the devmap with the relevant interfaces */
	for (int i = 0; i < ARRAY_SIZE(ifaces); i++) {
		int ifindex = if_nametoindex(ifaces[i]);
		int map_fd = bpf_map__fd(skeletons->xdp_redirect_multi_kern->maps.map_all);

		if (!ASSERT_GT(ifindex, 0, "could not get interface index"))
			goto out;

		err = bpf_map_update_elem(map_fd, &ifindex, &ifindex, 0);
		if (!ASSERT_OK(err, "add interface to map_all"))
			goto out;
	}

	if (xdp_attach(skeletons,
		       skeletons->xdp_redirect_multi_kern->progs.xdp_redirect_map_multi_prog,
		       "bond2"))
		goto out;

	restore_root_netns();

	if (send_udp_packets(BOND_MODE_ROUNDROBIN))
		goto out;

	veth1_1_rx = get_rx_packets("veth1_1");
	veth1_2_rx = get_rx_packets("veth1_2");

	ASSERT_EQ(veth1_1_rx, 0, "expected no packets on veth1_1");
	ASSERT_GE(veth1_2_rx, NPACKETS, "expected packets on veth1_2");

out:
	restore_root_netns();
	bonding_cleanup(skeletons);
}

/* Test that XDP programs cannot be attached to both the bond master and slaves simultaneously */
static void test_xdp_bonding_attach(struct skeletons *skeletons)
{
	struct bpf_link *link = NULL;
	struct bpf_link *link2 = NULL;
	int veth, bond, err;

	if (!ASSERT_OK(system("ip link add veth type veth"), "add veth"))
		goto out;
	if (!ASSERT_OK(system("ip link add bond type bond"), "add bond"))
		goto out;

	veth = if_nametoindex("veth");
	if (!ASSERT_GE(veth, 0, "if_nametoindex veth"))
		goto out;
	bond = if_nametoindex("bond");
	if (!ASSERT_GE(bond, 0, "if_nametoindex bond"))
		goto out;

	/* enslaving with a XDP program loaded is allowed */
	link = bpf_program__attach_xdp(skeletons->xdp_dummy->progs.xdp_dummy_prog, veth);
	if (!ASSERT_OK_PTR(link, "attach program to veth"))
		goto out;

	err = system("ip link set veth master bond");
	if (!ASSERT_OK(err, "set veth master"))
		goto out;

	bpf_link__destroy(link);
	link = NULL;

	/* attaching to slave when master has no program is allowed */
	link = bpf_program__attach_xdp(skeletons->xdp_dummy->progs.xdp_dummy_prog, veth);
	if (!ASSERT_OK_PTR(link, "attach program to slave when enslaved"))
		goto out;

	/* attaching to master not allowed when slave has program loaded */
	link2 = bpf_program__attach_xdp(skeletons->xdp_dummy->progs.xdp_dummy_prog, bond);
	if (!ASSERT_ERR_PTR(link2, "attach program to master when slave has program"))
		goto out;

	bpf_link__destroy(link);
	link = NULL;

	/* attaching XDP program to master allowed when slave has no program */
	link = bpf_program__attach_xdp(skeletons->xdp_dummy->progs.xdp_dummy_prog, bond);
	if (!ASSERT_OK_PTR(link, "attach program to master"))
		goto out;

	/* attaching to slave not allowed when master has program loaded */
	link2 = bpf_program__attach_xdp(skeletons->xdp_dummy->progs.xdp_dummy_prog, veth);
	if (!ASSERT_ERR_PTR(link2, "attach program to slave when master has program"))
		goto out;

	bpf_link__destroy(link);
	link = NULL;

	/* test program unwinding with a non-XDP slave */
	if (!ASSERT_OK(system("ip link add vxlan type vxlan id 1 remote 1.2.3.4 dstport 0 dev lo"),
		       "add vxlan"))
		goto out;

	err = system("ip link set vxlan master bond");
	if (!ASSERT_OK(err, "set vxlan master"))
		goto out;

	/* attaching not allowed when one slave does not support XDP */
	link = bpf_program__attach_xdp(skeletons->xdp_dummy->progs.xdp_dummy_prog, bond);
	if (!ASSERT_ERR_PTR(link, "attach program to master when slave does not support XDP"))
		goto out;

out:
	bpf_link__destroy(link);
	bpf_link__destroy(link2);

	system("ip link del veth");
	system("ip link del bond");
	system("ip link del vxlan");
}

/* Test with nested bonding devices to catch issue with negative jump label count */
static void test_xdp_bonding_nested(struct skeletons *skeletons)
{
	struct bpf_link *link = NULL;
	int bond, err;

	if (!ASSERT_OK(system("ip link add bond type bond"), "add bond"))
		goto out;

	bond = if_nametoindex("bond");
	if (!ASSERT_GE(bond, 0, "if_nametoindex bond"))
		goto out;

	if (!ASSERT_OK(system("ip link add bond_nest1 type bond"), "add bond_nest1"))
		goto out;

	err = system("ip link set bond_nest1 master bond");
	if (!ASSERT_OK(err, "set bond_nest1 master"))
		goto out;

	if (!ASSERT_OK(system("ip link add bond_nest2 type bond"), "add bond_nest1"))
		goto out;

	err = system("ip link set bond_nest2 master bond_nest1");
	if (!ASSERT_OK(err, "set bond_nest2 master"))
		goto out;

	link = bpf_program__attach_xdp(skeletons->xdp_dummy->progs.xdp_dummy_prog, bond);
	ASSERT_OK_PTR(link, "attach program to master");

out:
	bpf_link__destroy(link);
	system("ip link del bond");
	system("ip link del bond_nest1");
	system("ip link del bond_nest2");
}

static int libbpf_debug_print(enum libbpf_print_level level,
			      const char *format, va_list args)
{
	if (level != LIBBPF_WARN)
		vprintf(format, args);
	return 0;
}

struct bond_test_case {
	char *name;
	int mode;
	int xmit_policy;
};

static struct bond_test_case bond_test_cases[] = {
	{ "xdp_bonding_roundrobin", BOND_MODE_ROUNDROBIN, BOND_XMIT_POLICY_LAYER23, },
	{ "xdp_bonding_activebackup", BOND_MODE_ACTIVEBACKUP, BOND_XMIT_POLICY_LAYER23 },

	{ "xdp_bonding_xor_layer2", BOND_MODE_XOR, BOND_XMIT_POLICY_LAYER2, },
	{ "xdp_bonding_xor_layer23", BOND_MODE_XOR, BOND_XMIT_POLICY_LAYER23, },
	{ "xdp_bonding_xor_layer34", BOND_MODE_XOR, BOND_XMIT_POLICY_LAYER34, },
};

void serial_test_xdp_bonding(void)
{
	libbpf_print_fn_t old_print_fn;
	struct skeletons skeletons = {};
	int i;

	old_print_fn = libbpf_set_print(libbpf_debug_print);

	root_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	if (!ASSERT_GE(root_netns_fd, 0, "open /proc/self/ns/net"))
		goto out;

	skeletons.xdp_dummy = xdp_dummy__open_and_load();
	if (!ASSERT_OK_PTR(skeletons.xdp_dummy, "xdp_dummy__open_and_load"))
		goto out;

	skeletons.xdp_tx = xdp_tx__open_and_load();
	if (!ASSERT_OK_PTR(skeletons.xdp_tx, "xdp_tx__open_and_load"))
		goto out;

	skeletons.xdp_redirect_multi_kern = xdp_redirect_multi_kern__open_and_load();
	if (!ASSERT_OK_PTR(skeletons.xdp_redirect_multi_kern,
			   "xdp_redirect_multi_kern__open_and_load"))
		goto out;

	if (test__start_subtest("xdp_bonding_attach"))
		test_xdp_bonding_attach(&skeletons);

	if (test__start_subtest("xdp_bonding_nested"))
		test_xdp_bonding_nested(&skeletons);

	for (i = 0; i < ARRAY_SIZE(bond_test_cases); i++) {
		struct bond_test_case *test_case = &bond_test_cases[i];

		if (test__start_subtest(test_case->name))
			test_xdp_bonding_with_mode(
				&skeletons,
				test_case->mode,
				test_case->xmit_policy);
	}

	if (test__start_subtest("xdp_bonding_redirect_multi"))
		test_xdp_bonding_redirect_multi(&skeletons);

out:
	xdp_dummy__destroy(skeletons.xdp_dummy);
	xdp_tx__destroy(skeletons.xdp_tx);
	xdp_redirect_multi_kern__destroy(skeletons.xdp_redirect_multi_kern);

	libbpf_set_print(old_print_fn);
	if (root_netns_fd >= 0)
		close(root_netns_fd);
}
