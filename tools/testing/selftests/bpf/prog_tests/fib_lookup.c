// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <linux/rtnetlink.h>
#include <linux/if_ether.h>
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
#define IPV4_TBID_NONEIGH_DST	"172.0.0.5"
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
#define VLAN_ID			100
#define VLAN_IFACE		"veth1.100"
#define VLAN_ID_DOWN		102
#define VLAN_IFACE_DOWN		"veth1.102"
#define QINQ_OUTER_IFACE	"veth1.200"
#define QINQ_INNER_IFACE	"veth1.200.300"
#define VLAN_TABLE		"300"
#define IPV4_VLAN_IFACE_ADDR	"10.5.0.254"
#define IPV4_VLAN_EGRESS_DST	"10.5.0.2"
#define IPV4_QINQ_DST		"10.7.0.2"
#define IPV4_VLAN_DST		"10.6.0.2"
#define IPV4_VLAN_GW		"10.5.0.1"
#define IPV6_VLAN_IFACE_ADDR	"fd02::254"
#define IPV6_VLAN_EGRESS_DST	"fd02::2"
#define IPV6_VLAN_DST		"fd03::2"
#define IPV6_VLAN_GW		"fd02::1"
#define VLAN_VID_UNUSED		999
#define VRF_IFACE		"vrf-blue"
#define VRF_TABLE		"1000"
#define VRF_VLAN_ID		101
#define VRF_VLAN_IFACE		"veth1.101"
#define IPV4_VRF_IFACE_ADDR	"10.8.0.254"
#define IPV4_VRF_GW		"10.8.0.1"
#define IPV4_VRF_DST		"10.9.0.2"
#define TBID_VLAN_ID		50
#define TBID_VLAN_IFACE		"veth2.50"
#define IPV4_TBID_VLAN_DST	"172.2.0.2"
#define IPV4_BOND_VLAN_DST	"10.11.0.2"
#define IPV4_VLAN_MTU_DST	"10.5.9.2"
#define QINQ_AD_VLAN_ID		200
#define QINQ_INNER_VLAN_ID	300
#define BOND_IFACE		"bond99"
#define BOND_PORT		"veth3"
#define BOND_PORT_PEER		"veth4"
#define BOND_VLAN_ID		500
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
	/*
	 * input tag with BPF_FIB_LOOKUP_VLAN_INPUT; expected output tag
	 * with BPF_FIB_LOOKUP_VLAN (checked when check_vlan is set)
	 */
	__u16 vlan_proto;
	__u16 vlan_id;
	bool check_vlan;
	const char *expected_dev; /* expected params->ifindex after lookup */
	const char *iif;	  /* override the default veth1 input device */
	__u16 tot_len;		  /* triggers the in-lookup mtu check when set */
	__u16 expected_mtu;	  /* expected mtu_result (union with tot_len) */
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
	/*
	 * An error that returns after the egress device is resolved must
	 * report the egress ifindex, not the input. This routes from input
	 * veth1 via veth2 (table 100) to a dst with no neighbour, so
	 * input != egress, pinning NO_NEIGH to the egress device.
	 */
	{ .desc = "IPv4 NO_NEIGH reports the egress ifindex, not the input",
	  .daddr = IPV4_TBID_NONEIGH_DST,
	  .expected_ret = BPF_FIB_LKUP_RET_NO_NEIGH,
	  .lookup_flags = BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_TBID, .tbid = 100,
	  .expected_dev = "veth2", },
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
	/* vlan egress resolution */
	/*
	 * Invariant the VLAN-egress arms jointly enforce: a
	 * BPF_FIB_LOOKUP_VLAN SUCCESS always carries a physical,
	 * xmit-capable ifindex; no SUCCESS ever returns a VLAN-device
	 * ifindex. Reducible arms pin ifindex == the physical parent; the
	 * QinQ and foreign-netns arms pin VLAN_FAILURE with params->ifindex
	 * left at the input, so a regression to best-effort (SUCCESS + the
	 * VLAN ifindex) fails one.
	 */
	{ .desc = "IPv4 VLAN egress, no flag",
	  .daddr = IPV4_VLAN_EGRESS_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = VLAN_IFACE, .check_vlan = true, },
	{ .desc = "IPv4 VLAN egress, single VLAN",
	  .daddr = IPV4_VLAN_EGRESS_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = "veth1", .check_vlan = true,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	/*
	 * skb path without tot_len: mtu_result is the VLAN device's mtu
	 * (1400), not the parent's (1500)
	 */
	{ .desc = "IPv4 VLAN egress, skb-path mtu is the VLAN device's without the flag",
	  .daddr = IPV4_VLAN_EGRESS_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = VLAN_IFACE, .check_vlan = true, .expected_mtu = 1400, },
	{ .desc = "IPv4 VLAN egress, flag set but egress is not a VLAN",
	  .daddr = IPV4_NUD_FAILED_ADDR, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = "veth1", .check_vlan = true, },
	{ .desc = "IPv4 VLAN egress, QinQ not reducible (VLAN_FAILURE)",
	  .daddr = IPV4_QINQ_DST,
	  .expected_ret = BPF_FIB_LKUP_RET_VLAN_FAILURE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = "veth1", .check_vlan = true, },
	{ .desc = "IPv4 QinQ egress without the flag (escape hatch)",
	  .daddr = IPV4_QINQ_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = QINQ_INNER_IFACE, },
	{ .desc = "IPv6 VLAN egress, single VLAN",
	  .daddr = IPV6_VLAN_EGRESS_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = "veth1", .check_vlan = true,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN egress, neighbour on the VLAN device",
	  .daddr = IPV4_VLAN_EGRESS_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN,
	  .expected_dev = "veth1", .check_vlan = true,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, .dmac = DMAC_INIT, },
	{ .desc = "IPv4 VLAN egress in OUTPUT mode",
	  .daddr = IPV4_VLAN_EGRESS_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .iif = VLAN_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_OUTPUT | BPF_FIB_LOOKUP_VLAN |
			  BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = "veth1", .check_vlan = true,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN egress over a bond",
	  .daddr = IPV4_BOND_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = BOND_IFACE, .check_vlan = true,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = BOND_VLAN_ID, },
	{ .desc = "IPv4 VLAN egress via TBID table",
	  .daddr = IPV4_TBID_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .lookup_flags = BPF_FIB_LOOKUP_DIRECT | BPF_FIB_LOOKUP_TBID |
			  BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .tbid = 100,
	  .expected_dev = "veth2", .check_vlan = true,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = TBID_VLAN_ID, },
	{ .desc = "IPv4 VLAN egress, success writes mtu_result with the swap",
	  .daddr = IPV4_VLAN_MTU_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .tot_len = 500, .expected_mtu = 1000,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = "veth1", .check_vlan = true,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN egress, FRAG_NEEDED reports mtu, swap unwritten",
	  .daddr = IPV4_VLAN_MTU_DST, .expected_ret = BPF_FIB_LKUP_RET_FRAG_NEEDED,
	  .tot_len = 1400, .expected_mtu = 1000,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .expected_dev = "veth1", .check_vlan = true, },
	/* vlan tag as lookup input */
	{ .desc = "IPv4 VLAN input, no flag",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv4 VLAN input, tag selects subinterface route",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_VLAN_GW, .expected_dev = VLAN_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	{ .desc = "IPv6 VLAN input, tag selects subinterface route",
	  .daddr = IPV6_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV6_VLAN_GW, .expected_dev = VLAN_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN input and egress combined",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_VLAN_GW, .expected_dev = "veth1",
	  .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_VLAN |
			  BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN input, neighbour resolved on the route",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_VLAN_GW, .expected_dev = VLAN_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, .dmac = DMAC_INIT2, },
	{ .desc = "IPv4 VLAN input, source address from the subinterface",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_src = IPV4_VLAN_IFACE_ADDR,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SRC |
			  BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	/*
	 * VRF: the resolved subinterface is enslaved, so the l3mdev rule
	 * (full lookup) and l3mdev_fib_table_rcu() (DIRECT) must select
	 * the VRF table from the resolved ingress
	 */
	{ .desc = "IPv4 VLAN input, VRF subinterface, no flag",
	  .daddr = IPV4_VRF_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_GW1,
	  .lookup_flags = BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv4 VLAN input, tag selects VRF table",
	  .daddr = IPV4_VRF_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_VRF_GW, .expected_dev = VRF_VLAN_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VRF_VLAN_ID, },
	{ .desc = "IPv4 VLAN input, DIRECT uses VRF table from resolved ingress",
	  .daddr = IPV4_VRF_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_VRF_GW, .expected_dev = VRF_VLAN_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_DIRECT |
			  BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VRF_VLAN_ID, },
	/*
	 * failure arms also assert params is left untouched: ifindex still
	 * names the physical device and the input tag bytes survive
	 */
	{ .desc = "IPv4 VLAN input, invalid proto",
	  .daddr = IPV4_VLAN_DST, .expected_ret = -EINVAL,
	  .expected_dev = "veth1", .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = 0x1234, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN input, unmatched VID",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_NOT_FWDED,
	  .expected_dev = "veth1", .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_VID_UNUSED, },
	{ .desc = "IPv4 VLAN input, subinterface down",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_NOT_FWDED,
	  .expected_dev = "veth1", .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID_DOWN, },
	/*
	 * the resolver runs before the forwarding check, so on devices
	 * with forwarding off FWD_DISABLED (not NOT_FWDED) proves the tag
	 * resolved to that device and the lookup used it as ingress
	 */
	{ .desc = "IPv4 VLAN input, 802.1ad tag",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_FWD_DISABLED,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021AD, .vlan_id = QINQ_AD_VLAN_ID, },
	{ .desc = "IPv4 VLAN input, PCP and DEI bits ignored in TCI",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_SUCCESS,
	  .expected_dst = IPV4_VLAN_GW,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = 0xe000 | VLAN_ID, },
	{ .desc = "IPv4 VLAN input, inner QinQ device from VLAN ifindex",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_FWD_DISABLED,
	  .iif = QINQ_OUTER_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = QINQ_INNER_VLAN_ID, },
	/*
	 * bonding: the VLANs live on the master, as on receive, where the
	 * frame is steered to the master before VLAN processing; a port
	 * ifindex does not match (ports carry vid state but no VLAN devs)
	 */
	{ .desc = "IPv4 VLAN input, tag on bond master resolves",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_FWD_DISABLED,
	  .iif = BOND_IFACE,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = BOND_VLAN_ID, },
	{ .desc = "IPv4 VLAN input, tag on bond port does not match",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_NOT_FWDED,
	  .iif = BOND_PORT, .expected_dev = BOND_PORT, .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = BOND_VLAN_ID, },
	{ .desc = "IPv6 VLAN input, invalid proto",
	  .daddr = IPV6_VLAN_DST, .expected_ret = -EINVAL,
	  .expected_dev = "veth1", .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = 0x1234, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN input, VID 0 priority tag fails closed",
	  .daddr = IPV4_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_NOT_FWDED,
	  .expected_dev = "veth1", .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = 0, },
	{ .desc = "IPv6 VLAN input, unmatched VID",
	  .daddr = IPV6_VLAN_DST, .expected_ret = BPF_FIB_LKUP_RET_NOT_FWDED,
	  .expected_dev = "veth1", .check_vlan = true,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_SKIP_NEIGH,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_VID_UNUSED, },
	{ .desc = "unknown flag bit rejected",
	  .daddr = IPV4_VLAN_DST, .expected_ret = -EINVAL,
	  .lookup_flags = (1 << 14) | BPF_FIB_LOOKUP_SKIP_NEIGH, },
	{ .desc = "IPv4 VLAN input rejected with TBID",
	  .daddr = IPV4_VLAN_DST, .expected_ret = -EINVAL,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_TBID,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
	{ .desc = "IPv4 VLAN input rejected with OUTPUT",
	  .daddr = IPV4_VLAN_DST, .expected_ret = -EINVAL,
	  .lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT | BPF_FIB_LOOKUP_OUTPUT,
	  .vlan_proto = ETH_P_8021Q, .vlan_id = VLAN_ID, },
};

static int setup_netns(void)
{
	int err;

	/*
	 * a new netns copies the IPv4 conf from init_net, so on a host with
	 * forwarding enabled the arms that expect FWD_DISABLED would see the
	 * lookup succeed instead; pin it off here and enable it per device
	 */
	err = write_sysctl("/proc/sys/net/ipv4/conf/all/forwarding", "0");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.conf.all.forwarding)"))
		goto fail;

	err = write_sysctl("/proc/sys/net/ipv4/conf/default/forwarding", "0");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.conf.default.forwarding)"))
		goto fail;

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

	/*
	 * Setup for vlan tests: a subinterface for egress resolution and
	 * tag-as-input, a QinQ stack, and an iif rule so the input tests
	 * observe which device the lookup used as ingress.
	 */
	SYS(fail, "ip link add link veth1 name %s type vlan id %d",
	    VLAN_IFACE, VLAN_ID);
	SYS(fail, "ip link set dev %s up", VLAN_IFACE);
	/*
	 * lower than the veth1 parent (1500): the skb-path mtu check uses the
	 * FIB result (VLAN) device, so mtu_result is this value, which the
	 * no-flag arm below pins
	 */
	SYS(fail, "ip link set dev %s mtu 1400", VLAN_IFACE);
	SYS(fail, "ip addr add %s/24 dev %s", IPV4_VLAN_IFACE_ADDR, VLAN_IFACE);
	SYS(fail, "ip addr add %s/64 dev %s nodad", IPV6_VLAN_IFACE_ADDR, VLAN_IFACE);

	/*
	 * stays down: the input flag must treat its tag the way real
	 * ingress treats a frame arriving on a down VLAN device (drop)
	 */
	SYS(fail, "ip link add link veth1 name %s type vlan id %d",
	    VLAN_IFACE_DOWN, VLAN_ID_DOWN);

	err = write_sysctl("/proc/sys/net/ipv4/conf/" VLAN_IFACE "/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.conf." VLAN_IFACE ".forwarding)"))
		goto fail;

	err = write_sysctl("/proc/sys/net/ipv6/conf/" VLAN_IFACE "/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv6.conf." VLAN_IFACE ".forwarding)"))
		goto fail;

	SYS(fail, "ip link add link veth1 name %s type vlan proto 802.1ad id 200",
	    QINQ_OUTER_IFACE);
	SYS(fail, "ip link add link %s name %s type vlan id 300",
	    QINQ_OUTER_IFACE, QINQ_INNER_IFACE);
	SYS(fail, "ip link set dev %s up", QINQ_OUTER_IFACE);
	SYS(fail, "ip link set dev %s up", QINQ_INNER_IFACE);
	SYS(fail, "ip route add %s/32 dev %s", IPV4_QINQ_DST, QINQ_INNER_IFACE);

	SYS(fail, "ip route add %s/32 via %s", IPV4_VLAN_DST, IPV4_GW1);
	SYS(fail, "ip route add table %s %s/32 via %s",
	    VLAN_TABLE, IPV4_VLAN_DST, IPV4_VLAN_GW);
	SYS(fail, "ip rule add prio 3 iif %s lookup %s", VLAN_IFACE, VLAN_TABLE);
	SYS(fail, "ip -6 route add %s/128 via %s", IPV6_VLAN_DST, IPV6_GW1);
	SYS(fail, "ip -6 route add table %s %s/128 via %s",
	    VLAN_TABLE, IPV6_VLAN_DST, IPV6_VLAN_GW);
	SYS(fail, "ip -6 rule add prio 3 iif %s lookup %s", VLAN_IFACE, VLAN_TABLE);

	/* a bond with one port and a VLAN on the bond */
	SYS(fail, "ip link add %s type bond", BOND_IFACE);
	SYS(fail, "ip link add %s type veth peer name %s", BOND_PORT, BOND_PORT_PEER);
	SYS(fail, "ip link set %s master %s", BOND_PORT, BOND_IFACE);
	SYS(fail, "ip link set dev %s up", BOND_IFACE);
	SYS(fail, "ip link set dev %s up", BOND_PORT);
	SYS(fail, "ip link add link %s name %s.%d type vlan id %d",
	    BOND_IFACE, BOND_IFACE, BOND_VLAN_ID, BOND_VLAN_ID);
	SYS(fail, "ip link set dev %s.%d up", BOND_IFACE, BOND_VLAN_ID);
	SYS(fail, "ip route add %s/32 dev %s.%d",
	    IPV4_BOND_VLAN_DST, BOND_IFACE, BOND_VLAN_ID);

	/*
	 * a VRF with its own dedicated subinterface (the iif rules above
	 * must not see it), for the table-selection-by-ingress cases
	 */
	SYS(fail, "ip link add %s type vrf table %s", VRF_IFACE, VRF_TABLE);
	SYS(fail, "ip link set dev %s up", VRF_IFACE);
	SYS(fail, "ip link add link veth1 name %s type vlan id %d",
	    VRF_VLAN_IFACE, VRF_VLAN_ID);
	SYS(fail, "ip link set %s master %s", VRF_VLAN_IFACE, VRF_IFACE);
	SYS(fail, "ip link set dev %s up", VRF_VLAN_IFACE);
	SYS(fail, "ip addr add %s/24 dev %s", IPV4_VRF_IFACE_ADDR, VRF_VLAN_IFACE);
	err = write_sysctl("/proc/sys/net/ipv4/conf/" VRF_VLAN_IFACE "/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.conf." VRF_VLAN_IFACE ".forwarding)"))
		goto fail;
	SYS(fail, "ip route add %s/32 via %s", IPV4_VRF_DST, IPV4_GW1);
	SYS(fail, "ip route add table %s %s/32 via %s",
	    VRF_TABLE, IPV4_VRF_DST, IPV4_VRF_GW);

	/* neighbours on the VLAN subinterface for the non-SKIP_NEIGH cases */
	err = write_sysctl("/proc/sys/net/ipv4/neigh/" VLAN_IFACE "/gc_stale_time", "900");
	if (!ASSERT_OK(err, "write_sysctl(net.ipv4.neigh." VLAN_IFACE ".gc_stale_time)"))
		goto fail;
	SYS(fail, "ip neigh add %s dev %s lladdr %s nud stale",
	    IPV4_VLAN_EGRESS_DST, VLAN_IFACE, DMAC);
	SYS(fail, "ip neigh add %s dev %s lladdr %s nud stale",
	    IPV4_VLAN_GW, VLAN_IFACE, DMAC2);

	/* a VLAN on veth2 with a route in the tbid test table */
	SYS(fail, "ip link add link veth2 name %s type vlan id %d",
	    TBID_VLAN_IFACE, TBID_VLAN_ID);
	SYS(fail, "ip link set dev %s up", TBID_VLAN_IFACE);
	SYS(fail, "ip route add table 100 %s/32 dev %s",
	    IPV4_TBID_VLAN_DST, TBID_VLAN_IFACE);

	/* a locked-mtu route via the subinterface for the FRAG_NEEDED case */
	SYS(fail, "ip route add %s/32 dev %s mtu lock 1000",
	    IPV4_VLAN_MTU_DST, VLAN_IFACE);

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
	params->ifindex = test->iif ? if_nametoindex(test->iif) : ifindex;
	params->tbid = test->tbid;
	params->mark = test->mark;
	params->tot_len = test->tot_len;

	/* h_vlan_proto/h_vlan_TCI union with tbid */
	if (test->lookup_flags & BPF_FIB_LOOKUP_VLAN_INPUT) {
		params->h_vlan_proto = htons(test->vlan_proto);
		params->h_vlan_TCI = htons(test->vlan_id);
	}

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
	int prog_fd, xdp_fd, err, ret, i;

	/* The test does not use the skb->data, so
	 * use pkt_v6 for both v6 and v4 test.
	 */
	LIBBPF_OPTS(bpf_test_run_opts, run_opts,
		    .data_in = &pkt_v6,
		    .data_size_in = sizeof(pkt_v6),
		    .ctx_in = &skb,
		    .ctx_size_in = sizeof(skb),
	);
	LIBBPF_OPTS(bpf_test_run_opts, xdp_opts,
		    .data_in = &pkt_v6,
		    .data_size_in = sizeof(pkt_v6),
	);

	skel = fib_lookup__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;
	prog_fd = bpf_program__fd(skel->progs.fib_lookup);
	xdp_fd = bpf_program__fd(skel->progs.fib_lookup_xdp);

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

		/*
		 * BPF_FIB_LOOKUP_VLAN is XDP-only; the tc helper rejects it.
		 * These cases are exercised on the XDP path below.
		 */
		if (tests[i].lookup_flags & BPF_FIB_LOOKUP_VLAN) {
			ASSERT_EQ(skel->bss->fib_lookup_ret, -EINVAL,
				  "tc rejects BPF_FIB_LOOKUP_VLAN");
			continue;
		}

		ASSERT_EQ(skel->bss->fib_lookup_ret, tests[i].expected_ret,
			  "fib_lookup_ret");

		if (tests[i].expected_src)
			assert_src_ip(fib_params, tests[i].expected_src);

		if (tests[i].expected_dst)
			assert_dst_ip(fib_params, tests[i].expected_dst);

		if (tests[i].expected_dev)
			ASSERT_EQ(fib_params->ifindex,
				  if_nametoindex(tests[i].expected_dev), "ifindex");

		if (tests[i].expected_mtu)
			ASSERT_EQ(fib_params->mtu_result, tests[i].expected_mtu,
				  "mtu_result");

		if (tests[i].check_vlan) {
			ASSERT_EQ(fib_params->h_vlan_proto,
				  htons(tests[i].vlan_proto), "h_vlan_proto");
			ASSERT_EQ(fib_params->h_vlan_TCI,
				  htons(tests[i].vlan_id), "h_vlan_TCI");
		}

		ret = memcmp(tests[i].dmac, fib_params->dmac, sizeof(tests[i].dmac));
		if (!ASSERT_EQ(ret, 0, "dmac not match")) {
			char expected[18], actual[18];

			mac_str(expected, tests[i].dmac);
			mac_str(actual, fib_params->dmac);
			printf("dmac expected %s actual %s ", expected, actual);
		}

		/*
		 * ensure tbid is zero'd out after fib lookup. With
		 * BPF_FIB_LOOKUP_VLAN the union holds the packed vlan
		 * fields instead, so skip the check for those.
		 */
		if ((tests[i].lookup_flags & BPF_FIB_LOOKUP_DIRECT) &&
		    !(tests[i].lookup_flags & BPF_FIB_LOOKUP_VLAN)) {
			if (!ASSERT_EQ(skel->bss->fib_params.tbid, 0,
					"expected fib_params.tbid to be zero"))
				goto fail;
		}
	}

	/*
	 * Re-run the cases through bpf_xdp_fib_lookup(). test_run uses the
	 * current netns' loopback for ctx->rxq->dev, so dev_net() is NS_TEST
	 * and the lookup runs against its FIB. The path-independent results
	 * (return code, swapped ifindex, vlan tag, gateway) must match the skb
	 * path; the no-tot_len mtu_result is skb-specific and not rechecked.
	 */
	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (set_lookup_params(fib_params, &tests[i], skb.ifindex))
			continue;

		skel->bss->fib_lookup_ret = -1;
		skel->bss->lookup_flags = tests[i].lookup_flags;

		err = bpf_prog_test_run_opts(xdp_fd, &xdp_opts);
		if (!ASSERT_OK(err, "xdp test_run"))
			continue;

		if (!ASSERT_EQ(skel->bss->fib_lookup_ret, tests[i].expected_ret,
			       "xdp fib_lookup_ret"))
			printf("(xdp) %s\n", tests[i].desc);

		if (tests[i].expected_dev)
			ASSERT_EQ(fib_params->ifindex,
				  if_nametoindex(tests[i].expected_dev),
				  "xdp ifindex");

		if (tests[i].expected_dst)
			assert_dst_ip(fib_params, tests[i].expected_dst);

		if (tests[i].check_vlan) {
			ASSERT_EQ(fib_params->h_vlan_proto,
				  htons(tests[i].vlan_proto), "xdp h_vlan_proto");
			ASSERT_EQ(fib_params->h_vlan_TCI,
				  htons(tests[i].vlan_id), "xdp h_vlan_TCI");
		}

		ret = memcmp(tests[i].dmac, fib_params->dmac, sizeof(tests[i].dmac));
		ASSERT_EQ(ret, 0, "xdp dmac");

		/*
		 * mtu_result from a tot_len lookup is the route mtu and is
		 * path-independent; the no-tot_len arm reads dev->mtu and is
		 * skb-only, so gate on tot_len
		 */
		if (tests[i].expected_mtu && tests[i].tot_len)
			ASSERT_EQ(fib_params->mtu_result, tests[i].expected_mtu,
				  "xdp mtu_result");
	}

fail:
	if (nstoken)
		close_netns(nstoken);
	SYS_NOFAIL("ip netns del " NS_TEST);
	fib_lookup__destroy(skel);
}

#define NS_VLAN_A	"fib_lookup_vlan_ns_a"
#define NS_VLAN_B	"fib_lookup_vlan_ns_b"
#define IPV4_VLAN_NETNS_ADDR	"10.66.0.1"
#define IPV4_VLAN_NETNS_DST	"10.66.0.2"

/*
 * A VLAN device can be moved to another netns while staying registered
 * on its parent. Neither direction may then cross the boundary: the
 * egress flag must not publish the foreign parent's ifindex, and the
 * input flag must fail closed rather than use a foreign ingress.
 */
void test_fib_lookup_vlan_netns(void)
{
	struct bpf_fib_lookup *fib_params;
	struct nstoken *nstoken = NULL;
	struct __sk_buff skb = { };
	struct fib_lookup *skel = NULL;
	int prog_fd, xdp_fd, err, parent_idx, vlan_idx;

	LIBBPF_OPTS(bpf_test_run_opts, run_opts,
		    .data_in = &pkt_v6,
		    .data_size_in = sizeof(pkt_v6),
		    .ctx_in = &skb,
		    .ctx_size_in = sizeof(skb),
	);
	LIBBPF_OPTS(bpf_test_run_opts, xdp_opts,
		    .data_in = &pkt_v6,
		    .data_size_in = sizeof(pkt_v6),
	);

	skel = fib_lookup__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;
	prog_fd = bpf_program__fd(skel->progs.fib_lookup);
	xdp_fd = bpf_program__fd(skel->progs.fib_lookup_xdp);
	fib_params = &skel->bss->fib_params;

	SYS(fail, "ip netns add %s", NS_VLAN_A);
	SYS(fail, "ip netns add %s", NS_VLAN_B);

	nstoken = open_netns(NS_VLAN_A);
	if (!ASSERT_OK_PTR(nstoken, "open_netns(a)"))
		goto fail;

	SYS(fail, "ip link add veth7 type veth peer name veth8");
	SYS(fail, "ip link set dev veth7 up");
	SYS(fail, "ip link add link veth7 name veth7.66 type vlan id 66");
	SYS(fail, "ip link set veth7.66 netns %s", NS_VLAN_B);
	/*
	 * up it in B before the input lookup: the move closed it, and a
	 * down device fails the resolver on IFF_UP before reaching the
	 * netns check this subtest exists to pin
	 */
	SYS(fail, "ip -n %s link set dev veth7.66 up", NS_VLAN_B);

	parent_idx = if_nametoindex("veth7");
	if (!ASSERT_NEQ(parent_idx, 0, "if_nametoindex(veth7)"))
		goto fail;

	/*
	 * give this netns a route to the destination: the lookup below runs
	 * against this FIB, so without the route a kernel that resolved the
	 * moved device anyway would still return NOT_FWDED and the arm would
	 * pass for the wrong reason
	 */
	SYS(fail, "ip route add %s/32 dev veth7", IPV4_VLAN_NETNS_DST);

	/*
	 * input: the moved device is still in veth7's VLAN group, but it
	 * lives in another netns, so the lookup must fail closed
	 */
	skb.ifindex = parent_idx;
	memset(fib_params, 0, sizeof(*fib_params));
	fib_params->family = AF_INET;
	fib_params->l4_protocol = IPPROTO_TCP;
	fib_params->ifindex = parent_idx;
	fib_params->h_vlan_proto = htons(ETH_P_8021Q);
	fib_params->h_vlan_TCI = htons(66);
	if (!ASSERT_EQ(inet_pton(AF_INET, IPV4_VLAN_NETNS_DST, &fib_params->ipv4_dst),
		       1, "inet_pton(dst)"))
		goto fail;

	skel->bss->fib_lookup_ret = -1;
	skel->bss->lookup_flags = BPF_FIB_LOOKUP_VLAN_INPUT |
				  BPF_FIB_LOOKUP_SKIP_NEIGH;
	err = bpf_prog_test_run_opts(prog_fd, &run_opts);
	if (!ASSERT_OK(err, "test_run(input)"))
		goto fail;
	ASSERT_EQ(skel->bss->fib_lookup_ret, BPF_FIB_LKUP_RET_NOT_FWDED,
		  "input across netns fails closed");
	ASSERT_EQ(fib_params->ifindex, parent_idx, "ifindex untouched");
	ASSERT_EQ(fib_params->h_vlan_TCI, htons(66), "tag untouched");

	close_netns(nstoken);
	nstoken = open_netns(NS_VLAN_B);
	if (!ASSERT_OK_PTR(nstoken, "open_netns(b)"))
		goto fail;

	/*
	 * egress: the fib result is the VLAN device here, but its parent
	 * is in the other netns, so the swap must not happen
	 */
	SYS(fail, "ip addr add %s/24 dev veth7.66", IPV4_VLAN_NETNS_ADDR);
	err = write_sysctl("/proc/sys/net/ipv4/conf/veth7.66/forwarding", "1");
	if (!ASSERT_OK(err, "write_sysctl(forwarding)"))
		goto fail;

	vlan_idx = if_nametoindex("veth7.66");
	if (!ASSERT_NEQ(vlan_idx, 0, "if_nametoindex(veth7.66)"))
		goto fail;

	memset(fib_params, 0, sizeof(*fib_params));
	fib_params->family = AF_INET;
	fib_params->l4_protocol = IPPROTO_TCP;
	fib_params->ifindex = vlan_idx;
	if (!ASSERT_EQ(inet_pton(AF_INET, IPV4_VLAN_NETNS_DST, &fib_params->ipv4_dst),
		       1, "inet_pton(dst)") ||
	    !ASSERT_EQ(inet_pton(AF_INET, IPV4_VLAN_NETNS_ADDR, &fib_params->ipv4_src),
		       1, "inet_pton(src)"))
		goto fail;

	skel->bss->fib_lookup_ret = -1;
	skel->bss->lookup_flags = BPF_FIB_LOOKUP_VLAN |
				  BPF_FIB_LOOKUP_SKIP_NEIGH;
	err = bpf_prog_test_run_opts(xdp_fd, &xdp_opts);
	if (!ASSERT_OK(err, "test_run(egress)"))
		goto fail;
	ASSERT_EQ(skel->bss->fib_lookup_ret, BPF_FIB_LKUP_RET_VLAN_FAILURE,
		  "egress returns VLAN_FAILURE");
	ASSERT_EQ(fib_params->ifindex, vlan_idx,
		  "foreign parent not published");
	ASSERT_EQ(fib_params->h_vlan_TCI, 0, "vlan fields zero");

fail:
	if (nstoken)
		close_netns(nstoken);
	SYS_NOFAIL("ip netns del " NS_VLAN_A);
	SYS_NOFAIL("ip netns del " NS_VLAN_B);
	fib_lookup__destroy(skel);
}

#define REDIRECT_NPKTS 1000
#define NS_REDIRECT "fib_lookup_redirect_ns"

/*
 * The egress flag exists so an XDP program can redirect to the physical
 * parent. A redirect that lands on a VLAN device is dropped at
 * xdp_do_flush(), because a VLAN device has no ndo_xdp_xmit. Drive real
 * frames with BPF_F_TEST_XDP_LIVE_FRAMES, which runs the native
 * xdp_do_redirect() + xdp_do_flush() path: a reducible VLAN egress
 * resolves to veth1 and is delivered to its peer veth2, while a QinQ
 * egress returns VLAN_FAILURE and is passed to the stack instead of
 * redirected to a device that would silently drop it.
 */
void test_fib_lookup_vlan_redirect(void)
{
	int redirect_fd, err, veth1_idx, veth2_idx = -1;
	struct bpf_fib_lookup *fib_params;
	struct nstoken *nstoken = NULL;
	struct fib_lookup *skel = NULL;
	bool xdp_attached = false;

	LIBBPF_OPTS(bpf_test_run_opts, lf_opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .flags = BPF_F_TEST_XDP_LIVE_FRAMES,
		    .repeat = REDIRECT_NPKTS,
	);

	skel = fib_lookup__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;
	redirect_fd = bpf_program__fd(skel->progs.fib_lookup_redirect);
	fib_params = &skel->bss->fib_params;

	SYS(fail, "ip netns add %s", NS_REDIRECT);
	nstoken = open_netns(NS_REDIRECT);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto fail;
	if (setup_netns())
		goto fail;

	veth1_idx = if_nametoindex("veth1");
	veth2_idx = if_nametoindex("veth2");
	if (!ASSERT_NEQ(veth1_idx, 0, "if_nametoindex(veth1)") ||
	    !ASSERT_NEQ(veth2_idx, 0, "if_nametoindex(veth2)"))
		goto fail;

	/*
	 * A redirect to veth1 is delivered to its peer veth2. veth_xdp_xmit()
	 * only accepts the frame if veth2's NAPI is up, which on veth means
	 * veth2 carries an XDP program; xdp_count tallies what arrives.
	 */
	err = bpf_xdp_attach(veth2_idx, bpf_program__fd(skel->progs.xdp_count),
			     XDP_FLAGS_DRV_MODE, NULL);
	if (!ASSERT_OK(err, "attach xdp_count on veth2"))
		goto fail;
	xdp_attached = true;

	/* reducible VLAN egress: resolves to the physical parent veth1 */
	memset(fib_params, 0, sizeof(*fib_params));
	fib_params->family = AF_INET;
	fib_params->l4_protocol = IPPROTO_TCP;
	fib_params->ifindex = veth1_idx;
	if (!ASSERT_EQ(inet_pton(AF_INET, IPV4_IFACE_ADDR, &fib_params->ipv4_src),
		       1, "inet_pton(src)") ||
	    !ASSERT_EQ(inet_pton(AF_INET, IPV4_VLAN_EGRESS_DST, &fib_params->ipv4_dst),
		       1, "inet_pton(reducible dst)"))
		goto fail;
	skel->bss->lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH;
	skel->bss->redirected = 0;
	skel->bss->passed = 0;
	skel->bss->delivered = 0;

	err = bpf_prog_test_run_opts(redirect_fd, &lf_opts);
	if (!ASSERT_OK(err, "test_run(reducible egress)"))
		goto fail;
	ASSERT_EQ(skel->bss->redirected, REDIRECT_NPKTS, "reducible egress redirected");
	ASSERT_EQ(skel->bss->passed, 0, "reducible egress not passed");
	ASSERT_GT(skel->bss->delivered, 0, "reducible egress delivered to veth2");

	/*
	 * QinQ egress: not reducible, so the lookup returns VLAN_FAILURE and
	 * the program passes the frame instead of redirecting to the inner
	 * VLAN device. redirected == 0 is the assertion that matters: the
	 * program did not redirect to a device that would drop the frame at
	 * xdp_do_flush(). veth2's delivered count is not checked here, since
	 * a passed frame can still reach veth2 through the stack's forwarding
	 * path, which is unrelated to the redirect under test.
	 */
	memset(fib_params, 0, sizeof(*fib_params));
	fib_params->family = AF_INET;
	fib_params->l4_protocol = IPPROTO_TCP;
	fib_params->ifindex = veth1_idx;
	if (!ASSERT_EQ(inet_pton(AF_INET, IPV4_IFACE_ADDR, &fib_params->ipv4_src),
		       1, "inet_pton(src)") ||
	    !ASSERT_EQ(inet_pton(AF_INET, IPV4_QINQ_DST, &fib_params->ipv4_dst),
		       1, "inet_pton(qinq dst)"))
		goto fail;
	skel->bss->lookup_flags = BPF_FIB_LOOKUP_VLAN | BPF_FIB_LOOKUP_SKIP_NEIGH;
	skel->bss->redirected = 0;
	skel->bss->passed = 0;

	err = bpf_prog_test_run_opts(redirect_fd, &lf_opts);
	if (!ASSERT_OK(err, "test_run(qinq egress)"))
		goto fail;
	ASSERT_EQ(skel->bss->passed, REDIRECT_NPKTS, "qinq egress passed");
	ASSERT_EQ(skel->bss->redirected, 0, "qinq egress not redirected");

fail:
	if (xdp_attached)
		bpf_xdp_detach(veth2_idx, XDP_FLAGS_DRV_MODE, NULL);
	if (nstoken)
		close_netns(nstoken);
	SYS_NOFAIL("ip netns del " NS_REDIRECT);
	fib_lookup__destroy(skel);
}
