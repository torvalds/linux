// SPDX-License-Identifier: GPL-2.0-only
#include <netinet/in.h>

#include "network_helpers.h"
#include "test_progs.h"

#define BPF_FILE "test_lwt_ip_encap.bpf.o"

#define NETNS_NAME_SIZE	32
#define NETNS_BASE	"ns-lwt-ip-encap"

#define IP4_ADDR_1 "172.16.1.100"
#define IP4_ADDR_2 "172.16.2.100"
#define IP4_ADDR_3 "172.16.3.100"
#define IP4_ADDR_4 "172.16.4.100"
#define IP4_ADDR_5 "172.16.5.100"
#define IP4_ADDR_6 "172.16.6.100"
#define IP4_ADDR_7 "172.16.7.100"
#define IP4_ADDR_8 "172.16.8.100"
#define IP4_ADDR_GRE "172.16.16.100"

#define IP4_ADDR_SRC IP4_ADDR_1
#define IP4_ADDR_DST IP4_ADDR_4

#define IP6_ADDR_1 "fb01::1"
#define IP6_ADDR_2 "fb02::1"
#define IP6_ADDR_3 "fb03::1"
#define IP6_ADDR_4 "fb04::1"
#define IP6_ADDR_5 "fb05::1"
#define IP6_ADDR_6 "fb06::1"
#define IP6_ADDR_7 "fb07::1"
#define IP6_ADDR_8 "fb08::1"
#define IP6_ADDR_GRE "fb10::1"

#define IP6_ADDR_SRC IP6_ADDR_1
#define IP6_ADDR_DST IP6_ADDR_4

/* Setup/topology:
 *
 *    NS1             NS2             NS3
 *   veth1 <---> veth2   veth3 <---> veth4 (the top route)
 *   veth5 <---> veth6   veth7 <---> veth8 (the bottom route)
 *
 *   Each vethN gets IP[4|6]_ADDR_N address.
 *
 *   IP*_ADDR_SRC = IP*_ADDR_1
 *   IP*_ADDR_DST = IP*_ADDR_4
 *
 *   All tests test pings from IP*_ADDR__SRC to IP*_ADDR_DST.
 *
 *   By default, routes are configured to allow packets to go
 *   IP*_ADDR_1 <=> IP*_ADDR_2 <=> IP*_ADDR_3 <=> IP*_ADDR_4 (the top route).
 *
 *   A GRE device is installed in NS3 with IP*_ADDR_GRE, and
 *   NS1/NS2 are configured to route packets to IP*_ADDR_GRE via IP*_ADDR_8
 *   (the bottom route).
 *
 * Tests:
 *
 *   1. Routes NS2->IP*_ADDR_DST are brought down, so the only way a ping
 *      from IP*_ADDR_SRC to IP*_ADDR_DST can work is via IP*_ADDR_GRE.
 *
 *   2a. In an egress test, a bpf LWT_XMIT program is installed on veth1
 *       that encaps the packets with an IP/GRE header to route to IP*_ADDR_GRE.
 *
 *       ping: SRC->[encap at veth1:egress]->GRE:decap->DST
 *       ping replies go DST->SRC directly
 *
 *   2b. In an ingress test, a bpf LWT_IN program is installed on veth2
 *       that encaps the packets with an IP/GRE header to route to IP*_ADDR_GRE.
 *
 *       ping: SRC->[encap at veth2:ingress]->GRE:decap->DST
 *       ping replies go DST->SRC directly
 */

static int create_ns(char *name, size_t name_sz)
{
	if (!name)
		goto fail;

	if (!ASSERT_OK(append_tid(name, name_sz), "append TID"))
		goto fail;

	SYS(fail, "ip netns add %s", name);

	/* rp_filter gets confused by what these tests are doing, so disable it */
	SYS(fail, "ip netns exec %s sysctl -wq net.ipv4.conf.all.rp_filter=0", name);
	SYS(fail, "ip netns exec %s sysctl -wq net.ipv4.conf.default.rp_filter=0", name);
	/* Disable IPv6 DAD because it sometimes takes too long and fails tests */
	SYS(fail, "ip netns exec %s sysctl -wq net.ipv6.conf.all.accept_dad=0", name);
	SYS(fail, "ip netns exec %s sysctl -wq net.ipv6.conf.default.accept_dad=0", name);

	return 0;
fail:
	return -1;
}

static int set_top_addr(const char *ns1, const char *ns2, const char *ns3)
{
	SYS(fail, "ip -n %s    a add %s/24  dev veth1", ns1, IP4_ADDR_1);
	SYS(fail, "ip -n %s    a add %s/24  dev veth2", ns2, IP4_ADDR_2);
	SYS(fail, "ip -n %s    a add %s/24  dev veth3", ns2, IP4_ADDR_3);
	SYS(fail, "ip -n %s    a add %s/24  dev veth4", ns3, IP4_ADDR_4);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth1", ns1, IP6_ADDR_1);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth2", ns2, IP6_ADDR_2);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth3", ns2, IP6_ADDR_3);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth4", ns3, IP6_ADDR_4);

	SYS(fail, "ip -n %s link set dev veth1 up", ns1);
	SYS(fail, "ip -n %s link set dev veth2 up", ns2);
	SYS(fail, "ip -n %s link set dev veth3 up", ns2);
	SYS(fail, "ip -n %s link set dev veth4 up", ns3);

	return 0;
fail:
	return 1;
}

static int set_bottom_addr(const char *ns1, const char *ns2, const char *ns3)
{
	SYS(fail, "ip -n %s    a add %s/24  dev veth5", ns1, IP4_ADDR_5);
	SYS(fail, "ip -n %s    a add %s/24  dev veth6", ns2, IP4_ADDR_6);
	SYS(fail, "ip -n %s    a add %s/24  dev veth7", ns2, IP4_ADDR_7);
	SYS(fail, "ip -n %s    a add %s/24  dev veth8", ns3, IP4_ADDR_8);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth5", ns1, IP6_ADDR_5);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth6", ns2, IP6_ADDR_6);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth7", ns2, IP6_ADDR_7);
	SYS(fail, "ip -n %s -6 a add %s/128 dev veth8", ns3, IP6_ADDR_8);

	SYS(fail, "ip -n %s link set dev veth5 up", ns1);
	SYS(fail, "ip -n %s link set dev veth6 up", ns2);
	SYS(fail, "ip -n %s link set dev veth7 up", ns2);
	SYS(fail, "ip -n %s link set dev veth8 up", ns3);

	return 0;
fail:
	return 1;
}

static int configure_vrf(const char *ns1, const char *ns2)
{
	if (!ns1 || !ns2)
		goto fail;

	SYS(fail, "ip -n %s link add red type vrf table 1001", ns1);
	SYS(fail, "ip -n %s link set red up", ns1);
	SYS(fail, "ip -n %s route add table 1001 unreachable default metric 8192", ns1);
	SYS(fail, "ip -n %s -6 route add table 1001 unreachable default metric 8192", ns1);
	SYS(fail, "ip -n %s link set veth1 vrf red", ns1);
	SYS(fail, "ip -n %s link set veth5 vrf red", ns1);

	SYS(fail, "ip -n %s link add red type vrf table 1001", ns2);
	SYS(fail, "ip -n %s link set red up", ns2);
	SYS(fail, "ip -n %s route add table 1001 unreachable default metric 8192", ns2);
	SYS(fail, "ip -n %s -6 route add table 1001 unreachable default metric 8192", ns2);
	SYS(fail, "ip -n %s link set veth2 vrf red", ns2);
	SYS(fail, "ip -n %s link set veth3 vrf red", ns2);
	SYS(fail, "ip -n %s link set veth6 vrf red", ns2);
	SYS(fail, "ip -n %s link set veth7 vrf red", ns2);

	return 0;
fail:
	return -1;
}

static int configure_ns1(const char *ns1, const char *vrf)
{
	struct nstoken *nstoken = NULL;

	if (!ns1 || !vrf)
		goto fail;

	nstoken = open_netns(ns1);
	if (!ASSERT_OK_PTR(nstoken, "open ns1"))
		goto fail;

	/* Top route */
	SYS(fail, "ip    route add %s/32  dev veth1 %s", IP4_ADDR_2, vrf);
	SYS(fail, "ip    route add default dev veth1 via %s %s", IP4_ADDR_2, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth1 %s", IP6_ADDR_2, vrf);
	SYS(fail, "ip -6 route add default dev veth1 via %s %s", IP6_ADDR_2, vrf);
	/* Bottom route */
	SYS(fail, "ip    route add %s/32  dev veth5 %s", IP4_ADDR_6, vrf);
	SYS(fail, "ip    route add %s/32  dev veth5 via  %s %s", IP4_ADDR_7, IP4_ADDR_6, vrf);
	SYS(fail, "ip    route add %s/32  dev veth5 via  %s %s", IP4_ADDR_8, IP4_ADDR_6, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth5 %s", IP6_ADDR_6, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth5 via  %s %s", IP6_ADDR_7, IP6_ADDR_6, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth5 via  %s %s", IP6_ADDR_8, IP6_ADDR_6, vrf);

	close_netns(nstoken);
	return 0;
fail:
	close_netns(nstoken);
	return -1;
}

static int configure_ns2(const char *ns2, const char *vrf)
{
	struct nstoken *nstoken = NULL;

	if (!ns2 || !vrf)
		goto fail;

	nstoken = open_netns(ns2);
	if (!ASSERT_OK_PTR(nstoken, "open ns2"))
		goto fail;

	SYS(fail, "ip netns exec %s sysctl -wq net.ipv4.ip_forward=1", ns2);
	SYS(fail, "ip netns exec %s sysctl -wq net.ipv6.conf.all.forwarding=1", ns2);

	/* Top route */
	SYS(fail, "ip    route add %s/32  dev veth2 %s", IP4_ADDR_1, vrf);
	SYS(fail, "ip    route add %s/32  dev veth3 %s", IP4_ADDR_4, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth2 %s", IP6_ADDR_1, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth3 %s", IP6_ADDR_4, vrf);
	/* Bottom route */
	SYS(fail, "ip    route add %s/32  dev veth6 %s", IP4_ADDR_5, vrf);
	SYS(fail, "ip    route add %s/32  dev veth7 %s", IP4_ADDR_8, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth6 %s", IP6_ADDR_5, vrf);
	SYS(fail, "ip -6 route add %s/128 dev veth7 %s", IP6_ADDR_8, vrf);

	close_netns(nstoken);
	return 0;
fail:
	close_netns(nstoken);
	return -1;
}

static int configure_ns3(const char *ns3)
{
	struct nstoken *nstoken = NULL;

	if (!ns3)
		goto fail;

	nstoken = open_netns(ns3);
	if (!ASSERT_OK_PTR(nstoken, "open ns3"))
		goto fail;

	/* Top route */
	SYS(fail, "ip    route add %s/32  dev veth4", IP4_ADDR_3);
	SYS(fail, "ip    route add %s/32  dev veth4 via  %s", IP4_ADDR_1, IP4_ADDR_3);
	SYS(fail, "ip    route add %s/32  dev veth4 via  %s", IP4_ADDR_2, IP4_ADDR_3);
	SYS(fail, "ip -6 route add %s/128 dev veth4", IP6_ADDR_3);
	SYS(fail, "ip -6 route add %s/128 dev veth4 via  %s", IP6_ADDR_1, IP6_ADDR_3);
	SYS(fail, "ip -6 route add %s/128 dev veth4 via  %s", IP6_ADDR_2, IP6_ADDR_3);
	/* Bottom route */
	SYS(fail, "ip    route add %s/32  dev veth8", IP4_ADDR_7);
	SYS(fail, "ip    route add %s/32  dev veth8 via  %s", IP4_ADDR_5, IP4_ADDR_7);
	SYS(fail, "ip    route add %s/32  dev veth8 via  %s", IP4_ADDR_6, IP4_ADDR_7);
	SYS(fail, "ip -6 route add %s/128 dev veth8", IP6_ADDR_7);
	SYS(fail, "ip -6 route add %s/128 dev veth8 via  %s", IP6_ADDR_5, IP6_ADDR_7);
	SYS(fail, "ip -6 route add %s/128 dev veth8 via  %s", IP6_ADDR_6, IP6_ADDR_7);

	/* Configure IPv4 GRE device */
	SYS(fail, "ip tunnel add gre_dev mode gre remote %s local %s ttl 255",
	    IP4_ADDR_1, IP4_ADDR_GRE);
	SYS(fail, "ip link set gre_dev up");
	SYS(fail, "ip a add %s dev gre_dev", IP4_ADDR_GRE);

	/* Configure IPv6 GRE device */
	SYS(fail, "ip tunnel add gre6_dev mode ip6gre remote %s local %s ttl 255",
	    IP6_ADDR_1, IP6_ADDR_GRE);
	SYS(fail, "ip link set gre6_dev up");
	SYS(fail, "ip a add %s dev gre6_dev", IP6_ADDR_GRE);

	close_netns(nstoken);
	return 0;
fail:
	close_netns(nstoken);
	return -1;
}

static int setup_network(char *ns1, char *ns2, char *ns3, const char *vrf)
{
	if (!ns1 || !ns2 || !ns3 || !vrf)
		goto fail;

	SYS(fail, "ip -n %s link add veth1 type veth peer name veth2 netns %s", ns1, ns2);
	SYS(fail, "ip -n %s link add veth3 type veth peer name veth4 netns %s", ns2, ns3);
	SYS(fail, "ip -n %s link add veth5 type veth peer name veth6 netns %s", ns1, ns2);
	SYS(fail, "ip -n %s link add veth7 type veth peer name veth8 netns %s", ns2, ns3);

	if (vrf[0]) {
		if (!ASSERT_OK(configure_vrf(ns1, ns2), "configure vrf"))
			goto fail;
	}
	if (!ASSERT_OK(set_top_addr(ns1, ns2, ns3), "set top addresses"))
		goto fail;

	if (!ASSERT_OK(set_bottom_addr(ns1, ns2, ns3), "set bottom addresses"))
		goto fail;

	if (!ASSERT_OK(configure_ns1(ns1, vrf), "configure ns1 routes"))
		goto fail;

	if (!ASSERT_OK(configure_ns2(ns2, vrf), "configure ns2 routes"))
		goto fail;

	if (!ASSERT_OK(configure_ns3(ns3), "configure ns3 routes"))
		goto fail;

	/* Link bottom route to the GRE tunnels */
	SYS(fail, "ip -n %s route add %s/32 dev veth5 via %s %s",
	    ns1, IP4_ADDR_GRE, IP4_ADDR_6, vrf);
	SYS(fail, "ip -n %s route add %s/32 dev veth7 via %s %s",
	    ns2, IP4_ADDR_GRE, IP4_ADDR_8, vrf);
	SYS(fail, "ip -n %s -6 route add %s/128 dev veth5 via %s %s",
	    ns1, IP6_ADDR_GRE, IP6_ADDR_6, vrf);
	SYS(fail, "ip -n %s -6 route add %s/128 dev veth7 via %s %s",
	    ns2, IP6_ADDR_GRE, IP6_ADDR_8, vrf);

	return 0;
fail:
	return -1;
}

static int remove_routes_to_gredev(const char *ns1, const char *ns2, const char *vrf)
{
	SYS(fail, "ip -n %s route del %s dev veth5 %s", ns1, IP4_ADDR_GRE, vrf);
	SYS(fail, "ip -n %s route del %s dev veth7 %s", ns2, IP4_ADDR_GRE, vrf);
	SYS(fail, "ip -n %s -6 route del %s/128 dev veth5 %s", ns1, IP6_ADDR_GRE, vrf);
	SYS(fail, "ip -n %s -6 route del %s/128 dev veth7 %s", ns2, IP6_ADDR_GRE, vrf);

	return 0;
fail:
	return -1;
}

static int add_unreachable_routes_to_gredev(const char *ns1, const char *ns2, const char *vrf)
{
	SYS(fail, "ip -n %s route add unreachable %s/32 %s", ns1, IP4_ADDR_GRE, vrf);
	SYS(fail, "ip -n %s route add unreachable %s/32 %s", ns2, IP4_ADDR_GRE, vrf);
	SYS(fail, "ip -n %s -6 route add unreachable %s/128 %s", ns1, IP6_ADDR_GRE, vrf);
	SYS(fail, "ip -n %s -6 route add unreachable %s/128 %s", ns2, IP6_ADDR_GRE, vrf);

	return 0;
fail:
	return -1;
}

#define GSO_SIZE 5000
#define GSO_TCP_PORT 9000
/* This tests the fix from commit ea0371f78799 ("net: fix GSO in bpf_lwt_push_ip_encap") */
static int test_gso_fix(const char *ns1, const char *ns3, int family)
{
	const char *ip_addr = family == AF_INET ? IP4_ADDR_DST : IP6_ADDR_DST;
	char gso_packet[GSO_SIZE] = {};
	struct nstoken *nstoken = NULL;
	int sfd, cfd, afd;
	ssize_t bytes;
	int ret = -1;

	if (!ns1 || !ns3)
		return ret;

	nstoken = open_netns(ns3);
	if (!ASSERT_OK_PTR(nstoken, "open ns3"))
		return ret;

	sfd = start_server_str(family, SOCK_STREAM, ip_addr, GSO_TCP_PORT, NULL);
	if (!ASSERT_OK_FD(sfd, "start server"))
		goto close_netns;

	close_netns(nstoken);

	nstoken = open_netns(ns1);
	if (!ASSERT_OK_PTR(nstoken, "open ns1"))
		goto close_server;

	cfd = connect_to_addr_str(family, SOCK_STREAM, ip_addr, GSO_TCP_PORT, NULL);
	if (!ASSERT_OK_FD(cfd, "connect to server"))
		goto close_server;

	close_netns(nstoken);
	nstoken = NULL;

	afd = accept(sfd, NULL, NULL);
	if (!ASSERT_OK_FD(afd, "accept"))
		goto close_client;

	/* Send a packet larger than MTU */
	bytes = send(cfd, gso_packet, GSO_SIZE, 0);
	if (!ASSERT_EQ(bytes, GSO_SIZE, "send packet"))
		goto close_accept;

	/* Verify we received all expected bytes */
	bytes = read(afd, gso_packet, GSO_SIZE);
	if (!ASSERT_EQ(bytes, GSO_SIZE, "receive packet"))
		goto close_accept;

	ret = 0;

close_accept:
	close(afd);
close_client:
	close(cfd);
close_server:
	close(sfd);
close_netns:
	close_netns(nstoken);

	return ret;
}

static int check_ping_ok(const char *ns1)
{
	SYS(fail, "ip netns exec %s ping -c 1 -W1 -I veth1 %s > /dev/null", ns1, IP4_ADDR_DST);
	SYS(fail, "ip netns exec %s ping6 -c 1 -W1 -I veth1 %s > /dev/null", ns1, IP6_ADDR_DST);
	return 0;
fail:
	return -1;
}

static int check_ping_fails(const char *ns1)
{
	int ret;

	ret = SYS_NOFAIL("ip netns exec %s ping -c 1 -W1 -I veth1 %s", ns1, IP4_ADDR_DST);
	if (!ret)
		return -1;

	ret = SYS_NOFAIL("ip netns exec %s ping6 -c 1 -W1 -I veth1 %s", ns1, IP6_ADDR_DST);
	if (!ret)
		return -1;

	return 0;
}

#define EGRESS true
#define INGRESS false
#define IPV4_ENCAP true
#define IPV6_ENCAP false
static void lwt_ip_encap(bool ipv4_encap, bool egress, const char *vrf)
{
	char ns1[NETNS_NAME_SIZE] = NETNS_BASE "-1-";
	char ns2[NETNS_NAME_SIZE] = NETNS_BASE "-2-";
	char ns3[NETNS_NAME_SIZE] = NETNS_BASE "-3-";
	char *sec = ipv4_encap ?  "encap_gre" : "encap_gre6";

	if (!vrf)
		return;

	if (!ASSERT_OK(create_ns(ns1, NETNS_NAME_SIZE), "create ns1"))
		goto out;
	if (!ASSERT_OK(create_ns(ns2, NETNS_NAME_SIZE), "create ns2"))
		goto out;
	if (!ASSERT_OK(create_ns(ns3, NETNS_NAME_SIZE), "create ns3"))
		goto out;

	if (!ASSERT_OK(setup_network(ns1, ns2, ns3, vrf), "setup network"))
		goto out;

	/* By default, pings work */
	if (!ASSERT_OK(check_ping_ok(ns1), "ping OK"))
		goto out;

	/* Remove NS2->DST routes, ping fails */
	SYS(out, "ip -n %s    route del %s/32  dev veth3 %s", ns2, IP4_ADDR_DST, vrf);
	SYS(out, "ip -n %s -6 route del %s/128 dev veth3 %s", ns2, IP6_ADDR_DST, vrf);
	if (!ASSERT_OK(check_ping_fails(ns1), "ping expected fail"))
		goto out;

	/* Install replacement routes (LWT/eBPF), pings succeed */
	if (egress) {
		SYS(out, "ip -n %s route add %s encap bpf xmit obj %s sec %s dev veth1 %s",
		    ns1, IP4_ADDR_DST, BPF_FILE, sec, vrf);
		SYS(out, "ip -n %s -6 route add %s encap bpf xmit obj %s sec %s dev veth1 %s",
		    ns1, IP6_ADDR_DST, BPF_FILE, sec, vrf);
	} else {
		SYS(out, "ip -n %s route add %s encap bpf in obj %s sec %s dev veth2 %s",
		    ns2, IP4_ADDR_DST, BPF_FILE, sec, vrf);
		SYS(out, "ip -n %s -6 route add %s encap bpf in obj %s sec %s dev veth2 %s",
		    ns2, IP6_ADDR_DST, BPF_FILE, sec, vrf);
	}

	if (!ASSERT_OK(check_ping_ok(ns1), "ping OK"))
		goto out;

	/* Skip GSO tests with VRF: VRF routing needs properly assigned
	 * source IP/device, which is easy to do with ping but hard with TCP.
	 */
	if (egress && !vrf[0]) {
		if (!ASSERT_OK(test_gso_fix(ns1, ns3, AF_INET), "test GSO"))
			goto out;
	}

	/* Negative test: remove routes to GRE devices: ping fails */
	if (!ASSERT_OK(remove_routes_to_gredev(ns1, ns2, vrf), "remove routes to gredev"))
		goto out;
	if (!ASSERT_OK(check_ping_fails(ns1), "ping expected fail"))
		goto out;

	/* Another negative test */
	if (!ASSERT_OK(add_unreachable_routes_to_gredev(ns1, ns2, vrf),
		       "add unreachable routes"))
		goto out;
	ASSERT_OK(check_ping_fails(ns1), "ping expected fail");

out:
	SYS_NOFAIL("ip netns del %s", ns1);
	SYS_NOFAIL("ip netns del %s", ns2);
	SYS_NOFAIL("ip netns del %s", ns3);
}

void test_lwt_ip_encap_vrf_ipv6(void)
{
	if (test__start_subtest("egress"))
		lwt_ip_encap(IPV6_ENCAP, EGRESS, "vrf red");

	if (test__start_subtest("ingress"))
		lwt_ip_encap(IPV6_ENCAP, INGRESS, "vrf red");
}

void test_lwt_ip_encap_vrf_ipv4(void)
{
	if (test__start_subtest("egress"))
		lwt_ip_encap(IPV4_ENCAP, EGRESS, "vrf red");

	if (test__start_subtest("ingress"))
		lwt_ip_encap(IPV4_ENCAP, INGRESS, "vrf red");
}

void test_lwt_ip_encap_ipv6(void)
{
	if (test__start_subtest("egress"))
		lwt_ip_encap(IPV6_ENCAP, EGRESS, "");

	if (test__start_subtest("ingress"))
		lwt_ip_encap(IPV6_ENCAP, INGRESS, "");
}

void test_lwt_ip_encap_ipv4(void)
{
	if (test__start_subtest("egress"))
		lwt_ip_encap(IPV4_ENCAP, EGRESS, "");

	if (test__start_subtest("ingress"))
		lwt_ip_encap(IPV4_ENCAP, INGRESS, "");
}
