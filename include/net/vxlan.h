#ifndef __NET_VXLAN_H
#define __NET_VXLAN_H 1

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/udp.h>

#define VNI_HASH_BITS	10
#define VNI_HASH_SIZE	(1<<VNI_HASH_BITS)

/* VXLAN protocol header */
struct vxlanhdr {
	__be32 vx_flags;
	__be32 vx_vni;
};

struct vxlan_sock;
typedef void (vxlan_rcv_t)(struct vxlan_sock *vh, struct sk_buff *skb, __be32 key);

/* per UDP socket information */
struct vxlan_sock {
	struct hlist_node hlist;
	vxlan_rcv_t	 *rcv;
	void		 *data;
	struct work_struct del_work;
	struct socket	 *sock;
	struct rcu_head	  rcu;
	struct hlist_head vni_list[VNI_HASH_SIZE];
	atomic_t	  refcnt;
	struct udp_offload udp_offloads;
};

#define VXLAN_F_LEARN			0x01
#define VXLAN_F_PROXY			0x02
#define VXLAN_F_RSC			0x04
#define VXLAN_F_L2MISS			0x08
#define VXLAN_F_L3MISS			0x10
#define VXLAN_F_IPV6			0x20
#define VXLAN_F_UDP_CSUM		0x40
#define VXLAN_F_UDP_ZERO_CSUM6_TX	0x80
#define VXLAN_F_UDP_ZERO_CSUM6_RX	0x100

struct vxlan_sock *vxlan_sock_add(struct net *net, __be16 port,
				  vxlan_rcv_t *rcv, void *data,
				  bool no_share, u32 flags);

void vxlan_sock_release(struct vxlan_sock *vs);

int vxlan_xmit_skb(struct vxlan_sock *vs,
		   struct rtable *rt, struct sk_buff *skb,
		   __be32 src, __be32 dst, __u8 tos, __u8 ttl, __be16 df,
		   __be16 src_port, __be16 dst_port, __be32 vni, bool xnet);

static inline netdev_features_t vxlan_features_check(struct sk_buff *skb,
						     netdev_features_t features)
{
	u8 l4_hdr = 0;

	if (!skb->encapsulation)
		return features;

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		l4_hdr = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		l4_hdr = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		return features;;
	}

	if ((l4_hdr == IPPROTO_UDP) &&
	    (skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
	     skb->inner_protocol != htons(ETH_P_TEB) ||
	     (skb_inner_mac_header(skb) - skb_transport_header(skb) !=
	      sizeof(struct udphdr) + sizeof(struct vxlanhdr))))
		return features & ~(NETIF_F_ALL_CSUM | NETIF_F_GSO_MASK);

	return features;
}

/* IP header + UDP + VXLAN + Ethernet header */
#define VXLAN_HEADROOM (20 + 8 + 8 + 14)
/* IPv6 header + UDP + VXLAN + Ethernet header */
#define VXLAN6_HEADROOM (40 + 8 + 8 + 14)

#if IS_ENABLED(CONFIG_VXLAN)
void vxlan_get_rx_port(struct net_device *netdev);
#else
static inline void vxlan_get_rx_port(struct net_device *netdev)
{
}
#endif
#endif
