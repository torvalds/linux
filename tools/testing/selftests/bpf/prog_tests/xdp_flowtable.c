// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include <bpf/btf.h>
#include <linux/if_link.h>
#include <netinet/udp.h>
#include <net/if.h>
#include <unistd.h>

#include "xdp_flowtable.skel.h"

#define TX_NETNS_NAME	"ns0"
#define RX_NETNS_NAME	"ns1"

#define TX_NAME		"v0"
#define FORWARD_NAME	"v1"
#define RX_NAME		"d0"

#define TX_MAC		"00:00:00:00:00:01"
#define FORWARD_MAC	"00:00:00:00:00:02"
#define RX_MAC		"00:00:00:00:00:03"
#define DST_MAC		"00:00:00:00:00:04"

#define TX_ADDR		"10.0.0.1"
#define FORWARD_ADDR	"10.0.0.2"
#define RX_ADDR		"20.0.0.1"
#define DST_ADDR	"20.0.0.2"

#define PREFIX_LEN	"8"
#define N_PACKETS	10
#define UDP_PORT	12345
#define UDP_PORT_STR	"12345"

static int send_udp_traffic(void)
{
	struct sockaddr_storage addr;
	int i, sock;

	if (make_sockaddr(AF_INET, DST_ADDR, UDP_PORT, &addr, NULL))
		return -EINVAL;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	for (i = 0; i < N_PACKETS; i++) {
		unsigned char buf[] = { 0xaa, 0xbb, 0xcc };
		int n;

		n = sendto(sock, buf, sizeof(buf), MSG_NOSIGNAL | MSG_CONFIRM,
			   (struct sockaddr *)&addr, sizeof(addr));
		if (n != sizeof(buf)) {
			close(sock);
			return -EINVAL;
		}

		usleep(50000); /* 50ms */
	}
	close(sock);

	return 0;
}

void test_xdp_flowtable(void)
{
	struct xdp_flowtable *skel = NULL;
	struct nstoken *tok = NULL;
	int iifindex, stats_fd;
	__u32 value, key = 0;
	struct bpf_link *link;

	if (SYS_NOFAIL("nft -v")) {
		fprintf(stdout, "Missing required nft tool\n");
		test__skip();
		return;
	}

	SYS(out, "ip netns add " TX_NETNS_NAME);
	SYS(out, "ip netns add " RX_NETNS_NAME);

	tok = open_netns(RX_NETNS_NAME);
	if (!ASSERT_OK_PTR(tok, "setns"))
		goto out;

	SYS(out, "sysctl -qw net.ipv4.conf.all.forwarding=1");

	SYS(out, "ip link add " TX_NAME " type veth peer " FORWARD_NAME);
	SYS(out, "ip link set " TX_NAME " netns " TX_NETNS_NAME);
	SYS(out, "ip link set dev " FORWARD_NAME " address " FORWARD_MAC);
	SYS(out,
	    "ip addr add " FORWARD_ADDR "/" PREFIX_LEN " dev " FORWARD_NAME);
	SYS(out, "ip link set dev " FORWARD_NAME " up");

	SYS(out, "ip link add " RX_NAME " type dummy");
	SYS(out, "ip link set dev " RX_NAME " address " RX_MAC);
	SYS(out, "ip addr add " RX_ADDR "/" PREFIX_LEN " dev " RX_NAME);
	SYS(out, "ip link set dev " RX_NAME " up");

	/* configure the flowtable */
	SYS(out, "nft add table ip filter");
	SYS(out,
	    "nft add flowtable ip filter f { hook ingress priority 0\\; "
	    "devices = { " FORWARD_NAME ", " RX_NAME " }\\; }");
	SYS(out,
	    "nft add chain ip filter forward "
	    "{ type filter hook forward priority 0\\; }");
	SYS(out,
	    "nft add rule ip filter forward ip protocol udp th dport "
	    UDP_PORT_STR " flow add @f");

	/* Avoid ARP calls */
	SYS(out,
	    "ip -4 neigh add " DST_ADDR " lladdr " DST_MAC " dev " RX_NAME);

	close_netns(tok);
	tok = open_netns(TX_NETNS_NAME);
	if (!ASSERT_OK_PTR(tok, "setns"))
		goto out;

	SYS(out, "ip addr add " TX_ADDR "/" PREFIX_LEN " dev " TX_NAME);
	SYS(out, "ip link set dev " TX_NAME " address " TX_MAC);
	SYS(out, "ip link set dev " TX_NAME " up");
	SYS(out, "ip route add default via " FORWARD_ADDR);

	close_netns(tok);
	tok = open_netns(RX_NETNS_NAME);
	if (!ASSERT_OK_PTR(tok, "setns"))
		goto out;

	iifindex = if_nametoindex(FORWARD_NAME);
	if (!ASSERT_NEQ(iifindex, 0, "iifindex"))
		goto out;

	skel = xdp_flowtable__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		goto out;

	link = bpf_program__attach_xdp(skel->progs.xdp_flowtable_do_lookup,
				       iifindex);
	if (!ASSERT_OK_PTR(link, "prog_attach"))
		goto out;

	close_netns(tok);
	tok = open_netns(TX_NETNS_NAME);
	if (!ASSERT_OK_PTR(tok, "setns"))
		goto out;

	if (!ASSERT_OK(send_udp_traffic(), "send udp"))
		goto out;

	close_netns(tok);
	tok = open_netns(RX_NETNS_NAME);
	if (!ASSERT_OK_PTR(tok, "setns"))
		goto out;

	stats_fd = bpf_map__fd(skel->maps.stats);
	if (!ASSERT_OK(bpf_map_lookup_elem(stats_fd, &key, &value),
		       "bpf_map_update_elem stats"))
		goto out;

	ASSERT_GE(value, N_PACKETS - 2, "bpf_xdp_flow_lookup failed");
out:
	xdp_flowtable__destroy(skel);
	if (tok)
		close_netns(tok);
	SYS_NOFAIL("ip netns del " TX_NETNS_NAME);
	SYS_NOFAIL("ip netns del " RX_NETNS_NAME);
}
