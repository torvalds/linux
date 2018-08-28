// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <linux/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool		cfg_do_ipv4;
static bool		cfg_do_ipv6;
static bool		cfg_verbose;
static bool		cfg_overlap;
static unsigned short	cfg_port = 9000;

const struct in_addr addr4 = { .s_addr = __constant_htonl(INADDR_LOOPBACK + 2) };

#define IP4_HLEN	(sizeof(struct iphdr))
#define IP6_HLEN	(sizeof(struct ip6_hdr))
#define UDP_HLEN	(sizeof(struct udphdr))

static int msg_len;
static int max_frag_len;

#define MSG_LEN_MAX	60000	/* Max UDP payload length. */

#define IP4_MF		(1u << 13)  /* IPv4 MF flag. */

static uint8_t udp_payload[MSG_LEN_MAX];
static uint8_t ip_frame[IP_MAXPACKET];
static uint16_t ip_id = 0xabcd;
static int msg_counter;
static int frag_counter;
static unsigned int seed;

/* Receive a UDP packet. Validate it matches udp_payload. */
static void recv_validate_udp(int fd_udp)
{
	ssize_t ret;
	static uint8_t recv_buff[MSG_LEN_MAX];

	ret = recv(fd_udp, recv_buff, msg_len, 0);
	msg_counter++;

	if (cfg_overlap) {
		if (ret != -1)
			error(1, 0, "recv: expected timeout; got %d; seed = %u",
				(int)ret, seed);
		if (errno != ETIMEDOUT && errno != EAGAIN)
			error(1, errno, "recv: expected timeout: %d; seed = %u",
				 errno, seed);
		return;  /* OK */
	}

	if (ret == -1)
		error(1, errno, "recv: msg_len = %d max_frag_len = %d",
			msg_len, max_frag_len);
	if (ret != msg_len)
		error(1, 0, "recv: wrong size: %d vs %d", (int)ret, msg_len);
	if (memcmp(udp_payload, recv_buff, msg_len))
		error(1, 0, "recv: wrong data");
}

static uint32_t raw_checksum(uint8_t *buf, int len, uint32_t sum)
{
	int i;

	for (i = 0; i < (len & ~1U); i += 2) {
		sum += (u_int16_t)ntohs(*((u_int16_t *)(buf + i)));
		if (sum > 0xffff)
			sum -= 0xffff;
	}

	if (i < len) {
		sum += buf[i] << 8;
		if (sum > 0xffff)
			sum -= 0xffff;
	}

	return sum;
}

static uint16_t udp_checksum(struct ip *iphdr, struct udphdr *udphdr)
{
	uint32_t sum = 0;

	sum = raw_checksum((uint8_t *)&iphdr->ip_src, 2 * sizeof(iphdr->ip_src),
				IPPROTO_UDP + (uint32_t)(UDP_HLEN + msg_len));
	sum = raw_checksum((uint8_t *)udp_payload, msg_len, sum);
	sum = raw_checksum((uint8_t *)udphdr, UDP_HLEN, sum);
	return htons(0xffff & ~sum);
}

static void send_fragment(int fd_raw, struct sockaddr *addr, socklen_t alen,
				struct ip *iphdr, int offset)
{
	int frag_len;
	int res;

	if (msg_len - offset <= max_frag_len) {
		/* This is the last fragment. */
		frag_len = IP4_HLEN + msg_len - offset;
		iphdr->ip_off = htons((offset + UDP_HLEN) / 8);
	} else {
		frag_len = IP4_HLEN + max_frag_len;
		iphdr->ip_off = htons((offset + UDP_HLEN) / 8 | IP4_MF);
	}
	iphdr->ip_len = htons(frag_len);
	memcpy(ip_frame + IP4_HLEN, udp_payload + offset,
		 frag_len - IP4_HLEN);

	res = sendto(fd_raw, ip_frame, frag_len, 0, addr, alen);
	if (res < 0)
		error(1, errno, "send_fragment");
	if (res != frag_len)
		error(1, 0, "send_fragment: %d vs %d", res, frag_len);

	frag_counter++;
}

static void send_udp_frags_v4(int fd_raw, struct sockaddr *addr, socklen_t alen)
{
	struct ip *iphdr = (struct ip *)ip_frame;
	struct udphdr udphdr;
	int res;
	int offset;
	int frag_len;

	/* Send the UDP datagram using raw IP fragments: the 0th fragment
	 * has the UDP header; other fragments are pieces of udp_payload
	 * split in chunks of frag_len size.
	 *
	 * Odd fragments (1st, 3rd, 5th, etc.) are sent out first, then
	 * even fragments (0th, 2nd, etc.) are sent out.
	 */
	memset(iphdr, 0, sizeof(*iphdr));
	iphdr->ip_hl = 5;
	iphdr->ip_v = 4;
	iphdr->ip_tos = 0;
	iphdr->ip_id = htons(ip_id++);
	iphdr->ip_ttl = 0x40;
	iphdr->ip_p = IPPROTO_UDP;
	iphdr->ip_src.s_addr = htonl(INADDR_LOOPBACK);
	iphdr->ip_dst = addr4;
	iphdr->ip_sum = 0;

	/* Odd fragments. */
	offset = 0;
	while (offset < msg_len) {
		send_fragment(fd_raw, addr, alen, iphdr, offset);
		offset += 2 * max_frag_len;
	}

	if (cfg_overlap) {
		/* Send an extra random fragment. */
		offset = rand() % (UDP_HLEN + msg_len - 1);
		/* sendto() returns EINVAL if offset + frag_len is too small. */
		frag_len = IP4_HLEN + UDP_HLEN + rand() % 256;
		iphdr->ip_off = htons(offset / 8 | IP4_MF);
		iphdr->ip_len = htons(frag_len);
		res = sendto(fd_raw, ip_frame, frag_len, 0, addr, alen);
		if (res < 0)
			error(1, errno, "sendto overlap");
		if (res != frag_len)
			error(1, 0, "sendto overlap: %d vs %d", (int)res, frag_len);
		frag_counter++;
	}

	/* Zeroth fragment (UDP header). */
	frag_len = IP4_HLEN + UDP_HLEN;
	iphdr->ip_len = htons(frag_len);
	iphdr->ip_off = htons(IP4_MF);

	udphdr.source = htons(cfg_port + 1);
	udphdr.dest = htons(cfg_port);
	udphdr.len = htons(UDP_HLEN + msg_len);
	udphdr.check = 0;
	udphdr.check = udp_checksum(iphdr, &udphdr);

	memcpy(ip_frame + IP4_HLEN, &udphdr, UDP_HLEN);
	res = sendto(fd_raw, ip_frame, frag_len, 0, addr, alen);
	if (res < 0)
		error(1, errno, "sendto UDP header");
	if (res != frag_len)
		error(1, 0, "sendto UDP header: %d vs %d", (int)res, frag_len);
	frag_counter++;

	/* Even fragments. */
	offset = max_frag_len;
	while (offset < msg_len) {
		send_fragment(fd_raw, addr, alen, iphdr, offset);
		offset += 2 * max_frag_len;
	}
}

static void run_test(struct sockaddr *addr, socklen_t alen)
{
	int fd_tx_udp, fd_tx_raw, fd_rx_udp;
	struct timeval tv = { .tv_sec = 0, .tv_usec = 10 * 1000 };
	int idx;

	/* Initialize the payload. */
	for (idx = 0; idx < MSG_LEN_MAX; ++idx)
		udp_payload[idx] = idx % 256;

	/* Open sockets. */
	fd_tx_udp = socket(addr->sa_family, SOCK_DGRAM, 0);
	if (fd_tx_udp == -1)
		error(1, errno, "socket tx_udp");

	fd_tx_raw = socket(addr->sa_family, SOCK_RAW, IPPROTO_RAW);
	if (fd_tx_raw == -1)
		error(1, errno, "socket tx_raw");

	fd_rx_udp = socket(addr->sa_family, SOCK_DGRAM, 0);
	if (fd_rx_udp == -1)
		error(1, errno, "socket rx_udp");
	if (bind(fd_rx_udp, addr, alen))
		error(1, errno, "bind");
	/* Fail fast. */
	if (setsockopt(fd_rx_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
		error(1, errno, "setsockopt rcv timeout");

	for (msg_len = 1; msg_len < MSG_LEN_MAX; msg_len += (rand() % 4096)) {
		if (cfg_verbose)
			printf("msg_len: %d\n", msg_len);
		max_frag_len = addr->sa_family == AF_INET ? 8 : 1280;
		for (; max_frag_len < 1500 && max_frag_len <= msg_len;
				max_frag_len += 8) {
			send_udp_frags_v4(fd_tx_raw, addr, alen);
			recv_validate_udp(fd_rx_udp);
		}
	}

	/* Cleanup. */
	if (close(fd_tx_raw))
		error(1, errno, "close tx_raw");
	if (close(fd_tx_udp))
		error(1, errno, "close tx_udp");
	if (close(fd_rx_udp))
		error(1, errno, "close rx_udp");

	if (cfg_verbose)
		printf("processed %d messages, %d fragments\n",
			msg_counter, frag_counter);

	fprintf(stderr, "PASS\n");
}


static void run_test_v4(void)
{
	struct sockaddr_in addr = {0};

	addr.sin_family = AF_INET;
	addr.sin_port = htons(cfg_port);
	addr.sin_addr = addr4;

	run_test((void *)&addr, sizeof(addr));
}

static void run_test_v6(void)
{
	fprintf(stderr, "NOT IMPL.\n");
	exit(1);
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "46ov")) != -1) {
		switch (c) {
		case '4':
			cfg_do_ipv4 = true;
			break;
		case '6':
			cfg_do_ipv6 = true;
			break;
		case 'o':
			cfg_overlap = true;
			break;
		case 'v':
			cfg_verbose = true;
			break;
		default:
			error(1, 0, "%s: parse error", argv[0]);
		}
	}
}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);
	seed = time(NULL);
	srand(seed);

	if (cfg_do_ipv4)
		run_test_v4();
	if (cfg_do_ipv6)
		run_test_v6();

	return 0;
}
