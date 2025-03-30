// SPDX-License-Identifier: GPL-2.0

/* Create 3 namespaces with 3 veth peers, and forward packets in-between using
 * native XDP
 *
 * Network topology:
 *  ----------        ----------       ----------
 *  |  NS1   |        |  NS2   |       |  NS3   |
 *  | veth11 |        | veth22 |       | veth33 |
 *  ----|-----        -----|----       -----|----
 *      |                  |                |
 *  ----|------------------|----------------|----
 *  | veth1              veth2            veth3 |
 *  |                                           |
 *  |                     NSO                   |
 *  ---------------------------------------------
 *
 * Test cases:
 *  - [test_xdp_veth_redirect] : ping veth33 from veth11
 *
 *    veth11             veth22              veth33
 *  (XDP_PASS)          (XDP_TX)           (XDP_PASS)
 *       |                  |                  |
 *       |                  |                  |
 *     veth1             veth2              veth3
 * (XDP_REDIRECT)     (XDP_REDIRECT)     (XDP_REDIRECT)
 *      ^ |                ^ |                ^ |
 *      | |                | |                | |
 *      | ------------------ ------------------ |
 *      -----------------------------------------
 *
 * - [test_xdp_veth_broadcast_redirect]: broadcast from veth11
 *     - IPv4 ping : BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS
 *          -> echo request received by all except veth11
 *     - IPv4 ping : BPF_F_BROADCAST
 *          -> echo request received by all veth
 * - [test_xdp_veth_egress]:
 *     - all src mac should be the magic mac
 *
 *    veth11             veth22              veth33
 *  (XDP_PASS)         (XDP_PASS)          (XDP_PASS)
 *       |                  |                  |
 *       |                  |                  |
 *     veth1		  veth2              veth3
 * (XDP_REDIRECT)     (XDP_REDIRECT)     (XDP_REDIRECT)
 *      |                   ^                  ^
 *      |                   |                  |
 *      ----------------------------------------
 *
 */

#define _GNU_SOURCE
#include <net/if.h>
#include "test_progs.h"
#include "network_helpers.h"
#include "xdp_dummy.skel.h"
#include "xdp_redirect_map.skel.h"
#include "xdp_redirect_multi_kern.skel.h"
#include "xdp_tx.skel.h"
#include <uapi/linux/if_link.h>

#define VETH_PAIRS_COUNT	3
#define VETH_NAME_MAX_LEN	32
#define IP_MAX_LEN		16
#define IP_SRC				"10.1.1.11"
#define IP_DST				"10.1.1.33"
#define IP_NEIGH			"10.1.1.253"
#define PROG_NAME_MAX_LEN	128
#define NS_NAME_MAX_LEN		32

struct veth_configuration {
	char local_veth[VETH_NAME_MAX_LEN]; /* Interface in main namespace */
	char remote_veth[VETH_NAME_MAX_LEN]; /* Peer interface in dedicated namespace*/
	char namespace[NS_NAME_MAX_LEN]; /* Namespace for the remote veth */
	int next_veth; /* Local interface to redirect traffic to */
	char remote_addr[IP_MAX_LEN]; /* IP address of the remote veth */
};

struct net_configuration {
	char ns0_name[NS_NAME_MAX_LEN];
	struct veth_configuration veth_cfg[VETH_PAIRS_COUNT];
};

static const struct net_configuration default_config = {
	.ns0_name = "ns0-",
	{
		{
			.local_veth = "veth1-",
			.remote_veth = "veth11",
			.next_veth = 1,
			.remote_addr = IP_SRC,
			.namespace = "ns-veth11-"
		},
		{
			.local_veth = "veth2-",
			.remote_veth = "veth22",
			.next_veth = 2,
			.remote_addr = "",
			.namespace = "ns-veth22-"
		},
		{
			.local_veth = "veth3-",
			.remote_veth = "veth33",
			.next_veth = 0,
			.remote_addr = IP_DST,
			.namespace = "ns-veth33-"
		}
	}
};

struct prog_configuration {
	char local_name[PROG_NAME_MAX_LEN]; /* BPF prog to attach to local_veth */
	char remote_name[PROG_NAME_MAX_LEN]; /* BPF prog to attach to remote_veth */
	u32 local_flags; /* XDP flags to use on local_veth */
	u32 remote_flags; /* XDP flags to use on remote_veth */
};

static int attach_programs_to_veth_pair(struct bpf_object **objs, size_t nb_obj,
					struct net_configuration *net_config,
					struct prog_configuration *prog, int index)
{
	struct bpf_program *local_prog, *remote_prog;
	struct nstoken *nstoken;
	int interface, ret, i;

	for (i = 0; i < nb_obj; i++) {
		local_prog = bpf_object__find_program_by_name(objs[i], prog[index].local_name);
		if (local_prog)
			break;
	}
	if (!ASSERT_OK_PTR(local_prog, "find local program"))
		return -1;

	for (i = 0; i < nb_obj; i++) {
		remote_prog = bpf_object__find_program_by_name(objs[i], prog[index].remote_name);
		if (remote_prog)
			break;
	}
	if (!ASSERT_OK_PTR(remote_prog, "find remote program"))
		return -1;

	interface = if_nametoindex(net_config->veth_cfg[index].local_veth);
	if (!ASSERT_NEQ(interface, 0, "non zero interface index"))
		return -1;

	ret = bpf_xdp_attach(interface, bpf_program__fd(local_prog),
			     prog[index].local_flags, NULL);
	if (!ASSERT_OK(ret, "attach xdp program to local veth"))
		return -1;

	nstoken = open_netns(net_config->veth_cfg[index].namespace);
	if (!ASSERT_OK_PTR(nstoken, "switch to remote veth namespace"))
		return -1;

	interface = if_nametoindex(net_config->veth_cfg[index].remote_veth);
	if (!ASSERT_NEQ(interface, 0, "non zero interface index")) {
		close_netns(nstoken);
		return -1;
	}

	ret = bpf_xdp_attach(interface, bpf_program__fd(remote_prog),
			     prog[index].remote_flags, NULL);
	if (!ASSERT_OK(ret, "attach xdp program to remote veth")) {
		close_netns(nstoken);
		return -1;
	}

	close_netns(nstoken);
	return 0;
}

static int create_network(struct net_configuration *net_config)
{
	struct nstoken *nstoken = NULL;
	int i, err;

	memcpy(net_config, &default_config, sizeof(struct net_configuration));

	/* Create unique namespaces */
	err = append_tid(net_config->ns0_name, NS_NAME_MAX_LEN);
	if (!ASSERT_OK(err, "append TID to ns0 name"))
		goto fail;
	SYS(fail, "ip netns add %s", net_config->ns0_name);

	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		err = append_tid(net_config->veth_cfg[i].namespace, NS_NAME_MAX_LEN);
		if (!ASSERT_OK(err, "append TID to ns name"))
			goto fail;
		SYS(fail, "ip netns add %s", net_config->veth_cfg[i].namespace);
	}

	/* Create interfaces */
	nstoken = open_netns(net_config->ns0_name);
	if (!nstoken)
		goto fail;

	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		SYS(fail, "ip link add %s type veth peer name %s netns %s",
		    net_config->veth_cfg[i].local_veth, net_config->veth_cfg[i].remote_veth,
		    net_config->veth_cfg[i].namespace);
		SYS(fail, "ip link set dev %s up", net_config->veth_cfg[i].local_veth);
		if (net_config->veth_cfg[i].remote_addr[0])
			SYS(fail, "ip -n %s addr add %s/24 dev %s",
			    net_config->veth_cfg[i].namespace,
			    net_config->veth_cfg[i].remote_addr,
			    net_config->veth_cfg[i].remote_veth);
		SYS(fail, "ip -n %s link set dev %s up", net_config->veth_cfg[i].namespace,
		    net_config->veth_cfg[i].remote_veth);
	}

	close_netns(nstoken);
	return 0;

fail:
	close_netns(nstoken);
	return -1;
}

static void cleanup_network(struct net_configuration *net_config)
{
	int i;

	SYS_NOFAIL("ip netns del %s", net_config->ns0_name);
	for (i = 0; i < VETH_PAIRS_COUNT; i++)
		SYS_NOFAIL("ip netns del %s", net_config->veth_cfg[i].namespace);
}

#define VETH_REDIRECT_SKEL_NB	3
static void xdp_veth_redirect(u32 flags)
{
	struct prog_configuration ping_config[VETH_PAIRS_COUNT] = {
		{
			.local_name = "xdp_redirect_map_0",
			.remote_name = "xdp_dummy_prog",
			.local_flags = flags,
			.remote_flags = flags,
		},
		{
			.local_name = "xdp_redirect_map_1",
			.remote_name = "xdp_tx",
			.local_flags = flags,
			.remote_flags = flags,
		},
		{
			.local_name = "xdp_redirect_map_2",
			.remote_name = "xdp_dummy_prog",
			.local_flags = flags,
			.remote_flags = flags,
		}
	};
	struct bpf_object *bpf_objs[VETH_REDIRECT_SKEL_NB];
	struct xdp_redirect_map *xdp_redirect_map;
	struct net_configuration net_config;
	struct nstoken *nstoken = NULL;
	struct xdp_dummy *xdp_dummy;
	struct xdp_tx *xdp_tx;
	int map_fd;
	int i;

	xdp_dummy = xdp_dummy__open_and_load();
	if (!ASSERT_OK_PTR(xdp_dummy, "xdp_dummy__open_and_load"))
		return;

	xdp_tx = xdp_tx__open_and_load();
	if (!ASSERT_OK_PTR(xdp_tx, "xdp_tx__open_and_load"))
		goto destroy_xdp_dummy;

	xdp_redirect_map = xdp_redirect_map__open_and_load();
	if (!ASSERT_OK_PTR(xdp_redirect_map, "xdp_redirect_map__open_and_load"))
		goto destroy_xdp_tx;

	if (!ASSERT_OK(create_network(&net_config), "create network"))
		goto destroy_xdp_redirect_map;

	/* Then configure the redirect map and attach programs to interfaces */
	map_fd = bpf_map__fd(xdp_redirect_map->maps.tx_port);
	if (!ASSERT_OK_FD(map_fd, "open redirect map"))
		goto destroy_xdp_redirect_map;

	bpf_objs[0] = xdp_dummy->obj;
	bpf_objs[1] = xdp_tx->obj;
	bpf_objs[2] = xdp_redirect_map->obj;

	nstoken = open_netns(net_config.ns0_name);
	if (!ASSERT_OK_PTR(nstoken, "open NS0"))
		goto destroy_xdp_redirect_map;

	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		int next_veth = net_config.veth_cfg[i].next_veth;
		int interface_id;
		int err;

		interface_id = if_nametoindex(net_config.veth_cfg[next_veth].local_veth);
		if (!ASSERT_NEQ(interface_id, 0, "non zero interface index"))
			goto destroy_xdp_redirect_map;
		err = bpf_map_update_elem(map_fd, &i, &interface_id, BPF_ANY);
		if (!ASSERT_OK(err, "configure interface redirection through map"))
			goto destroy_xdp_redirect_map;
		if (attach_programs_to_veth_pair(bpf_objs, VETH_REDIRECT_SKEL_NB,
						 &net_config, ping_config, i))
			goto destroy_xdp_redirect_map;
	}

	/* Test: if all interfaces are properly configured, we must be able to ping
	 * veth33 from veth11
	 */
	ASSERT_OK(SYS_NOFAIL("ip netns exec %s ping -c 1 -W 1 %s > /dev/null",
			     net_config.veth_cfg[0].namespace, IP_DST), "ping");

destroy_xdp_redirect_map:
	close_netns(nstoken);
	xdp_redirect_map__destroy(xdp_redirect_map);
destroy_xdp_tx:
	xdp_tx__destroy(xdp_tx);
destroy_xdp_dummy:
	xdp_dummy__destroy(xdp_dummy);

	cleanup_network(&net_config);
}

#define BROADCAST_REDIRECT_SKEL_NB	2
static void xdp_veth_broadcast_redirect(u32 attach_flags, u64 redirect_flags)
{
	struct prog_configuration prog_cfg[VETH_PAIRS_COUNT] = {
		{
			.local_name = "xdp_redirect_map_multi_prog",
			.remote_name = "xdp_count_0",
			.local_flags = attach_flags,
			.remote_flags = attach_flags,
		},
		{
			.local_name = "xdp_redirect_map_multi_prog",
			.remote_name = "xdp_count_1",
			.local_flags = attach_flags,
			.remote_flags = attach_flags,
		},
		{
			.local_name = "xdp_redirect_map_multi_prog",
			.remote_name = "xdp_count_2",
			.local_flags = attach_flags,
			.remote_flags = attach_flags,
		}
	};
	struct bpf_object *bpf_objs[BROADCAST_REDIRECT_SKEL_NB];
	struct xdp_redirect_multi_kern *xdp_redirect_multi_kern;
	struct xdp_redirect_map *xdp_redirect_map;
	struct bpf_devmap_val devmap_val = {};
	struct net_configuration net_config;
	struct nstoken *nstoken = NULL;
	u16 protocol = ETH_P_IP;
	int group_map;
	int flags_map;
	int cnt_map;
	u64 cnt = 0;
	int i, err;

	xdp_redirect_multi_kern = xdp_redirect_multi_kern__open_and_load();
	if (!ASSERT_OK_PTR(xdp_redirect_multi_kern, "xdp_redirect_multi_kern__open_and_load"))
		return;

	xdp_redirect_map = xdp_redirect_map__open_and_load();
	if (!ASSERT_OK_PTR(xdp_redirect_map, "xdp_redirect_map__open_and_load"))
		goto destroy_xdp_redirect_multi_kern;

	if (!ASSERT_OK(create_network(&net_config), "create network"))
		goto destroy_xdp_redirect_map;

	group_map = bpf_map__fd(xdp_redirect_multi_kern->maps.map_all);
	if (!ASSERT_OK_FD(group_map, "open map_all"))
		goto destroy_xdp_redirect_map;

	flags_map = bpf_map__fd(xdp_redirect_multi_kern->maps.redirect_flags);
	if (!ASSERT_OK_FD(group_map, "open map_all"))
		goto destroy_xdp_redirect_map;

	err = bpf_map_update_elem(flags_map, &protocol, &redirect_flags, BPF_NOEXIST);
	if (!ASSERT_OK(err, "init IP count"))
		goto destroy_xdp_redirect_map;

	cnt_map = bpf_map__fd(xdp_redirect_map->maps.rxcnt);
	if (!ASSERT_OK_FD(cnt_map, "open rxcnt map"))
		goto destroy_xdp_redirect_map;

	bpf_objs[0] = xdp_redirect_multi_kern->obj;
	bpf_objs[1] = xdp_redirect_map->obj;

	nstoken = open_netns(net_config.ns0_name);
	if (!ASSERT_OK_PTR(nstoken, "open NS0"))
		goto destroy_xdp_redirect_map;

	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		int ifindex = if_nametoindex(net_config.veth_cfg[i].local_veth);

		if (attach_programs_to_veth_pair(bpf_objs, BROADCAST_REDIRECT_SKEL_NB,
						 &net_config, prog_cfg, i))
			goto destroy_xdp_redirect_map;

		SYS(destroy_xdp_redirect_map,
		    "ip -n %s neigh add %s lladdr 00:00:00:00:00:01 dev %s",
		    net_config.veth_cfg[i].namespace, IP_NEIGH, net_config.veth_cfg[i].remote_veth);

		devmap_val.ifindex = ifindex;
		err = bpf_map_update_elem(group_map, &ifindex, &devmap_val, 0);
		if (!ASSERT_OK(err, "bpf_map_update_elem"))
			goto destroy_xdp_redirect_map;

	}

	SYS_NOFAIL("ip netns exec %s ping %s -i 0.1 -c 4 -W1 > /dev/null ",
		    net_config.veth_cfg[0].namespace, IP_NEIGH);

	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		err =  bpf_map_lookup_elem(cnt_map, &i, &cnt);
		if (!ASSERT_OK(err, "get IP cnt"))
			goto destroy_xdp_redirect_map;

		if (redirect_flags & BPF_F_EXCLUDE_INGRESS)
			/* veth11 shouldn't receive the ICMP requests;
			 * others should
			 */
			ASSERT_EQ(cnt, i ? 4 : 0, "compare IP cnt");
		else
			/* All remote veth should receive the ICMP requests */
			ASSERT_EQ(cnt, 4, "compare IP cnt");
	}

destroy_xdp_redirect_map:
	close_netns(nstoken);
	xdp_redirect_map__destroy(xdp_redirect_map);
destroy_xdp_redirect_multi_kern:
	xdp_redirect_multi_kern__destroy(xdp_redirect_multi_kern);

	cleanup_network(&net_config);
}

#define VETH_EGRESS_SKEL_NB	3
static void xdp_veth_egress(u32 flags)
{
	struct prog_configuration prog_cfg[VETH_PAIRS_COUNT] = {
		{
			.local_name = "xdp_redirect_map_all_prog",
			.remote_name = "xdp_dummy_prog",
			.local_flags = flags,
			.remote_flags = flags,
		},
		{
			.local_name = "xdp_redirect_map_all_prog",
			.remote_name = "store_mac_1",
			.local_flags = flags,
			.remote_flags = flags,
		},
		{
			.local_name = "xdp_redirect_map_all_prog",
			.remote_name = "store_mac_2",
			.local_flags = flags,
			.remote_flags = flags,
		}
	};
	const char magic_mac[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
	struct xdp_redirect_multi_kern *xdp_redirect_multi_kern;
	struct bpf_object *bpf_objs[VETH_EGRESS_SKEL_NB];
	struct xdp_redirect_map *xdp_redirect_map;
	struct bpf_devmap_val devmap_val = {};
	struct net_configuration net_config;
	int mac_map, egress_map, res_map;
	struct nstoken *nstoken = NULL;
	struct xdp_dummy *xdp_dummy;
	int err;
	int i;

	xdp_dummy = xdp_dummy__open_and_load();
	if (!ASSERT_OK_PTR(xdp_dummy, "xdp_dummy__open_and_load"))
		return;

	xdp_redirect_multi_kern = xdp_redirect_multi_kern__open_and_load();
	if (!ASSERT_OK_PTR(xdp_redirect_multi_kern, "xdp_redirect_multi_kern__open_and_load"))
		goto destroy_xdp_dummy;

	xdp_redirect_map = xdp_redirect_map__open_and_load();
	if (!ASSERT_OK_PTR(xdp_redirect_map, "xdp_redirect_map__open_and_load"))
		goto destroy_xdp_redirect_multi_kern;

	if (!ASSERT_OK(create_network(&net_config), "create network"))
		goto destroy_xdp_redirect_map;

	mac_map = bpf_map__fd(xdp_redirect_multi_kern->maps.mac_map);
	if (!ASSERT_OK_FD(mac_map, "open mac_map"))
		goto destroy_xdp_redirect_map;

	egress_map = bpf_map__fd(xdp_redirect_multi_kern->maps.map_egress);
	if (!ASSERT_OK_FD(egress_map, "open map_egress"))
		goto destroy_xdp_redirect_map;

	devmap_val.bpf_prog.fd = bpf_program__fd(xdp_redirect_multi_kern->progs.xdp_devmap_prog);

	bpf_objs[0] = xdp_dummy->obj;
	bpf_objs[1] = xdp_redirect_multi_kern->obj;
	bpf_objs[2] = xdp_redirect_map->obj;

	nstoken = open_netns(net_config.ns0_name);
	if (!ASSERT_OK_PTR(nstoken, "open NS0"))
		goto destroy_xdp_redirect_map;

	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		int ifindex = if_nametoindex(net_config.veth_cfg[i].local_veth);

		SYS(destroy_xdp_redirect_map,
		    "ip -n %s neigh add %s lladdr 00:00:00:00:00:01 dev %s",
		    net_config.veth_cfg[i].namespace, IP_NEIGH, net_config.veth_cfg[i].remote_veth);

		if (attach_programs_to_veth_pair(bpf_objs, VETH_REDIRECT_SKEL_NB,
						 &net_config, prog_cfg, i))
			goto destroy_xdp_redirect_map;

		err = bpf_map_update_elem(mac_map, &ifindex, magic_mac, 0);
		if (!ASSERT_OK(err, "bpf_map_update_elem"))
			goto destroy_xdp_redirect_map;

		devmap_val.ifindex = ifindex;
		err = bpf_map_update_elem(egress_map, &ifindex, &devmap_val, 0);
		if (!ASSERT_OK(err, "bpf_map_update_elem"))
			goto destroy_xdp_redirect_map;
	}

	SYS_NOFAIL("ip netns exec %s ping %s -i 0.1 -c 4 -W1 > /dev/null ",
		    net_config.veth_cfg[0].namespace, IP_NEIGH);

	res_map = bpf_map__fd(xdp_redirect_map->maps.rx_mac);
	if (!ASSERT_OK_FD(res_map, "open rx_map"))
		goto destroy_xdp_redirect_map;

	for (i = 0; i < 2; i++) {
		u32 key = i;
		u64 res;

		err = bpf_map_lookup_elem(res_map, &key, &res);
		if (!ASSERT_OK(err, "get MAC res"))
			goto destroy_xdp_redirect_map;

		ASSERT_STRNEQ((const char *)&res, magic_mac, ETH_ALEN, "compare mac");
	}

destroy_xdp_redirect_map:
	close_netns(nstoken);
	xdp_redirect_map__destroy(xdp_redirect_map);
destroy_xdp_redirect_multi_kern:
	xdp_redirect_multi_kern__destroy(xdp_redirect_multi_kern);
destroy_xdp_dummy:
	xdp_dummy__destroy(xdp_dummy);

	cleanup_network(&net_config);
}

void test_xdp_veth_redirect(void)
{
	if (test__start_subtest("0"))
		xdp_veth_redirect(0);

	if (test__start_subtest("DRV_MODE"))
		xdp_veth_redirect(XDP_FLAGS_DRV_MODE);

	if (test__start_subtest("SKB_MODE"))
		xdp_veth_redirect(XDP_FLAGS_SKB_MODE);
}

void test_xdp_veth_broadcast_redirect(void)
{
	if (test__start_subtest("0/BROADCAST"))
		xdp_veth_broadcast_redirect(0, BPF_F_BROADCAST);

	if (test__start_subtest("0/(BROADCAST | EXCLUDE_INGRESS)"))
		xdp_veth_broadcast_redirect(0, BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);

	if (test__start_subtest("DRV_MODE/BROADCAST"))
		xdp_veth_broadcast_redirect(XDP_FLAGS_DRV_MODE, BPF_F_BROADCAST);

	if (test__start_subtest("DRV_MODE/(BROADCAST | EXCLUDE_INGRESS)"))
		xdp_veth_broadcast_redirect(XDP_FLAGS_DRV_MODE,
					    BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);

	if (test__start_subtest("SKB_MODE/BROADCAST"))
		xdp_veth_broadcast_redirect(XDP_FLAGS_SKB_MODE, BPF_F_BROADCAST);

	if (test__start_subtest("SKB_MODE/(BROADCAST | EXCLUDE_INGRESS)"))
		xdp_veth_broadcast_redirect(XDP_FLAGS_SKB_MODE,
					    BPF_F_BROADCAST | BPF_F_EXCLUDE_INGRESS);
}

void test_xdp_veth_egress(void)
{
	if (test__start_subtest("0/egress"))
		xdp_veth_egress(0);

	if (test__start_subtest("DRV_MODE/egress"))
		xdp_veth_egress(XDP_FLAGS_DRV_MODE);

	if (test__start_subtest("SKB_MODE/egress"))
		xdp_veth_egress(XDP_FLAGS_SKB_MODE);
}
