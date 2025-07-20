// SPDX-License-Identifier: GPL-2.0-only

/* Connects 6 network namespaces through veths.
 * Each NS may have different IPv6 global scope addresses :
 *
 *          NS1            NS2             NS3              NS4               NS5             NS6
 *      lo  veth1 <-> veth2 veth3 <-> veth4 veth5 <-> veth6 lo veth7 <-> veth8 veth9 <-> veth10 lo
 * fb00 ::1  ::12      ::21  ::34      ::43  ::56      ::65     ::78      ::87  ::910     ::109  ::6
 * fd00                                                                                          ::4
 * fc42                                                     ::1
 *
 * All IPv6 packets going to fb00::/16 through NS2 will be encapsulated in a
 * IPv6 header with a Segment Routing Header, with segments :
 *	fd00::1 -> fd00::2 -> fd00::3 -> fd00::4
 *
 * 3 fd00::/16 IPv6 addresses are binded to seg6local End.BPF actions :
 * - fd00::1 : add a TLV, change the flags and apply a End.X action to fc42::1
 * - fd00::2 : remove the TLV, change the flags, add a tag
 * - fd00::3 : apply an End.T action to fd00::4, through routing table 117
 *
 * fd00::4 is a simple Segment Routing node decapsulating the inner IPv6 packet.
 * Each End.BPF action will validate the operations applied on the SRH by the
 * previous BPF program in the chain, otherwise the packet is dropped.
 *
 * An UDP datagram is sent from fb00::1 to fb00::6. The test succeeds if this
 * datagram can be read on NS6 when binding to fb00::6.
 */

#include "network_helpers.h"
#include "test_progs.h"

#define NETNS_BASE "lwt-seg6local-"
#define BPF_FILE "test_lwt_seg6local.bpf.o"

static void cleanup(void)
{
	int ns;

	for (ns = 1; ns < 7; ns++)
		SYS_NOFAIL("ip netns del %s%d", NETNS_BASE, ns);
}

static int setup(void)
{
	int ns;

	for (ns = 1; ns < 7; ns++)
		SYS(fail, "ip netns add %s%d", NETNS_BASE, ns);

	SYS(fail, "ip -n %s6 link set dev lo up", NETNS_BASE);

	for (ns = 1; ns < 6; ns++) {
		int local_id = ns * 2 - 1;
		int peer_id = ns * 2;
		int next_ns = ns + 1;

		SYS(fail, "ip -n %s%d link add veth%d type veth peer name veth%d netns %s%d",
		    NETNS_BASE, ns, local_id, peer_id, NETNS_BASE, next_ns);

		SYS(fail, "ip -n %s%d link set dev veth%d up", NETNS_BASE, ns, local_id);
		SYS(fail, "ip -n %s%d link set dev veth%d up", NETNS_BASE, next_ns, peer_id);

		/* All link scope addresses to veths */
		SYS(fail, "ip -n %s%d -6 addr add fb00::%d%d/16 dev veth%d scope link",
		    NETNS_BASE, ns, local_id, peer_id, local_id);
		SYS(fail, "ip -n %s%d -6 addr add fb00::%d%d/16 dev veth%d scope link",
		    NETNS_BASE, next_ns, peer_id, local_id, peer_id);
	}


	SYS(fail, "ip -n %s5 -6 route add fb00::109 table 117 dev veth9 scope link", NETNS_BASE);

	SYS(fail, "ip -n %s1 -6 addr add fb00::1/16 dev lo", NETNS_BASE);
	SYS(fail, "ip -n %s1 -6 route add fb00::6 dev veth1 via fb00::21", NETNS_BASE);

	SYS(fail, "ip -n %s2 -6 route add fb00::6 encap bpf in obj %s sec encap_srh dev veth2",
	    NETNS_BASE, BPF_FILE);
	SYS(fail, "ip -n %s2 -6 route add fd00::1 dev veth3 via fb00::43 scope link", NETNS_BASE);

	SYS(fail, "ip -n %s3 -6 route add fc42::1 dev veth5 via fb00::65", NETNS_BASE);
	SYS(fail,
	    "ip -n %s3 -6 route add fd00::1 encap seg6local action End.BPF endpoint obj %s sec add_egr_x dev veth4",
	    NETNS_BASE, BPF_FILE);

	SYS(fail,
	    "ip -n %s4 -6 route add fd00::2 encap seg6local action End.BPF endpoint obj %s sec pop_egr dev veth6",
	    NETNS_BASE, BPF_FILE);
	SYS(fail, "ip -n %s4 -6 addr add fc42::1 dev lo", NETNS_BASE);
	SYS(fail, "ip -n %s4 -6 route add fd00::3 dev veth7 via fb00::87", NETNS_BASE);

	SYS(fail, "ip -n %s5 -6 route add fd00::4 table 117 dev veth9 via fb00::109", NETNS_BASE);
	SYS(fail,
	    "ip -n %s5 -6 route add fd00::3 encap seg6local action End.BPF endpoint obj %s sec inspect_t dev veth8",
	    NETNS_BASE, BPF_FILE);

	SYS(fail, "ip -n %s6 -6 addr add fb00::6/16 dev lo", NETNS_BASE);
	SYS(fail, "ip -n %s6 -6 addr add fd00::4/16 dev lo", NETNS_BASE);

	for (ns = 1; ns < 6; ns++)
		SYS(fail, "ip netns exec %s%d sysctl -wq net.ipv6.conf.all.forwarding=1",
		    NETNS_BASE, ns);

	SYS(fail, "ip netns exec %s6 sysctl -wq net.ipv6.conf.all.seg6_enabled=1", NETNS_BASE);
	SYS(fail, "ip netns exec %s6 sysctl -wq net.ipv6.conf.lo.seg6_enabled=1", NETNS_BASE);
	SYS(fail, "ip netns exec %s6 sysctl -wq net.ipv6.conf.veth10.seg6_enabled=1", NETNS_BASE);

	return 0;
fail:
	return -1;
}

#define SERVER_PORT 7330
#define CLIENT_PORT 2121
void test_lwt_seg6local(void)
{
	struct sockaddr_in6 server_addr = {};
	const char *ns1 = NETNS_BASE "1";
	const char *ns6 = NETNS_BASE "6";
	struct nstoken *nstoken = NULL;
	const char *foobar = "foobar";
	ssize_t bytes;
	int sfd, cfd;
	char buf[7];

	if (!ASSERT_OK(setup(), "setup"))
		goto out;

	nstoken = open_netns(ns6);
	if (!ASSERT_OK_PTR(nstoken, "open ns6"))
		goto out;

	sfd = start_server_str(AF_INET6, SOCK_DGRAM, "fb00::6", SERVER_PORT, NULL);
	if (!ASSERT_OK_FD(sfd, "start server"))
		goto close_netns;

	close_netns(nstoken);

	nstoken = open_netns(ns1);
	if (!ASSERT_OK_PTR(nstoken, "open ns1"))
		goto close_server;

	cfd = start_server_str(AF_INET6, SOCK_DGRAM, "fb00::1", CLIENT_PORT, NULL);
	if (!ASSERT_OK_FD(cfd, "start client"))
		goto close_server;

	close_netns(nstoken);
	nstoken = NULL;

	/* Send a packet larger than MTU */
	server_addr.sin6_family = AF_INET6;
	server_addr.sin6_port = htons(SERVER_PORT);
	if (!ASSERT_EQ(inet_pton(AF_INET6, "fb00::6", &server_addr.sin6_addr), 1,
		       "build target addr"))
		goto close_client;

	bytes = sendto(cfd, foobar, sizeof(foobar), 0,
		       (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (!ASSERT_EQ(bytes, sizeof(foobar), "send packet"))
		goto close_client;

	/* Verify we received all expected bytes */
	bytes = read(sfd, buf, sizeof(buf));
	if (!ASSERT_EQ(bytes, sizeof(buf), "receive packet"))
		goto close_client;
	ASSERT_STREQ(buf, foobar, "check udp packet");

close_client:
	close(cfd);
close_server:
	close(sfd);
close_netns:
	close_netns(nstoken);

out:
	cleanup();
}
