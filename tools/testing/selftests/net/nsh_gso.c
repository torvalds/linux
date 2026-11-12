// SPDX-License-Identifier: GPL-2.0
/* Send one TCPv4 GSO packet encapsulated in a stack of NSH headers,
 * through a packet socket with PACKET_VNET_HDR.
 *
 * Usage: nsh_gso <ifname> <number of NSH headers>
 *
 * The packet is made of 4 segments. The send() result is not checked:
 * the caller looks at the device counters.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/virtio_net.h>
#include <net/if.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef ETH_P_NSH
#define ETH_P_NSH	0x894F
#endif

#define NSH_HLEN	8	/* base header + service path header */
#define NSH_MD_TYPE2	2	/* no mandatory context header */
#define NSH_NP_IPV4	1
#define NSH_NP_NSH	4

#define GSO_SIZE	500
#define NR_SEGS		4

struct nsh_base {
	__be16	ver_flags_ttl_len;
	__u8	mdtype;
	__u8	np;
	__be32	path_hdr;
};

int main(int argc, char **argv)
{
	unsigned int depth, hlen, len, i;
	struct sockaddr_ll addr = {};
	struct virtio_net_hdr *vnet;
	struct nsh_base *nsh;
	struct ethhdr *eth;
	struct tcphdr *tcp;
	int fd, one = 1;
	struct iphdr *ip;
	char *buf;

	if (argc != 3)
		error(1, 0, "usage: %s <ifname> <depth>", argv[0]);

	depth = strtoul(argv[2], NULL, 0);
	hlen = sizeof(*eth) + depth * NSH_HLEN + sizeof(*ip) + sizeof(*tcp);
	if (!depth || hlen > UINT16_MAX)
		error(1, 0, "invalid depth %u", depth);
	len = sizeof(*vnet) + hlen + NR_SEGS * GSO_SIZE;

	buf = calloc(1, len);
	if (!buf)
		error(1, errno, "calloc");

	vnet = (void *)buf;
	vnet->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
	vnet->gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
	vnet->hdr_len = hlen;
	vnet->gso_size = GSO_SIZE;
	vnet->csum_start = hlen - sizeof(*tcp);
	vnet->csum_offset = __builtin_offsetof(struct tcphdr, check);

	eth = (void *)(vnet + 1);
	memset(eth->h_dest, 0xff, ETH_ALEN);
	eth->h_source[0] = 0x02;
	eth->h_proto = htons(ETH_P_NSH);

	nsh = (void *)(eth + 1);
	for (i = 0; i < depth; i++, nsh++) {
		/* version 0, ttl 63, length in 4 byte words */
		nsh->ver_flags_ttl_len = htons(63 << 6 | NSH_HLEN / 4);
		nsh->mdtype = NSH_MD_TYPE2;
		nsh->np = i == depth - 1 ? NSH_NP_IPV4 : NSH_NP_NSH;
		nsh->path_hdr = htonl(1 << 8 | 255);
	}

	ip = (void *)nsh;
	ip->version = 4;
	ip->ihl = 5;
	ip->ttl = 64;
	ip->protocol = IPPROTO_TCP;
	ip->saddr = htonl(0xc0000201);	/* 192.0.2.1 */
	ip->daddr = htonl(0xc0000202);	/* 192.0.2.2 */

	tcp = (void *)(ip + 1);
	tcp->source = htons(1234);
	tcp->dest = htons(5678);
	tcp->doff = 5;
	tcp->ack = 1;

	fd = socket(AF_PACKET, SOCK_RAW, 0);
	if (fd < 0)
		error(1, errno, "socket");
	if (setsockopt(fd, SOL_PACKET, PACKET_VNET_HDR, &one, sizeof(one)))
		error(1, errno, "setsockopt PACKET_VNET_HDR");

	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(ETH_P_NSH);
	addr.sll_ifindex = if_nametoindex(argv[1]);
	if (!addr.sll_ifindex)
		error(1, errno, "if_nametoindex %s", argv[1]);

	/* A packet dropped at segmentation time is reported as an error,
	 * this is expected for deep stacks.
	 */
	sendto(fd, buf, len, 0, (void *)&addr, sizeof(addr));

	close(fd);
	free(buf);
	return 0;
}
