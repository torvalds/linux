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
 *    veth1              veth2            veth3
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
 */

#define _GNU_SOURCE
#include <net/if.h>
#include "test_progs.h"
#include "network_helpers.h"
#include "xdp_dummy.skel.h"
#include "xdp_redirect_map.skel.h"
#include "xdp_tx.skel.h"
#include <uapi/linux/if_link.h>

#define VETH_PAIRS_COUNT	3
#define VETH_NAME_MAX_LEN	32
#define IP_MAX_LEN		16
#define IP_SRC				"10.1.1.11"
#define IP_DST				"10.1.1.33"
#define PROG_NAME_MAX_LEN	128
#define NS_NAME_MAX_LEN		32

struct veth_configuration {
	char local_veth[VETH_NAME_MAX_LEN]; /* Interface in main namespace */
	char remote_veth[VETH_NAME_MAX_LEN]; /* Peer interface in dedicated namespace*/
	char namespace[NS_NAME_MAX_LEN]; /* Namespace for the remote veth */
	int next_veth; /* Local interface to redirect traffic to */
	char remote_addr[IP_MAX_LEN]; /* IP address of the remote veth */
};

static const struct veth_configuration default_config[VETH_PAIRS_COUNT] = {
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
};

struct prog_configuration {
	char local_name[PROG_NAME_MAX_LEN]; /* BPF prog to attach to local_veth */
	char remote_name[PROG_NAME_MAX_LEN]; /* BPF prog to attach to remote_veth */
	u32 local_flags; /* XDP flags to use on local_veth */
	u32 remote_flags; /* XDP flags to use on remote_veth */
};

static int attach_programs_to_veth_pair(struct bpf_object **objs, size_t nb_obj,
					struct veth_configuration *net_config,
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

	interface = if_nametoindex(net_config[index].local_veth);
	if (!ASSERT_NEQ(interface, 0, "non zero interface index"))
		return -1;

	ret = bpf_xdp_attach(interface, bpf_program__fd(local_prog),
			     prog[index].local_flags, NULL);
	if (!ASSERT_OK(ret, "attach xdp program to local veth"))
		return -1;

	nstoken = open_netns(net_config[index].namespace);
	if (!ASSERT_OK_PTR(nstoken, "switch to remote veth namespace"))
		return -1;

	interface = if_nametoindex(net_config[index].remote_veth);
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

static int create_network(struct veth_configuration *net_config)
{
	int i, err;

	memcpy(net_config, default_config, VETH_PAIRS_COUNT * sizeof(struct veth_configuration));

	/* First create and configure all interfaces */
	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		err = append_tid(net_config[i].namespace, NS_NAME_MAX_LEN);
		if (!ASSERT_OK(err, "append TID to ns name"))
			return -1;

		err = append_tid(net_config[i].local_veth, VETH_NAME_MAX_LEN);
		if (!ASSERT_OK(err, "append TID to local veth name"))
			return -1;

		SYS(fail, "ip netns add %s", net_config[i].namespace);
		SYS(fail, "ip link add %s type veth peer name %s netns %s",
		    net_config[i].local_veth, net_config[i].remote_veth, net_config[i].namespace);
		SYS(fail, "ip link set dev %s up", net_config[i].local_veth);
		if (net_config[i].remote_addr[0])
			SYS(fail, "ip -n %s addr add %s/24 dev %s",	net_config[i].namespace,
			    net_config[i].remote_addr, net_config[i].remote_veth);
		SYS(fail, "ip -n %s link set dev %s up", net_config[i].namespace,
		    net_config[i].remote_veth);
	}

	return 0;

fail:
	return -1;
}

static void cleanup_network(struct veth_configuration *net_config)
{
	struct nstoken *nstoken;
	int i;

	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		bpf_xdp_detach(if_nametoindex(net_config[i].local_veth), 0, NULL);
		nstoken = open_netns(net_config[i].namespace);
		if (nstoken) {
			bpf_xdp_detach(if_nametoindex(net_config[i].remote_veth), 0, NULL);
			close_netns(nstoken);
		}
		/* in case the detach failed */
		SYS_NOFAIL("ip link del %s", net_config[i].local_veth);
		SYS_NOFAIL("ip netns del %s", net_config[i].namespace);
	}
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
	struct veth_configuration net_config[VETH_PAIRS_COUNT];
	struct bpf_object *bpf_objs[VETH_REDIRECT_SKEL_NB];
	struct xdp_redirect_map *xdp_redirect_map;
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

	if (!ASSERT_OK(create_network(net_config), "create network"))
		goto destroy_xdp_redirect_map;

	/* Then configure the redirect map and attach programs to interfaces */
	map_fd = bpf_map__fd(xdp_redirect_map->maps.tx_port);
	if (!ASSERT_OK_FD(map_fd, "open redirect map"))
		goto destroy_xdp_redirect_map;

	bpf_objs[0] = xdp_dummy->obj;
	bpf_objs[1] = xdp_tx->obj;
	bpf_objs[2] = xdp_redirect_map->obj;
	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		int next_veth = net_config[i].next_veth;
		int interface_id;
		int err;

		interface_id = if_nametoindex(net_config[next_veth].local_veth);
		if (!ASSERT_NEQ(interface_id, 0, "non zero interface index"))
			goto destroy_xdp_redirect_map;
		err = bpf_map_update_elem(map_fd, &i, &interface_id, BPF_ANY);
		if (!ASSERT_OK(err, "configure interface redirection through map"))
			goto destroy_xdp_redirect_map;
		if (attach_programs_to_veth_pair(bpf_objs, VETH_REDIRECT_SKEL_NB,
						 net_config, ping_config, i))
			goto destroy_xdp_redirect_map;
	}

	/* Test: if all interfaces are properly configured, we must be able to ping
	 * veth33 from veth11
	 */
	ASSERT_OK(SYS_NOFAIL("ip netns exec %s ping -c 1 -W 1 %s > /dev/null",
			     net_config[0].namespace, IP_DST), "ping");

destroy_xdp_redirect_map:
	xdp_redirect_map__destroy(xdp_redirect_map);
destroy_xdp_tx:
	xdp_tx__destroy(xdp_tx);
destroy_xdp_dummy:
	xdp_dummy__destroy(xdp_dummy);

	cleanup_network(net_config);
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
