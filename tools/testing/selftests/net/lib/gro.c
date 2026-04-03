// SPDX-License-Identifier: GPL-2.0
/*
 * This testsuite provides conformance testing for GRO coalescing.
 *
 * Test cases:
 *
 * data_*:
 *  Data packets of the same size and same header setup with correct
 *  sequence numbers coalesce. The one exception being the last data
 *  packet coalesced: it can be smaller than the rest and coalesced
 *  as long as it is in the same flow.
 *   - data_same:    same size packets coalesce
 *   - data_lrg_sml:   large then small coalesces
 *   - data_lrg_1byte: large then 1 byte coalesces (Ethernet padding)
 *   - data_sml_lrg:   small then large doesn't coalesce
 *   - data_burst:   two bursts of two, separated by 100ms
 *
 * ack:
 *  Pure ACK does not coalesce.
 *
 * flags_*:
 *  No packets with PSH, SYN, URG, RST, CWR set will be coalesced.
 *   - flags_psh, flags_syn, flags_rst, flags_urg, flags_cwr
 *
 * tcp_*:
 *  Packets with incorrect checksum, non-consecutive seqno and
 *  different TCP header options shouldn't coalesce. Nit: given that
 *  some extension headers have paddings, such as timestamp, headers
 *  that are padded differently would not be coalesced.
 *   - tcp_csum: incorrect checksum
 *   - tcp_seq:  non-consecutive sequence numbers
 *   - tcp_ts:   different timestamps
 *   - tcp_opt:  different TCP options
 *
 * ip_*:
 *  Packets with different (ECN, TTL, TOS) header, IP options or
 *  IP fragments shouldn't coalesce.
 *   - ip_ecn, ip_tos:            shared between IPv4/IPv6
 *   - ip_csum:                   IPv4 only, bad IP header checksum
 *   - ip_ttl, ip_opt, ip_frag4:  IPv4 only
 *   - ip_id_df*:                 IPv4 IP ID field coalescing tests
 *   - ip_frag6, ip_v6ext_*:      IPv6 only
 *
 * large_*:
 *  Packets larger than GRO_MAX_SIZE packets shouldn't coalesce.
 *   - large_max: exceeding max size
 *   - large_rem: remainder handling
 *
 * single, capacity:
 *  Boring cases used to test coalescing machinery itself and stats
 *  more than protocol behavior.
 *
 * MSS is defined as 4096 - header because if it is too small
 * (i.e. 1500 MTU - header), it will result in many packets,
 * increasing the "large" test case's flakiness. This is because
 * due to time sensitivity in the coalescing window, the receiver
 * may not coalesce all of the packets.
 *
 * Note the timing issue applies to all of the test cases, so some
 * flakiness is to be expected.
 *
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <linux/net_tstamp.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kselftest.h"
#include "ksft.h"

#define DPORT 8000
#define SPORT 1500
#define PAYLOAD_LEN 100
#define NUM_PACKETS 4
#define START_SEQ 100
#define START_ACK 100
#define ETH_P_NONE 0
#define ASSUMED_MTU 4096
#define MAX_MSS (ASSUMED_MTU - sizeof(struct iphdr) - sizeof(struct tcphdr))
#define MAX_HDR_LEN \
	(ETH_HLEN + sizeof(struct ipv6hdr) * 2 + sizeof(struct tcphdr))
#define MAX_LARGE_PKT_CNT ((IP_MAXPACKET - (MAX_HDR_LEN - ETH_HLEN)) /	\
			   (ASSUMED_MTU - (MAX_HDR_LEN - ETH_HLEN)))
#define MIN_EXTHDR_SIZE 8
#define EXT_PAYLOAD_1 "\x00\x00\x00\x00\x00\x00"
#define EXT_PAYLOAD_2 "\x11\x11\x11\x11\x11\x11"

#define ipv6_optlen(p)  (((p)->hdrlen+1) << 3) /* calculate IPv6 extension header len */
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

enum flush_id_case {
	FLUSH_ID_DF1_INC,
	FLUSH_ID_DF1_FIXED,
	FLUSH_ID_DF0_INC,
	FLUSH_ID_DF0_FIXED,
	FLUSH_ID_DF1_INC_FIXED,
	FLUSH_ID_DF1_FIXED_INC,
};

static const char *addr6_src = "fdaa::2";
static const char *addr6_dst = "fdaa::1";
static const char *addr4_src = "192.168.1.200";
static const char *addr4_dst = "192.168.1.100";
static int proto = -1;
static uint8_t src_mac[ETH_ALEN], dst_mac[ETH_ALEN];
static char *testname = "data";
static char *ifname = "eth0";
static char *smac = "aa:00:00:00:00:02";
static char *dmac = "aa:00:00:00:00:01";
static bool verbose;
static bool tx_socket = true;
static int tcp_offset = -1;
static int total_hdr_len = -1;
static int ethhdr_proto = -1;
static bool ipip;
static bool ip6ip6;
static uint64_t txtime_ns;
static int num_flows = 4;
static bool order_check;

#define CAPACITY_PAYLOAD_LEN 200

#define TXTIME_DELAY_MS 5

/* Max TCP payload that GRO will coalesce. The outer header overhead
 * varies by encapsulation, reducing the effective max payload.
 */
static int max_payload(void)
{
	return IP_MAXPACKET - (total_hdr_len - ETH_HLEN);
}

static int calc_mss(void)
{
	return ASSUMED_MTU - (total_hdr_len - ETH_HLEN);
}

static int num_large_pkt(void)
{
	return max_payload() / calc_mss();
}

static void vlog(const char *fmt, ...)
{
	va_list args;

	if (verbose) {
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
	}
}

static void setup_sock_filter(int fd)
{
	const int dport_off = tcp_offset + offsetof(struct tcphdr, dest);
	const int ethproto_off = offsetof(struct ethhdr, h_proto);
	int optlen = 0;
	int ipproto_off, opt_ipproto_off;

	if (proto == PF_INET)
		ipproto_off = tcp_offset - sizeof(struct iphdr) +
			      offsetof(struct iphdr, protocol);
	else
		ipproto_off = tcp_offset - sizeof(struct ipv6hdr) +
			      offsetof(struct ipv6hdr, nexthdr);

	/* Overridden later if exthdrs are used: */
	opt_ipproto_off = ipproto_off;

	if (strcmp(testname, "ip_opt") == 0) {
		optlen = sizeof(struct ip_timestamp);
	} else if (strcmp(testname, "ip_frag6") == 0 ||
		   strcmp(testname, "ip_v6ext_same") == 0 ||
		   strcmp(testname, "ip_v6ext_diff") == 0) {
		BUILD_BUG_ON(sizeof(struct ip6_hbh) > MIN_EXTHDR_SIZE);
		BUILD_BUG_ON(sizeof(struct ip6_dest) > MIN_EXTHDR_SIZE);
		BUILD_BUG_ON(sizeof(struct ip6_frag) > MIN_EXTHDR_SIZE);

		/* same size for HBH and Fragment extension header types */
		optlen = MIN_EXTHDR_SIZE;
		opt_ipproto_off = ETH_HLEN + sizeof(struct ipv6hdr)
			+ offsetof(struct ip6_ext, ip6e_nxt);
	}

	/* this filter validates the following:
	 *	- packet is IPv4/IPv6 according to the running test.
	 *	- packet is TCP. Also handles the case of one extension header and then TCP.
	 *	- checks the packet tcp dport equals to DPORT. Also handles the case of one
	 *	  extension header and then TCP.
	 */
	struct sock_filter filter[] = {
			BPF_STMT(BPF_LD  + BPF_H   + BPF_ABS, ethproto_off),
			BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, ntohs(ethhdr_proto), 0, 9),
			BPF_STMT(BPF_LD  + BPF_B   + BPF_ABS, ipproto_off),
			BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_TCP, 2, 0),
			BPF_STMT(BPF_LD  + BPF_B   + BPF_ABS, opt_ipproto_off),
			BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_TCP, 0, 5),
			BPF_STMT(BPF_LD  + BPF_H   + BPF_ABS, dport_off),
			BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, DPORT, 2, 0),
			BPF_STMT(BPF_LD  + BPF_H   + BPF_ABS, dport_off + optlen),
			BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, DPORT, 0, 1),
			BPF_STMT(BPF_RET + BPF_K, 0xFFFFFFFF),
			BPF_STMT(BPF_RET + BPF_K, 0),
	};

	struct sock_fprog bpf = {
		.len = ARRAY_SIZE(filter),
		.filter = filter,
	};

	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf)) < 0)
		error(1, errno, "error setting filter");
}

static uint32_t checksum_nofold(void *data, size_t len, uint32_t sum)
{
	uint16_t *words = data;
	int i;

	for (i = 0; i < len / 2; i++)
		sum += words[i];
	if (len & 1)
		sum += ((char *)data)[len - 1];
	return sum;
}

static uint16_t checksum_fold(void *data, size_t len, uint32_t sum)
{
	sum = checksum_nofold(data, len, sum);
	while (sum > 0xFFFF)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return ~sum;
}

static uint16_t tcp_checksum(void *buf, int payload_len)
{
	struct pseudo_header6 {
		struct in6_addr saddr;
		struct in6_addr daddr;
		uint16_t protocol;
		uint16_t payload_len;
	} ph6;
	struct pseudo_header4 {
		struct in_addr saddr;
		struct in_addr daddr;
		uint16_t protocol;
		uint16_t payload_len;
	} ph4;
	uint32_t sum = 0;

	if (proto == PF_INET6) {
		if (inet_pton(AF_INET6, addr6_src, &ph6.saddr) != 1)
			error(1, errno, "inet_pton6 source ip pseudo");
		if (inet_pton(AF_INET6, addr6_dst, &ph6.daddr) != 1)
			error(1, errno, "inet_pton6 dest ip pseudo");
		ph6.protocol = htons(IPPROTO_TCP);
		ph6.payload_len = htons(sizeof(struct tcphdr) + payload_len);

		sum = checksum_nofold(&ph6, sizeof(ph6), 0);
	} else if (proto == PF_INET) {
		if (inet_pton(AF_INET, addr4_src, &ph4.saddr) != 1)
			error(1, errno, "inet_pton source ip pseudo");
		if (inet_pton(AF_INET, addr4_dst, &ph4.daddr) != 1)
			error(1, errno, "inet_pton dest ip pseudo");
		ph4.protocol = htons(IPPROTO_TCP);
		ph4.payload_len = htons(sizeof(struct tcphdr) + payload_len);

		sum = checksum_nofold(&ph4, sizeof(ph4), 0);
	}

	return checksum_fold(buf, sizeof(struct tcphdr) + payload_len, sum);
}

static void read_MAC(uint8_t *mac_addr, char *mac)
{
	if (sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6)
		error(1, 0, "sscanf");
}

static void fill_datalinklayer(void *buf)
{
	struct ethhdr *eth = buf;

	memcpy(eth->h_dest, dst_mac, ETH_ALEN);
	memcpy(eth->h_source, src_mac, ETH_ALEN);
	eth->h_proto = ethhdr_proto;
}

static void fill_networklayer(void *buf, int payload_len, int protocol)
{
	struct ipv6hdr *ip6h = buf;
	struct iphdr *iph = buf;

	if (proto == PF_INET6) {
		memset(ip6h, 0, sizeof(*ip6h));

		ip6h->version = 6;
		ip6h->payload_len = htons(sizeof(struct tcphdr) + payload_len);
		ip6h->nexthdr = protocol;
		ip6h->hop_limit = 8;
		if (inet_pton(AF_INET6, addr6_src, &ip6h->saddr) != 1)
			error(1, errno, "inet_pton source ip6");
		if (inet_pton(AF_INET6, addr6_dst, &ip6h->daddr) != 1)
			error(1, errno, "inet_pton dest ip6");
	} else if (proto == PF_INET) {
		memset(iph, 0, sizeof(*iph));

		iph->version = 4;
		iph->ihl = 5;
		iph->ttl = 8;
		iph->protocol	= protocol;
		iph->tot_len = htons(sizeof(struct tcphdr) +
				payload_len + sizeof(struct iphdr));
		iph->frag_off = htons(0x4000); /* DF = 1, MF = 0 */
		if (inet_pton(AF_INET, addr4_src, &iph->saddr) != 1)
			error(1, errno, "inet_pton source ip");
		if (inet_pton(AF_INET, addr4_dst, &iph->daddr) != 1)
			error(1, errno, "inet_pton dest ip");
		iph->check = checksum_fold(buf, sizeof(struct iphdr), 0);
	}
}

static void fill_transportlayer(void *buf, int seq_offset, int ack_offset,
				int payload_len, int fin)
{
	struct tcphdr *tcph = buf;

	memset(tcph, 0, sizeof(*tcph));

	tcph->source = htons(SPORT);
	tcph->dest = htons(DPORT);
	tcph->seq = ntohl(START_SEQ + seq_offset);
	tcph->ack_seq = ntohl(START_ACK + ack_offset);
	tcph->ack = 1;
	tcph->fin = fin;
	tcph->doff = 5;
	tcph->window = htons(TCP_MAXWIN);
	tcph->urg_ptr = 0;
	tcph->check = tcp_checksum(tcph, payload_len);
}

static void write_packet(int fd, char *buf, int len, struct sockaddr_ll *daddr)
{
	char control[CMSG_SPACE(sizeof(uint64_t))];
	struct msghdr msg = {};
	struct iovec iov = {};
	struct cmsghdr *cm;
	int ret = -1;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = daddr;
	msg.msg_namelen = sizeof(*daddr);

	if (txtime_ns) {
		memset(control, 0, sizeof(control));
		msg.msg_control = control;
		msg.msg_controllen = sizeof(control);

		cm = CMSG_FIRSTHDR(&msg);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_TXTIME;
		cm->cmsg_len = CMSG_LEN(sizeof(uint64_t));
		memcpy(CMSG_DATA(cm), &txtime_ns, sizeof(txtime_ns));
	}

	ret = sendmsg(fd, &msg, 0);
	if (ret == -1)
		error(1, errno, "sendmsg failure");
	if (ret != len)
		error(1, 0, "sendmsg wrong length: %d vs %d", ret, len);
}

static void create_packet(void *buf, int seq_offset, int ack_offset,
			  int payload_len, int fin)
{
	int ip_hdr_len = (proto == PF_INET) ?
			 sizeof(struct iphdr) : sizeof(struct ipv6hdr);
	int inner_ip_off = tcp_offset - ip_hdr_len;

	memset(buf, 0, total_hdr_len);
	memset(buf + total_hdr_len, 'a', payload_len);

	fill_transportlayer(buf + tcp_offset, seq_offset, ack_offset,
			    payload_len, fin);

	fill_networklayer(buf + inner_ip_off, payload_len, IPPROTO_TCP);
	if (inner_ip_off > ETH_HLEN) {
		int encap_proto = (proto == PF_INET) ?
				  IPPROTO_IPIP : IPPROTO_IPV6;

		fill_networklayer(buf + ETH_HLEN,
				  payload_len + ip_hdr_len, encap_proto);
	}

	fill_datalinklayer(buf);
}

static void create_capacity_packet(void *buf, int flow_id, int pkt_idx, int psh)
{
	int seq_offset = pkt_idx * CAPACITY_PAYLOAD_LEN;
	struct tcphdr *tcph;

	create_packet(buf, seq_offset, 0, CAPACITY_PAYLOAD_LEN, 0);

	/* Customize for this flow id */
	memset(buf + total_hdr_len, 'a' + flow_id, CAPACITY_PAYLOAD_LEN);

	tcph = buf + tcp_offset;
	tcph->source = htons(SPORT + flow_id);
	tcph->psh = psh;
	tcph->check = 0;
	tcph->check = tcp_checksum(tcph, CAPACITY_PAYLOAD_LEN);
}

/* Send a capacity test, 2 packets per flow, all first packets then all second:
 *  A1 B1 C1 D1 ... A2 B2 C2 D2 ...
 */
static void send_capacity(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + CAPACITY_PAYLOAD_LEN];
	int pkt_size = total_hdr_len + CAPACITY_PAYLOAD_LEN;
	int i;

	/* Send first packet of each flow (no PSH) */
	for (i = 0; i < num_flows; i++) {
		create_capacity_packet(buf, i, 0, 0);
		write_packet(fd, buf, pkt_size, daddr);
	}

	/* Send second packet of each flow (with PSH to flush) */
	for (i = 0; i < num_flows; i++) {
		create_capacity_packet(buf, i, 1, 1);
		write_packet(fd, buf, pkt_size, daddr);
	}
}

#ifndef TH_CWR
#define TH_CWR 0x80
#endif
static void set_flags(struct tcphdr *tcph, int payload_len, int psh, int syn,
		      int rst, int urg, int cwr)
{
	tcph->psh = psh;
	tcph->syn = syn;
	tcph->rst = rst;
	tcph->urg = urg;
	if (cwr)
		tcph->th_flags |= TH_CWR;
	else
		tcph->th_flags &= ~TH_CWR;
	tcph->check = 0;
	tcph->check = tcp_checksum(tcph, payload_len);
}

/* send extra flags of the (NUM_PACKETS / 2) and (NUM_PACKETS / 2 - 1)
 * pkts, not first and not last pkt
 */
static void send_flags(int fd, struct sockaddr_ll *daddr, int psh, int syn,
		       int rst, int urg, int cwr)
{
	static char flag_buf[2][MAX_HDR_LEN + PAYLOAD_LEN];
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	int payload_len, pkt_size, i;
	struct tcphdr *tcph;
	int flag[2];

	payload_len = PAYLOAD_LEN * (psh || cwr);
	pkt_size = total_hdr_len + payload_len;
	flag[0] = NUM_PACKETS / 2;
	flag[1] = NUM_PACKETS / 2 - 1;

	/* Create and configure packets with flags
	 */
	for (i = 0; i < 2; i++) {
		if (flag[i] > 0) {
			create_packet(flag_buf[i], flag[i] * payload_len, 0,
				      payload_len, 0);
			tcph = (struct tcphdr *)(flag_buf[i] + tcp_offset);
			set_flags(tcph, payload_len, psh, syn, rst, urg, cwr);
		}
	}

	for (i = 0; i < NUM_PACKETS + 1; i++) {
		if (i == flag[0]) {
			write_packet(fd, flag_buf[0], pkt_size, daddr);
			continue;
		} else if (i == flag[1] && cwr) {
			write_packet(fd, flag_buf[1], pkt_size, daddr);
			continue;
		}
		create_packet(buf, i * PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
		write_packet(fd, buf, total_hdr_len + PAYLOAD_LEN, daddr);
	}
}

/* Test for data of same length, smaller than previous
 * and of different lengths
 */
static void send_data_pkts(int fd, struct sockaddr_ll *daddr,
			   int payload_len1, int payload_len2)
{
	static char buf[ETH_HLEN + IP_MAXPACKET];

	create_packet(buf, 0, 0, payload_len1, 0);
	write_packet(fd, buf, total_hdr_len + payload_len1, daddr);
	create_packet(buf, payload_len1, 0, payload_len2, 0);
	write_packet(fd, buf, total_hdr_len + payload_len2, daddr);
}

/* If incoming segments make tracked segment length exceed
 * legal IP datagram length, do not coalesce
 */
static void send_large(int fd, struct sockaddr_ll *daddr, int remainder)
{
	static char pkts[MAX_LARGE_PKT_CNT][MAX_HDR_LEN + MAX_MSS];
	static char new_seg[MAX_HDR_LEN + MAX_MSS];
	static char last[MAX_HDR_LEN + MAX_MSS];
	const int num_pkt = num_large_pkt();
	const int mss = calc_mss();
	int i;

	for (i = 0; i < num_pkt; i++)
		create_packet(pkts[i], i * mss, 0, mss, 0);
	create_packet(last, num_pkt * mss, 0, remainder, 0);
	create_packet(new_seg, (num_pkt + 1) * mss, 0, remainder, 0);

	for (i = 0; i < num_pkt; i++)
		write_packet(fd, pkts[i], total_hdr_len + mss, daddr);
	write_packet(fd, last, total_hdr_len + remainder, daddr);
	write_packet(fd, new_seg, total_hdr_len + remainder, daddr);
}

/* Pure acks and dup acks don't coalesce */
static void send_ack(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN];

	create_packet(buf, 0, 0, 0, 0);
	write_packet(fd, buf, total_hdr_len, daddr);
	write_packet(fd, buf, total_hdr_len, daddr);
	create_packet(buf, 0, 1, 0, 0);
	write_packet(fd, buf, total_hdr_len, daddr);
}

static void recompute_packet(char *buf, char *no_ext, int extlen)
{
	struct tcphdr *tcphdr = (struct tcphdr *)(buf + tcp_offset);
	int off;

	memmove(buf, no_ext, total_hdr_len);
	memmove(buf + total_hdr_len + extlen,
		no_ext + total_hdr_len, PAYLOAD_LEN);

	tcphdr->doff = tcphdr->doff + (extlen / 4);
	tcphdr->check = 0;
	tcphdr->check = tcp_checksum(tcphdr, PAYLOAD_LEN + extlen);
	if (proto == PF_INET) {
		for (off = ETH_HLEN; off < tcp_offset;
		     off += sizeof(struct iphdr)) {
			struct iphdr *iph = (struct iphdr *)(buf + off);

			iph->tot_len = htons(ntohs(iph->tot_len) + extlen);
			iph->check = 0;
			iph->check = checksum_fold(iph, sizeof(struct iphdr), 0);
		}
	} else {
		for (off = ETH_HLEN; off < tcp_offset;
		     off += sizeof(struct ipv6hdr)) {
			struct ipv6hdr *ip6h = (struct ipv6hdr *)(buf + off);

			ip6h->payload_len =
				htons(ntohs(ip6h->payload_len) + extlen);
		}
	}
}

static void tcp_write_options(char *buf, int kind, int ts)
{
	struct tcp_option_ts {
		uint8_t kind;
		uint8_t len;
		uint32_t tsval;
		uint32_t tsecr;
	} *opt_ts = (void *)buf;
	struct tcp_option_window {
		uint8_t kind;
		uint8_t len;
		uint8_t shift;
	} *opt_window = (void *)buf;

	switch (kind) {
	case TCPOPT_NOP:
		buf[0] = TCPOPT_NOP;
		break;
	case TCPOPT_WINDOW:
		memset(opt_window, 0, sizeof(struct tcp_option_window));
		opt_window->kind = TCPOPT_WINDOW;
		opt_window->len = TCPOLEN_WINDOW;
		opt_window->shift = 0;
		break;
	case TCPOPT_TIMESTAMP:
		memset(opt_ts, 0, sizeof(struct tcp_option_ts));
		opt_ts->kind = TCPOPT_TIMESTAMP;
		opt_ts->len = TCPOLEN_TIMESTAMP;
		opt_ts->tsval = ts;
		opt_ts->tsecr = 0;
		break;
	default:
		error(1, 0, "unimplemented TCP option");
		break;
	}
}

/* TCP with options is always a permutation of {TS, NOP, NOP}.
 * Implement different orders to verify coalescing stops.
 */
static void add_standard_tcp_options(char *buf, char *no_ext, int ts, int order)
{
	switch (order) {
	case 0:
		tcp_write_options(buf + total_hdr_len, TCPOPT_NOP, 0);
		tcp_write_options(buf + total_hdr_len + 1, TCPOPT_NOP, 0);
		tcp_write_options(buf + total_hdr_len + 2 /* two NOP opts */,
				  TCPOPT_TIMESTAMP, ts);
		break;
	case 1:
		tcp_write_options(buf + total_hdr_len, TCPOPT_NOP, 0);
		tcp_write_options(buf + total_hdr_len + 1,
				  TCPOPT_TIMESTAMP, ts);
		tcp_write_options(buf + total_hdr_len + 1 + TCPOLEN_TIMESTAMP,
				  TCPOPT_NOP, 0);
		break;
	case 2:
		tcp_write_options(buf + total_hdr_len, TCPOPT_TIMESTAMP, ts);
		tcp_write_options(buf + total_hdr_len + TCPOLEN_TIMESTAMP + 1,
				  TCPOPT_NOP, 0);
		tcp_write_options(buf + total_hdr_len + TCPOLEN_TIMESTAMP + 2,
				  TCPOPT_NOP, 0);
		break;
	default:
		error(1, 0, "unknown order");
		break;
	}
	recompute_packet(buf, no_ext, TCPOLEN_TSTAMP_APPA);
}

/* Packets with invalid checksum don't coalesce. */
static void send_changed_checksum(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	struct tcphdr *tcph = (struct tcphdr *)(buf + tcp_offset);
	int pkt_size = total_hdr_len + PAYLOAD_LEN;

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	tcph->check = tcph->check - 1;
	write_packet(fd, buf, pkt_size, daddr);
}

/* Packets with incorrect IPv4 header checksum don't coalesce. */
static void send_changed_ip_checksum(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	struct iphdr *iph = (struct iphdr *)(buf + ETH_HLEN);
	int pkt_size = total_hdr_len + PAYLOAD_LEN;

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	iph->check = iph->check - 1;
	write_packet(fd, buf, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN * 2, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);
}

 /* Packets with non-consecutive sequence number don't coalesce.*/
static void send_changed_seq(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	struct tcphdr *tcph = (struct tcphdr *)(buf + tcp_offset);
	int pkt_size = total_hdr_len + PAYLOAD_LEN;

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	tcph->seq = ntohl(htonl(tcph->seq) + 1);
	tcph->check = 0;
	tcph->check = tcp_checksum(tcph, PAYLOAD_LEN);
	write_packet(fd, buf, pkt_size, daddr);
}

 /* Packet with different timestamp option or different timestamps
  * don't coalesce.
  */
static void send_changed_ts(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	static char extpkt[sizeof(buf) + TCPOLEN_TSTAMP_APPA];
	int pkt_size = total_hdr_len + PAYLOAD_LEN + TCPOLEN_TSTAMP_APPA;

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	add_standard_tcp_options(extpkt, buf, 0, 0);
	write_packet(fd, extpkt, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	add_standard_tcp_options(extpkt, buf, 0, 0);
	write_packet(fd, extpkt, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN * 2, 0, PAYLOAD_LEN, 0);
	add_standard_tcp_options(extpkt, buf, 100, 0);
	write_packet(fd, extpkt, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN * 3, 0, PAYLOAD_LEN, 0);
	add_standard_tcp_options(extpkt, buf, 100, 1);
	write_packet(fd, extpkt, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN * 4, 0, PAYLOAD_LEN, 0);
	add_standard_tcp_options(extpkt, buf, 100, 2);
	write_packet(fd, extpkt, pkt_size, daddr);
}

/* Packet with different tcp options don't coalesce. */
static void send_diff_opt(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	static char extpkt1[sizeof(buf) + TCPOLEN_TSTAMP_APPA];
	static char extpkt2[sizeof(buf) + TCPOLEN_MAXSEG];
	int extpkt1_size = total_hdr_len + PAYLOAD_LEN + TCPOLEN_TSTAMP_APPA;
	int extpkt2_size = total_hdr_len + PAYLOAD_LEN + TCPOLEN_MAXSEG;

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	add_standard_tcp_options(extpkt1, buf, 0, 0);
	write_packet(fd, extpkt1, extpkt1_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	add_standard_tcp_options(extpkt1, buf, 0, 0);
	write_packet(fd, extpkt1, extpkt1_size, daddr);

	create_packet(buf, PAYLOAD_LEN * 2, 0, PAYLOAD_LEN, 0);
	tcp_write_options(extpkt2 + MAX_HDR_LEN, TCPOPT_NOP, 0);
	tcp_write_options(extpkt2 + MAX_HDR_LEN + 1, TCPOPT_WINDOW, 0);
	recompute_packet(extpkt2, buf, TCPOLEN_WINDOW + 1);
	write_packet(fd, extpkt2, extpkt2_size, daddr);
}

static void add_ipv4_ts_option(void *buf, void *optpkt)
{
	struct ip_timestamp *ts = (struct ip_timestamp *)(optpkt + tcp_offset);
	int optlen = sizeof(struct ip_timestamp);
	struct iphdr *iph;

	if (optlen % 4)
		error(1, 0, "ipv4 timestamp length is not a multiple of 4B");

	ts->ipt_code = IPOPT_TS;
	ts->ipt_len = optlen;
	ts->ipt_ptr = 5;
	ts->ipt_flg = IPOPT_TS_TSONLY;

	memcpy(optpkt, buf, tcp_offset);
	memcpy(optpkt + tcp_offset + optlen, buf + tcp_offset,
	       sizeof(struct tcphdr) + PAYLOAD_LEN);

	iph = (struct iphdr *)(optpkt + ETH_HLEN);
	iph->ihl = 5 + (optlen / 4);
	iph->tot_len = htons(ntohs(iph->tot_len) + optlen);
	iph->check = 0;
	iph->check = checksum_fold(iph, sizeof(struct iphdr) + optlen, 0);
}

static void add_ipv6_exthdr(void *buf, void *optpkt, __u8 exthdr_type, char *ext_payload)
{
	struct ipv6_opt_hdr *exthdr = (struct ipv6_opt_hdr *)(optpkt + tcp_offset);
	struct ipv6hdr *iph = (struct ipv6hdr *)(optpkt + ETH_HLEN);
	char *exthdr_payload_start = (char *)(exthdr + 1);

	exthdr->hdrlen = 0;
	exthdr->nexthdr = IPPROTO_TCP;

	memcpy(exthdr_payload_start, ext_payload, MIN_EXTHDR_SIZE - sizeof(*exthdr));

	memcpy(optpkt, buf, tcp_offset);
	memcpy(optpkt + tcp_offset + MIN_EXTHDR_SIZE, buf + tcp_offset,
		sizeof(struct tcphdr) + PAYLOAD_LEN);

	iph->nexthdr = exthdr_type;
	iph->payload_len = htons(ntohs(iph->payload_len) + MIN_EXTHDR_SIZE);
}

static void fix_ip4_checksum(struct iphdr *iph)
{
	iph->check = 0;
	iph->check = checksum_fold(iph, sizeof(struct iphdr), 0);
}

static void send_flush_id_case(int fd, struct sockaddr_ll *daddr,
			       enum flush_id_case tcase)
{
	static char buf1[MAX_HDR_LEN + PAYLOAD_LEN];
	static char buf2[MAX_HDR_LEN + PAYLOAD_LEN];
	static char buf3[MAX_HDR_LEN + PAYLOAD_LEN];
	bool send_three = false;
	struct iphdr *iph1;
	struct iphdr *iph2;
	struct iphdr *iph3;

	iph1 = (struct iphdr *)(buf1 + ETH_HLEN);
	iph2 = (struct iphdr *)(buf2 + ETH_HLEN);
	iph3 = (struct iphdr *)(buf3 + ETH_HLEN);

	create_packet(buf1, 0, 0, PAYLOAD_LEN, 0);
	create_packet(buf2, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	create_packet(buf3, PAYLOAD_LEN * 2, 0, PAYLOAD_LEN, 0);

	switch (tcase) {
	case FLUSH_ID_DF1_INC: /* DF=1, Incrementing - should coalesce */
		iph1->frag_off |= htons(IP_DF);
		iph1->id = htons(8);

		iph2->frag_off |= htons(IP_DF);
		iph2->id = htons(9);
		break;

	case FLUSH_ID_DF1_FIXED: /* DF=1, Fixed - should coalesce */
		iph1->frag_off |= htons(IP_DF);
		iph1->id = htons(8);

		iph2->frag_off |= htons(IP_DF);
		iph2->id = htons(8);
		break;

	case FLUSH_ID_DF0_INC: /* DF=0, Incrementing - should coalesce */
		iph1->frag_off &= ~htons(IP_DF);
		iph1->id = htons(8);

		iph2->frag_off &= ~htons(IP_DF);
		iph2->id = htons(9);
		break;

	case FLUSH_ID_DF0_FIXED: /* DF=0, Fixed - should coalesce */
		iph1->frag_off &= ~htons(IP_DF);
		iph1->id = htons(8);

		iph2->frag_off &= ~htons(IP_DF);
		iph2->id = htons(8);
		break;

	case FLUSH_ID_DF1_INC_FIXED: /* DF=1, two packets incrementing, and
				      * one fixed - should coalesce only the
				      * first two packets
				      */
		iph1->frag_off |= htons(IP_DF);
		iph1->id = htons(8);

		iph2->frag_off |= htons(IP_DF);
		iph2->id = htons(9);

		iph3->frag_off |= htons(IP_DF);
		iph3->id = htons(9);
		send_three = true;
		break;

	case FLUSH_ID_DF1_FIXED_INC: /* DF=1, two packets fixed, and one
				      * incrementing - should coalesce only
				      * the first two packets
				      */
		iph1->frag_off |= htons(IP_DF);
		iph1->id = htons(8);

		iph2->frag_off |= htons(IP_DF);
		iph2->id = htons(8);

		iph3->frag_off |= htons(IP_DF);
		iph3->id = htons(9);
		send_three = true;
		break;
	}

	fix_ip4_checksum(iph1);
	fix_ip4_checksum(iph2);
	write_packet(fd, buf1, total_hdr_len + PAYLOAD_LEN, daddr);
	write_packet(fd, buf2, total_hdr_len + PAYLOAD_LEN, daddr);

	if (send_three) {
		fix_ip4_checksum(iph3);
		write_packet(fd, buf3, total_hdr_len + PAYLOAD_LEN, daddr);
	}
}

static void send_ipv6_exthdr(int fd, struct sockaddr_ll *daddr, char *ext_data1, char *ext_data2)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	static char exthdr_pck[sizeof(buf) + MIN_EXTHDR_SIZE];

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	add_ipv6_exthdr(buf, exthdr_pck, IPPROTO_DSTOPTS, ext_data1);
	write_packet(fd, exthdr_pck, total_hdr_len + PAYLOAD_LEN + MIN_EXTHDR_SIZE, daddr);

	create_packet(buf, PAYLOAD_LEN * 1, 0, PAYLOAD_LEN, 0);
	add_ipv6_exthdr(buf, exthdr_pck, IPPROTO_DSTOPTS, ext_data2);
	write_packet(fd, exthdr_pck, total_hdr_len + PAYLOAD_LEN + MIN_EXTHDR_SIZE, daddr);
}

/* IPv4 options shouldn't coalesce */
static void send_ip_options(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	static char optpkt[sizeof(buf) + sizeof(struct ip_timestamp)];
	int optlen = sizeof(struct ip_timestamp);
	int pkt_size = total_hdr_len + PAYLOAD_LEN + optlen;

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, total_hdr_len + PAYLOAD_LEN, daddr);

	create_packet(buf, PAYLOAD_LEN * 1, 0, PAYLOAD_LEN, 0);
	add_ipv4_ts_option(buf, optpkt);
	write_packet(fd, optpkt, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN * 2, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, total_hdr_len + PAYLOAD_LEN, daddr);
}

/*  IPv4 fragments shouldn't coalesce */
static void send_fragment4(int fd, struct sockaddr_ll *daddr)
{
	static char buf[IP_MAXPACKET];
	struct iphdr *iph = (struct iphdr *)(buf + ETH_HLEN);
	int pkt_size = total_hdr_len + PAYLOAD_LEN;

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);

	/* Once fragmented, packet would retain the total_len.
	 * Tcp header is prepared as if rest of data is in follow-up frags,
	 * but follow up frags aren't actually sent.
	 */
	memset(buf + total_hdr_len, 'a', PAYLOAD_LEN * 2);
	fill_transportlayer(buf + tcp_offset, PAYLOAD_LEN, 0, PAYLOAD_LEN * 2, 0);
	fill_networklayer(buf + ETH_HLEN, PAYLOAD_LEN, IPPROTO_TCP);
	fill_datalinklayer(buf);

	iph->frag_off = htons(0x6000); // DF = 1, MF = 1
	iph->check = 0;
	iph->check = checksum_fold(iph, sizeof(struct iphdr), 0);
	write_packet(fd, buf, pkt_size, daddr);
}

/* IPv4 packets with different ttl don't coalesce.*/
static void send_changed_ttl(int fd, struct sockaddr_ll *daddr)
{
	int pkt_size = total_hdr_len + PAYLOAD_LEN;
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	struct iphdr *iph = (struct iphdr *)(buf + ETH_HLEN);

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	iph->ttl = 7;
	iph->check = 0;
	iph->check = checksum_fold(iph, sizeof(struct iphdr), 0);
	write_packet(fd, buf, pkt_size, daddr);
}

/* Packets with different tos don't coalesce.*/
static void send_changed_tos(int fd, struct sockaddr_ll *daddr)
{
	int pkt_size = total_hdr_len + PAYLOAD_LEN;
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	struct iphdr *iph = (struct iphdr *)(buf + ETH_HLEN);
	struct ipv6hdr *ip6h = (struct ipv6hdr *)(buf + ETH_HLEN);

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	if (proto == PF_INET) {
		iph->tos = 1;
		iph->check = 0;
		iph->check = checksum_fold(iph, sizeof(struct iphdr), 0);
	} else if (proto == PF_INET6) {
		ip6h->priority = 0xf;
	}
	write_packet(fd, buf, pkt_size, daddr);
}

/* Packets with different ECN don't coalesce.*/
static void send_changed_ECN(int fd, struct sockaddr_ll *daddr)
{
	int pkt_size = total_hdr_len + PAYLOAD_LEN;
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	struct iphdr *iph = (struct iphdr *)(buf + ETH_HLEN);

	create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, pkt_size, daddr);

	create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
	if (proto == PF_INET) {
		buf[ETH_HLEN + 1] ^= 0x2; // ECN set to 10
		iph->check = 0;
		iph->check = checksum_fold(iph, sizeof(struct iphdr), 0);
	} else {
		buf[ETH_HLEN + 1] ^= 0x20; // ECN set to 10
	}
	write_packet(fd, buf, pkt_size, daddr);
}

/* IPv6 fragments and packets with extensions don't coalesce.*/
static void send_fragment6(int fd, struct sockaddr_ll *daddr)
{
	static char buf[MAX_HDR_LEN + PAYLOAD_LEN];
	static char extpkt[MAX_HDR_LEN + PAYLOAD_LEN +
			   sizeof(struct ip6_frag)];
	struct ipv6hdr *ip6h = (struct ipv6hdr *)(buf + ETH_HLEN);
	struct ip6_frag *frag = (void *)(extpkt + tcp_offset);
	int extlen = sizeof(struct ip6_frag);
	int bufpkt_len = total_hdr_len + PAYLOAD_LEN;
	int extpkt_len = bufpkt_len + extlen;
	int i;

	for (i = 0; i < 2; i++) {
		create_packet(buf, PAYLOAD_LEN * i, 0, PAYLOAD_LEN, 0);
		write_packet(fd, buf, bufpkt_len, daddr);
	}
	sleep(1);
	create_packet(buf, PAYLOAD_LEN * 2, 0, PAYLOAD_LEN, 0);
	memset(extpkt, 0, extpkt_len);

	ip6h->nexthdr = IPPROTO_FRAGMENT;
	ip6h->payload_len = htons(ntohs(ip6h->payload_len) + extlen);
	frag->ip6f_nxt = IPPROTO_TCP;

	memcpy(extpkt, buf, tcp_offset);
	memcpy(extpkt + tcp_offset + extlen, buf + tcp_offset,
	       sizeof(struct tcphdr) + PAYLOAD_LEN);
	write_packet(fd, extpkt, extpkt_len, daddr);

	create_packet(buf, PAYLOAD_LEN * 3, 0, PAYLOAD_LEN, 0);
	write_packet(fd, buf, bufpkt_len, daddr);
}

static void bind_packetsocket(int fd)
{
	struct sockaddr_ll daddr = {};

	daddr.sll_family = AF_PACKET;
	daddr.sll_protocol = ethhdr_proto;
	daddr.sll_ifindex = if_nametoindex(ifname);
	if (daddr.sll_ifindex == 0)
		error(1, errno, "if_nametoindex");

	if (bind(fd, (void *)&daddr, sizeof(daddr)) < 0)
		error(1, errno, "could not bind socket");
}

static void set_timeout(int fd)
{
	struct timeval timeout;

	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
		       sizeof(timeout)) < 0)
		error(1, errno, "cannot set timeout, setsockopt failed");
}

static void set_rcvbuf(int fd)
{
	int bufsize = 1 * 1024 * 1024; /* 1 MB */

	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)))
		error(1, errno, "cannot set rcvbuf size, setsockopt failed");
}

static void recv_error(int fd, int rcv_errno)
{
	struct tpacket_stats stats;
	socklen_t len;

	len = sizeof(stats);
	if (getsockopt(fd, SOL_PACKET, PACKET_STATISTICS, &stats, &len))
		error(1, errno, "can't get stats");

	fprintf(stderr, "Socket stats: packets=%u, drops=%u\n",
		stats.tp_packets, stats.tp_drops);
	error(1, rcv_errno, "could not receive");
}

static void check_recv_pkts(int fd, int *correct_payload,
			    int correct_num_pkts)
{
	static char buffer[IP_MAXPACKET + ETH_HLEN + 1];
	struct iphdr *iph = (struct iphdr *)(buffer + ETH_HLEN);
	struct ipv6hdr *ip6h = (struct ipv6hdr *)(buffer + ETH_HLEN);
	struct tcphdr *tcph;
	bool bad_packet = false;
	int tcp_ext_len = 0;
	int ip_ext_len = 0;
	int pkt_size = -1;
	int data_len = 0;
	int num_pkt = 0;
	int i;

	vlog("Expected {");
	for (i = 0; i < correct_num_pkts; i++)
		vlog("%d ", correct_payload[i]);
	vlog("}, Total %d packets\nReceived {", correct_num_pkts);

	while (1) {
		ip_ext_len = 0;
		pkt_size = recv(fd, buffer, IP_MAXPACKET + ETH_HLEN + 1, 0);
		if (pkt_size < 0)
			recv_error(fd, errno);

		if (iph->version == 4)
			ip_ext_len = (iph->ihl - 5) * 4;
		else if (ip6h->version == 6 && !ip6ip6 &&
			 ip6h->nexthdr != IPPROTO_TCP)
			ip_ext_len = MIN_EXTHDR_SIZE;

		tcph = (struct tcphdr *)(buffer + tcp_offset + ip_ext_len);

		if (tcph->fin)
			break;

		tcp_ext_len = (tcph->doff - 5) * 4;
		data_len = pkt_size - total_hdr_len - tcp_ext_len - ip_ext_len;
		/* Min ethernet frame payload is 46(ETH_ZLEN - ETH_HLEN) by RFC 802.3.
		 * Ipv4/tcp packets without at least 6 bytes of data will be padded.
		 * Packet sockets are protocol agnostic, and will not trim the padding.
		 */
		if (pkt_size == ETH_ZLEN && iph->version == 4) {
			data_len = ntohs(iph->tot_len)
				- sizeof(struct tcphdr) - sizeof(struct iphdr);
		}
		vlog("%d ", data_len);
		if (data_len != correct_payload[num_pkt]) {
			vlog("[!=%d]", correct_payload[num_pkt]);
			bad_packet = true;
		}
		num_pkt++;
	}
	vlog("}, Total %d packets.\n", num_pkt);
	if (num_pkt != correct_num_pkts)
		error(1, 0, "incorrect number of packets");
	if (bad_packet)
		error(1, 0, "incorrect packet geometry");

	printf("Test succeeded\n\n");
}

static void check_capacity_pkts(int fd)
{
	static char buffer[IP_MAXPACKET + ETH_HLEN + 1];
	struct iphdr *iph = (struct iphdr *)(buffer + ETH_HLEN);
	struct ipv6hdr *ip6h = (struct ipv6hdr *)(buffer + ETH_HLEN);
	int num_pkt = 0, num_coal = 0, pkt_idx;
	const char *fail_reason = NULL;
	int flow_order[num_flows * 2];
	int coalesced[num_flows];
	struct tcphdr *tcph;
	int ip_ext_len = 0;
	int total_data = 0;
	int pkt_size = -1;
	int data_len = 0;
	int flow_id;
	int sport;

	memset(coalesced, 0, sizeof(coalesced));
	memset(flow_order, -1, sizeof(flow_order));

	while (1) {
		ip_ext_len = 0;
		pkt_size = recv(fd, buffer, IP_MAXPACKET + ETH_HLEN + 1, 0);
		if (pkt_size < 0)
			recv_error(fd, errno);

		if (iph->version == 4)
			ip_ext_len = (iph->ihl - 5) * 4;
		else if (ip6h->version == 6 && !ip6ip6 &&
			 ip6h->nexthdr != IPPROTO_TCP)
			ip_ext_len = MIN_EXTHDR_SIZE;

		tcph = (struct tcphdr *)(buffer + tcp_offset + ip_ext_len);

		if (tcph->fin)
			break;

		sport = ntohs(tcph->source);
		flow_id = sport - SPORT;

		if (flow_id < 0 || flow_id >= num_flows) {
			vlog("Invalid flow_id %d from sport %d\n",
			     flow_id, sport);
			fail_reason = fail_reason ?: "invalid packet";
			continue;
		}

		/* Calculate payload length */
		if (pkt_size == ETH_ZLEN && iph->version == 4) {
			data_len = ntohs(iph->tot_len)
				- sizeof(struct tcphdr) - sizeof(struct iphdr);
		} else {
			data_len = pkt_size - total_hdr_len - ip_ext_len;
		}

		if (num_pkt < num_flows * 2) {
			flow_order[num_pkt] = flow_id;
		} else if (num_pkt == num_flows * 2) {
			vlog("More packets than expected (%d)\n",
			     num_flows * 2);
			fail_reason = fail_reason ?: "too many packets";
		}
		coalesced[flow_id] = data_len;

		if (data_len == CAPACITY_PAYLOAD_LEN * 2) {
			num_coal++;
		} else {
			vlog("Pkt %d: flow %d, sport %d, len %d (expected %d)\n",
			     num_pkt, flow_id, sport, data_len,
			     CAPACITY_PAYLOAD_LEN * 2);
			fail_reason = fail_reason ?: "not coalesced";
		}

		num_pkt++;
		total_data += data_len;
	}

	/* Check flow ordering. We expect to see all non-coalesced first segs
	 * then interleaved coalesced and non-coalesced second frames.
	 */
	pkt_idx = 0;
	for (flow_id = 0; order_check && flow_id < num_flows; flow_id++) {
		bool coaled = coalesced[flow_id] > CAPACITY_PAYLOAD_LEN;

		if (coaled)
			continue;

		if (flow_order[pkt_idx] != flow_id) {
			vlog("Flow order mismatch (non-coalesced) at position %d: expected flow %d, got flow %d\n",
			     pkt_idx, flow_id, flow_order[pkt_idx]);
			fail_reason = fail_reason ?: "bad packet order (1)";
		}
		pkt_idx++;
	}
	for (flow_id = 0; order_check && flow_id < num_flows; flow_id++) {
		bool coaled = coalesced[flow_id] > CAPACITY_PAYLOAD_LEN;

		if (flow_order[pkt_idx] != flow_id) {
			vlog("Flow order mismatch at position %d: expected flow %d, got flow %d, coalesced: %d\n",
			     pkt_idx, flow_id, flow_order[pkt_idx], coaled);
			fail_reason = fail_reason ?: "bad packet order (2)";
		}
		pkt_idx++;
	}

	if (!fail_reason) {
		vlog("All %d flows coalesced correctly\n", num_flows);
		printf("Test succeeded\n\n");
	} else {
		printf("FAILED\n");
	}

	/* Always print stats for external validation */
	printf("STATS: received=%d wire=%d coalesced=%d\n",
	       num_pkt, num_pkt + num_coal, num_coal);

	if (fail_reason)
		error(1, 0, "capacity test failed %s", fail_reason);
}

static void gro_sender(void)
{
	int bufsize = 4 * 1024 * 1024; /* 4 MB */
	const int fin_delay_us = 100 * 1000;
	static char fin_pkt[MAX_HDR_LEN];
	struct sockaddr_ll daddr = {};
	int txfd = -1;

	txfd = socket(PF_PACKET, SOCK_RAW, IPPROTO_RAW);
	if (txfd < 0)
		error(1, errno, "socket creation");

	if (setsockopt(txfd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)))
		error(1, errno, "cannot set sndbuf size, setsockopt failed");

	/* Enable SO_TXTIME unless test case generates more than one flow
	 * SO_TXTIME could result in qdisc layer sorting the packets at sender.
	 */
	if (strcmp(testname, "single") && strcmp(testname, "capacity")) {
		struct sock_txtime so_txtime = { .clockid = CLOCK_MONOTONIC, };
		struct timespec ts;

		if (setsockopt(txfd, SOL_SOCKET, SO_TXTIME,
			       &so_txtime, sizeof(so_txtime)))
			error(1, errno, "setsockopt SO_TXTIME");

		if (clock_gettime(CLOCK_MONOTONIC, &ts))
			error(1, errno, "clock_gettime");

		txtime_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
		txtime_ns += TXTIME_DELAY_MS * 1000000ULL;
	}

	memset(&daddr, 0, sizeof(daddr));
	daddr.sll_ifindex = if_nametoindex(ifname);
	if (daddr.sll_ifindex == 0)
		error(1, errno, "if_nametoindex");
	daddr.sll_family = AF_PACKET;
	memcpy(daddr.sll_addr, dst_mac, ETH_ALEN);
	daddr.sll_halen = ETH_ALEN;
	create_packet(fin_pkt, PAYLOAD_LEN * 2, 0, 0, 1);

	/* data sub-tests */
	if (strcmp(testname, "data_same") == 0) {
		send_data_pkts(txfd, &daddr, PAYLOAD_LEN, PAYLOAD_LEN);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "data_lrg_sml") == 0) {
		send_data_pkts(txfd, &daddr, PAYLOAD_LEN, PAYLOAD_LEN / 2);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "data_lrg_1byte") == 0) {
		send_data_pkts(txfd, &daddr, PAYLOAD_LEN, 1);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "data_sml_lrg") == 0) {
		send_data_pkts(txfd, &daddr, PAYLOAD_LEN / 2, PAYLOAD_LEN);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "data_burst") == 0) {
		static char buf[MAX_HDR_LEN + PAYLOAD_LEN];

		create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
		write_packet(txfd, buf, total_hdr_len + PAYLOAD_LEN, &daddr);
		create_packet(buf, PAYLOAD_LEN, 0, PAYLOAD_LEN, 0);
		write_packet(txfd, buf, total_hdr_len + PAYLOAD_LEN, &daddr);

		usleep(100 * 1000); /* 100ms */
		create_packet(buf, PAYLOAD_LEN * 2, 0, PAYLOAD_LEN, 0);
		write_packet(txfd, buf, total_hdr_len + PAYLOAD_LEN, &daddr);
		create_packet(buf, PAYLOAD_LEN * 3, 0, PAYLOAD_LEN, 0);
		write_packet(txfd, buf, total_hdr_len + PAYLOAD_LEN, &daddr);

		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* ack test */
	} else if (strcmp(testname, "ack") == 0) {
		send_ack(txfd, &daddr);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* flags sub-tests */
	} else if (strcmp(testname, "flags_psh") == 0) {
		send_flags(txfd, &daddr, 1, 0, 0, 0, 0);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "flags_syn") == 0) {
		send_flags(txfd, &daddr, 0, 1, 0, 0, 0);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "flags_rst") == 0) {
		send_flags(txfd, &daddr, 0, 0, 1, 0, 0);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "flags_urg") == 0) {
		send_flags(txfd, &daddr, 0, 0, 0, 1, 0);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "flags_cwr") == 0) {
		send_flags(txfd, &daddr, 0, 0, 0, 0, 1);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* tcp sub-tests */
	} else if (strcmp(testname, "tcp_csum") == 0) {
		send_changed_checksum(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "tcp_seq") == 0) {
		send_changed_seq(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "tcp_ts") == 0) {
		send_changed_ts(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "tcp_opt") == 0) {
		send_diff_opt(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* ip sub-tests - shared between IPv4 and IPv6 */
	} else if (strcmp(testname, "ip_ecn") == 0) {
		send_changed_ECN(txfd, &daddr);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_tos") == 0) {
		send_changed_tos(txfd, &daddr);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* ip sub-tests - IPv4 only */
	} else if (strcmp(testname, "ip_csum") == 0) {
		send_changed_ip_checksum(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_ttl") == 0) {
		send_changed_ttl(txfd, &daddr);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_opt") == 0) {
		send_ip_options(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_frag4") == 0) {
		send_fragment4(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_id_df1_inc") == 0) {
		send_flush_id_case(txfd, &daddr, FLUSH_ID_DF1_INC);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_id_df1_fixed") == 0) {
		send_flush_id_case(txfd, &daddr, FLUSH_ID_DF1_FIXED);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_id_df0_inc") == 0) {
		send_flush_id_case(txfd, &daddr, FLUSH_ID_DF0_INC);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_id_df0_fixed") == 0) {
		send_flush_id_case(txfd, &daddr, FLUSH_ID_DF0_FIXED);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_id_df1_inc_fixed") == 0) {
		send_flush_id_case(txfd, &daddr, FLUSH_ID_DF1_INC_FIXED);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_id_df1_fixed_inc") == 0) {
		send_flush_id_case(txfd, &daddr, FLUSH_ID_DF1_FIXED_INC);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* ip sub-tests - IPv6 only */
	} else if (strcmp(testname, "ip_frag6") == 0) {
		send_fragment6(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_v6ext_same") == 0) {
		send_ipv6_exthdr(txfd, &daddr, EXT_PAYLOAD_1, EXT_PAYLOAD_1);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "ip_v6ext_diff") == 0) {
		send_ipv6_exthdr(txfd, &daddr, EXT_PAYLOAD_1, EXT_PAYLOAD_2);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* large sub-tests */
	} else if (strcmp(testname, "large_max") == 0) {
		int remainder = max_payload() % calc_mss();

		send_large(txfd, &daddr, remainder);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "large_rem") == 0) {
		int remainder = max_payload() % calc_mss();

		send_large(txfd, &daddr, remainder + 1);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	/* machinery sub-tests */
	} else if (strcmp(testname, "single") == 0) {
		static char buf[MAX_HDR_LEN + PAYLOAD_LEN];

		create_packet(buf, 0, 0, PAYLOAD_LEN, 0);
		write_packet(txfd, buf, total_hdr_len + PAYLOAD_LEN, &daddr);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);
	} else if (strcmp(testname, "capacity") == 0) {
		send_capacity(txfd, &daddr);
		usleep(fin_delay_us);
		write_packet(txfd, fin_pkt, total_hdr_len, &daddr);

	} else {
		error(1, 0, "Unknown testcase: %s", testname);
	}

	if (close(txfd))
		error(1, errno, "socket close");
}

static void gro_receiver(void)
{
	static int correct_payload[NUM_PACKETS];
	int rxfd = -1;

	rxfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_NONE));
	if (rxfd < 0)
		error(1, 0, "socket creation");
	setup_sock_filter(rxfd);
	set_timeout(rxfd);
	set_rcvbuf(rxfd);
	bind_packetsocket(rxfd);

	ksft_ready();

	memset(correct_payload, 0, sizeof(correct_payload));

	/* data sub-tests */
	if (strcmp(testname, "data_same") == 0) {
		printf("pure data packet of same size: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "data_lrg_sml") == 0) {
		printf("large data packets followed by a smaller one: ");
		correct_payload[0] = PAYLOAD_LEN * 1.5;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "data_lrg_1byte") == 0) {
		printf("large data packet followed by a 1 byte one: ");
		correct_payload[0] = PAYLOAD_LEN + 1;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "data_sml_lrg") == 0) {
		printf("small data packets followed by a larger one: ");
		correct_payload[0] = PAYLOAD_LEN / 2;
		correct_payload[1] = PAYLOAD_LEN;
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "data_burst") == 0) {
		printf("two bursts of two data packets: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = PAYLOAD_LEN * 2;
		check_recv_pkts(rxfd, correct_payload, 2);

	/* ack test */
	} else if (strcmp(testname, "ack") == 0) {
		printf("duplicate ack and pure ack: ");
		check_recv_pkts(rxfd, correct_payload, 3);

	/* flags sub-tests */
	} else if (strcmp(testname, "flags_psh") == 0) {
		correct_payload[0] = PAYLOAD_LEN * 3;
		correct_payload[1] = PAYLOAD_LEN * 2;
		printf("psh flag ends coalescing: ");
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "flags_syn") == 0) {
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = 0;
		correct_payload[2] = PAYLOAD_LEN * 2;
		printf("syn flag ends coalescing: ");
		check_recv_pkts(rxfd, correct_payload, 3);
	} else if (strcmp(testname, "flags_rst") == 0) {
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = 0;
		correct_payload[2] = PAYLOAD_LEN * 2;
		printf("rst flag ends coalescing: ");
		check_recv_pkts(rxfd, correct_payload, 3);
	} else if (strcmp(testname, "flags_urg") == 0) {
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = 0;
		correct_payload[2] = PAYLOAD_LEN * 2;
		printf("urg flag ends coalescing: ");
		check_recv_pkts(rxfd, correct_payload, 3);
	} else if (strcmp(testname, "flags_cwr") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN * 2;
		correct_payload[2] = PAYLOAD_LEN * 2;
		printf("cwr flag ends coalescing: ");
		check_recv_pkts(rxfd, correct_payload, 3);

	/* tcp sub-tests */
	} else if (strcmp(testname, "tcp_csum") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		printf("changed checksum does not coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "tcp_seq") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		printf("Wrong Seq number doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "tcp_ts") == 0) {
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = PAYLOAD_LEN;
		correct_payload[2] = PAYLOAD_LEN;
		correct_payload[3] = PAYLOAD_LEN;
		printf("Different timestamp doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 4);
	} else if (strcmp(testname, "tcp_opt") == 0) {
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = PAYLOAD_LEN;
		printf("Different options doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 2);

	/* ip sub-tests - shared between IPv4 and IPv6 */
	} else if (strcmp(testname, "ip_ecn") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		printf("different ECN doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "ip_tos") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		printf("different tos doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 2);

	/* ip sub-tests - IPv4 only */
	} else if (strcmp(testname, "ip_csum") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		correct_payload[2] = PAYLOAD_LEN;
		printf("bad ip checksum doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 3);
	} else if (strcmp(testname, "ip_ttl") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		printf("different ttl doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "ip_opt") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		correct_payload[2] = PAYLOAD_LEN;
		printf("ip options doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 3);
	} else if (strcmp(testname, "ip_frag4") == 0) {
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		printf("fragmented ip4 doesn't coalesce: ");
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "ip_id_df1_inc") == 0) {
		printf("DF=1, Incrementing - should coalesce: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "ip_id_df1_fixed") == 0) {
		printf("DF=1, Fixed - should coalesce: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "ip_id_df0_inc") == 0) {
		printf("DF=0, Incrementing - should coalesce: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "ip_id_df0_fixed") == 0) {
		printf("DF=0, Fixed - should coalesce: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "ip_id_df1_inc_fixed") == 0) {
		printf("DF=1, 2 Incrementing and one fixed - should coalesce only first 2 packets: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = PAYLOAD_LEN;
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "ip_id_df1_fixed_inc") == 0) {
		printf("DF=1, 2 Fixed and one incrementing - should coalesce only first 2 packets: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = PAYLOAD_LEN;
		check_recv_pkts(rxfd, correct_payload, 2);

	/* ip sub-tests - IPv6 only */
	} else if (strcmp(testname, "ip_frag6") == 0) {
		/* GRO doesn't check for ipv6 hop limit when flushing.
		 * Hence no corresponding test to the ipv4 case.
		 */
		printf("fragmented ip6 doesn't coalesce: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		correct_payload[1] = PAYLOAD_LEN;
		correct_payload[2] = PAYLOAD_LEN;
		check_recv_pkts(rxfd, correct_payload, 3);
	} else if (strcmp(testname, "ip_v6ext_same") == 0) {
		printf("ipv6 with ext header does coalesce: ");
		correct_payload[0] = PAYLOAD_LEN * 2;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "ip_v6ext_diff") == 0) {
		printf("ipv6 with ext header with different payloads doesn't coalesce: ");
		correct_payload[0] = PAYLOAD_LEN;
		correct_payload[1] = PAYLOAD_LEN;
		check_recv_pkts(rxfd, correct_payload, 2);

	/* large sub-tests */
	} else if (strcmp(testname, "large_max") == 0) {
		int remainder = max_payload() % calc_mss();

		correct_payload[0] = max_payload();
		correct_payload[1] = remainder;
		printf("Shouldn't coalesce if exceed IP max pkt size: ");
		check_recv_pkts(rxfd, correct_payload, 2);
	} else if (strcmp(testname, "large_rem") == 0) {
		int remainder = max_payload() % calc_mss();

		/* last segment sent individually, doesn't start new segment */
		correct_payload[0] = max_payload() - remainder;
		correct_payload[1] = remainder + 1;
		correct_payload[2] = remainder + 1;
		printf("last segment sent individually: ");
		check_recv_pkts(rxfd, correct_payload, 3);

	/* machinery sub-tests */
	} else if (strcmp(testname, "single") == 0) {
		printf("single data packet: ");
		correct_payload[0] = PAYLOAD_LEN;
		check_recv_pkts(rxfd, correct_payload, 1);
	} else if (strcmp(testname, "capacity") == 0) {
		check_capacity_pkts(rxfd);

	} else {
		error(1, 0, "Test case error: unknown testname %s", testname);
	}

	if (close(rxfd))
		error(1, 0, "socket close");
}

static void parse_args(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "daddr", required_argument, NULL, 'd' },
		{ "dmac", required_argument, NULL, 'D' },
		{ "iface", required_argument, NULL, 'i' },
		{ "ipv4", no_argument, NULL, '4' },
		{ "ipv6", no_argument, NULL, '6' },
		{ "ipip", no_argument, NULL, 'e' },
		{ "ip6ip6", no_argument, NULL, 'E' },
		{ "num-flows", required_argument, NULL, 'n' },
		{ "rx", no_argument, NULL, 'r' },
		{ "saddr", required_argument, NULL, 's' },
		{ "smac", required_argument, NULL, 'S' },
		{ "test", required_argument, NULL, 't' },
		{ "order-check", no_argument, NULL, 'o' },
		{ "verbose", no_argument, NULL, 'v' },
		{ 0, 0, 0, 0 }
	};
	int c;

	while ((c = getopt_long(argc, argv, "46d:D:eEi:n:rs:S:t:ov", opts, NULL)) != -1) {
		switch (c) {
		case '4':
			proto = PF_INET;
			ethhdr_proto = htons(ETH_P_IP);
			break;
		case '6':
			proto = PF_INET6;
			ethhdr_proto = htons(ETH_P_IPV6);
			break;
		case 'e':
			ipip = true;
			proto = PF_INET;
			ethhdr_proto = htons(ETH_P_IP);
			break;
		case 'E':
			ip6ip6 = true;
			proto = PF_INET6;
			ethhdr_proto = htons(ETH_P_IPV6);
			break;
		case 'd':
			addr4_dst = addr6_dst = optarg;
			break;
		case 'D':
			dmac = optarg;
			break;
		case 'i':
			ifname = optarg;
			break;
		case 'n':
			num_flows = atoi(optarg);
			break;
		case 'r':
			tx_socket = false;
			break;
		case 's':
			addr4_src = addr6_src = optarg;
			break;
		case 'S':
			smac = optarg;
			break;
		case 't':
			testname = optarg;
			break;
		case 'o':
			order_check = true;
			break;
		case 'v':
			verbose = true;
			break;
		default:
			error(1, 0, "%s invalid option %c\n", __func__, c);
			break;
		}
	}
}

int main(int argc, char **argv)
{
	parse_args(argc, argv);

	if (ipip) {
		tcp_offset = ETH_HLEN + sizeof(struct iphdr) * 2;
		total_hdr_len = tcp_offset + sizeof(struct tcphdr);
	} else if (ip6ip6) {
		tcp_offset = ETH_HLEN + sizeof(struct ipv6hdr) * 2;
		total_hdr_len = tcp_offset + sizeof(struct tcphdr);
	} else if (proto == PF_INET) {
		tcp_offset = ETH_HLEN + sizeof(struct iphdr);
		total_hdr_len = tcp_offset + sizeof(struct tcphdr);
	} else if (proto == PF_INET6) {
		tcp_offset = ETH_HLEN + sizeof(struct ipv6hdr);
		total_hdr_len = tcp_offset + sizeof(struct tcphdr);
	} else {
		error(1, 0, "Protocol family is not ipv4 or ipv6");
	}

	read_MAC(src_mac, smac);
	read_MAC(dst_mac, dmac);

	if (tx_socket) {
		gro_sender();
	} else {
		/* Only the receiver exit status determines test success. */
		gro_receiver();
		fprintf(stderr, "Gro::%s test passed.\n", testname);
	}

	return 0;
}
