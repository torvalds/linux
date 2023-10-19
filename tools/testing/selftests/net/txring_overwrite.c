// SPDX-License-Identifier: GPL-2.0

/*
 * Verify that consecutive sends over packet tx_ring are mirrored
 * with their original content intact.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const int eth_off = TPACKET_HDRLEN - sizeof(struct sockaddr_ll);
const int cfg_frame_size = 1000;

static void build_packet(void *buffer, size_t blen, char payload_char)
{
	struct udphdr *udph;
	struct ethhdr *eth;
	struct iphdr *iph;
	size_t off = 0;

	memset(buffer, 0, blen);

	eth = buffer;
	eth->h_proto = htons(ETH_P_IP);

	off += sizeof(*eth);
	iph = buffer + off;
	iph->ttl	= 8;
	iph->ihl	= 5;
	iph->version	= 4;
	iph->saddr	= htonl(INADDR_LOOPBACK);
	iph->daddr	= htonl(INADDR_LOOPBACK + 1);
	iph->protocol	= IPPROTO_UDP;
	iph->tot_len	= htons(blen - off);
	iph->check	= 0;

	off += sizeof(*iph);
	udph = buffer + off;
	udph->dest	= htons(8000);
	udph->source	= htons(8001);
	udph->len	= htons(blen - off);
	udph->check	= 0;

	off += sizeof(*udph);
	memset(buffer + off, payload_char, blen - off);
}

static int setup_rx(void)
{
	int fdr;

	fdr = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	if (fdr == -1)
		error(1, errno, "socket r");

	return fdr;
}

static int setup_tx(char **ring)
{
	struct sockaddr_ll laddr = {};
	struct tpacket_req req = {};
	int fdt;

	fdt = socket(PF_PACKET, SOCK_RAW, 0);
	if (fdt == -1)
		error(1, errno, "socket t");

	laddr.sll_family = AF_PACKET;
	laddr.sll_protocol = htons(0);
	laddr.sll_ifindex = if_nametoindex("lo");
	if (!laddr.sll_ifindex)
		error(1, errno, "if_nametoindex");

	if (bind(fdt, (void *)&laddr, sizeof(laddr)))
		error(1, errno, "bind fdt");

	req.tp_block_size = getpagesize();
	req.tp_block_nr   = 1;
	req.tp_frame_size = getpagesize();
	req.tp_frame_nr   = 1;

	if (setsockopt(fdt, SOL_PACKET, PACKET_TX_RING,
		       (void *)&req, sizeof(req)))
		error(1, errno, "setsockopt ring");

	*ring = mmap(0, req.tp_block_size * req.tp_block_nr,
		     PROT_READ | PROT_WRITE, MAP_SHARED, fdt, 0);
	if (*ring == MAP_FAILED)
		error(1, errno, "mmap");

	return fdt;
}

static void send_pkt(int fdt, void *slot, char payload_char)
{
	struct tpacket_hdr *header = slot;
	int ret;

	while (header->tp_status != TP_STATUS_AVAILABLE)
		usleep(1000);

	build_packet(slot + eth_off, cfg_frame_size, payload_char);

	header->tp_len = cfg_frame_size;
	header->tp_status = TP_STATUS_SEND_REQUEST;

	ret = sendto(fdt, NULL, 0, 0, NULL, 0);
	if (ret == -1)
		error(1, errno, "kick tx");
}

static int read_verify_pkt(int fdr, char payload_char)
{
	char buf[100];
	int ret;

	ret = read(fdr, buf, sizeof(buf));
	if (ret != sizeof(buf))
		error(1, errno, "read");

	if (buf[60] != payload_char) {
		printf("wrong pattern: 0x%x != 0x%x\n", buf[60], payload_char);
		return 1;
	}

	printf("read: %c (0x%x)\n", buf[60], buf[60]);
	return 0;
}

int main(int argc, char **argv)
{
	const char payload_patterns[] = "ab";
	char *ring;
	int fdr, fdt, ret = 0;

	fdr = setup_rx();
	fdt = setup_tx(&ring);

	send_pkt(fdt, ring, payload_patterns[0]);
	send_pkt(fdt, ring, payload_patterns[1]);

	ret |= read_verify_pkt(fdr, payload_patterns[0]);
	ret |= read_verify_pkt(fdr, payload_patterns[1]);

	if (close(fdt))
		error(1, errno, "close t");
	if (close(fdr))
		error(1, errno, "close r");

	return ret;
}
