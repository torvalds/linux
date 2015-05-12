#ifndef _NET_FLOW_DISSECTOR_H
#define _NET_FLOW_DISSECTOR_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/in6.h>
#include <uapi/linux/if_ether.h>

/**
 * struct flow_dissector_key_basic:
 * @thoff: Transport header offset
 * @n_proto: Network header protocol (eg. IPv4/IPv6)
 * @ip_proto: Transport header protocol (eg. TCP/UDP)
 */
struct flow_dissector_key_basic {
	u16	thoff;
	__be16	n_proto;
	u8	ip_proto;
};

/**
 * struct flow_dissector_key_addrs:
 * @src: source ip address in case of IPv4
 *	 For IPv6 it contains 32bit hash of src address
 * @dst: destination ip address in case of IPv4
 *	 For IPv6 it contains 32bit hash of dst address
 */
struct flow_dissector_key_addrs {
	/* (src,dst) must be grouped, in the same way than in IP header */
	__be32 src;
	__be32 dst;
};

/**
 * flow_dissector_key_tp_ports:
 *	@ports: port numbers of Transport header
 *		src: source port number
 *		dst: destination port number
 */
struct flow_dissector_key_ports {
	union {
		__be32 ports;
		struct {
			__be16 src;
			__be16 dst;
		};
	};
};

/**
 * struct flow_dissector_key_ipv6_addrs:
 * @src: source ip address
 * @dst: destination ip address
 */
struct flow_dissector_key_ipv6_addrs {
	/* (src,dst) must be grouped, in the same way than in IP header */
	struct in6_addr src;
	struct in6_addr dst;
};

/**
 * struct flow_dissector_key_eth_addrs:
 * @src: source Ethernet address
 * @dst: destination Ethernet address
 */
struct flow_dissector_key_eth_addrs {
	/* (dst,src) must be grouped, in the same way than in ETH header */
	unsigned char dst[ETH_ALEN];
	unsigned char src[ETH_ALEN];
};

enum flow_dissector_key_id {
	FLOW_DISSECTOR_KEY_BASIC, /* struct flow_dissector_key_basic */
	FLOW_DISSECTOR_KEY_IPV4_ADDRS, /* struct flow_dissector_key_addrs */
	FLOW_DISSECTOR_KEY_IPV6_HASH_ADDRS, /* struct flow_dissector_key_addrs */
	FLOW_DISSECTOR_KEY_PORTS, /* struct flow_dissector_key_ports */
	FLOW_DISSECTOR_KEY_IPV6_ADDRS, /* struct flow_dissector_key_ipv6_addrs */
	FLOW_DISSECTOR_KEY_ETH_ADDRS, /* struct flow_dissector_key_eth_addrs */

	FLOW_DISSECTOR_KEY_MAX,
};

struct flow_dissector_key {
	enum flow_dissector_key_id key_id;
	size_t offset; /* offset of struct flow_dissector_key_*
			  in target the struct */
};

struct flow_dissector {
	unsigned int used_keys; /* each bit repesents presence of one key id */
	unsigned short int offset[FLOW_DISSECTOR_KEY_MAX];
};

void skb_flow_dissector_init(struct flow_dissector *flow_dissector,
			     const struct flow_dissector_key *key,
			     unsigned int key_count);

bool __skb_flow_dissect(const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container,
			void *data, __be16 proto, int nhoff, int hlen);

static inline bool skb_flow_dissect(const struct sk_buff *skb,
				    struct flow_dissector *flow_dissector,
				    void *target_container)
{
	return __skb_flow_dissect(skb, flow_dissector, target_container,
				  NULL, 0, 0, 0);
}

struct flow_keys {
	struct flow_dissector_key_addrs addrs;
	struct flow_dissector_key_ports ports;
	struct flow_dissector_key_basic basic;
};

extern struct flow_dissector flow_keys_dissector;
extern struct flow_dissector flow_keys_buf_dissector;

static inline bool skb_flow_dissect_flow_keys(const struct sk_buff *skb,
					      struct flow_keys *flow)
{
	memset(flow, 0, sizeof(*flow));
	return __skb_flow_dissect(skb, &flow_keys_dissector, flow,
				  NULL, 0, 0, 0);
}

static inline bool skb_flow_dissect_flow_keys_buf(struct flow_keys *flow,
						  void *data, __be16 proto,
						  int nhoff, int hlen)
{
	memset(flow, 0, sizeof(*flow));
	return __skb_flow_dissect(NULL, &flow_keys_buf_dissector, flow,
				  data, proto, nhoff, hlen);
}

__be32 __skb_flow_get_ports(const struct sk_buff *skb, int thoff, u8 ip_proto,
			    void *data, int hlen_proto);

static inline __be32 skb_flow_get_ports(const struct sk_buff *skb,
					int thoff, u8 ip_proto)
{
	return __skb_flow_get_ports(skb, thoff, ip_proto, NULL, 0);
}

u32 flow_hash_from_keys(struct flow_keys *keys);
void __skb_get_hash(struct sk_buff *skb);
u32 skb_get_poff(const struct sk_buff *skb);
u32 __skb_get_poff(const struct sk_buff *skb, void *data,
		   const struct flow_keys *keys, int hlen);

/* struct flow_keys_digest:
 *
 * This structure is used to hold a digest of the full flow keys. This is a
 * larger "hash" of a flow to allow definitively matching specific flows where
 * the 32 bit skb->hash is not large enough. The size is limited to 16 bytes so
 * that it can by used in CB of skb (see sch_choke for an example).
 */
#define FLOW_KEYS_DIGEST_LEN	16
struct flow_keys_digest {
	u8	data[FLOW_KEYS_DIGEST_LEN];
};

void make_flow_keys_digest(struct flow_keys_digest *digest,
			   const struct flow_keys *flow);

#endif
