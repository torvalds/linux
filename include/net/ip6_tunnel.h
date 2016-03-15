#ifndef _NET_IP6_TUNNEL_H
#define _NET_IP6_TUNNEL_H

#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/if_tunnel.h>
#include <linux/ip6_tunnel.h>
#include <net/ip_tunnels.h>

#define IP6TUNNEL_ERR_TIMEO (30*HZ)

/* capable of sending packets */
#define IP6_TNL_F_CAP_XMIT 0x10000
/* capable of receiving packets */
#define IP6_TNL_F_CAP_RCV 0x20000
/* determine capability on a per-packet basis */
#define IP6_TNL_F_CAP_PER_PACKET 0x40000

struct __ip6_tnl_parm {
	char name[IFNAMSIZ];	/* name of tunnel device */
	int link;		/* ifindex of underlying L2 interface */
	__u8 proto;		/* tunnel protocol */
	__u8 encap_limit;	/* encapsulation limit for tunnel */
	__u8 hop_limit;		/* hop limit for tunnel */
	__be32 flowinfo;	/* traffic class and flowlabel for tunnel */
	__u32 flags;		/* tunnel flags */
	struct in6_addr laddr;	/* local tunnel end-point address */
	struct in6_addr raddr;	/* remote tunnel end-point address */

	__be16			i_flags;
	__be16			o_flags;
	__be32			i_key;
	__be32			o_key;
};

struct ip6_tnl_dst {
	seqlock_t lock;
	struct dst_entry __rcu *dst;
	u32 cookie;
};

/* IPv6 tunnel */
struct ip6_tnl {
	struct ip6_tnl __rcu *next;	/* next tunnel in list */
	struct net_device *dev;	/* virtual device associated with tunnel */
	struct net *net;	/* netns for packet i/o */
	struct __ip6_tnl_parm parms;	/* tunnel configuration parameters */
	struct flowi fl;	/* flowi template for xmit */
	struct ip6_tnl_dst __percpu *dst_cache;	/* cached dst */

	int err_count;
	unsigned long err_time;

	/* These fields used only by GRE */
	__u32 i_seqno;	/* The last seen seqno	*/
	__u32 o_seqno;	/* The last output seqno */
	int hlen;       /* Precalculated GRE header length */
	int mlink;
};

/* Tunnel encapsulation limit destination sub-option */

struct ipv6_tlv_tnl_enc_lim {
	__u8 type;		/* type-code for option         */
	__u8 length;		/* option length                */
	__u8 encap_limit;	/* tunnel encapsulation limit   */
} __packed;

struct dst_entry *ip6_tnl_dst_get(struct ip6_tnl *t);
int ip6_tnl_dst_init(struct ip6_tnl *t);
void ip6_tnl_dst_destroy(struct ip6_tnl *t);
void ip6_tnl_dst_reset(struct ip6_tnl *t);
void ip6_tnl_dst_set(struct ip6_tnl *t, struct dst_entry *dst);
int ip6_tnl_rcv_ctl(struct ip6_tnl *t, const struct in6_addr *laddr,
		const struct in6_addr *raddr);
int ip6_tnl_xmit_ctl(struct ip6_tnl *t, const struct in6_addr *laddr,
		     const struct in6_addr *raddr);
__u16 ip6_tnl_parse_tlv_enc_lim(struct sk_buff *skb, __u8 *raw);
__u32 ip6_tnl_get_cap(struct ip6_tnl *t, const struct in6_addr *laddr,
			     const struct in6_addr *raddr);
struct net *ip6_tnl_get_link_net(const struct net_device *dev);
int ip6_tnl_get_iflink(const struct net_device *dev);

#ifdef CONFIG_INET
static inline void ip6tunnel_xmit(struct sock *sk, struct sk_buff *skb,
				  struct net_device *dev)
{
	int pkt_len, err;

	pkt_len = skb->len - skb_inner_network_offset(skb);
	err = ip6_local_out(dev_net(skb_dst(skb)->dev), sk, skb);
	if (unlikely(net_xmit_eval(err)))
		pkt_len = -1;
	iptunnel_xmit_stats(dev, pkt_len);
}
#endif
#endif
