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

#ifdef SUBPROGS
#define INLINING __noinline
#else
#define INLINING __always_inline
#endif

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

/* Linux packet pointers are either aligned to NET_IP_ALIGN (aka 2 bytes),
 * or not aligned if the arch supports efficient unaligned access.
 *
 * Since the verifier ensures that eBPF packet accesses follow these rules,
 * we can tell LLVM to emit code as if we always had a larger alignment.
 * It will yell at us if we end up on a platform where this is not valid.
 */
typedef uint8_t *net_ptr __attribute__((align_value(8)));

typedef struct buf {
	struct __sk_buff *skb;
	net_ptr head;
	/* NB: tail musn't have alignment other than 1, otherwise
	* LLVM will go and eliminate code, e.g. when checking packet lengths.
	*/
	uint8_t *const tail;
} buf_t;

static __always_inline size_t buf_off(const buf_t *buf)
{
	/* Clang seems to optimize constructs like
	 *    a - b + c
	 * if c is known:
	 *    r? = c
	 *    r? -= b
	 *    r? += a
	 *
	 * This is a problem if a and b are packet pointers,
	 * since the verifier allows subtracting two pointers to
	 * get a scalar, but not a scalar and a pointer.
	 *
	 * Use inline asm to break this optimization.
	 */
	size_t off = (size_t)buf->head;
	asm("%0 -= %1" : "+r"(off) : "r"(buf->skb->data));
	return off;
}

static __always_inline bool buf_copy(buf_t *buf, void *dst, size_t len)
{
	if (bpf_skb_load_bytes(buf->skb, buf_off(buf), dst, len)) {
		return false;
	}

	buf->head += len;
	return true;
}

static __always_inline bool buf_skip(buf_t *buf, const size_t len)
{
	/* Check whether off + len is valid in the non-linear part. */
	if (buf_off(buf) + len > buf->skb->len) {
		return false;
	}

	buf->head += len;
	return true;
}

/* Returns a pointer to the start of buf, or NULL if len is
 * larger than the remaining data. Consumes len bytes on a successful
 * call.
 *
 * If scratch is not NULL, the function will attempt to load non-linear
 * data via bpf_skb_load_bytes. On success, scratch is returned.
 */
static __always_inline void *buf_assign(buf_t *buf, const size_t len, void *scratch)
{
	if (buf->head + len > buf->tail) {
		if (scratch == NULL) {
			return NULL;
		}

		return buf_copy(buf, scratch, len) ? scratch : NULL;
	}

	void *ptr = buf->head;
	buf->head += len;
	return ptr;
}

static INLINING bool pkt_skip_ipv4_options(buf_t *buf, const struct iphdr *ipv4)
{
	if (ipv4->ihl <= 5) {
		return true;
	}

	return buf_skip(buf, (ipv4->ihl - 5) * 4);
}

static INLINING bool ipv4_is_fragment(const struct iphdr *ip)
{
	uint16_t frag_off = ip->frag_off & bpf_htons(IP_OFFSET_MASK);
	return (ip->frag_off & bpf_htons(IP_MF)) != 0 || frag_off > 0;
}

static __always_inline struct iphdr *pkt_parse_ipv4(buf_t *pkt, struct iphdr *scratch)
{
	struct iphdr *ipv4 = buf_assign(pkt, sizeof(*ipv4), scratch);
	if (ipv4 == NULL) {
		return NULL;
	}

	if (ipv4->ihl < 5) {
		return NULL;
	}

	if (!pkt_skip_ipv4_options(pkt, ipv4)) {
		return NULL;
	}

	return ipv4;
}

/* Parse the L4 ports from a packet, assuming a layout like TCP or UDP. */
static INLINING bool pkt_parse_icmp_l4_ports(buf_t *pkt, flow_ports_t *ports)
{
	if (!buf_copy(pkt, ports, sizeof(*ports))) {
		return false;
	}

	/* Ports in the L4 headers are reversed, since we are parsing an ICMP
	 * payload which is going towards the eyeball.
	 */
	uint16_t dst = ports->src;
	ports->src = ports->dst;
	ports->dst = dst;
	return true;
}

static INLINING uint16_t pkt_checksum_fold(uint32_t csum)
{
	/* The highest reasonable value for an IPv4 header
	 * checksum requires two folds, so we just do that always.
	 */
	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);
	return (uint16_t)~csum;
}

static INLINING void pkt_ipv4_checksum(struct iphdr *iph)
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

#pragma clang loop unroll(full)
	for (size_t i = 0; i < sizeof(struct iphdr) / 2; i++) {
		acc += ipw[i];
	}

	iph->check = pkt_checksum_fold(acc);
}

static INLINING
bool pkt_skip_ipv6_extension_headers(buf_t *pkt,
				     const struct ipv6hdr *ipv6,
				     uint8_t *upper_proto,
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

#pragma clang loop unroll(full)
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
			if (!buf_copy(pkt, &exthdr, sizeof(exthdr))) {
				return false;
			}

			/* hdrlen is in 8-octet units, and excludes the first 8 octets. */
			if (!buf_skip(pkt,
				      (exthdr.len + 1) * 8 - sizeof(exthdr))) {
				return false;
			}

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

/* This function has to be inlined, because the verifier otherwise rejects it
 * due to returning a pointer to the stack. This is technically correct, since
 * scratch is allocated on the stack. However, this usage should be safe since
 * it's the callers stack after all.
 */
static __always_inline struct ipv6hdr *
pkt_parse_ipv6(buf_t *pkt, struct ipv6hdr *scratch, uint8_t *proto,
	       bool *is_fragment)
{
	struct ipv6hdr *ipv6 = buf_assign(pkt, sizeof(*ipv6), scratch);
	if (ipv6 == NULL) {
		return NULL;
	}

	if (!pkt_skip_ipv6_extension_headers(pkt, ipv6, proto, is_fragment)) {
		return NULL;
	}

	return ipv6;
}

/* Global metrics, per CPU
 */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, unsigned int);
	__type(value, metrics_t);
} metrics_map SEC(".maps");

static INLINING metrics_t *get_global_metrics(void)
{
	uint64_t key = 0;
	return bpf_map_lookup_elem(&metrics_map, &key);
}

static INLINING ret_t accept_locally(struct __sk_buff *skb, encap_headers_t *encap)
{
	const int payload_off =
		sizeof(*encap) +
		sizeof(struct in_addr) * encap->unigue.hop_count;
	int32_t encap_overhead = payload_off - sizeof(struct ethhdr);

	// Changing the ethertype if the encapsulated packet is ipv6
	if (encap->gue.proto_ctype == IPPROTO_IPV6) {
		encap->eth.h_proto = bpf_htons(ETH_P_IPV6);
	}

	if (bpf_skb_adjust_room(skb, -encap_overhead, BPF_ADJ_ROOM_MAC,
				BPF_F_ADJ_ROOM_FIXED_GSO |
				BPF_F_ADJ_ROOM_NO_CSUM_RESET) ||
	    bpf_csum_level(skb, BPF_CSUM_LEVEL_DEC))
		return TC_ACT_SHOT;

	return bpf_redirect(skb->ifindex, BPF_F_INGRESS);
}

static INLINING ret_t forward_with_gre(struct __sk_buff *skb, encap_headers_t *encap,
				       struct in_addr *next_hop, metrics_t *metrics)
{
	metrics->forwarded_packets_total_gre++;

	const int payload_off =
		sizeof(*encap) +
		sizeof(struct in_addr) * encap->unigue.hop_count;
	int32_t encap_overhead =
		payload_off - sizeof(struct ethhdr) - sizeof(struct iphdr);
	int32_t delta = sizeof(struct gre_base_hdr) - encap_overhead;
	uint16_t proto = ETH_P_IP;
	uint32_t mtu_len = 0;

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

	buf_t pkt = {
		.skb = skb,
		.head = (uint8_t *)(long)skb->data,
		.tail = (uint8_t *)(long)skb->data_end,
	};

	encap_gre_t *encap_gre = buf_assign(&pkt, sizeof(encap_gre_t), NULL);
	if (encap_gre == NULL) {
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

	return bpf_redirect(skb->ifindex, 0);
}

static INLINING ret_t forward_to_next_hop(struct __sk_buff *skb, encap_headers_t *encap,
					  struct in_addr *next_hop, metrics_t *metrics)
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
		return forward_with_gre(skb, encap, next_hop, metrics);
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

static INLINING ret_t skip_next_hops(buf_t *pkt, int n)
{
	switch (n) {
	case 1:
		if (!buf_skip(pkt, sizeof(struct in_addr)))
			return TC_ACT_SHOT;
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
static INLINING ret_t get_next_hop(buf_t *pkt, encap_headers_t *encap,
				   struct in_addr *next_hop)
{
	if (encap->unigue.next_hop > encap->unigue.hop_count) {
		return TC_ACT_SHOT;
	}

	/* Skip "used" next hops. */
	MAYBE_RETURN(skip_next_hops(pkt, encap->unigue.next_hop));

	if (encap->unigue.next_hop == encap->unigue.hop_count) {
		/* No more next hops, we are at the end of the GLB header. */
		next_hop->s_addr = 0;
		return CONTINUE_PROCESSING;
	}

	if (!buf_copy(pkt, next_hop, sizeof(*next_hop))) {
		return TC_ACT_SHOT;
	}

	/* Skip the remainig next hops (may be zero). */
	return skip_next_hops(pkt, encap->unigue.hop_count -
					   encap->unigue.next_hop - 1);
}

/* Fill a bpf_sock_tuple to be used with the socket lookup functions.
 * This is a kludge that let's us work around verifier limitations:
 *
 *    fill_tuple(&t, foo, sizeof(struct iphdr), 123, 321)
 *
 * clang will substitue a costant for sizeof, which allows the verifier
 * to track it's value. Based on this, it can figure out the constant
 * return value, and calling code works while still being "generic" to
 * IPv4 and IPv6.
 */
static INLINING uint64_t fill_tuple(struct bpf_sock_tuple *tuple, void *iph,
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

static INLINING verdict_t classify_tcp(struct __sk_buff *skb,
				       struct bpf_sock_tuple *tuple, uint64_t tuplen,
				       void *iph, struct tcphdr *tcp)
{
	struct bpf_sock *sk =
		bpf_skc_lookup_tcp(skb, tuple, tuplen, BPF_F_CURRENT_NETNS, 0);
	if (sk == NULL) {
		return UNKNOWN;
	}

	if (sk->state != BPF_TCP_LISTEN) {
		bpf_sk_release(sk);
		return ESTABLISHED;
	}

	if (iph != NULL && tcp != NULL) {
		/* Kludge: we've run out of arguments, but need the length of the ip header. */
		uint64_t iphlen = sizeof(struct iphdr);
		if (tuplen == sizeof(tuple->ipv6)) {
			iphlen = sizeof(struct ipv6hdr);
		}

		if (bpf_tcp_check_syncookie(sk, iph, iphlen, tcp,
					    sizeof(*tcp)) == 0) {
			bpf_sk_release(sk);
			return SYN_COOKIE;
		}
	}

	bpf_sk_release(sk);
	return UNKNOWN;
}

static INLINING verdict_t classify_udp(struct __sk_buff *skb,
				       struct bpf_sock_tuple *tuple, uint64_t tuplen)
{
	struct bpf_sock *sk =
		bpf_sk_lookup_udp(skb, tuple, tuplen, BPF_F_CURRENT_NETNS, 0);
	if (sk == NULL) {
		return UNKNOWN;
	}

	if (sk->state == BPF_TCP_ESTABLISHED) {
		bpf_sk_release(sk);
		return ESTABLISHED;
	}

	bpf_sk_release(sk);
	return UNKNOWN;
}

static INLINING verdict_t classify_icmp(struct __sk_buff *skb, uint8_t proto,
					struct bpf_sock_tuple *tuple, uint64_t tuplen,
					metrics_t *metrics)
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

static INLINING verdict_t process_icmpv4(buf_t *pkt, metrics_t *metrics)
{
	struct icmphdr icmp;
	if (!buf_copy(pkt, &icmp, sizeof(icmp))) {
		metrics->errors_total_malformed_icmp++;
		return INVALID;
	}

	/* We should never receive encapsulated echo replies. */
	if (icmp.type == ICMP_ECHOREPLY) {
		metrics->errors_total_icmp_echo_replies++;
		return INVALID;
	}

	if (icmp.type == ICMP_ECHO) {
		return ECHO_REQUEST;
	}

	if (icmp.type != ICMP_DEST_UNREACH || icmp.code != ICMP_FRAG_NEEDED) {
		metrics->errors_total_unwanted_icmp++;
		return INVALID;
	}

	struct iphdr _ip4;
	const struct iphdr *ipv4 = pkt_parse_ipv4(pkt, &_ip4);
	if (ipv4 == NULL) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	/* The source address in the outer IP header is from the entity that
	 * originated the ICMP message. Use the original IP header to restore
	 * the correct flow tuple.
	 */
	struct bpf_sock_tuple tuple;
	tuple.ipv4.saddr = ipv4->daddr;
	tuple.ipv4.daddr = ipv4->saddr;

	if (!pkt_parse_icmp_l4_ports(pkt, (flow_ports_t *)&tuple.ipv4.sport)) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	return classify_icmp(pkt->skb, ipv4->protocol, &tuple,
			     sizeof(tuple.ipv4), metrics);
}

static INLINING verdict_t process_icmpv6(buf_t *pkt, metrics_t *metrics)
{
	struct icmp6hdr icmp6;
	if (!buf_copy(pkt, &icmp6, sizeof(icmp6))) {
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

	bool is_fragment;
	uint8_t l4_proto;
	struct ipv6hdr _ipv6;
	const struct ipv6hdr *ipv6 =
		pkt_parse_ipv6(pkt, &_ipv6, &l4_proto, &is_fragment);
	if (ipv6 == NULL) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	if (is_fragment) {
		metrics->errors_total_fragmented_ip++;
		return INVALID;
	}

	/* Swap source and dest addresses. */
	struct bpf_sock_tuple tuple;
	memcpy(&tuple.ipv6.saddr, &ipv6->daddr, sizeof(tuple.ipv6.saddr));
	memcpy(&tuple.ipv6.daddr, &ipv6->saddr, sizeof(tuple.ipv6.daddr));

	if (!pkt_parse_icmp_l4_ports(pkt, (flow_ports_t *)&tuple.ipv6.sport)) {
		metrics->errors_total_malformed_icmp_pkt_too_big++;
		return INVALID;
	}

	return classify_icmp(pkt->skb, l4_proto, &tuple, sizeof(tuple.ipv6),
			     metrics);
}

static INLINING verdict_t process_tcp(buf_t *pkt, void *iph, uint64_t iphlen,
				      metrics_t *metrics)
{
	metrics->l4_protocol_packets_total_tcp++;

	struct tcphdr _tcp;
	struct tcphdr *tcp = buf_assign(pkt, sizeof(_tcp), &_tcp);
	if (tcp == NULL) {
		metrics->errors_total_malformed_tcp++;
		return INVALID;
	}

	if (tcp->syn) {
		return SYN;
	}

	struct bpf_sock_tuple tuple;
	uint64_t tuplen =
		fill_tuple(&tuple, iph, iphlen, tcp->source, tcp->dest);
	return classify_tcp(pkt->skb, &tuple, tuplen, iph, tcp);
}

static INLINING verdict_t process_udp(buf_t *pkt, void *iph, uint64_t iphlen,
				      metrics_t *metrics)
{
	metrics->l4_protocol_packets_total_udp++;

	struct udphdr _udp;
	struct udphdr *udph = buf_assign(pkt, sizeof(_udp), &_udp);
	if (udph == NULL) {
		metrics->errors_total_malformed_udp++;
		return INVALID;
	}

	struct bpf_sock_tuple tuple;
	uint64_t tuplen =
		fill_tuple(&tuple, iph, iphlen, udph->source, udph->dest);
	return classify_udp(pkt->skb, &tuple, tuplen);
}

static INLINING verdict_t process_ipv4(buf_t *pkt, metrics_t *metrics)
{
	metrics->l3_protocol_packets_total_ipv4++;

	struct iphdr _ip4;
	struct iphdr *ipv4 = pkt_parse_ipv4(pkt, &_ip4);
	if (ipv4 == NULL) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (ipv4->version != 4) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (ipv4_is_fragment(ipv4)) {
		metrics->errors_total_fragmented_ip++;
		return INVALID;
	}

	switch (ipv4->protocol) {
	case IPPROTO_ICMP:
		return process_icmpv4(pkt, metrics);

	case IPPROTO_TCP:
		return process_tcp(pkt, ipv4, sizeof(*ipv4), metrics);

	case IPPROTO_UDP:
		return process_udp(pkt, ipv4, sizeof(*ipv4), metrics);

	default:
		metrics->errors_total_unknown_l4_proto++;
		return INVALID;
	}
}

static INLINING verdict_t process_ipv6(buf_t *pkt, metrics_t *metrics)
{
	metrics->l3_protocol_packets_total_ipv6++;

	uint8_t l4_proto;
	bool is_fragment;
	struct ipv6hdr _ipv6;
	struct ipv6hdr *ipv6 =
		pkt_parse_ipv6(pkt, &_ipv6, &l4_proto, &is_fragment);
	if (ipv6 == NULL) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (ipv6->version != 6) {
		metrics->errors_total_malformed_ip++;
		return INVALID;
	}

	if (is_fragment) {
		metrics->errors_total_fragmented_ip++;
		return INVALID;
	}

	switch (l4_proto) {
	case IPPROTO_ICMPV6:
		return process_icmpv6(pkt, metrics);

	case IPPROTO_TCP:
		return process_tcp(pkt, ipv6, sizeof(*ipv6), metrics);

	case IPPROTO_UDP:
		return process_udp(pkt, ipv6, sizeof(*ipv6), metrics);

	default:
		metrics->errors_total_unknown_l4_proto++;
		return INVALID;
	}
}

SEC("tc")
int cls_redirect(struct __sk_buff *skb)
{
	metrics_t *metrics = get_global_metrics();
	if (metrics == NULL) {
		return TC_ACT_SHOT;
	}

	metrics->processed_packets_total++;

	/* Pass bogus packets as long as we're not sure they're
	 * destined for us.
	 */
	if (skb->protocol != bpf_htons(ETH_P_IP)) {
		return TC_ACT_OK;
	}

	encap_headers_t *encap;

	/* Make sure that all encapsulation headers are available in
	 * the linear portion of the skb. This makes it easy to manipulate them.
	 */
	if (bpf_skb_pull_data(skb, sizeof(*encap))) {
		return TC_ACT_OK;
	}

	buf_t pkt = {
		.skb = skb,
		.head = (uint8_t *)(long)skb->data,
		.tail = (uint8_t *)(long)skb->data_end,
	};

	encap = buf_assign(&pkt, sizeof(*encap), NULL);
	if (encap == NULL) {
		return TC_ACT_OK;
	}

	if (encap->ip.ihl != 5) {
		/* We never have any options. */
		return TC_ACT_OK;
	}

	if (encap->ip.daddr != ENCAPSULATION_IP ||
	    encap->ip.protocol != IPPROTO_UDP) {
		return TC_ACT_OK;
	}

	/* TODO Check UDP length? */
	if (encap->udp.dest != ENCAPSULATION_PORT) {
		return TC_ACT_OK;
	}

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

	if (encap->unigue.reserved != 0) {
		return TC_ACT_SHOT;
	}

	struct in_addr next_hop;
	MAYBE_RETURN(get_next_hop(&pkt, encap, &next_hop));

	if (next_hop.s_addr == 0) {
		metrics->accepted_packets_total_last_hop++;
		return accept_locally(skb, encap);
	}

	verdict_t verdict;
	switch (encap->gue.proto_ctype) {
	case IPPROTO_IPIP:
		verdict = process_ipv4(&pkt, metrics);
		break;

	case IPPROTO_IPV6:
		verdict = process_ipv6(&pkt, metrics);
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
		return forward_to_next_hop(skb, encap, &next_hop, metrics);

	case ECHO_REQUEST:
		metrics->accepted_packets_total_icmp_echo_request++;
		break;

	case SYN:
		if (encap->unigue.forward_syn) {
			return forward_to_next_hop(skb, encap, &next_hop,
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

	return accept_locally(skb, encap);
}
