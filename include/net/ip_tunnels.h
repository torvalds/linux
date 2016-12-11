#ifndef __NET_IP_TUNNELS_H
#define __NET_IP_TUNNELS_H 1

#include <linux/if_tunnel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <linux/u64_stats_sync.h>
#include <net/dsfield.h>
#include <net/gro_cells.h>
#include <net/inet_ecn.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>
#include <net/lwtunnel.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#endif

/* Keep error state on tunnel for 30 sec */
#define IPTUNNEL_ERR_TIMEO	(30*HZ)

/* Used to memset ip_tunnel padding. */
#define IP_TUNNEL_KEY_SIZE	offsetofend(struct ip_tunnel_key, tp_dst)

/* Used to memset ipv4 address padding. */
#define IP_TUNNEL_KEY_IPV4_PAD	offsetofend(struct ip_tunnel_key, u.ipv4.dst)
#define IP_TUNNEL_KEY_IPV4_PAD_LEN				\
	(FIELD_SIZEOF(struct ip_tunnel_key, u) -		\
	 FIELD_SIZEOF(struct ip_tunnel_key, u.ipv4))

struct ip_tunnel_key {
	__be64			tun_id;
	union {
		struct {
			__be32	src;
			__be32	dst;
		} ipv4;
		struct {
			struct in6_addr src;
			struct in6_addr dst;
		} ipv6;
	} u;
	__be16			tun_flags;
	u8			tos;		/* TOS for IPv4, TC for IPv6 */
	u8			ttl;		/* TTL for IPv4, HL for IPv6 */
	__be16			tp_src;
	__be16			tp_dst;
};

/* Flags for ip_tunnel_info mode. */
#define IP_TUNNEL_INFO_TX	0x01	/* represents tx tunnel parameters */
#define IP_TUNNEL_INFO_IPV6	0x02	/* key contains IPv6 addresses */

struct ip_tunnel_info {
	struct ip_tunnel_key	key;
	u8			options_len;
	u8			mode;
};

/* 6rd prefix/relay information */
#ifdef CONFIG_IPV6_SIT_6RD
struct ip_tunnel_6rd_parm {
	struct in6_addr		prefix;
	__be32			relay_prefix;
	u16			prefixlen;
	u16			relay_prefixlen;
};
#endif

struct ip_tunnel_encap {
	u16			type;
	u16			flags;
	__be16			sport;
	__be16			dport;
};

struct ip_tunnel_prl_entry {
	struct ip_tunnel_prl_entry __rcu *next;
	__be32				addr;
	u16				flags;
	struct rcu_head			rcu_head;
};

struct ip_tunnel_dst {
	struct dst_entry __rcu 		*dst;
	__be32				 saddr;
};

struct metadata_dst;

struct ip_tunnel {
	struct ip_tunnel __rcu	*next;
	struct hlist_node hash_node;
	struct net_device	*dev;
	struct net		*net;	/* netns for packet i/o */

	int		err_count;	/* Number of arrived ICMP errors */
	unsigned long	err_time;	/* Time when the last ICMP error
					 * arrived */

	/* These four fields used only by GRE */
	u32		i_seqno;	/* The last seen seqno	*/
	u32		o_seqno;	/* The last output seqno */
	int		tun_hlen;	/* Precalculated header length */
	int		mlink;

	struct ip_tunnel_dst __percpu *dst_cache;

	struct ip_tunnel_parm parms;

	int		encap_hlen;	/* Encap header length (FOU,GUE) */
	struct ip_tunnel_encap encap;

	int		hlen;		/* tun_hlen + encap_hlen */

	/* for SIT */
#ifdef CONFIG_IPV6_SIT_6RD
	struct ip_tunnel_6rd_parm ip6rd;
#endif
	struct ip_tunnel_prl_entry __rcu *prl;	/* potential router list */
	unsigned int		prl_count;	/* # of entries in PRL */
	int			ip_tnl_net_id;
	struct gro_cells	gro_cells;
	bool			collect_md;
};

#define TUNNEL_CSUM		__cpu_to_be16(0x01)
#define TUNNEL_ROUTING		__cpu_to_be16(0x02)
#define TUNNEL_KEY		__cpu_to_be16(0x04)
#define TUNNEL_SEQ		__cpu_to_be16(0x08)
#define TUNNEL_STRICT		__cpu_to_be16(0x10)
#define TUNNEL_REC		__cpu_to_be16(0x20)
#define TUNNEL_VERSION		__cpu_to_be16(0x40)
#define TUNNEL_NO_KEY		__cpu_to_be16(0x80)
#define TUNNEL_DONT_FRAGMENT    __cpu_to_be16(0x0100)
#define TUNNEL_OAM		__cpu_to_be16(0x0200)
#define TUNNEL_CRIT_OPT		__cpu_to_be16(0x0400)
#define TUNNEL_GENEVE_OPT	__cpu_to_be16(0x0800)
#define TUNNEL_VXLAN_OPT	__cpu_to_be16(0x1000)

#define TUNNEL_OPTIONS_PRESENT	(TUNNEL_GENEVE_OPT | TUNNEL_VXLAN_OPT)

struct tnl_ptk_info {
	__be16 flags;
	__be16 proto;
	__be32 key;
	__be32 seq;
};

#define PACKET_RCVD	0
#define PACKET_REJECT	1

#define IP_TNL_HASH_BITS   7
#define IP_TNL_HASH_SIZE   (1 << IP_TNL_HASH_BITS)

struct ip_tunnel_net {
	struct net_device *fb_tunnel_dev;
	struct hlist_head tunnels[IP_TNL_HASH_SIZE];
	struct ip_tunnel __rcu *collect_md_tun;
};

struct ip_tunnel_encap_ops {
	size_t (*encap_hlen)(struct ip_tunnel_encap *e);
	int (*build_header)(struct sk_buff *skb, struct ip_tunnel_encap *e,
			    u8 *protocol, struct flowi4 *fl4);
};

#define MAX_IPTUN_ENCAP_OPS 8

extern const struct ip_tunnel_encap_ops __rcu *
		iptun_encaps[MAX_IPTUN_ENCAP_OPS];

int ip_tunnel_encap_add_ops(const struct ip_tunnel_encap_ops *op,
			    unsigned int num);
int ip_tunnel_encap_del_ops(const struct ip_tunnel_encap_ops *op,
			    unsigned int num);

static inline void ip_tunnel_key_init(struct ip_tunnel_key *key,
				      __be32 saddr, __be32 daddr,
				      u8 tos, u8 ttl,
				      __be16 tp_src, __be16 tp_dst,
				      __be64 tun_id, __be16 tun_flags)
{
	key->tun_id = tun_id;
	key->u.ipv4.src = saddr;
	key->u.ipv4.dst = daddr;
	memset((unsigned char *)key + IP_TUNNEL_KEY_IPV4_PAD,
	       0, IP_TUNNEL_KEY_IPV4_PAD_LEN);
	key->tos = tos;
	key->ttl = ttl;
	key->tun_flags = tun_flags;

	/* For the tunnel types on the top of IPsec, the tp_src and tp_dst of
	 * the upper tunnel are used.
	 * E.g: GRE over IPSEC, the tp_src and tp_port are zero.
	 */
	key->tp_src = tp_src;
	key->tp_dst = tp_dst;

	/* Clear struct padding. */
	if (sizeof(*key) != IP_TUNNEL_KEY_SIZE)
		memset((unsigned char *)key + IP_TUNNEL_KEY_SIZE,
		       0, sizeof(*key) - IP_TUNNEL_KEY_SIZE);
}

static inline unsigned short ip_tunnel_info_af(const struct ip_tunnel_info
					       *tun_info)
{
	return tun_info->mode & IP_TUNNEL_INFO_IPV6 ? AF_INET6 : AF_INET;
}

#ifdef CONFIG_INET

int ip_tunnel_init(struct net_device *dev);
void ip_tunnel_uninit(struct net_device *dev);
void  ip_tunnel_dellink(struct net_device *dev, struct list_head *head);
struct net *ip_tunnel_get_link_net(const struct net_device *dev);
int ip_tunnel_get_iflink(const struct net_device *dev);
int ip_tunnel_init_net(struct net *net, int ip_tnl_net_id,
		       struct rtnl_link_ops *ops, char *devname);

void ip_tunnel_delete_net(struct ip_tunnel_net *itn, struct rtnl_link_ops *ops);

void ip_tunnel_xmit(struct sk_buff *skb, struct net_device *dev,
		    const struct iphdr *tnl_params, const u8 protocol);
int ip_tunnel_ioctl(struct net_device *dev, struct ip_tunnel_parm *p, int cmd);
int ip_tunnel_encap(struct sk_buff *skb, struct ip_tunnel *t,
		    u8 *protocol, struct flowi4 *fl4);
int __ip_tunnel_change_mtu(struct net_device *dev, int new_mtu, bool strict);
int ip_tunnel_change_mtu(struct net_device *dev, int new_mtu);

struct rtnl_link_stats64 *ip_tunnel_get_stats64(struct net_device *dev,
						struct rtnl_link_stats64 *tot);
struct ip_tunnel *ip_tunnel_lookup(struct ip_tunnel_net *itn,
				   int link, __be16 flags,
				   __be32 remote, __be32 local,
				   __be32 key);

int ip_tunnel_rcv(struct ip_tunnel *tunnel, struct sk_buff *skb,
		  const struct tnl_ptk_info *tpi, struct metadata_dst *tun_dst,
		  bool log_ecn_error);
int ip_tunnel_changelink(struct net_device *dev, struct nlattr *tb[],
			 struct ip_tunnel_parm *p);
int ip_tunnel_newlink(struct net_device *dev, struct nlattr *tb[],
		      struct ip_tunnel_parm *p);
void ip_tunnel_setup(struct net_device *dev, int net_id);
void ip_tunnel_dst_reset_all(struct ip_tunnel *t);
int ip_tunnel_encap_setup(struct ip_tunnel *t,
			  struct ip_tunnel_encap *ipencap);

/* Extract dsfield from inner protocol */
static inline u8 ip_tunnel_get_dsfield(const struct iphdr *iph,
				       const struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP))
		return iph->tos;
	else if (skb->protocol == htons(ETH_P_IPV6))
		return ipv6_get_dsfield((const struct ipv6hdr *)iph);
	else
		return 0;
}

/* Propogate ECN bits out */
static inline u8 ip_tunnel_ecn_encap(u8 tos, const struct iphdr *iph,
				     const struct sk_buff *skb)
{
	u8 inner = ip_tunnel_get_dsfield(iph, skb);

	return INET_ECN_encapsulate(tos, inner);
}

int iptunnel_pull_header(struct sk_buff *skb, int hdr_len, __be16 inner_proto);
int iptunnel_xmit(struct sock *sk, struct rtable *rt, struct sk_buff *skb,
		  __be32 src, __be32 dst, u8 proto,
		  u8 tos, u8 ttl, __be16 df, bool xnet);
struct metadata_dst *iptunnel_metadata_reply(struct metadata_dst *md,
					     gfp_t flags);

struct sk_buff *iptunnel_handle_offloads(struct sk_buff *skb, bool gre_csum,
					 int gso_type_mask);

static inline int iptunnel_pull_offloads(struct sk_buff *skb)
{
	if (skb_is_gso(skb)) {
		int err;

		err = skb_unclone(skb, GFP_ATOMIC);
		if (unlikely(err))
			return err;
		skb_shinfo(skb)->gso_type &= ~(NETIF_F_GSO_ENCAP_ALL >>
					       NETIF_F_GSO_SHIFT);
	}

	skb->encapsulation = 0;
	return 0;
}

static inline void iptunnel_xmit_stats(int err,
				       struct net_device_stats *err_stats,
				       struct pcpu_sw_netstats __percpu *stats)
{
	if (err > 0) {
		struct pcpu_sw_netstats *tstats = get_cpu_ptr(stats);

		u64_stats_update_begin(&tstats->syncp);
		tstats->tx_bytes += err;
		tstats->tx_packets++;
		u64_stats_update_end(&tstats->syncp);
		put_cpu_ptr(tstats);
	} else if (err < 0) {
		err_stats->tx_errors++;
		err_stats->tx_aborted_errors++;
	} else {
		err_stats->tx_dropped++;
	}
}

static inline void *ip_tunnel_info_opts(struct ip_tunnel_info *info)
{
	return info + 1;
}

static inline void ip_tunnel_info_opts_get(void *to,
					   const struct ip_tunnel_info *info)
{
	memcpy(to, info + 1, info->options_len);
}

static inline void ip_tunnel_info_opts_set(struct ip_tunnel_info *info,
					   const void *from, int len)
{
	memcpy(ip_tunnel_info_opts(info), from, len);
	info->options_len = len;
}

static inline struct ip_tunnel_info *lwt_tun_info(struct lwtunnel_state *lwtstate)
{
	return (struct ip_tunnel_info *)lwtstate->data;
}

extern struct static_key ip_tunnel_metadata_cnt;

/* Returns > 0 if metadata should be collected */
static inline int ip_tunnel_collect_metadata(void)
{
	return static_key_false(&ip_tunnel_metadata_cnt);
}

void __init ip_tunnel_core_init(void);

void ip_tunnel_need_metadata(void);
void ip_tunnel_unneed_metadata(void);

#else /* CONFIG_INET */

static inline struct ip_tunnel_info *lwt_tun_info(struct lwtunnel_state *lwtstate)
{
	return NULL;
}

static inline void ip_tunnel_need_metadata(void)
{
}

static inline void ip_tunnel_unneed_metadata(void)
{
}

#endif /* CONFIG_INET */

#endif /* __NET_IP_TUNNELS_H */
