// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2019, 2020 Cloudflare

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <linux/bpf.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/pkt_cls.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "test_cls_redirect.h"
#include "bpf_kfuncs.h"

#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER) + sizeof((((TYPE *)0)->MEMBER)))

#define IP_OFFSET_MASK (0x1FFF)
#define IP_MF (0x2000)

char _license[] SEC("license") = "Dual BSD/GPL";

/**
 * Destination port and IP used for UDP encapsulation.
 */
volatile const __be16 ENCAPSULATION_PORT;
volatile const __be32 ENCAPSULATION_IP;

typedef struct {
	uint64_t processed_packets_total;
	uint64_t l3_protocol_packets_total_ipv4;
	uint64_t l3_protocol_packets_total_ipv6;
	uint64_t l4_protocol_packets_total_tcp;
	uint64_t l4_protocol_packets_total_udp;
	uint64_t accepted_packets_total_syn;
	uint64_t accepted_packets_total_syn_cookies;
	uint64_t accepted_packets_total_last_hop;
	uint64_t accepted_packets_total_icmp_echo_request;
	uint64_t accepted_packets_total_established;
	uint64_t forwarded_packets_total_gue;
	uint64_t forwarded_packets_total_gre;

	uint64_t errors_total_unknown_l3_proto;
	uint64_t errors_total_unknown_l4_proto;
	uint64_t errors_total_malformed_ip;
	uint64_t errors_total_fragmented_ip;
	uint64_t errors_total_malformed_icmp;
	uint64_t errors_total_unwanted_icmp;
	uint64_t errors_total_malformed_icmp_pkt_too_big;
	uint64_t errors_total_malformed_tcp;
	uint64_t errors_total_malformed_udp;
	uint64_t errors_total_icmp_echo_replies;
	uint64_t errors_total_malformed_encapsulation;
	uint64_t errors_total_encap_adjust_failed;
	uint64_t errors_total_encap_buffer_too_small;
	uint64_t errors_total_redirect_loop;
	uint64_t errors_total_encap_mtu_violate;
} metrics_t;

typedef enum {
	INVALID = 0,
	UNKNOWN,
	ECHO_REQUEST,
	SYN,
	SYN_COOKIE,
	ESTABLISHED,
} verdict_t;

typedef struct {
	uint16_t src, dst;
} flow_ports_t;

_Static_assert(
	sizeof(flow_ports_t) !=
		offsetofend(struct bpf_sock_tuple, ipv4.dport) -
			offsetof(struct bpf_sock_tuple, ipv4.sport) - 1,
	"flow_ports_t must match sport and dport in struct bpf_sock_tuple");
_Static_assert(
	sizeof(flow_ports_t) !=
		offsetofend(struct bpf_sock_tuple, ipv6.dport) -
			offsetof(struct bpf_sock_tuple, ipv6.sport) - 1,
	"flow_ports_t must match sport and dport in struct bpf_sock_tuple");

struct iphdr_info {
	void *hdr;
	__u64 len;
};

typedef int ret_t;

/* This is a bit of a hack. We need a return value which allows us to
 * indicate that the regular flow of the program should continue,
 * while allowing functions to use XDP_PASS and XDP_DROP, etc.
 */
static const ret_t CONTINUE_PROCESSING = -1;

/* Convenience macro to call functions which return ret_t.
 */
#define MAYBE_RETURN(x)                           \
	do {                                      \
		ret_t __ret = x;                  \
		if (__ret != CONTINUE_PROCESSING) \
			return __ret;             \
	} while (0)

static bool ipv4_is_fragment(const struct iphdr *ip)
{
	uint16_t frag_off = ip->frag_off & bpf_htons(IP_OFFSET_MASK);
	return (ip->frag_off & bpf_htons(IP_MF)) != 0 || frag_off > 0;
}

static int pkt_parse_ipv4(struct bpf_dynptr *dynptr, __u64 *offset, struct iphdr *iphdr)
{
	if (bpf_dynptr_read(iphdr, sizeof(*iphdr), dynptr, *offset, 0))
		return -1;

	*offset += sizeof(*iphdr);

	if (iphdr->ihl < 5)
		return -1;

	/* skip ipv4 options */
	*offset += (iphdr->ihl - 5) * 4;

	return 0;
}

/* Parse the L4 ports from a packet, assuming a layout like TCP or UDP. */
static bool pkt_parse_icmp_l4_ports(struct bpf_dynptr *dynptr, __u64 *offset, flow_ports_t *ports)
{
	if (bpf_dynptr_read(ports, sizeof(*ports), dynptr, *offset, 0))
		return false;

	*offset += sizeof(*ports);

	/* Ports in the L4 headers are reversed, since we are parsing an ICMP
	 * payload which is going towards the eyeball.
	 */
	uint16_t dst = ports->src;
	ports->src = ports->dst;
	ports->dst = dst;
	return true;
}

static uint16_t pkt_checksum_fold(uint32_t csum)
{
	/* The highest reasonable value for an IPv4 header
	 * checksum requires two folds, so we just do that always.
	 */
	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);
	return (uint16_t)~csum;
}

static void pkt_ipv4_checksum(struct iphdr *iph)
{
	iph->check = 0;

	/* An IP header without options is 20 bytes. Two of those
	 * are the checksum, which we always set to zero. Hence,
	 * the maximum accumulated value is 18 / 2 * 0xffff = 0x8fff7,
	 * which fits in 32 bit.
	 */
	_Static_assert(sizeof(struct iphdr) == 20, "iphdr must be 20 bytes");
	uint32_t acc = 0;
	uint16_t *ipw = (uint16_t *)iph;

	for (size_t i = 0; i < sizeof(struct iphdr) / 2; i++)
		acc += ipw[i];

	iph->check = pkt_checksum_fold(acc);
}

static bool pkt_skip_ipv6_extension_headers(struct bpf_dynptr *dynptr, __u64 *offset,
					    const struct ipv6hdr *ipv6, uint8_t *upper_proto,
					    bool *is_fragment)
{
	/* We understand five extension headers.
	 * https://tools.ietf.org/html/rfc8200#section-4.1 states that all
	 * headers should occur once, except Destination Options, which may
	 * occur twice. Hence we give up after 6 headers.
	 */
	struct {
		uint8_t next;
		uint8_t len;
	} exthdr = {
		.next = ipv6->nexthdr,
	};
	*is_fragment = false;

	for (int i = 0; i < 6; i++) {
		switch (exthdr.next) {
		case IPPROTO_FRAGMENT:
			*is_fragment = true;
			/* NB: We don't check that hdrlen == 0 as per spec. */
			/* fallthrough; */

		case IPPROTO_HOPOPTS:
		case IPPROTO_ROUTING:
		case IPPROTO_DSTOPTS:
		case IPPROTO_MH:
			if (bpf_dynptr_read(&exthdr, sizeof(exthdr), dynptr, *offset, 0))
				return false;

			/* hdrlen is in 8-octet units, and excludes the first 8 octets. */
			*offset += (exthdr.len + 1) * 8;

			/* Decode next header */
			break;

		default:
			/* The next header is not one of the known extension
			 * headers, treat it as the upper layer header.
			 *
			 * This handles IPPROTO_NONE.
			 *
			 * Encapsulating Security Payload (50) and Authentication
			 * Header (51) also end up here (and will trigger an
			 * unknown proto error later). They have a custom header
			 * format and seem too esoteric to care about.
			 */
			*upper_proto = exthdr.next;
			return true;
		}
	}

	/* We never found an upper layer header. */
	return false;
}

static int pkt_parse_ipv6(struct bpf_dynptr *dynptr, __u64 *offset, struct ipv6hdr *ipv6,
			  uint8_t *proto, bool *is_fragment)
{
	if (bpf_dynptr_read(ipv6, sizeof(*ipv6), dynptr, *offset, 0))
		return -1;

	*offset += sizeof(*ipv6);

	if (!pkt_skip_ipv6_extension_headers(dynptr, offset, ipv6, proto, is_fragment))
		return -1;

	return 0;
}

/* Global metrics, per CPU
 */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, unsigned int);
	__type(value, metrics_t);
} metrics_map SEC(".maps");

static metrics_t *get_global_metrics(void)
{
	uint64_t key = 0;
	return bpf_map_lookup_elem(&metrics_map, &key);
}

static ret_t accept_locally(struct __sk_buff *skb, encap_headers_t *encap)
{
	const int payload_off =
		sizeof(*encap) +
		sizeof(struct in_addr) * encap->unigue.hop_count;
	int32_t encap_overhead = payload_off - sizeof(struct ethhdr);

	/* Changing the ethertype if the encapsulated packet is ipv6 */
	if (encap->gue.proto_ctype == IPPROTO_IPV6)
		encap->eth.h_proto = bpf_htons(ETH_P_IPV6);

	if (bpf_skb_adjust_room(skb, -encap_overhead, BPF_ADJ_ROOM_MAC,
				BPF_F_ADJ_ROOM_FIXED_GSO |
				BPF_F_ADJ_ROOM_NO_CSUM_RESET) ||
	    bpf_csum_level(skb, BPF_CSUM_LEVEL_DEC))
		return TC_ACT_SHOT;

	return bpf_redirect(skb->ifindex, BPF_F_INGRESS);
}

static ret_t forward_with_gre(struct __sk_buff *skb, struct bpf_dynptr *dynptr,
			      encap_headers_t *encap, struct in_addr *next_hop,
			      metrics_t *metrics)
{
	const int payload_off =
		sizeof(*encap) +
		sizeof(struct in_addr) * encap->unigue.hop_count;
	int32_t encap_overhead =
		payload_off - sizeof(struct ethhdr) - sizeof(struct iphdr);
	int32_t delta = sizeof(struct gre_base_hdr) - encap_overhead;
	__u8 encap_buffer[sizeof(encap_gre_t)] = {};
	uint16_t proto = ETH_P_IP;
	uint32_t mtu_len = 0;
	encap_gre_t *encap_gre;

	metrics->forwarded_packets_total_gre++;

	/* Loop protection: the inner packet's TTL is decremented as a safeguard
	 * against any forwarding loop. As the only interesting field is the TTL
	 * hop limit for IPv6, it is easier to use bpf_skb_load_bytes/bpf_skb_store_bytes
	 * as they handle the split packets if needed (no need for the data to be
	 * in the linear section).
	 */
	if (encap->gue.proto_ctype == IPPROTO_IPV6) {
		proto = ETH_P_IPV6;
		uint8_t ttl;
		int rc;

		rc = bpf_skb_load_bytes(
			skb, payload_off + offsetof(struct ipv6hdr, hop_limit),
			&ttl, 1);
		if (rc != 0) {
			metrics->errors_total_malformed_encapsulation++;
			return TC_ACT_SHOT;
		}

		if (ttl == 0) {
			metrics->errors_total_redirect_loop++;
			return TC_ACT_SHOT;
		}

		ttl--;
		rc = bpf_skb_store_bytes(
			skb, payload_off + offsetof(struct ipv6hdr, hop_limit),
			&ttl, 1, 0);
		if (rc != 0) {
			metrics->errors_total_malformed_encapsulation++;
			return TC_ACT_SHOT;
		}
	} else {
		uint8_t ttl;
		int rc;

		rc = bpf_skb_load_bytes(
			skb, payload_off + offsetof(struct iphdr, ttl), &ttl,
			1);
		if (rc != 0) {
			metrics->errors_total_malformed_encapsulation++;
			return TC_ACT_SHOT;
		}

		if (ttl == 0) {
			metrics->errors_total_redirect_loop++;
			return TC_ACT_SHOT;
		}

		/* IPv4 also has a checksum to patch. While the TTL is only one byte,
		 * this function only works for 2 and 4 bytes arguments (the result is
		 * the same).
		 */
		rc = bpf_l3_csum_replace(
			skb, payload_off + offsetof(struct iphdr, check), ttl,
			ttl - 1, 2);
		if (rc != 0) {
			metrics->errors_total_malformed_encapsulation++;
			return TC_ACT_SHOT;
		}

		ttl--;
		rc = bpf_skb_store_bytes(
			skb, payload_off + offsetof(struct iphdr, ttl), &ttl, 1,
			0);
		if (rc != 0) {
			metrics->errors_total_malformed_encapsulation++;
			return TC_ACT_SHOT;
		}
	}

	if (bpf_check_mtu(skb, skb->ifindex, &mtu_len, delta, 0)) {
		metrics->errors_total_encap_mtu_violate++;
		return TC_ACT_SHOT;
	}

	if (bpf_skb_adjust_room(skb, delta, BPF_ADJ_ROOM_NET,
				BPF_F_ADJ_ROOM_FIXED_GSO |
				BPF_F_ADJ_ROOM_NO_CSUM_RESET) ||
	    bpf_csum_level(skb, BPF_CSUM_LEVEL_INC)) {
		metrics->errors_total_encap_adjust_failed++;
		return TC_ACT_SHOT;
	}

	if (bpf_skb_pull_data(skb, sizeof(encap_gre_t))) {
		metrics->errors_total_encap_buffer_too_small++;
		return TC_ACT_SHOT;
	}

	encap_gre = bpf_dynptr_slice_rdwr(dynptr, 0, encap_buffer, sizeof(encap_buffer));
	if (!encap_gre) {
		metrics->errors_total_encap_buffer_too_small++;
		return TC_ACT_SHOT;
	}

	encap_gre->ip.protocol = IPPROTO_GRE;
	encap_gre->ip.daddr = next_hop->s_addr;
	encap_gre->ip.saddr = ENCAPSULATION_IP;
	encap_gre->ip.tot_len =
		bpf_htons(bpf_ntohs(encap_gre->ip.tot_len) + delta);
	encap_gre->gre.flags = 0;
	encap_gre->gre.protocol = bpf_htons(proto);
	pkt_ipv4_checksum((void *)&encap_gre->ip);

	if (encap_gre == encap_buffer)
		bpf_dynptr_write(dynptr, 0, encap_buffer, sizeof(encap_buffer), 0);

	return bpf_redirect(skb->ifindex, 0);
}

static ret_t forward_to_next_hop(struct __sk_buff *skb, struct bpf_dynptr *dynptr,
				 encap_headers_t *encap, struct in_addr *next_hop,
				 metrics_t *metrics)
{
	/* swap L2 addresses */
	/* This assumes that packets are received from a router.
	 * So just swapping the MAC addresses here will make the packet go back to
	 * the router, which will send it to the appropriate machine.
	 */
	unsigned char temp[ETH_ALEN];
	memcpy(temp, encap->eth.h_dest, sizeof(temp));
	memcpy(encap->eth.h_dest, encap->eth.h_source,
	       sizeof(encap->eth.h_dest));
	memcpy(encap->eth.h_source, temp, sizeof(encap->eth.h_source));

	if (encap->unigue.next_hop == encap->unigue.hop_count - 1 &&
	    encap->unigue.last_hop_gre) {
		return forward_with_gre(skb, dynptr, encap, next_hop, metrics);
	}

	metrics->forwarded_packets_total_gue++;
	uint32_t old_saddr = encap->ip.saddr;
	encap->ip.saddr = encap->ip.daddr;
	encap->ip.daddr = next_hop->s_addr;
	if (encap->unigue.next_hop < encap->unigue.hop_count) {
		encap->unigue.next_hop++;
	}

	/* Remove ip->saddr, add next_hop->s_addr */
	const uint64_t off = offsetof(typeof(*encap), ip.check);
	int ret = bpf_l3_csum_replace(skb, off, old_saddr, next_hop->s_addr, 4);
	if (ret < 0) {
		return TC_ACT_SHOT;
	}

	return bpf_redirect(skb->ifindex, 0);
}

static ret_t skip_next_hops(__u64 *offset, int n)
{
	switch (n) {
	case 1:
		*offset += sizeof(struct in_addr);
	case 0:
		return CONTINUE_PROCESSING;

	default:
		return TC_ACT_SHOT;
	}
}

/* Get the next hop from the GLB header.
 *
 * Sets next_hop->s_addr to 0 if there are no more hops left.
 * pkt is positioned just after the variable length GLB header
 * iff the call is successful.
 */
static ret_t get_next_hop(struct bpf_dynptr *dynptr, __u64 *offset, encap_headers_t *encap,
			  struct in_addr *next_hop)
{
	if (encap->unigue.next_hop > encap->unigue.hop_count)
		return TC_ACT_SHOT;

	/* Skip "used" next hops. */
	MAYBE_RETURN(skip_next_hops(offset, encap->unigue.next_hop));

	if (encap->unigue.next_hop == encap->unigue.hop_count) {
		/* No more next hops, we are at the end of the GLB header. */
		next_hop->s_addr = 0;
		return CONTINUE_PROCESSING;
	}

	if (bpf_dynptr_read(next_hop, sizeof(*next_hop), dynptr, *offset, 0))
		return TC_ACT_SHOT;

	*offset += sizeof(*next_hop);

	/* Skip the remainig next hops (may be zero). */
	return skip_next_hops(offset, encap->unigue.hop_count - encap->unigue.next_hop - 1);
}

/* Fill a bpf_sock_tuple to be used with the socket lookup functions.
 * This is a kludge that let's us work around verifier limitations:
 *
 *    fill_tuple(&t, foo, sizeof(struct iphdr), 123, 321)
 *
 * clang will substitute a constant for sizeof, which allows the verifier
 * to track it's value. Based on this, it can figure out the constant
 * return value, and calling code works while still being "generic" to
 * IPv4 and IPv6.
 */
static uint64_t fill_tuple(struct bpf_sock_tuple *tuple, void *iph,
				    uint64_t iphlen, uint16_t sport, uint16_t dport)
{
	switch (iphlen) {
	case sizeof(struct iphdr): {
		struct iphdr *ipv4 = (struct iphdr *)iph;
		tuple->ipv4.daddr = ipv4->daddr;
		tuple->ipv4.saddr = ipv4->saddr;
		tuple->ipv4.sport = sport;
		tuple->ipv4.dport = dport;
		return sizeof(tuple->ipv4);
	}

	case sizeof(struct ipv6hdr): {
		struct ipv6hdr *ipv6 = (struct ipv6hdr *)iph;
		memcpy(&tuple->ipv6.daddr, &ipv6->daddr,
		       sizeof(tuple->ipv6.daddr));
		memcpy(&tuple->ipv6.saddr, &ipv6->saddr,
		       sizeof(tuple->ipv6.saddr));
		tuple->ipv6.sport = sport;
		tuple->ipv6.dport = dport;
		return sizeof(tuple->ipv6);
	}

	default:
		return 0;
	}
}

static verdict_t classify_tcp(struct __sk_buff *skb, struct bpf_sock_tuple *tuple,
			      uint64_t tuplen, void *iph, struct tcphdr *tcp)
{
	struct bpf_sock *sk =
		bpf_skc_lookup_tcp(skb, tuple, tuplen, BPF_F_CURRENT_NETNS, 0);

	if (sk == NULL)
		return UNKNOWN;

	if (sk->state != BPF_TCP_LISTEN) {
		bpf_sk_release(sk);
		return ESTABLISHED;
	}

	if (iph != NULL && tcp != NULL) {
		/* Kludge: we've run out of arguments, but need the length of the ip header. */
		uint64_t iphlen = sizeof(struct iphdr);

		if (tuplen == sizeof(tuple->ipv6))
			iphlen = sizeof(struct ipv6hdr);

		if (bpf_tcp_check_syncookie(sk, iph, iphlen, tcp,
					    sizeof(*tcp)) == 0) {
			bpf_sk_release(sk);
			return SYN_COOKIE;
		}
	}

	bpf_sk_release(sk);
	return UNKNOWN;
}

static verdict_t classify_udp(struct __sk_buff *skb, struct bpf_sock_tuple *tuple, uint64_t tuplen)
{
	struct bpf_sock *sk =
		bpf_sk_lookup_udp(skb, tuple, tuplen, BPF_F_CURRENT_NETNS, 0);

	if (sk == NULL)
		return UNKNOWN;

	if (sk->state == BPF_TCP_ESTABLISHED) {
		bpf_sk_release(sk);
		return ESTABLISHED;
	}

	bpf_sk_release(sk);
	return UNKNOWN;
}

static verdict_t classify_icmp(struct __sk_buff *skb, uint8_t proto, struct bpf_sock_tuple *tuple,
			       uint64_t tuplen, metrics_t *metrics)
{
	switch (proto) {
	case IPPROTO_TCP:
		return classify_tcp(skb, tuple, tuplen, NULL, NULL);

	case IPPROTO_UDP:
		return classify_udp(skb, tuple, tuplen);

	default:
		metrics->errors_total_malformed_icmp++;
		return INVALID;
	}
}

static verdict_t process_icmpv4(struct __sk_buff *skb, struct bpf_dynptr *dynptr, __u64 *offset,
				metrics_t *metrics)
{
	struct icmphdr icmp;
	struct iphdr ipv4;

	if (bpf_dynptr_read(&icmp, sizeof(icmp), dynptr, *offset, 0)) {
		metrics->errors_total_malformed_icmp++;
		return INVALID;
	}

	*offset += sizeof(icmp);

	/* We should never receive encapsulated echo replies. */
	if (icmp.type == ICMP_ECHOREPLY) {
		metrics->errors_total_icmp_echo_replies++;
		return INVALID;
	}

	if (icmp.type == ICMP_ECHO)
		return ECHO_REQUEST;

	if (icmp.type != ICMP_DEST_UNREACH || icmp.code != ICMP_FRAG_NEEDED) {
		metrics->errors_total_unwanted_icmp++;
		return INVALID;
	}

	if (pkt_parse_ipv4(dynptr, offset, &ipv4)) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	/* The source address in the outer IP header is from the entity that
	 * originated the ICMP message. Use the original IP header to restore
	 * the correct flow tuple.
	 */
	struct bpf_sock_tuple tuple;
	tuple.ipv4.saddr = ipv4.daddr;
	tuple.ipv4.daddr = ipv4.saddr;

	if (!pkt_parse_icmp_l4_ports(dynptr, offset, (flow_ports_t *)&tuple.ipv4.sport)) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	return classify_icmp(skb, ipv4.protocol, &tuple,
			     sizeof(tuple.ipv4), metrics);
}

static verdict_t process_icmpv6(struct bpf_dynptr *dynptr, __u64 *offset, struct __sk_buff *skb,
				metrics_t *metrics)
{
	struct bpf_sock_tuple tuple;
	struct ipv6hdr ipv6;
	struct icmp6hdr icmp6;
	bool is_fragment;
	uint8_t l4_proto;

	if (bpf_dynptr_read(&icmp6, sizeof(icmp6), dynptr, *offset, 0)) {
		metrics->errors_total_malformed_icmp++;
		return INVALID;
	}

	/* We should never receive encapsulated echo replies. */
	if (icmp6.icmp6_type == ICMPV6_ECHO_REPLY) {
		metrics->errors_total_icmp_echo_replies++;
		return INVALID;
	}

	if (icmp6.icmp6_type == ICMPV6_ECHO_REQUEST) {
		return ECHO_REQUEST;
	}

	if (icmp6.icmp6_type != ICMPV6_PKT_TOOBIG) {
		metrics->errors_total_unwanted_icmp++;
		return INVALID;
	}

	if (pkt_parse_ipv6(dynptr, offset, &ipv6, &l4_proto, &is_fragment)) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	if (is_fragment) {
		metrics->errors_total_fragmented_ip++;
		return INVALID;
	}

	/* Swap source and dest addresses. */
	memcpy(&tuple.ipv6.saddr, &ipv6.daddr, sizeof(tuple.ipv6.saddr));
	memcpy(&tuple.ipv6.daddr, &ipv6.saddr, sizeof(tuple.ipv6.daddr));

	if (!pkt_parse_icmp_l4_ports(dynptr, offset, (flow_ports_t *)&tuple.ipv6.sport)) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	return classify_icmp(skb, l4_proto, &tuple, sizeof(tuple.ipv6),
			     metrics);
}

static verdict_t process_tcp(struct bpf_dynptr *dynptr, __u64 *offset, struct __sk_buff *skb,
			     struct iphdr_info *info, metrics_t *metrics)
{
	struct bpf_sock_tuple tuple;
	struct tcphdr tcp;
	uint64_t tuplen;

	metrics->l4_protocol_packets_total_tcp++;

	if (bpf_dynptr_read(&tcp, sizeof(tcp), dynptr, *offset, 0)) {
		metrics->errors_total_malformed_tcp++;
		return INVALID;
	}

	*offset += sizeof(tcp);

	if (tcp.syn)
		return SYN;

	tuplen = fill_tuple(&tuple, info->hdr, info->len, tcp.source, tcp.dest);
	return classify_tcp(skb, &tuple, tuplen, info->hdr, &tcp);
}

static verdict_t process_udp(struct bpf_dynptr *dynptr, __u64 *offset, struct __sk_buff *skb,
			     struct iphdr_info *info, metrics_t *metrics)
{
	struct bpf_sock_tuple tuple;
	struct udphdr udph;
	uint64_t tuplen;

	metrics->l4_protocol_packets_total_udp++;

	if (bpf_dynptr_read(&udph, sizeof(udph), dynptr, *offset, 0)) {
		metrics->errors_total_malformed_udp++;
		return INVALID;
	}
	*offset += sizeof(udph);

	tuplen = fill_tuple(&tuple, info->hdr, info->len, udph.source, udph.dest);
	return classify_udp(skb, &tuple, tuplen);
}

static verdict_t process_ipv4(struct __sk_buff *skb, struct bpf_dynptr *dynptr,
			      __u64 *offset, metrics_t *metrics)
{
	struct iphdr ipv4;
	struct iphdr_info info = {
		.hdr = &ipv4,
		.len = sizeof(ipv4),
	};

	metrics->l3_protocol_packets_total_ipv4++;

	if (pkt_parse_ipv4(dynptr, offset, &ipv4)) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (ipv4.version != 4) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (ipv4_is_fragment(&ipv4)) {
		metrics->errors_total_fragmented_ip++;
		return INVALID;
	}

	switch (ipv4.protocol) {
	case IPPROTO_ICMP:
		return process_icmpv4(skb, dynptr, offset, metrics);

	case IPPROTO_TCP:
		return process_tcp(dynptr, offset, skb, &info, metrics);

	case IPPROTO_UDP:
		return process_udp(dynptr, offset, skb, &info, metrics);

	default:
		metrics->errors_total_unknown_l4_proto++;
		return INVALID;
	}
}

static verdict_t process_ipv6(struct __sk_buff *skb, struct bpf_dynptr *dynptr,
			      __u64 *offset, metrics_t *metrics)
{
	struct ipv6hdr ipv6;
	struct iphdr_info info = {
		.hdr = &ipv6,
		.len = sizeof(ipv6),
	};
	uint8_t l4_proto;
	bool is_fragment;

	metrics->l3_protocol_packets_total_ipv6++;

	if (pkt_parse_ipv6(dynptr, offset, &ipv6, &l4_proto, &is_fragment)) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (ipv6.version != 6) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (is_fragment) {
		metrics->errors_total_fragmented_ip++;
		return INVALID;
	}

	switch (l4_proto) {
	case IPPROTO_ICMPV6:
		return process_icmpv6(dynptr, offset, skb, metrics);

	case IPPROTO_TCP:
		return process_tcp(dynptr, offset, skb, &info, metrics);

	case IPPROTO_UDP:
		return process_udp(dynptr, offset, skb, &info, metrics);

	default:
		metrics->errors_total_unknown_l4_proto++;
		return INVALID;
	}
}

SEC("tc")
int cls_redirect(struct __sk_buff *skb)
{
	__u8 encap_buffer[sizeof(encap_headers_t)] = {};
	struct bpf_dynptr dynptr;
	struct in_addr next_hop;
	/* Tracks offset of the dynptr. This will be unnecessary once
	 * bpf_dynptr_advance() is available.
	 */
	__u64 off = 0;
	ret_t ret;

	bpf_dynptr_from_skb(skb, 0, &dynptr);

	metrics_t *metrics = get_global_metrics();
	if (metrics == NULL)
		return TC_ACT_SHOT;

	metrics->processed_packets_total++;

	/* Pass bogus packets as long as we're not sure they're
	 * destined for us.
	 */
	if (skb->protocol != bpf_htons(ETH_P_IP))
		return TC_ACT_OK;

	encap_headers_t *encap;

	/* Make sure that all encapsulation headers are available in
	 * the linear portion of the skb. This makes it easy to manipulate them.
	 */
	if (bpf_skb_pull_data(skb, sizeof(*encap)))
		return TC_ACT_OK;

	encap = bpf_dynptr_slice_rdwr(&dynptr, 0, encap_buffer, sizeof(encap_buffer));
	if (!encap)
		return TC_ACT_OK;

	off += sizeof(*encap);

	if (encap->ip.ihl != 5)
		/* We never have any options. */
		return TC_ACT_OK;

	if (encap->ip.daddr != ENCAPSULATION_IP ||
	    encap->ip.protocol != IPPROTO_UDP)
		return TC_ACT_OK;

	/* TODO Check UDP length? */
	if (encap->udp.dest != ENCAPSULATION_PORT)
		return TC_ACT_OK;

	/* We now know that the packet is destined to us, we can
	 * drop bogus ones.
	 */
	if (ipv4_is_fragment((void *)&encap->ip)) {
		metrics->errors_total_fragmented_ip++;
		return TC_ACT_SHOT;
	}

	if (encap->gue.variant != 0) {
		metrics->errors_total_malformed_encapsulation++;
		return TC_ACT_SHOT;
	}

	if (encap->gue.control != 0) {
		metrics->errors_total_malformed_encapsulation++;
		return TC_ACT_SHOT;
	}

	if (encap->gue.flags != 0) {
		metrics->errors_total_malformed_encapsulation++;
		return TC_ACT_SHOT;
	}

	if (encap->gue.hlen !=
	    sizeof(encap->unigue) / 4 + encap->unigue.hop_count) {
		metrics->errors_total_malformed_encapsulation++;
		return TC_ACT_SHOT;
	}

	if (encap->unigue.version != 0) {
		metrics->errors_total_malformed_encapsulation++;
		return TC_ACT_SHOT;
	}

	if (encap->unigue.reserved != 0)
		return TC_ACT_SHOT;

	MAYBE_RETURN(get_next_hop(&dynptr, &off, encap, &next_hop));

	if (next_hop.s_addr == 0) {
		metrics->accepted_packets_total_last_hop++;
		return accept_locally(skb, encap);
	}

	verdict_t verdict;
	switch (encap->gue.proto_ctype) {
	case IPPROTO_IPIP:
		verdict = process_ipv4(skb, &dynptr, &off, metrics);
		break;

	case IPPROTO_IPV6:
		verdict = process_ipv6(skb, &dynptr, &off, metrics);
		break;

	default:
		metrics->errors_total_unknown_l3_proto++;
		return TC_ACT_SHOT;
	}

	switch (verdict) {
	case INVALID:
		/* metrics have already been bumped */
		return TC_ACT_SHOT;

	case UNKNOWN:
		return forward_to_next_hop(skb, &dynptr, encap, &next_hop, metrics);

	case ECHO_REQUEST:
		metrics->accepted_packets_total_icmp_echo_request++;
		break;

	case SYN:
		if (encap->unigue.forward_syn) {
			return forward_to_next_hop(skb, &dynptr, encap, &next_hop,
						   metrics);
		}

		metrics->accepted_packets_total_syn++;
		break;

	case SYN_COOKIE:
		metrics->accepted_packets_total_syn_cookies++;
		break;

	case ESTABLISHED:
		metrics->accepted_packets_total_established++;
		break;
	}

	ret = accept_locally(skb, encap);

	if (encap == encap_buffer)
		bpf_dynptr_write(&dynptr, 0, encap_buffer, sizeof(encap_buffer), 0);

	return ret;
}
