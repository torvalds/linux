/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_FLOW_DISSECTOR_H
#define _NET_FLOW_DISSECTOR_H

#include <linux/types.h>
#include <linux/in6.h>
#include <linux/siphash.h>
#include <linux/string.h>
#include <uapi/linux/if_ether.h>

struct bpf_prog;
struct net;
struct sk_buff;

/**
 * struct flow_dissector_key_control:
 * @thoff:     Transport header offset
 * @addr_type: Type of key. One of FLOW_DISSECTOR_KEY_*
 * @flags:     Key flags. Any of FLOW_DIS_(IS_FRAGMENT|FIRST_FRAGENCAPSULATION)
 */
struct flow_dissector_key_control {
	u16	thoff;
	u16	addr_type;
	u32	flags;
};

#define FLOW_DIS_IS_FRAGMENT	BIT(0)
#define FLOW_DIS_FIRST_FRAG	BIT(1)
#define FLOW_DIS_ENCAPSULATION	BIT(2)

enum flow_dissect_ret {
	FLOW_DISSECT_RET_OUT_GOOD,
	FLOW_DISSECT_RET_OUT_BAD,
	FLOW_DISSECT_RET_PROTO_AGAIN,
	FLOW_DISSECT_RET_IPPROTO_AGAIN,
	FLOW_DISSECT_RET_CONTINUE,
};

/**
 * struct flow_dissector_key_basic:
 * @n_proto:  Network header protocol (eg. IPv4/IPv6)
 * @ip_proto: Transport header protocol (eg. TCP/UDP)
 * @padding:  Unused
 */
struct flow_dissector_key_basic {
	__be16	n_proto;
	u8	ip_proto;
	u8	padding;
};

struct flow_dissector_key_tags {
	u32	flow_label;
};

struct flow_dissector_key_vlan {
	union {
		struct {
			u16	vlan_id:12,
				vlan_dei:1,
				vlan_priority:3;
		};
		__be16	vlan_tci;
	};
	__be16	vlan_tpid;
	__be16	vlan_eth_type;
	u16	padding;
};

struct flow_dissector_mpls_lse {
	u32	mpls_ttl:8,
		mpls_bos:1,
		mpls_tc:3,
		mpls_label:20;
};

#define FLOW_DIS_MPLS_MAX 7
struct flow_dissector_key_mpls {
	struct flow_dissector_mpls_lse ls[FLOW_DIS_MPLS_MAX]; /* Label Stack */
	u8 used_lses; /* One bit set for each Label Stack Entry in use */
};

static inline void dissector_set_mpls_lse(struct flow_dissector_key_mpls *mpls,
					  int lse_index)
{
	mpls->used_lses |= 1 << lse_index;
}

#define FLOW_DIS_TUN_OPTS_MAX 255
/**
 * struct flow_dissector_key_enc_opts:
 * @data: tunnel option data
 * @len: length of tunnel option data
 * @dst_opt_type: tunnel option type
 */
struct flow_dissector_key_enc_opts {
	u8 data[FLOW_DIS_TUN_OPTS_MAX];	/* Using IP_TUNNEL_OPTS_MAX is desired
					 * here but seems difficult to #include
					 */
	u8 len;
	__be16 dst_opt_type;
};

struct flow_dissector_key_keyid {
	__be32	keyid;
};

/**
 * struct flow_dissector_key_ipv4_addrs:
 * @src: source ip address
 * @dst: destination ip address
 */
struct flow_dissector_key_ipv4_addrs {
	/* (src,dst) must be grouped, in the same way than in IP header */
	__be32 src;
	__be32 dst;
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
 * struct flow_dissector_key_tipc:
 * @key: source node address combined with selector
 */
struct flow_dissector_key_tipc {
	__be32 key;
};

/**
 * struct flow_dissector_key_addrs:
 * @v4addrs: IPv4 addresses
 * @v6addrs: IPv6 addresses
 * @tipckey: TIPC key
 */
struct flow_dissector_key_addrs {
	union {
		struct flow_dissector_key_ipv4_addrs v4addrs;
		struct flow_dissector_key_ipv6_addrs v6addrs;
		struct flow_dissector_key_tipc tipckey;
	};
};

/**
 * struct flow_dissector_key_arp:
 * @sip: Sender IP address
 * @tip: Target IP address
 * @op:  Operation
 * @sha: Sender hardware address
 * @tha: Target hardware address
 */
struct flow_dissector_key_arp {
	__u32 sip;
	__u32 tip;
	__u8 op;
	unsigned char sha[ETH_ALEN];
	unsigned char tha[ETH_ALEN];
};

/**
 * struct flow_dissector_key_ports:
 * @ports: port numbers of Transport header
 * @src: source port number
 * @dst: destination port number
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
 * struct flow_dissector_key_ports_range
 * @tp: port number from packet
 * @tp_min: min port number in range
 * @tp_max: max port number in range
 */
struct flow_dissector_key_ports_range {
	union {
		struct flow_dissector_key_ports tp;
		struct {
			struct flow_dissector_key_ports tp_min;
			struct flow_dissector_key_ports tp_max;
		};
	};
};

/**
 * struct flow_dissector_key_icmp:
 * @type: ICMP type
 * @code: ICMP code
 * @id:   Session identifier
 */
struct flow_dissector_key_icmp {
	struct {
		u8 type;
		u8 code;
	};
	u16 id;
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

/**
 * struct flow_dissector_key_tcp:
 * @flags: flags
 */
struct flow_dissector_key_tcp {
	__be16 flags;
};

/**
 * struct flow_dissector_key_ip:
 * @tos: tos
 * @ttl: ttl
 */
struct flow_dissector_key_ip {
	__u8	tos;
	__u8	ttl;
};

/**
 * struct flow_dissector_key_meta:
 * @ingress_ifindex: ingress ifindex
 * @ingress_iftype: ingress interface type
 * @l2_miss: packet did not match an L2 entry during forwarding
 */
struct flow_dissector_key_meta {
	int ingress_ifindex;
	u16 ingress_iftype;
	u8 l2_miss;
};

/**
 * struct flow_dissector_key_ct:
 * @ct_state: conntrack state after converting with map
 * @ct_mark: conttrack mark
 * @ct_zone: conntrack zone
 * @ct_labels: conntrack labels
 */
struct flow_dissector_key_ct {
	u16	ct_state;
	u16	ct_zone;
	u32	ct_mark;
	u32	ct_labels[4];
};

/**
 * struct flow_dissector_key_hash:
 * @hash: hash value
 */
struct flow_dissector_key_hash {
	u32 hash;
};

/**
 * struct flow_dissector_key_num_of_vlans:
 * @num_of_vlans: num_of_vlans value
 */
struct flow_dissector_key_num_of_vlans {
	u8 num_of_vlans;
};

/**
 * struct flow_dissector_key_pppoe:
 * @session_id: pppoe session id
 * @ppp_proto: ppp protocol
 * @type: pppoe eth type
 */
struct flow_dissector_key_pppoe {
	__be16 session_id;
	__be16 ppp_proto;
	__be16 type;
};

/**
 * struct flow_dissector_key_l2tpv3:
 * @session_id: identifier for a l2tp session
 */
struct flow_dissector_key_l2tpv3 {
	__be32 session_id;
};

/**
 * struct flow_dissector_key_cfm
 * @mdl_ver: maintenance domain level (mdl) and cfm protocol version
 * @opcode: code specifying a type of cfm protocol packet
 *
 * See 802.1ag, ITU-T G.8013/Y.1731
 *         1               2
 * |7 6 5 4 3 2 1 0|7 6 5 4 3 2 1 0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | mdl | version |     opcode    |
 * +-----+---------+-+-+-+-+-+-+-+-+
 */
struct flow_dissector_key_cfm {
	u8	mdl_ver;
	u8	opcode;
};

#define FLOW_DIS_CFM_MDL_MASK GENMASK(7, 5)
#define FLOW_DIS_CFM_MDL_MAX 7

enum flow_dissector_key_id {
	FLOW_DISSECTOR_KEY_CONTROL, /* struct flow_dissector_key_control */
	FLOW_DISSECTOR_KEY_BASIC, /* struct flow_dissector_key_basic */
	FLOW_DISSECTOR_KEY_IPV4_ADDRS, /* struct flow_dissector_key_ipv4_addrs */
	FLOW_DISSECTOR_KEY_IPV6_ADDRS, /* struct flow_dissector_key_ipv6_addrs */
	FLOW_DISSECTOR_KEY_PORTS, /* struct flow_dissector_key_ports */
	FLOW_DISSECTOR_KEY_PORTS_RANGE, /* struct flow_dissector_key_ports */
	FLOW_DISSECTOR_KEY_ICMP, /* struct flow_dissector_key_icmp */
	FLOW_DISSECTOR_KEY_ETH_ADDRS, /* struct flow_dissector_key_eth_addrs */
	FLOW_DISSECTOR_KEY_TIPC, /* struct flow_dissector_key_tipc */
	FLOW_DISSECTOR_KEY_ARP, /* struct flow_dissector_key_arp */
	FLOW_DISSECTOR_KEY_VLAN, /* struct flow_dissector_key_vlan */
	FLOW_DISSECTOR_KEY_FLOW_LABEL, /* struct flow_dissector_key_tags */
	FLOW_DISSECTOR_KEY_GRE_KEYID, /* struct flow_dissector_key_keyid */
	FLOW_DISSECTOR_KEY_MPLS_ENTROPY, /* struct flow_dissector_key_keyid */
	FLOW_DISSECTOR_KEY_ENC_KEYID, /* struct flow_dissector_key_keyid */
	FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS, /* struct flow_dissector_key_ipv4_addrs */
	FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS, /* struct flow_dissector_key_ipv6_addrs */
	FLOW_DISSECTOR_KEY_ENC_CONTROL, /* struct flow_dissector_key_control */
	FLOW_DISSECTOR_KEY_ENC_PORTS, /* struct flow_dissector_key_ports */
	FLOW_DISSECTOR_KEY_MPLS, /* struct flow_dissector_key_mpls */
	FLOW_DISSECTOR_KEY_TCP, /* struct flow_dissector_key_tcp */
	FLOW_DISSECTOR_KEY_IP, /* struct flow_dissector_key_ip */
	FLOW_DISSECTOR_KEY_CVLAN, /* struct flow_dissector_key_vlan */
	FLOW_DISSECTOR_KEY_ENC_IP, /* struct flow_dissector_key_ip */
	FLOW_DISSECTOR_KEY_ENC_OPTS, /* struct flow_dissector_key_enc_opts */
	FLOW_DISSECTOR_KEY_META, /* struct flow_dissector_key_meta */
	FLOW_DISSECTOR_KEY_CT, /* struct flow_dissector_key_ct */
	FLOW_DISSECTOR_KEY_HASH, /* struct flow_dissector_key_hash */
	FLOW_DISSECTOR_KEY_NUM_OF_VLANS, /* struct flow_dissector_key_num_of_vlans */
	FLOW_DISSECTOR_KEY_PPPOE, /* struct flow_dissector_key_pppoe */
	FLOW_DISSECTOR_KEY_L2TPV3, /* struct flow_dissector_key_l2tpv3 */
	FLOW_DISSECTOR_KEY_CFM, /* struct flow_dissector_key_cfm */

	FLOW_DISSECTOR_KEY_MAX,
};

#define FLOW_DISSECTOR_F_PARSE_1ST_FRAG		BIT(0)
#define FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL	BIT(1)
#define FLOW_DISSECTOR_F_STOP_AT_ENCAP		BIT(2)
#define FLOW_DISSECTOR_F_STOP_BEFORE_ENCAP	BIT(3)

struct flow_dissector_key {
	enum flow_dissector_key_id key_id;
	size_t offset; /* offset of struct flow_dissector_key_*
			  in target the struct */
};

struct flow_dissector {
	unsigned int used_keys; /* each bit repesents presence of one key id */
	unsigned short int offset[FLOW_DISSECTOR_KEY_MAX];
};

struct flow_keys_basic {
	struct flow_dissector_key_control control;
	struct flow_dissector_key_basic basic;
};

struct flow_keys {
	struct flow_dissector_key_control control;
#define FLOW_KEYS_HASH_START_FIELD basic
	struct flow_dissector_key_basic basic __aligned(SIPHASH_ALIGNMENT);
	struct flow_dissector_key_tags tags;
	struct flow_dissector_key_vlan vlan;
	struct flow_dissector_key_vlan cvlan;
	struct flow_dissector_key_keyid keyid;
	struct flow_dissector_key_ports ports;
	struct flow_dissector_key_icmp icmp;
	/* 'addrs' must be the last member */
	struct flow_dissector_key_addrs addrs;
};

#define FLOW_KEYS_HASH_OFFSET		\
	offsetof(struct flow_keys, FLOW_KEYS_HASH_START_FIELD)

__be32 flow_get_u32_src(const struct flow_keys *flow);
__be32 flow_get_u32_dst(const struct flow_keys *flow);

extern struct flow_dissector flow_keys_dissector;
extern struct flow_dissector flow_keys_basic_dissector;

/* struct flow_keys_digest:
 *
 * This structure is used to hold a digest of the full flow keys. This is a
 * larger "hash" of a flow to allow definitively matching specific flows where
 * the 32 bit skb->hash is not large enough. The size is limited to 16 bytes so
 * that it can be used in CB of skb (see sch_choke for an example).
 */
#define FLOW_KEYS_DIGEST_LEN	16
struct flow_keys_digest {
	u8	data[FLOW_KEYS_DIGEST_LEN];
};

void make_flow_keys_digest(struct flow_keys_digest *digest,
			   const struct flow_keys *flow);

static inline bool flow_keys_have_l4(const struct flow_keys *keys)
{
	return (keys->ports.ports || keys->tags.flow_label);
}

u32 flow_hash_from_keys(struct flow_keys *keys);
void skb_flow_get_icmp_tci(const struct sk_buff *skb,
			   struct flow_dissector_key_icmp *key_icmp,
			   const void *data, int thoff, int hlen);

static inline bool dissector_uses_key(const struct flow_dissector *flow_dissector,
				      enum flow_dissector_key_id key_id)
{
	return flow_dissector->used_keys & (1 << key_id);
}

static inline void *skb_flow_dissector_target(struct flow_dissector *flow_dissector,
					      enum flow_dissector_key_id key_id,
					      void *target_container)
{
	return ((char *)target_container) + flow_dissector->offset[key_id];
}

struct bpf_flow_dissector {
	struct bpf_flow_keys	*flow_keys;
	const struct sk_buff	*skb;
	const void		*data;
	const void		*data_end;
};

static inline void
flow_dissector_init_keys(struct flow_dissector_key_control *key_control,
			 struct flow_dissector_key_basic *key_basic)
{
	memset(key_control, 0, sizeof(*key_control));
	memset(key_basic, 0, sizeof(*key_basic));
}

#ifdef CONFIG_BPF_SYSCALL
int flow_dissector_bpf_prog_attach_check(struct net *net,
					 struct bpf_prog *prog);
#endif /* CONFIG_BPF_SYSCALL */

#endif
