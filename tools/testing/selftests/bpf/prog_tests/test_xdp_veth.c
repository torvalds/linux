// SPDX-License-Identifier: GPL-2.0

/* Create 3 namespaces with 3 veth peers, and forward packets in-between using
 * native XDP
 *
 *                      XDP_TX
 * NS1(veth11)        NS2(veth22)        NS3(veth33)
 *      |                  |                  |
 *      |                  |                  |
 *   (veth1,            (veth2,            (veth3,
 *   id:111)            id:122)            id:133)
 *     ^ |                ^ |                ^ |
 *     | |  XDP_REDIRECT  | |  XDP_REDIRECT  | |
 *     | ------------------ ------------------ |
 *     -----------------------------------------
 *                    XDP_REDIRECT
 */

#define _GNU_SOURCE
#include <net/if.h>
#include "test_progs.h"
#include "network_helpers.h"
#include "xdp_dummy.skel.h"
#include "xdp_redirect_map.skel.h"
#include "xdp_tx.skel.h"

#define VETH_PAIRS_COUNT	3
#define NS_SUFFIX_LEN		6
#define VETH_NAME_MAX_LEN	16
#define IP_SRC				"10.1.1.11"
#define IP_DST				"10.1.1.33"
#define IP_CMD_MAX_LEN		128

struct skeletons {
	struct xdp_dummy *xdp_dummy;
	struct xdp_tx *xdp_tx;
	struct xdp_redirect_map *xdp_redirect_maps;
};

struct veth_configuration {
	char local_veth[VETH_NAME_MAX_LEN]; /* Interface in main namespace */
	char remote_veth[VETH_NAME_MAX_LEN]; /* Peer interface in dedicated namespace*/
	const char *namespace; /* Namespace for the remote veth */
	char next_veth[VETH_NAME_MAX_LEN]; /* Local interface to redirect traffic to */
	char *remote_addr; /* IP address of the remote veth */
};

static struct veth_configuration config[VETH_PAIRS_COUNT] = {
	{
		.local_veth = "veth1",
		.remote_veth = "veth11",
		.next_veth = "veth2",
		.remote_addr = IP_SRC,
		.namespace = "ns-veth11"
	},
	{
		.local_veth = "veth2",
		.remote_veth = "veth22",
		.next_veth = "veth3",
		.remote_addr = NULL,
		.namespace = "ns-veth22"
	},
	{
		.local_veth = "veth3",
		.remote_veth = "veth33",
		.next_veth = "veth1",
		.remote_addr = IP_DST,
		.namespace = "ns-veth33"
	}
};

static int attach_programs_to_veth_pair(struct skeletons *skeletons, int index)
{
	struct bpf_program *local_prog, *remote_prog;
	struct bpf_link **local_link, **remote_link;
	struct nstoken *nstoken;
	struct bpf_link *link;
	int interface;

	switch (index) {
	case 0:
		local_prog = skeletons->xdp_redirect_maps->progs.xdp_redirect_map_0;
		local_link = &skeletons->xdp_redirect_maps->links.xdp_redirect_map_0;
		remote_prog = skeletons->xdp_dummy->progs.xdp_dummy_prog;
		remote_link = &skeletons->xdp_dummy->links.xdp_dummy_prog;
		break;
	case 1:
		local_prog = skeletons->xdp_redirect_maps->progs.xdp_redirect_map_1;
		local_link = &skeletons->xdp_redirect_maps->links.xdp_redirect_map_1;
		remote_prog = skeletons->xdp_tx->progs.xdp_tx;
		remote_link = &skeletons->xdp_tx->links.xdp_tx;
		break;
	case 2:
		local_prog = skeletons->xdp_redirect_maps->progs.xdp_redirect_map_2;
		local_link = &skeletons->xdp_redirect_maps->links.xdp_redirect_map_2;
		remote_prog = skeletons->xdp_dummy->progs.xdp_dummy_prog;
		remote_link = &skeletons->xdp_dummy->links.xdp_dummy_prog;
		break;
	}
	interface = if_nametoindex(config[index].local_veth);
	if (!ASSERT_NEQ(interface, 0, "non zero interface index"))
		return -1;
	link = bpf_program__attach_xdp(local_prog, interface);
	if (!ASSERT_OK_PTR(link, "attach xdp program to local veth"))
		return -1;
	*local_link = link;
	nstoken = open_netns(config[index].namespace);
	if (!ASSERT_OK_PTR(nstoken, "switch to remote veth namespace"))
		return -1;
	interface = if_nametoindex(config[index].remote_veth);
	if (!ASSERT_NEQ(interface, 0, "non zero interface index")) {
		close_netns(nstoken);
		return -1;
	}
	link = bpf_program__attach_xdp(remote_prog, interface);
	*remote_link = link;
	close_netns(nstoken);
	if (!ASSERT_OK_PTR(link, "attach xdp program to remote veth"))
		return -1;

	return 0;
}

static int configure_network(struct skeletons *skeletons)
{
	int interface_id;
	int map_fd;
	int err;
	int i = 0;

	/* First create and configure all interfaces */
	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		SYS(fail, "ip netns add %s", config[i].namespace);
		SYS(fail, "ip link add %s type veth peer name %s netns %s",
		    config[i].local_veth, config[i].remote_veth, config[i].namespace);
		SYS(fail, "ip link set dev %s up", config[i].local_veth);
		if (config[i].remote_addr)
			SYS(fail, "ip -n %s addr add %s/24 dev %s",	config[i].namespace,
			    config[i].remote_addr, config[i].remote_veth);
		SYS(fail, "ip -n %s link set dev %s up", config[i].namespace,
		    config[i].remote_veth);
	}

	/* Then configure the redirect map and attach programs to interfaces */
	map_fd = bpf_map__fd(skeletons->xdp_redirect_maps->maps.tx_port);
	if (!ASSERT_GE(map_fd, 0, "open redirect map"))
		goto fail;
	for (i = 0; i < VETH_PAIRS_COUNT; i++) {
		interface_id = if_nametoindex(config[i].next_veth);
		if (!ASSERT_NEQ(interface_id, 0, "non zero interface index"))
			goto fail;
		err = bpf_map_update_elem(map_fd, &i, &interface_id, BPF_ANY);
		if (!ASSERT_OK(err, "configure interface redirection through map"))
			goto fail;
		if (attach_programs_to_veth_pair(skeletons, i))
			goto fail;
	}

	return 0;

fail:
	return -1;
}

static void cleanup_network(void)
{
	int i;

	/* Deleting namespaces is enough to automatically remove veth pairs as well
	 */
	for (i = 0; i < VETH_PAIRS_COUNT; i++)
		SYS_NOFAIL("ip netns del %s", config[i].namespace);
}

static int check_ping(struct skeletons *skeletons)
{
	/* Test: if all interfaces are properly configured, we must be able to ping
	 * veth33 from veth11
	 */
	return SYS_NOFAIL("ip netns exec %s ping -c 1 -W 1 %s > /dev/null",
					  config[0].namespace, IP_DST);
}

void test_xdp_veth_redirect(void)
{
	struct skeletons skeletons = {};

	skeletons.xdp_dummy = xdp_dummy__open_and_load();
	if (!ASSERT_OK_PTR(skeletons.xdp_dummy, "xdp_dummy__open_and_load"))
		return;

	skeletons.xdp_tx = xdp_tx__open_and_load();
	if (!ASSERT_OK_PTR(skeletons.xdp_tx, "xdp_tx__open_and_load"))
		goto destroy_xdp_dummy;

	skeletons.xdp_redirect_maps = xdp_redirect_map__open_and_load();
	if (!ASSERT_OK_PTR(skeletons.xdp_redirect_maps, "xdp_redirect_map__open_and_load"))
		goto destroy_xdp_tx;

	if (configure_network(&skeletons))
		goto destroy_xdp_redirect_map;

	ASSERT_OK(check_ping(&skeletons), "ping");

destroy_xdp_redirect_map:
	xdp_redirect_map__destroy(skeletons.xdp_redirect_maps);
destroy_xdp_tx:
	xdp_tx__destroy(skeletons.xdp_tx);
destroy_xdp_dummy:
	xdp_dummy__destroy(skeletons.xdp_dummy);

	cleanup_network();
}
