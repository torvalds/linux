// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/if_packet.h>
#include <linux/if_vlan.h>
#include <linux/virtio_net.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "psock_lib.h"

static bool	cfg_use_bind;
static bool	cfg_use_csum_off;
static bool	cfg_use_csum_off_bad;
static bool	cfg_use_dgram;
static bool	cfg_use_gso;
static bool	cfg_use_qdisc_bypass;
static bool	cfg_use_vlan;
static bool	cfg_use_vnet;

static char	*cfg_ifname = "lo";
static int	cfg_mtu	= 1500;
static int	cfg_payload_len = DATA_LEN;
static int	cfg_truncate_len = INT_MAX;
static uint16_t	cfg_port = 8000;

/* test sending up to max mtu + 1 */
#define TEST_SZ	(sizeof(struct virtio_net_hdr) + ETH_HLEN + ETH_MAX_MTU + 1)

static char tbuf[TEST_SZ], rbuf[TEST_SZ];

static unsigned long add_csum_hword(const uint16_t *start, int num_u16)
{
	unsigned long sum = 0;
	int i;

	for (i = 0; i < num_u16; i++)
		sum += start[i];

	return sum;
}

static uint16_t build_ip_csum(const uint16_t *start, int num_u16,
			      unsigned long sum)
{
	sum += add_csum_hword(start, num_u16);

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

static int build_vnet_header(void *header)
{
	struct virtio_net_hdr *vh = header;

	vh->hdr_len = ETH_HLEN + sizeof(struct iphdr) + sizeof(struct udphdr);

	if (cfg_use_csum_off) {
		vh->flags |= VIRTIO_NET_HDR_F_NEEDS_CSUM;
		vh->csum_start = ETH_HLEN + sizeof(struct iphdr);
		vh->csum_offset = __builtin_offsetof(struct udphdr, check);

		/* position check field exactly one byte beyond end of packet */
		if (cfg_use_csum_off_bad)
			vh->csum_start += sizeof(struct udphdr) + cfg_payload_len -
					  vh->csum_offset - 1;
	}

	if (cfg_use_gso) {
		vh->gso_type = VIRTIO_NET_HDR_GSO_UDP;
		vh->gso_size = cfg_mtu - sizeof(struct iphdr);
	}

	return sizeof(*vh);
}

static int build_eth_header(void *header)
{
	struct ethhdr *eth = header;

	if (cfg_use_vlan) {
		uint16_t *tag = header + ETH_HLEN;

		eth->h_proto = htons(ETH_P_8021Q);
		tag[1] = htons(ETH_P_IP);
		return ETH_HLEN + 4;
	}

	eth->h_proto = htons(ETH_P_IP);
	return ETH_HLEN;
}

static int build_ipv4_header(void *header, int payload_len)
{
	struct iphdr *iph = header;

	iph->ihl = 5;
	iph->version = 4;
	iph->ttl = 8;
	iph->tot_len = htons(sizeof(*iph) + sizeof(struct udphdr) + payload_len);
	iph->id = htons(1337);
	iph->protocol = IPPROTO_UDP;
	iph->saddr = htonl((172 << 24) | (17 << 16) | 2);
	iph->daddr = htonl((172 << 24) | (17 << 16) | 1);
	iph->check = build_ip_csum((void *) iph, iph->ihl << 1, 0);

	return iph->ihl << 2;
}

static int build_udp_header(void *header, int payload_len)
{
	const int alen = sizeof(uint32_t);
	struct udphdr *udph = header;
	int len = sizeof(*udph) + payload_len;

	udph->source = htons(9);
	udph->dest = htons(cfg_port);
	udph->len = htons(len);

	if (cfg_use_csum_off)
		udph->check = build_ip_csum(header - (2 * alen), alen,
					    htons(IPPROTO_UDP) + udph->len);
	else
		udph->check = 0;

	return sizeof(*udph);
}

static int build_packet(int payload_len)
{
	int off = 0;

	off += build_vnet_header(tbuf);
	off += build_eth_header(tbuf + off);
	off += build_ipv4_header(tbuf + off, payload_len);
	off += build_udp_header(tbuf + off, payload_len);

	if (off + payload_len > sizeof(tbuf))
		error(1, 0, "payload length exceeds max");

	memset(tbuf + off, DATA_CHAR, payload_len);

	return off + payload_len;
}

static void do_bind(int fd)
{
	struct sockaddr_ll laddr = {0};

	laddr.sll_family = AF_PACKET;
	laddr.sll_protocol = htons(ETH_P_IP);
	laddr.sll_ifindex = if_nametoindex(cfg_ifname);
	if (!laddr.sll_ifindex)
		error(1, errno, "if_nametoindex");

	if (bind(fd, (void *)&laddr, sizeof(laddr)))
		error(1, errno, "bind");
}

static void do_send(int fd, char *buf, int len)
{
	int ret;

	if (!cfg_use_vnet) {
		buf += sizeof(struct virtio_net_hdr);
		len -= sizeof(struct virtio_net_hdr);
	}
	if (cfg_use_dgram) {
		buf += ETH_HLEN;
		len -= ETH_HLEN;
	}

	if (cfg_use_bind) {
		ret = write(fd, buf, len);
	} else {
		struct sockaddr_ll laddr = {0};

		laddr.sll_protocol = htons(ETH_P_IP);
		laddr.sll_ifindex = if_nametoindex(cfg_ifname);
		if (!laddr.sll_ifindex)
			error(1, errno, "if_nametoindex");

		ret = sendto(fd, buf, len, 0, (void *)&laddr, sizeof(laddr));
	}

	if (ret == -1)
		error(1, errno, "write");
	if (ret != len)
		error(1, 0, "write: %u %u", ret, len);

	fprintf(stderr, "tx: %u\n", ret);
}

static int do_tx(void)
{
	const int one = 1;
	int fd, len;

	fd = socket(PF_PACKET, cfg_use_dgram ? SOCK_DGRAM : SOCK_RAW, 0);
	if (fd == -1)
		error(1, errno, "socket t");

	if (cfg_use_bind)
		do_bind(fd);

	if (cfg_use_qdisc_bypass &&
	    setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one)))
		error(1, errno, "setsockopt qdisc bypass");

	if (cfg_use_vnet &&
	    setsockopt(fd, SOL_PACKET, PACKET_VNET_HDR, &one, sizeof(one)))
		error(1, errno, "setsockopt vnet");

	len = build_packet(cfg_payload_len);

	if (cfg_truncate_len < len)
		len = cfg_truncate_len;

	do_send(fd, tbuf, len);

	if (close(fd))
		error(1, errno, "close t");

	return len;
}

static int setup_rx(void)
{
	struct timeval tv = { .tv_usec = 100 * 1000 };
	struct sockaddr_in raddr = {0};
	int fd;

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket r");

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
		error(1, errno, "setsockopt rcv timeout");

	raddr.sin_family = AF_INET;
	raddr.sin_port = htons(cfg_port);
	raddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(fd, (void *)&raddr, sizeof(raddr)))
		error(1, errno, "bind r");

	return fd;
}

static void do_rx(int fd, int expected_len, char *expected)
{
	int ret;

	ret = recv(fd, rbuf, sizeof(rbuf), 0);
	if (ret == -1)
		error(1, errno, "recv");
	if (ret != expected_len)
		error(1, 0, "recv: %u != %u", ret, expected_len);

	if (memcmp(rbuf, expected, ret))
		error(1, 0, "recv: data mismatch");

	fprintf(stderr, "rx: %u\n", ret);
}

static int setup_sniffer(void)
{
	struct timeval tv = { .tv_usec = 100 * 1000 };
	int fd;

	fd = socket(PF_PACKET, SOCK_RAW, 0);
	if (fd == -1)
		error(1, errno, "socket p");

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
		error(1, errno, "setsockopt rcv timeout");

	pair_udp_setfilter(fd);
	do_bind(fd);

	return fd;
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "bcCdgl:qt:vV")) != -1) {
		switch (c) {
		case 'b':
			cfg_use_bind = true;
			break;
		case 'c':
			cfg_use_csum_off = true;
			break;
		case 'C':
			cfg_use_csum_off_bad = true;
			break;
		case 'd':
			cfg_use_dgram = true;
			break;
		case 'g':
			cfg_use_gso = true;
			break;
		case 'l':
			cfg_payload_len = strtoul(optarg, NULL, 0);
			break;
		case 'q':
			cfg_use_qdisc_bypass = true;
			break;
		case 't':
			cfg_truncate_len = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			cfg_use_vnet = true;
			break;
		case 'V':
			cfg_use_vlan = true;
			break;
		default:
			error(1, 0, "%s: parse error", argv[0]);
		}
	}

	if (cfg_use_vlan && cfg_use_dgram)
		error(1, 0, "option vlan (-V) conflicts with dgram (-d)");

	if (cfg_use_csum_off && !cfg_use_vnet)
		error(1, 0, "option csum offload (-c) requires vnet (-v)");

	if (cfg_use_csum_off_bad && !cfg_use_csum_off)
		error(1, 0, "option csum bad (-C) requires csum offload (-c)");

	if (cfg_use_gso && !cfg_use_csum_off)
		error(1, 0, "option gso (-g) requires csum offload (-c)");
}

static void run_test(void)
{
	int fdr, fds, total_len;

	fdr = setup_rx();
	fds = setup_sniffer();

	total_len = do_tx();

	/* BPF filter accepts only this length, vlan changes MAC */
	if (cfg_payload_len == DATA_LEN && !cfg_use_vlan)
		do_rx(fds, total_len - sizeof(struct virtio_net_hdr),
		      tbuf + sizeof(struct virtio_net_hdr));

	do_rx(fdr, cfg_payload_len, tbuf + total_len - cfg_payload_len);

	if (close(fds))
		error(1, errno, "close s");
	if (close(fdr))
		error(1, errno, "close r");
}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);

	if (system("ip link set dev lo mtu 1500"))
		error(1, errno, "ip link set mtu");
	if (system("ip addr add dev lo 172.17.0.1/24"))
		error(1, errno, "ip addr add");

	run_test();

	fprintf(stderr, "OK\n\n");
	return 0;
}
