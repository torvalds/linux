// SPDX-License-Identifier: GPL-2.0
/*
 * Selftest that verifies that incomping ICMPs are ignored,
 * the TCP connection stays alive, no hard or soft errors get reported
 * to the usespace and the counter for ignored ICMPs is updated.
 *
 * RFC5925, 7.8:
 * >> A TCP-AO implementation MUST default to ignore incoming ICMPv4
 * messages of Type 3 (destination unreachable), Codes 2-4 (protocol
 * unreachable, port unreachable, and fragmentation needed -- ’hard
 * errors’), and ICMPv6 Type 1 (destination unreachable), Code 1
 * (administratively prohibited) and Code 4 (port unreachable) intended
 * for connections in synchronized states (ESTABLISHED, FIN-WAIT-1, FIN-
 * WAIT-2, CLOSE-WAIT, CLOSING, LAST-ACK, TIME-WAIT) that match MKTs.
 *
 * Author: Dmitry Safonov <dima@arista.com>
 */
#include <inttypes.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/ipv6.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include "aolib.h"
#include "../../../../include/linux/compiler.h"

const size_t packets_nr = 20;
const size_t packet_size = 100;
const char *tcpao_icmps	= "TCPAODroppedIcmps";

#ifdef IPV6_TEST
const char *dst_unreach	= "Icmp6InDestUnreachs";
const int sk_ip_level	= SOL_IPV6;
const int sk_recverr	= IPV6_RECVERR;
#else
const char *dst_unreach	= "InDestUnreachs";
const int sk_ip_level	= SOL_IP;
const int sk_recverr	= IP_RECVERR;
#endif

/* Server is expected to fail with hard error if ::accept_icmp is set */
#ifdef TEST_ICMPS_ACCEPT
# define test_icmps_fail test_ok
# define test_icmps_ok test_fail
#else
# define test_icmps_fail test_fail
# define test_icmps_ok test_ok
#endif

static void serve_interfered(int sk)
{
	ssize_t test_quota = packet_size * packets_nr * 10;
	uint64_t dest_unreach_a, dest_unreach_b;
	uint64_t icmp_ignored_a, icmp_ignored_b;
	struct tcp_ao_counters ao_cnt1, ao_cnt2;
	bool counter_not_found;
	struct netstat *ns_after, *ns_before;
	ssize_t bytes;

	ns_before = netstat_read();
	dest_unreach_a = netstat_get(ns_before, dst_unreach, NULL);
	icmp_ignored_a = netstat_get(ns_before, tcpao_icmps, NULL);
	if (test_get_tcp_ao_counters(sk, &ao_cnt1))
		test_error("test_get_tcp_ao_counters()");
	bytes = test_server_run(sk, test_quota, 0);
	ns_after = netstat_read();
	netstat_print_diff(ns_before, ns_after);
	dest_unreach_b = netstat_get(ns_after, dst_unreach, NULL);
	icmp_ignored_b = netstat_get(ns_after, tcpao_icmps,
					&counter_not_found);
	if (test_get_tcp_ao_counters(sk, &ao_cnt2))
		test_error("test_get_tcp_ao_counters()");

	netstat_free(ns_before);
	netstat_free(ns_after);

	if (dest_unreach_a >= dest_unreach_b) {
		test_fail("%s counter didn't change: %" PRIu64 " >= %" PRIu64,
				dst_unreach, dest_unreach_a, dest_unreach_b);
		return;
	}
	test_ok("%s delivered %" PRIu64,
		dst_unreach, dest_unreach_b - dest_unreach_a);
	if (bytes < 0)
		test_icmps_fail("Server failed with %zd: %s", bytes, strerrordesc_np(-bytes));
	else
		test_icmps_ok("Server survived %zd bytes of traffic", test_quota);
	if (counter_not_found) {
		test_fail("Not found %s counter", tcpao_icmps);
		return;
	}
#ifdef TEST_ICMPS_ACCEPT
	test_tcp_ao_counters_cmp(NULL, &ao_cnt1, &ao_cnt2, TEST_CNT_GOOD);
#else
	test_tcp_ao_counters_cmp(NULL, &ao_cnt1, &ao_cnt2, TEST_CNT_GOOD | TEST_CNT_AO_DROPPED_ICMP);
#endif
	if (icmp_ignored_a >= icmp_ignored_b) {
		test_icmps_fail("%s counter didn't change: %" PRIu64 " >= %" PRIu64,
				tcpao_icmps, icmp_ignored_a, icmp_ignored_b);
		return;
	}
	test_icmps_ok("ICMPs ignored %" PRIu64, icmp_ignored_b - icmp_ignored_a);
}

static void *server_fn(void *arg)
{
	int val, sk, lsk;
	bool accept_icmps = false;

	lsk = test_listen_socket(this_ip_addr, test_server_port, 1);

#ifdef TEST_ICMPS_ACCEPT
	accept_icmps = true;
#endif

	if (test_set_ao_flags(lsk, false, accept_icmps))
		test_error("setsockopt(TCP_AO_INFO)");

	if (test_add_key(lsk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");
	synchronize_threads();

	if (test_wait_fd(lsk, TEST_TIMEOUT_SEC, 0))
		test_error("test_wait_fd()");

	sk = accept(lsk, NULL, NULL);
	if (sk < 0)
		test_error("accept()");

	/* Fail on hard ip errors, such as dest unreachable (RFC1122) */
	val = 1;
	if (setsockopt(sk, sk_ip_level, sk_recverr, &val, sizeof(val)))
		test_error("setsockopt()");

	synchronize_threads();

	serve_interfered(sk);
	return NULL;
}

static size_t packets_sent;
static size_t icmps_sent;

static uint32_t checksum4_nofold(void *data, size_t len, uint32_t sum)
{
	uint16_t *words = data;
	size_t i;

	for (i = 0; i < len / sizeof(uint16_t); i++)
		sum += words[i];
	if (len & 1)
		sum += ((char *)data)[len - 1];
	return sum;
}

static uint16_t checksum4_fold(void *data, size_t len, uint32_t sum)
{
	sum = checksum4_nofold(data, len, sum);
	while (sum > 0xFFFF)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return ~sum;
}

static void set_ip4hdr(struct iphdr *iph, size_t packet_len, int proto,
		struct sockaddr_in *src, struct sockaddr_in *dst)
{
	iph->version	= 4;
	iph->ihl	= 5;
	iph->tos	= 0;
	iph->tot_len	= htons(packet_len);
	iph->ttl	= 2;
	iph->protocol	= proto;
	iph->saddr	= src->sin_addr.s_addr;
	iph->daddr	= dst->sin_addr.s_addr;
	iph->check	= checksum4_fold((void *)iph, iph->ihl << 1, 0);
}

static void icmp_interfere4(uint8_t type, uint8_t code, uint32_t rcv_nxt,
		struct sockaddr_in *src, struct sockaddr_in *dst)
{
	int sk = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	struct {
		struct iphdr iph;
		struct icmphdr icmph;
		struct iphdr iphe;
		struct {
			uint16_t sport;
			uint16_t dport;
			uint32_t seq;
		} tcph;
	} packet = {};
	size_t packet_len;
	ssize_t bytes;

	if (sk < 0)
		test_error("socket(AF_INET, SOCK_RAW, IPPROTO_RAW)");

	packet_len = sizeof(packet);
	set_ip4hdr(&packet.iph, packet_len, IPPROTO_ICMP, src, dst);

	packet.icmph.type = type;
	packet.icmph.code = code;
	if (code == ICMP_FRAG_NEEDED) {
		randomize_buffer(&packet.icmph.un.frag.mtu,
				sizeof(packet.icmph.un.frag.mtu));
	}

	packet_len = sizeof(packet.iphe) + sizeof(packet.tcph);
	set_ip4hdr(&packet.iphe, packet_len, IPPROTO_TCP, dst, src);

	packet.tcph.sport = dst->sin_port;
	packet.tcph.dport = src->sin_port;
	packet.tcph.seq = htonl(rcv_nxt);

	packet_len = sizeof(packet) - sizeof(packet.iph);
	packet.icmph.checksum = checksum4_fold((void *)&packet.icmph,
						packet_len, 0);

	bytes = sendto(sk, &packet, sizeof(packet), 0,
		       (struct sockaddr *)dst, sizeof(*dst));
	if (bytes != sizeof(packet))
		test_error("send(): %zd", bytes);
	icmps_sent++;

	close(sk);
}

static void set_ip6hdr(struct ipv6hdr *iph, size_t packet_len, int proto,
		struct sockaddr_in6 *src, struct sockaddr_in6 *dst)
{
	iph->version		= 6;
	iph->payload_len	= htons(packet_len);
	iph->nexthdr		= proto;
	iph->hop_limit		= 2;
	iph->saddr		= src->sin6_addr;
	iph->daddr		= dst->sin6_addr;
}

static inline uint16_t csum_fold(uint32_t csum)
{
	uint32_t sum = csum;

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return (uint16_t)~sum;
}

static inline uint32_t csum_add(uint32_t csum, uint32_t addend)
{
	uint32_t res = csum;

	res += addend;
	return res + (res < addend);
}

noinline uint32_t checksum6_nofold(void *data, size_t len, uint32_t sum)
{
	uint16_t *words = data;
	size_t i;

	for (i = 0; i < len / sizeof(uint16_t); i++)
		sum = csum_add(sum, words[i]);
	if (len & 1)
		sum = csum_add(sum, ((char *)data)[len - 1]);
	return sum;
}

noinline uint16_t icmp6_checksum(struct sockaddr_in6 *src,
				 struct sockaddr_in6 *dst,
				 void *ptr, size_t len, uint8_t proto)
{
	struct {
		struct in6_addr saddr;
		struct in6_addr daddr;
		uint32_t payload_len;
		uint8_t zero[3];
		uint8_t nexthdr;
	} pseudo_header = {};
	uint32_t sum;

	pseudo_header.saddr		= src->sin6_addr;
	pseudo_header.daddr		= dst->sin6_addr;
	pseudo_header.payload_len	= htonl(len);
	pseudo_header.nexthdr		= proto;

	sum = checksum6_nofold(&pseudo_header, sizeof(pseudo_header), 0);
	sum = checksum6_nofold(ptr, len, sum);

	return csum_fold(sum);
}

static void icmp6_interfere(int type, int code, uint32_t rcv_nxt,
		struct sockaddr_in6 *src, struct sockaddr_in6 *dst)
{
	int sk = socket(AF_INET6, SOCK_RAW, IPPROTO_RAW);
	struct sockaddr_in6 dst_raw = *dst;
	struct {
		struct ipv6hdr iph;
		struct icmp6hdr icmph;
		struct ipv6hdr iphe;
		struct {
			uint16_t sport;
			uint16_t dport;
			uint32_t seq;
		} tcph;
	} packet = {};
	size_t packet_len;
	ssize_t bytes;


	if (sk < 0)
		test_error("socket(AF_INET6, SOCK_RAW, IPPROTO_RAW)");

	packet_len = sizeof(packet) - sizeof(packet.iph);
	set_ip6hdr(&packet.iph, packet_len, IPPROTO_ICMPV6, src, dst);

	packet.icmph.icmp6_type = type;
	packet.icmph.icmp6_code = code;

	packet_len = sizeof(packet.iphe) + sizeof(packet.tcph);
	set_ip6hdr(&packet.iphe, packet_len, IPPROTO_TCP, dst, src);

	packet.tcph.sport = dst->sin6_port;
	packet.tcph.dport = src->sin6_port;
	packet.tcph.seq = htonl(rcv_nxt);

	packet_len = sizeof(packet) - sizeof(packet.iph);

	packet.icmph.icmp6_cksum = icmp6_checksum(src, dst,
			(void *)&packet.icmph, packet_len, IPPROTO_ICMPV6);

	dst_raw.sin6_port = htons(IPPROTO_RAW);
	bytes = sendto(sk, &packet, sizeof(packet), 0,
		       (struct sockaddr *)&dst_raw, sizeof(dst_raw));
	if (bytes != sizeof(packet))
		test_error("send(): %zd", bytes);
	icmps_sent++;

	close(sk);
}

static uint32_t get_rcv_nxt(int sk)
{
	int val = TCP_REPAIR_ON;
	uint32_t ret;
	socklen_t sz = sizeof(ret);

	if (setsockopt(sk, SOL_TCP, TCP_REPAIR, &val, sizeof(val)))
		test_error("setsockopt(TCP_REPAIR)");
	val = TCP_RECV_QUEUE;
	if (setsockopt(sk, SOL_TCP, TCP_REPAIR_QUEUE, &val, sizeof(val)))
		test_error("setsockopt(TCP_REPAIR_QUEUE)");
	if (getsockopt(sk, SOL_TCP, TCP_QUEUE_SEQ, &ret, &sz))
		test_error("getsockopt(TCP_QUEUE_SEQ)");
	val = TCP_REPAIR_OFF_NO_WP;
	if (setsockopt(sk, SOL_TCP, TCP_REPAIR, &val, sizeof(val)))
		test_error("setsockopt(TCP_REPAIR)");
	return ret;
}

static void icmp_interfere(const size_t nr, uint32_t rcv_nxt, void *src, void *dst)
{
	struct sockaddr_in *saddr4 = src;
	struct sockaddr_in *daddr4 = dst;
	struct sockaddr_in6 *saddr6 = src;
	struct sockaddr_in6 *daddr6 = dst;
	size_t i;

	if (saddr4->sin_family != daddr4->sin_family)
		test_error("Different address families");

	for (i = 0; i < nr; i++) {
		if (saddr4->sin_family == AF_INET) {
			icmp_interfere4(ICMP_DEST_UNREACH, ICMP_PROT_UNREACH,
					rcv_nxt, saddr4, daddr4);
			icmp_interfere4(ICMP_DEST_UNREACH, ICMP_PORT_UNREACH,
					rcv_nxt, saddr4, daddr4);
			icmp_interfere4(ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
					rcv_nxt, saddr4, daddr4);
			icmps_sent += 3;
		} else if (saddr4->sin_family == AF_INET6) {
			icmp6_interfere(ICMPV6_DEST_UNREACH,
					ICMPV6_ADM_PROHIBITED,
					rcv_nxt, saddr6, daddr6);
			icmp6_interfere(ICMPV6_DEST_UNREACH,
					ICMPV6_PORT_UNREACH,
					rcv_nxt, saddr6, daddr6);
			icmps_sent += 2;
		} else {
			test_error("Not ip address family");
		}
	}
}

static void send_interfered(int sk)
{
	const unsigned int timeout = TEST_TIMEOUT_SEC;
	struct sockaddr_in6 src, dst;
	socklen_t addr_sz;

	addr_sz = sizeof(src);
	if (getsockname(sk, &src, &addr_sz))
		test_error("getsockname()");
	addr_sz = sizeof(dst);
	if (getpeername(sk, &dst, &addr_sz))
		test_error("getpeername()");

	while (1) {
		uint32_t rcv_nxt;

		if (test_client_verify(sk, packet_size, packets_nr, timeout)) {
			test_fail("client: connection is broken");
			return;
		}
		packets_sent += packets_nr;
		rcv_nxt = get_rcv_nxt(sk);
		icmp_interfere(packets_nr, rcv_nxt, (void *)&src, (void *)&dst);
	}
}

static void *client_fn(void *arg)
{
	int sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);

	if (sk < 0)
		test_error("socket()");

	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	synchronize_threads();
	if (test_connect_socket(sk, this_ip_dest, test_server_port) <= 0)
		test_error("failed to connect()");
	synchronize_threads();

	send_interfered(sk);

	/* Not expecting client to quit */
	test_fail("client disconnected");

	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(4, server_fn, client_fn);
	return 0;
}
