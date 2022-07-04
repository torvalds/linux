// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/*
 * End-to-end eBPF tunnel test suite
 *   The file tests BPF network tunnel implementation.
 *
 * Topology:
 * ---------
 *     root namespace   |     at_ns0 namespace
 *                       |
 *       -----------     |     -----------
 *       | tnl dev |     |     | tnl dev |  (overlay network)
 *       -----------     |     -----------
 *       metadata-mode   |     metadata-mode
 *        with bpf       |       with bpf
 *                       |
 *       ----------      |     ----------
 *       |  veth1  | --------- |  veth0  |  (underlay network)
 *       ----------    peer    ----------
 *
 *
 *  Device Configuration
 *  --------------------
 *  root namespace with metadata-mode tunnel + BPF
 *  Device names and addresses:
 *	veth1 IP 1: 172.16.1.200, IPv6: 00::22 (underlay)
 *		IP 2: 172.16.1.20, IPv6: 00::bb (underlay)
 *	tunnel dev <type>11, ex: gre11, IPv4: 10.1.1.200, IPv6: 1::22 (overlay)
 *
 *  Namespace at_ns0 with native tunnel
 *  Device names and addresses:
 *	veth0 IPv4: 172.16.1.100, IPv6: 00::11 (underlay)
 *	tunnel dev <type>00, ex: gre00, IPv4: 10.1.1.100, IPv6: 1::11 (overlay)
 *
 *
 * End-to-end ping packet flow
 *  ---------------------------
 *  Most of the tests start by namespace creation, device configuration,
 *  then ping the underlay and overlay network.  When doing 'ping 10.1.1.100'
 *  from root namespace, the following operations happen:
 *  1) Route lookup shows 10.1.1.100/24 belongs to tnl dev, fwd to tnl dev.
 *  2) Tnl device's egress BPF program is triggered and set the tunnel metadata,
 *     with local_ip=172.16.1.200, remote_ip=172.16.1.100. BPF program choose
 *     the primary or secondary ip of veth1 as the local ip of tunnel. The
 *     choice is made based on the value of bpf map local_ip_map.
 *  3) Outer tunnel header is prepended and route the packet to veth1's egress.
 *  4) veth0's ingress queue receive the tunneled packet at namespace at_ns0.
 *  5) Tunnel protocol handler, ex: vxlan_rcv, decap the packet.
 *  6) Forward the packet to the overlay tnl dev.
 */

#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <linux/limits.h>
#include <linux/sysctl.h>
#include <linux/time_types.h>
#include <linux/net_tstamp.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "test_tunnel_kern.skel.h"

#define IP4_ADDR_VETH0 "172.16.1.100"
#define IP4_ADDR1_VETH1 "172.16.1.200"
#define IP4_ADDR2_VETH1 "172.16.1.20"
#define IP4_ADDR_TUNL_DEV0 "10.1.1.100"
#define IP4_ADDR_TUNL_DEV1 "10.1.1.200"

#define IP6_ADDR_VETH0 "::11"
#define IP6_ADDR1_VETH1 "::22"
#define IP6_ADDR2_VETH1 "::bb"

#define IP4_ADDR1_HEX_VETH1 0xac1001c8
#define IP4_ADDR2_HEX_VETH1 0xac100114
#define IP6_ADDR1_HEX_VETH1 0x22
#define IP6_ADDR2_HEX_VETH1 0xbb

#define MAC_TUNL_DEV0 "52:54:00:d9:01:00"
#define MAC_TUNL_DEV1 "52:54:00:d9:02:00"

#define VXLAN_TUNL_DEV0 "vxlan00"
#define VXLAN_TUNL_DEV1 "vxlan11"
#define IP6VXLAN_TUNL_DEV0 "ip6vxlan00"
#define IP6VXLAN_TUNL_DEV1 "ip6vxlan11"

#define PING_ARGS "-i 0.01 -c 3 -w 10 -q"

#define SYS(fmt, ...)						\
	({							\
		char cmd[1024];					\
		snprintf(cmd, sizeof(cmd), fmt, ##__VA_ARGS__);	\
		if (!ASSERT_OK(system(cmd), cmd))		\
			goto fail;				\
	})

#define SYS_NOFAIL(fmt, ...)					\
	({							\
		char cmd[1024];					\
		snprintf(cmd, sizeof(cmd), fmt, ##__VA_ARGS__);	\
		system(cmd);					\
	})

static int config_device(void)
{
	SYS("ip netns add at_ns0");
	SYS("ip link add veth0 type veth peer name veth1");
	SYS("ip link set veth0 netns at_ns0");
	SYS("ip addr add " IP4_ADDR1_VETH1 "/24 dev veth1");
	SYS("ip addr add " IP4_ADDR2_VETH1 "/24 dev veth1");
	SYS("ip link set dev veth1 up mtu 1500");
	SYS("ip netns exec at_ns0 ip addr add " IP4_ADDR_VETH0 "/24 dev veth0");
	SYS("ip netns exec at_ns0 ip link set dev veth0 up mtu 1500");

	return 0;
fail:
	return -1;
}

static void cleanup(void)
{
	SYS_NOFAIL("test -f /var/run/netns/at_ns0 && ip netns delete at_ns0");
	SYS_NOFAIL("ip link del veth1 2> /dev/null");
	SYS_NOFAIL("ip link del %s 2> /dev/null", VXLAN_TUNL_DEV1);
	SYS_NOFAIL("ip link del %s 2> /dev/null", IP6VXLAN_TUNL_DEV1);
}

static int add_vxlan_tunnel(void)
{
	/* at_ns0 namespace */
	SYS("ip netns exec at_ns0 ip link add dev %s type vxlan external gbp dstport 4789",
	    VXLAN_TUNL_DEV0);
	SYS("ip netns exec at_ns0 ip link set dev %s address %s up",
	    VXLAN_TUNL_DEV0, MAC_TUNL_DEV0);
	SYS("ip netns exec at_ns0 ip addr add dev %s %s/24",
	    VXLAN_TUNL_DEV0, IP4_ADDR_TUNL_DEV0);
	SYS("ip netns exec at_ns0 ip neigh add %s lladdr %s dev %s",
	    IP4_ADDR_TUNL_DEV1, MAC_TUNL_DEV1, VXLAN_TUNL_DEV0);

	/* root namespace */
	SYS("ip link add dev %s type vxlan external gbp dstport 4789",
	    VXLAN_TUNL_DEV1);
	SYS("ip link set dev %s address %s up", VXLAN_TUNL_DEV1, MAC_TUNL_DEV1);
	SYS("ip addr add dev %s %s/24", VXLAN_TUNL_DEV1, IP4_ADDR_TUNL_DEV1);
	SYS("ip neigh add %s lladdr %s dev %s",
	    IP4_ADDR_TUNL_DEV0, MAC_TUNL_DEV0, VXLAN_TUNL_DEV1);

	return 0;
fail:
	return -1;
}

static void delete_vxlan_tunnel(void)
{
	SYS_NOFAIL("ip netns exec at_ns0 ip link delete dev %s",
		   VXLAN_TUNL_DEV0);
	SYS_NOFAIL("ip link delete dev %s", VXLAN_TUNL_DEV1);
}

static int add_ip6vxlan_tunnel(void)
{
	SYS("ip netns exec at_ns0 ip -6 addr add %s/96 dev veth0",
	    IP6_ADDR_VETH0);
	SYS("ip netns exec at_ns0 ip link set dev veth0 up");
	SYS("ip -6 addr add %s/96 dev veth1", IP6_ADDR1_VETH1);
	SYS("ip -6 addr add %s/96 dev veth1", IP6_ADDR2_VETH1);
	SYS("ip link set dev veth1 up");

	/* at_ns0 namespace */
	SYS("ip netns exec at_ns0 ip link add dev %s type vxlan external dstport 4789",
	    IP6VXLAN_TUNL_DEV0);
	SYS("ip netns exec at_ns0 ip addr add dev %s %s/24",
	    IP6VXLAN_TUNL_DEV0, IP4_ADDR_TUNL_DEV0);
	SYS("ip netns exec at_ns0 ip link set dev %s address %s up",
	    IP6VXLAN_TUNL_DEV0, MAC_TUNL_DEV0);

	/* root namespace */
	SYS("ip link add dev %s type vxlan external dstport 4789",
	    IP6VXLAN_TUNL_DEV1);
	SYS("ip addr add dev %s %s/24", IP6VXLAN_TUNL_DEV1, IP4_ADDR_TUNL_DEV1);
	SYS("ip link set dev %s address %s up",
	    IP6VXLAN_TUNL_DEV1, MAC_TUNL_DEV1);

	return 0;
fail:
	return -1;
}

static void delete_ip6vxlan_tunnel(void)
{
	SYS_NOFAIL("ip netns exec at_ns0 ip -6 addr delete %s/96 dev veth0",
		   IP6_ADDR_VETH0);
	SYS_NOFAIL("ip -6 addr delete %s/96 dev veth1", IP6_ADDR1_VETH1);
	SYS_NOFAIL("ip -6 addr delete %s/96 dev veth1", IP6_ADDR2_VETH1);
	SYS_NOFAIL("ip netns exec at_ns0 ip link delete dev %s",
		   IP6VXLAN_TUNL_DEV0);
	SYS_NOFAIL("ip link delete dev %s", IP6VXLAN_TUNL_DEV1);
}

static int test_ping(int family, const char *addr)
{
	SYS("%s %s %s > /dev/null", ping_command(family), PING_ARGS, addr);
	return 0;
fail:
	return -1;
}

static int attach_tc_prog(struct bpf_tc_hook *hook, int igr_fd, int egr_fd)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts1, .handle = 1,
			    .priority = 1, .prog_fd = igr_fd);
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts2, .handle = 1,
			    .priority = 1, .prog_fd = egr_fd);
	int ret;

	ret = bpf_tc_hook_create(hook);
	if (!ASSERT_OK(ret, "create tc hook"))
		return ret;

	if (igr_fd >= 0) {
		hook->attach_point = BPF_TC_INGRESS;
		ret = bpf_tc_attach(hook, &opts1);
		if (!ASSERT_OK(ret, "bpf_tc_attach")) {
			bpf_tc_hook_destroy(hook);
			return ret;
		}
	}

	if (egr_fd >= 0) {
		hook->attach_point = BPF_TC_EGRESS;
		ret = bpf_tc_attach(hook, &opts2);
		if (!ASSERT_OK(ret, "bpf_tc_attach")) {
			bpf_tc_hook_destroy(hook);
			return ret;
		}
	}

	return 0;
}

static void test_vxlan_tunnel(void)
{
	struct test_tunnel_kern *skel = NULL;
	struct nstoken *nstoken;
	int local_ip_map_fd = -1;
	int set_src_prog_fd, get_src_prog_fd;
	int set_dst_prog_fd;
	int key = 0, ifindex = -1;
	uint local_ip;
	int err;
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, tc_hook,
			    .attach_point = BPF_TC_INGRESS);

	/* add vxlan tunnel */
	err = add_vxlan_tunnel();
	if (!ASSERT_OK(err, "add vxlan tunnel"))
		goto done;

	/* load and attach bpf prog to tunnel dev tc hook point */
	skel = test_tunnel_kern__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_tunnel_kern__open_and_load"))
		goto done;
	ifindex = if_nametoindex(VXLAN_TUNL_DEV1);
	if (!ASSERT_NEQ(ifindex, 0, "vxlan11 ifindex"))
		goto done;
	tc_hook.ifindex = ifindex;
	get_src_prog_fd = bpf_program__fd(skel->progs.vxlan_get_tunnel_src);
	set_src_prog_fd = bpf_program__fd(skel->progs.vxlan_set_tunnel_src);
	if (!ASSERT_GE(get_src_prog_fd, 0, "bpf_program__fd"))
		goto done;
	if (!ASSERT_GE(set_src_prog_fd, 0, "bpf_program__fd"))
		goto done;
	if (attach_tc_prog(&tc_hook, get_src_prog_fd, set_src_prog_fd))
		goto done;

	/* load and attach prog set_md to tunnel dev tc hook point at_ns0 */
	nstoken = open_netns("at_ns0");
	if (!ASSERT_OK_PTR(nstoken, "setns src"))
		goto done;
	ifindex = if_nametoindex(VXLAN_TUNL_DEV0);
	if (!ASSERT_NEQ(ifindex, 0, "vxlan00 ifindex"))
		goto done;
	tc_hook.ifindex = ifindex;
	set_dst_prog_fd = bpf_program__fd(skel->progs.vxlan_set_tunnel_dst);
	if (!ASSERT_GE(set_dst_prog_fd, 0, "bpf_program__fd"))
		goto done;
	if (attach_tc_prog(&tc_hook, -1, set_dst_prog_fd))
		goto done;
	close_netns(nstoken);

	/* use veth1 ip 2 as tunnel source ip */
	local_ip_map_fd = bpf_map__fd(skel->maps.local_ip_map);
	if (!ASSERT_GE(local_ip_map_fd, 0, "bpf_map__fd"))
		goto done;
	local_ip = IP4_ADDR2_HEX_VETH1;
	err = bpf_map_update_elem(local_ip_map_fd, &key, &local_ip, BPF_ANY);
	if (!ASSERT_OK(err, "update bpf local_ip_map"))
		goto done;

	/* ping test */
	err = test_ping(AF_INET, IP4_ADDR_TUNL_DEV0);
	if (!ASSERT_OK(err, "test_ping"))
		goto done;

done:
	/* delete vxlan tunnel */
	delete_vxlan_tunnel();
	if (local_ip_map_fd >= 0)
		close(local_ip_map_fd);
	if (skel)
		test_tunnel_kern__destroy(skel);
}

static void test_ip6vxlan_tunnel(void)
{
	struct test_tunnel_kern *skel = NULL;
	struct nstoken *nstoken;
	int local_ip_map_fd = -1;
	int set_src_prog_fd, get_src_prog_fd;
	int set_dst_prog_fd;
	int key = 0, ifindex = -1;
	uint local_ip;
	int err;
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, tc_hook,
			    .attach_point = BPF_TC_INGRESS);

	/* add vxlan tunnel */
	err = add_ip6vxlan_tunnel();
	if (!ASSERT_OK(err, "add_ip6vxlan_tunnel"))
		goto done;

	/* load and attach bpf prog to tunnel dev tc hook point */
	skel = test_tunnel_kern__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_tunnel_kern__open_and_load"))
		goto done;
	ifindex = if_nametoindex(IP6VXLAN_TUNL_DEV1);
	if (!ASSERT_NEQ(ifindex, 0, "ip6vxlan11 ifindex"))
		goto done;
	tc_hook.ifindex = ifindex;
	get_src_prog_fd = bpf_program__fd(skel->progs.ip6vxlan_get_tunnel_src);
	set_src_prog_fd = bpf_program__fd(skel->progs.ip6vxlan_set_tunnel_src);
	if (!ASSERT_GE(set_src_prog_fd, 0, "bpf_program__fd"))
		goto done;
	if (!ASSERT_GE(get_src_prog_fd, 0, "bpf_program__fd"))
		goto done;
	if (attach_tc_prog(&tc_hook, get_src_prog_fd, set_src_prog_fd))
		goto done;

	/* load and attach prog set_md to tunnel dev tc hook point at_ns0 */
	nstoken = open_netns("at_ns0");
	if (!ASSERT_OK_PTR(nstoken, "setns src"))
		goto done;
	ifindex = if_nametoindex(IP6VXLAN_TUNL_DEV0);
	if (!ASSERT_NEQ(ifindex, 0, "ip6vxlan00 ifindex"))
		goto done;
	tc_hook.ifindex = ifindex;
	set_dst_prog_fd = bpf_program__fd(skel->progs.ip6vxlan_set_tunnel_dst);
	if (!ASSERT_GE(set_dst_prog_fd, 0, "bpf_program__fd"))
		goto done;
	if (attach_tc_prog(&tc_hook, -1, set_dst_prog_fd))
		goto done;
	close_netns(nstoken);

	/* use veth1 ip 2 as tunnel source ip */
	local_ip_map_fd = bpf_map__fd(skel->maps.local_ip_map);
	if (!ASSERT_GE(local_ip_map_fd, 0, "get local_ip_map fd"))
		goto done;
	local_ip = IP6_ADDR2_HEX_VETH1;
	err = bpf_map_update_elem(local_ip_map_fd, &key, &local_ip, BPF_ANY);
	if (!ASSERT_OK(err, "update bpf local_ip_map"))
		goto done;

	/* ping test */
	err = test_ping(AF_INET, IP4_ADDR_TUNL_DEV0);
	if (!ASSERT_OK(err, "test_ping"))
		goto done;

done:
	/* delete ipv6 vxlan tunnel */
	delete_ip6vxlan_tunnel();
	if (local_ip_map_fd >= 0)
		close(local_ip_map_fd);
	if (skel)
		test_tunnel_kern__destroy(skel);
}

#define RUN_TEST(name)							\
	({								\
		if (test__start_subtest(#name)) {			\
			test_ ## name();				\
		}							\
	})

static void *test_tunnel_run_tests(void *arg)
{
	cleanup();
	config_device();

	RUN_TEST(vxlan_tunnel);
	RUN_TEST(ip6vxlan_tunnel);

	cleanup();

	return NULL;
}

void serial_test_tunnel(void)
{
	pthread_t test_thread;
	int err;

	/* Run the tests in their own thread to isolate the namespace changes
	 * so they do not affect the environment of other tests.
	 * (specifically needed because of unshare(CLONE_NEWNS) in open_netns())
	 */
	err = pthread_create(&test_thread, NULL, &test_tunnel_run_tests, NULL);
	if (ASSERT_OK(err, "pthread_create"))
		ASSERT_OK(pthread_join(test_thread, NULL), "pthread_join");
}
