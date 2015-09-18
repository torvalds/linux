#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"
#include <uapi/linux/in.h>
#include <uapi/linux/if.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/ipv6.h>
#include <uapi/linux/if_tunnel.h>
#define IP_MF		0x2000
#define IP_OFFSET	0x1FFF

struct vlan_hdr {
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};

struct flow_keys {
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	__u16 thoff;
	__u8 ip_proto;
};

static inline int proto_ports_offset(__u64 proto)
{
	switch (proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_DCCP:
	case IPPROTO_ESP:
	case IPPROTO_SCTP:
	case IPPROTO_UDPLITE:
		return 0;
	case IPPROTO_AH:
		return 4;
	default:
		return 0;
	}
}

static inline int ip_is_fragment(struct __sk_buff *ctx, __u64 nhoff)
{
	return load_half(ctx, nhoff + offsetof(struct iphdr, frag_off))
		& (IP_MF | IP_OFFSET);
}

static inline __u32 ipv6_addr_hash(struct __sk_buff *ctx, __u64 off)
{
	__u64 w0 = load_word(ctx, off);
	__u64 w1 = load_word(ctx, off + 4);
	__u64 w2 = load_word(ctx, off + 8);
	__u64 w3 = load_word(ctx, off + 12);

	return (__u32)(w0 ^ w1 ^ w2 ^ w3);
}

static inline __u64 parse_ip(struct __sk_buff *skb, __u64 nhoff, __u64 *ip_proto,
			     struct flow_keys *flow)
{
	__u64 verlen;

	if (unlikely(ip_is_fragment(skb, nhoff)))
		*ip_proto = 0;
	else
		*ip_proto = load_byte(skb, nhoff + offsetof(struct iphdr, protocol));

	if (*ip_proto != IPPROTO_GRE) {
		flow->src = load_word(skb, nhoff + offsetof(struct iphdr, saddr));
		flow->dst = load_word(skb, nhoff + offsetof(struct iphdr, daddr));
	}

	verlen = load_byte(skb, nhoff + 0/*offsetof(struct iphdr, ihl)*/);
	if (likely(verlen == 0x45))
		nhoff += 20;
	else
		nhoff += (verlen & 0xF) << 2;

	return nhoff;
}

static inline __u64 parse_ipv6(struct __sk_buff *skb, __u64 nhoff, __u64 *ip_proto,
			       struct flow_keys *flow)
{
	*ip_proto = load_byte(skb,
			      nhoff + offsetof(struct ipv6hdr, nexthdr));
	flow->src = ipv6_addr_hash(skb,
				   nhoff + offsetof(struct ipv6hdr, saddr));
	flow->dst = ipv6_addr_hash(skb,
				   nhoff + offsetof(struct ipv6hdr, daddr));
	nhoff += sizeof(struct ipv6hdr);

	return nhoff;
}

static inline bool flow_dissector(struct __sk_buff *skb, struct flow_keys *flow)
{
	__u64 nhoff = ETH_HLEN;
	__u64 ip_proto;
	__u64 proto = load_half(skb, 12);
	int poff;

	if (proto == ETH_P_8021AD) {
		proto = load_half(skb, nhoff + offsetof(struct vlan_hdr,
							h_vlan_encapsulated_proto));
		nhoff += sizeof(struct vlan_hdr);
	}

	if (proto == ETH_P_8021Q) {
		proto = load_half(skb, nhoff + offsetof(struct vlan_hdr,
							h_vlan_encapsulated_proto));
		nhoff += sizeof(struct vlan_hdr);
	}

	if (likely(proto == ETH_P_IP))
		nhoff = parse_ip(skb, nhoff, &ip_proto, flow);
	else if (proto == ETH_P_IPV6)
		nhoff = parse_ipv6(skb, nhoff, &ip_proto, flow);
	else
		return false;

	switch (ip_proto) {
	case IPPROTO_GRE: {
		struct gre_hdr {
			__be16 flags;
			__be16 proto;
		};

		__u64 gre_flags = load_half(skb,
					    nhoff + offsetof(struct gre_hdr, flags));
		__u64 gre_proto = load_half(skb,
					    nhoff + offsetof(struct gre_hdr, proto));

		if (gre_flags & (GRE_VERSION|GRE_ROUTING))
			break;

		proto = gre_proto;
		nhoff += 4;
		if (gre_flags & GRE_CSUM)
			nhoff += 4;
		if (gre_flags & GRE_KEY)
			nhoff += 4;
		if (gre_flags & GRE_SEQ)
			nhoff += 4;

		if (proto == ETH_P_8021Q) {
			proto = load_half(skb,
					  nhoff + offsetof(struct vlan_hdr,
							   h_vlan_encapsulated_proto));
			nhoff += sizeof(struct vlan_hdr);
		}

		if (proto == ETH_P_IP)
			nhoff = parse_ip(skb, nhoff, &ip_proto, flow);
		else if (proto == ETH_P_IPV6)
			nhoff = parse_ipv6(skb, nhoff, &ip_proto, flow);
		else
			return false;
		break;
	}
	case IPPROTO_IPIP:
		nhoff = parse_ip(skb, nhoff, &ip_proto, flow);
		break;
	case IPPROTO_IPV6:
		nhoff = parse_ipv6(skb, nhoff, &ip_proto, flow);
		break;
	default:
		break;
	}

	flow->ip_proto = ip_proto;
	poff = proto_ports_offset(ip_proto);
	if (poff >= 0) {
		nhoff += poff;
		flow->ports = load_word(skb, nhoff);
	}

	flow->thoff = (__u16) nhoff;

	return true;
}

struct pair {
	long packets;
	long bytes;
};

struct bpf_map_def SEC("maps") hash_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(__be32),
	.value_size = sizeof(struct pair),
	.max_entries = 1024,
};

SEC("socket2")
int bpf_prog2(struct __sk_buff *skb)
{
	struct flow_keys flow;
	struct pair *value;
	u32 key;

	if (!flow_dissector(skb, &flow))
		return 0;

	key = flow.dst;
	value = bpf_map_lookup_elem(&hash_map, &key);
	if (value) {
		__sync_fetch_and_add(&value->packets, 1);
		__sync_fetch_and_add(&value->bytes, skb->len);
	} else {
		struct pair val = {1, skb->len};

		bpf_map_update_elem(&hash_map, &key, &val, BPF_ANY);
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
