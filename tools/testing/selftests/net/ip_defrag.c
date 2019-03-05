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
const struct in6_addr addr6 = IN6ADDR_LOOPBACK_INIT;

#define IP4_HLEN	(sizeof(struct iphdr))
#define IP6_HLEN	(sizeof(struct ip6_hdr))
#define UDP_HLEN	(sizeof(struct udphdr))

/* IPv6 fragment header lenth. */
#define FRAG_HLEN	8

static int payload_len;
static int max_frag_len;

#define MSG_LEN_MAX	60000	/* Max UDP payload length. */

#define IP4_MF		(1u << 13)  /* IPv4 MF flag. */
#define IP6_MF		(1)  /* IPv6 MF flag. */

#define CSUM_MANGLED_0 (0xffff)

static uint8_t udp_payload[MSG_LEN_MAX];
static uint8_t ip_frame[IP_MAXPACKET];
static uint32_t ip_id = 0xabcd;
static int msg_counter;
static int frag_counter;
static unsigned int seed;

/* Receive a UDP packet. Validate it matches udp_payload. */
static void recv_validate_udp(int fd_udp)
{
	ssize_t ret;
	static uint8_t recv_buff[MSG_LEN_MAX];

	ret = recv(fd_udp, recv_buff, payload_len, 0);
	msg_counter++;

	if (cfg_overlap) {
		if (ret != -1)
			error(1, 0, "recv: expected timeout; got %d",
				(int)ret);
		if (errno != ETIMEDOUT && errno != EAGAIN)
			error(1, errno, "recv: expected timeout: %d",
				 errno);
		return;  /* OK */
	}

	if (ret == -1)
		error(1, errno, "recv: payload_len = %d max_frag_len = %d",
			payload_len, max_frag_len);
	if (ret != payload_len)
		error(1, 0, "recv: wrong size: %d vs %d", (int)ret, payload_len);
	if (memcmp(udp_payload, recv_buff, payload_len))
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
	uint16_t res;

	sum = raw_checksum((uint8_t *)&iphdr->ip_src, 2 * sizeof(iphdr->ip_src),
				IPPROTO_UDP + (uint32_t)(UDP_HLEN + payload_len));
	sum = raw_checksum((uint8_t *)udphdr, UDP_HLEN, sum);
	sum = raw_checksum((uint8_t *)udp_payload, payload_len, sum);
	res = 0xffff & ~sum;
	if (res)
		return htons(res);
	else
		return CSUM_MANGLED_0;
}

static uint16_t udp6_checksum(struct ip6_hdr *iphdr, struct udphdr *udphdr)
{
	uint32_t sum = 0;
	uint16_t res;

	sum = raw_checksum((uint8_t *)&iphdr->ip6_src, 2 * sizeof(iphdr->ip6_src),
				IPPROTO_UDP);
	sum = raw_checksum((uint8_t *)&udphdr->len, sizeof(udphdr->len), sum);
	sum = raw_checksum((uint8_t *)udphdr, UDP_HLEN, sum);
	sum = raw_checksum((uint8_t *)udp_payload, payload_len, sum);
	res = 0xffff & ~sum;
	if (res)
		return htons(res);
	else
		return CSUM_MANGLED_0;
}

static void send_fragment(int fd_raw, struct sockaddr *addr, socklen_t alen,
				int offset, bool ipv6)
{
	int frag_len;
	int res;
	int payload_offset = offset > 0 ? offset - UDP_HLEN : 0;
	uint8_t *frag_start = ipv6 ? ip_frame + IP6_HLEN + FRAG_HLEN :
					ip_frame + IP4_HLEN;

	if (offset == 0) {
		struct udphdr udphdr;
		udphdr.source = htons(cfg_port + 1);
		udphdr.dest = htons(cfg_port);
		udphdr.len = htons(UDP_HLEN + payload_len);
		udphdr.check = 0;
		if (ipv6)
			udphdr.check = udp6_checksum((struct ip6_hdr *)ip_frame, &udphdr);
		else
			udphdr.check = udp_checksum((struct ip *)ip_frame, &udphdr);
		memcpy(frag_start, &udphdr, UDP_HLEN);
	}

	if (ipv6) {
		struct ip6_hdr *ip6hdr = (struct ip6_hdr *)ip_frame;
		struct ip6_frag *fraghdr = (struct ip6_frag *)(ip_frame + IP6_HLEN);
		if (payload_len - payload_offset <= max_frag_len && offset > 0) {
			/* This is the last fragment. */
			frag_len = FRAG_HLEN + payload_len - payload_offset;
			fraghdr->ip6f_offlg = htons(offset);
		} else {
			frag_len = FRAG_HLEN + max_frag_len;
			fraghdr->ip6f_offlg = htons(offset | IP6_MF);
		}
		ip6hdr->ip6_plen = htons(frag_len);
		if (offset == 0)
			memcpy(frag_start + UDP_HLEN, udp_payload,
				frag_len - FRAG_HLEN - UDP_HLEN);
		else
			memcpy(frag_start, udp_payload + payload_offset,
				frag_len - FRAG_HLEN);
		frag_len += IP6_HLEN;
	} else {
		struct ip *iphdr = (struct ip *)ip_frame;
		if (payload_len - payload_offset <= max_frag_len && offset > 0) {
			/* This is the last fragment. */
			frag_len = IP4_HLEN + payload_len - payload_offset;
			iphdr->ip_off = htons(offset / 8);
		} else {
			frag_len = IP4_HLEN + max_frag_len;
			iphdr->ip_off = htons(offset / 8 | IP4_MF);
		}
		iphdr->ip_len = htons(frag_len);
		if (offset == 0)
			memcpy(frag_start + UDP_HLEN, udp_payload,
				frag_len - IP4_HLEN - UDP_HLEN);
		else
			memcpy(frag_start, udp_payload + payload_offset,
				frag_len - IP4_HLEN);
	}

	res = sendto(fd_raw, ip_frame, frag_len, 0, addr, alen);
	if (res < 0)
		error(1, errno, "send_fragment");
	if (res != frag_len)
		error(1, 0, "send_fragment: %d vs %d", res, frag_len);

	frag_counter++;
}

static void send_udp_frags(int fd_raw, struct sockaddr *addr,
				socklen_t alen, bool ipv6)
{
	struct ip *iphdr = (struct ip *)ip_frame;
	struct ip6_hdr *ip6hdr = (struct ip6_hdr *)ip_frame;
	const bool ipv4 = !ipv6;
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
	if (ipv6) {
		struct ip6_frag *fraghdr = (struct ip6_frag *)(ip_frame + IP6_HLEN);
		((struct sockaddr_in6 *)addr)->sin6_port = 0;
		memset(ip6hdr, 0, sizeof(*ip6hdr));
		ip6hdr->ip6_flow = htonl(6<<28);  /* Version. */
		ip6hdr->ip6_nxt = IPPROTO_FRAGMENT;
		ip6hdr->ip6_hops = 255;
		ip6hdr->ip6_src = addr6;
		ip6hdr->ip6_dst = addr6;
		fraghdr->ip6f_nxt = IPPROTO_UDP;
		fraghdr->ip6f_reserved = 0;
		fraghdr->ip6f_ident = htonl(ip_id++);
	} else {
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
	}

	/* Occasionally test in-order fragments. */
	if (!cfg_overlap && (rand() % 100 < 15)) {
		offset = 0;
		while (offset < (UDP_HLEN + payload_len)) {
			send_fragment(fd_raw, addr, alen, offset, ipv6);
			offset += max_frag_len;
		}
		return;
	}

	/* Occasionally test IPv4 "runs" (see net/ipv4/ip_fragment.c) */
	if (ipv4 && !cfg_overlap && (rand() % 100 < 20) &&
			(payload_len > 9 * max_frag_len)) {
		offset = 6 * max_frag_len;
		while (offset < (UDP_HLEN + payload_len)) {
			send_fragment(fd_raw, addr, alen, offset, ipv6);
			offset += max_frag_len;
		}
		offset = 3 * max_frag_len;
		while (offset < 6 * max_frag_len) {
			send_fragment(fd_raw, addr, alen, offset, ipv6);
			offset += max_frag_len;
		}
		offset = 0;
		while (offset < 3 * max_frag_len) {
			send_fragment(fd_raw, addr, alen, offset, ipv6);
			offset += max_frag_len;
		}
		return;
	}

	/* Odd fragments. */
	offset = max_frag_len;
	while (offset < (UDP_HLEN + payload_len)) {
		send_fragment(fd_raw, addr, alen, offset, ipv6);
		/* IPv4 ignores duplicates, so randomly send a duplicate. */
		if (ipv4 && (1 == rand() % 100))
			send_fragment(fd_raw, addr, alen, offset, ipv6);
		offset += 2 * max_frag_len;
	}

	if (cfg_overlap) {
		/* Send an extra random fragment. */
		if (ipv6) {
			struct ip6_frag *fraghdr = (struct ip6_frag *)(ip_frame + IP6_HLEN);
			/* sendto() returns EINVAL if offset + frag_len is too small. */
			offset = rand() % (UDP_HLEN + payload_len - 1);
			frag_len = max_frag_len + rand() % 256;
			/* In IPv6 if !!(frag_len % 8), the fragment is dropped. */
			frag_len &= ~0x7;
			fraghdr->ip6f_offlg = htons(offset / 8 | IP6_MF);
			ip6hdr->ip6_plen = htons(frag_len);
			frag_len += IP6_HLEN;
		} else {
			/* In IPv4, duplicates and some fragments completely inside
			 * previously sent fragments are dropped/ignored. So
			 * random offset and frag_len can result in a dropped
			 * fragment instead of a dropped queue/packet. So we
			 * hard-code offset and frag_len.
			 *
			 * See ade446403bfb ("net: ipv4: do not handle duplicate
			 * fragments as overlapping").
			 */
			if (max_frag_len * 4 < payload_len || max_frag_len < 16) {
				/* not enough payload to play with random offset and frag_len. */
				offset = 8;
				frag_len = IP4_HLEN + UDP_HLEN + max_frag_len;
			} else {
				offset = rand() % (payload_len / 2);
				frag_len = 2 * max_frag_len + 1 + rand() % 256;
			}
			iphdr->ip_off = htons(offset / 8 | IP4_MF);
			iphdr->ip_len = htons(frag_len);
		}
		res = sendto(fd_raw, ip_frame, frag_len, 0, addr, alen);
		if (res < 0)
			error(1, errno, "sendto overlap: %d", frag_len);
		if (res != frag_len)
			error(1, 0, "sendto overlap: %d vs %d", (int)res, frag_len);
		frag_counter++;
	}

	/* Event fragments. */
	offset = 0;
	while (offset < (UDP_HLEN + payload_len)) {
		send_fragment(fd_raw, addr, alen, offset, ipv6);
		/* IPv4 ignores duplicates, so randomly send a duplicate. */
		if (ipv4 && (1 == rand() % 100))
			send_fragment(fd_raw, addr, alen, offset, ipv6);
		offset += 2 * max_frag_len;
	}
}

static void run_test(struct sockaddr *addr, socklen_t alen, bool ipv6)
{
	int fd_tx_raw, fd_rx_udp;
	/* Frag queue timeout is set to one second in the calling script;
	 * socket timeout should be just a bit longer to avoid tests interfering
	 * with each other.
	 */
	struct timeval tv = { .tv_sec = 1, .tv_usec = 10 };
	int idx;
	int min_frag_len = ipv6 ? 1280 : 8;

	/* Initialize the payload. */
	for (idx = 0; idx < MSG_LEN_MAX; ++idx)
		udp_payload[idx] = idx % 256;

	/* Open sockets. */
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

	for (payload_len = min_frag_len; payload_len < MSG_LEN_MAX;
			payload_len += (rand() % 4096)) {
		if (cfg_verbose)
			printf("payload_len: %d\n", payload_len);

		if (cfg_overlap) {
			/* With overlaps, one send/receive pair below takes
			 * at least one second (== timeout) to run, so there
			 * is not enough test time to run a nested loop:
			 * the full overlap test takes 20-30 seconds.
			 */
			max_frag_len = min_frag_len +
				rand() % (1500 - FRAG_HLEN - min_frag_len);
			send_udp_frags(fd_tx_raw, addr, alen, ipv6);
			recv_validate_udp(fd_rx_udp);
		} else {
			/* Without overlaps, each packet reassembly (== one
			 * send/receive pair below) takes very little time to
			 * run, so we can easily afford more thourough testing
			 * with a nested loop: the full non-overlap test takes
			 * less than one second).
			 */
			max_frag_len = min_frag_len;
			do {
				send_udp_frags(fd_tx_raw, addr, alen, ipv6);
				recv_validate_udp(fd_rx_udp);
				max_frag_len += 8 * (rand() % 8);
			} while (max_frag_len < (1500 - FRAG_HLEN) &&
				 max_frag_len <= payload_len);
		}
	}

	/* Cleanup. */
	if (close(fd_tx_raw))
		error(1, errno, "close tx_raw");
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

	run_test((void *)&addr, sizeof(addr), false /* !ipv6 */);
}

static void run_test_v6(void)
{
	struct sockaddr_in6 addr = {0};

	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(cfg_port);
	addr.sin6_addr = addr6;

	run_test((void *)&addr, sizeof(addr), true /* ipv6 */);
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
	/* Print the seed to track/reproduce potential failures. */
	printf("seed = %d\n", seed);

	if (cfg_do_ipv4)
		run_test_v4();
	if (cfg_do_ipv6)
		run_test_v6();

	return 0;
}
