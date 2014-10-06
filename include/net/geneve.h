#ifndef __NET_GENEVE_H
#define __NET_GENEVE_H  1

#ifdef CONFIG_INET
#include <net/udp_tunnel.h>
#endif


/* Geneve Header:
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |Ver|  Opt Len  |O|C|    Rsvd.  |          Protocol Type        |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |        Virtual Network Identifier (VNI)       |    Reserved   |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    Variable Length Options                    |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Option Header:
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |          Option Class         |      Type     |R|R|R| Length  |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                      Variable Option Data                     |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

struct geneve_opt {
	__be16	opt_class;
	u8	type;
#ifdef __LITTLE_ENDIAN_BITFIELD
	u8	length:5;
	u8	r3:1;
	u8	r2:1;
	u8	r1:1;
#else
	u8	r1:1;
	u8	r2:1;
	u8	r3:1;
	u8	length:5;
#endif
	u8	opt_data[];
};

#define GENEVE_CRIT_OPT_TYPE (1 << 7)

struct genevehdr {
#ifdef __LITTLE_ENDIAN_BITFIELD
	u8 opt_len:6;
	u8 ver:2;
	u8 rsvd1:6;
	u8 critical:1;
	u8 oam:1;
#else
	u8 ver:2;
	u8 opt_len:6;
	u8 oam:1;
	u8 critical:1;
	u8 rsvd1:6;
#endif
	__be16 proto_type;
	u8 vni[3];
	u8 rsvd2;
	struct geneve_opt options[];
};

#ifdef CONFIG_INET
struct geneve_sock;

typedef void (geneve_rcv_t)(struct geneve_sock *gs, struct sk_buff *skb);

struct geneve_sock {
	struct hlist_node	hlist;
	geneve_rcv_t		*rcv;
	void			*rcv_data;
	struct work_struct	del_work;
	struct socket		*sock;
	struct rcu_head		rcu;
	atomic_t		refcnt;
	struct udp_offload	udp_offloads;
};

#define GENEVE_VER 0
#define GENEVE_BASE_HLEN (sizeof(struct udphdr) + sizeof(struct genevehdr))

struct geneve_sock *geneve_sock_add(struct net *net, __be16 port,
				    geneve_rcv_t *rcv, void *data,
				    bool no_share, bool ipv6);

void geneve_sock_release(struct geneve_sock *vs);

int geneve_xmit_skb(struct geneve_sock *gs, struct rtable *rt,
		    struct sk_buff *skb, __be32 src, __be32 dst, __u8 tos,
		    __u8 ttl, __be16 df, __be16 src_port, __be16 dst_port,
		    __be16 tun_flags, u8 vni[3], u8 opt_len, u8 *opt,
		    bool xnet);
#endif /*ifdef CONFIG_INET */

#endif /*ifdef__NET_GENEVE_H */
