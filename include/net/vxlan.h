/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_VXLAN_H
#define __NET_VXLAN_H 1

#include <linux/if_vlan.h>
#include <net/udp_tunnel.h>
#include <net/dst_metadata.h>
#include <net/rtnetlink.h>
#include <net/switchdev.h>
#include <net/nexthop.h>

#define IANA_VXLAN_UDP_PORT     4789

/* VXLAN protocol (RFC 7348) header:
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |R|R|R|R|I|R|R|R|               Reserved                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                VXLAN Network Identifier (VNI) |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * I = VXLAN Network Identifier (VNI) present.
 */
struct vxlanhdr {
	__be32 vx_flags;
	__be32 vx_vni;
};

/* VXLAN header flags. */
#define VXLAN_HF_VNI	cpu_to_be32(BIT(27))

#define VXLAN_N_VID     (1u << 24)
#define VXLAN_VID_MASK  (VXLAN_N_VID - 1)
#define VXLAN_VNI_MASK	cpu_to_be32(VXLAN_VID_MASK << 8)
#define VXLAN_HLEN (sizeof(struct udphdr) + sizeof(struct vxlanhdr))

#define VNI_HASH_BITS	10
#define VNI_HASH_SIZE	(1<<VNI_HASH_BITS)
#define FDB_HASH_BITS	8
#define FDB_HASH_SIZE	(1<<FDB_HASH_BITS)

/* Remote checksum offload for VXLAN (VXLAN_F_REMCSUM_[RT]X):
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |R|R|R|R|I|R|R|R|R|R|C|              Reserved                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           VXLAN Network Identifier (VNI)      |O| Csum start  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * C = Remote checksum offload bit. When set indicates that the
 *     remote checksum offload data is present.
 *
 * O = Offset bit. Indicates the checksum offset relative to
 *     checksum start.
 *
 * Csum start = Checksum start divided by two.
 *
 * http://tools.ietf.org/html/draft-herbert-vxlan-rco
 */

/* VXLAN-RCO header flags. */
#define VXLAN_HF_RCO	cpu_to_be32(BIT(21))

/* Remote checksum offload header option */
#define VXLAN_RCO_MASK	cpu_to_be32(0x7f)  /* Last byte of vni field */
#define VXLAN_RCO_UDP	cpu_to_be32(0x80)  /* Indicate UDP RCO (TCP when not set *) */
#define VXLAN_RCO_SHIFT	1		   /* Left shift of start */
#define VXLAN_RCO_SHIFT_MASK ((1 << VXLAN_RCO_SHIFT) - 1)
#define VXLAN_MAX_REMCSUM_START (0x7f << VXLAN_RCO_SHIFT)

/*
 * VXLAN Group Based Policy Extension (VXLAN_F_GBP):
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |G|R|R|R|I|R|R|R|R|D|R|R|A|R|R|R|        Group Policy ID        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                VXLAN Network Identifier (VNI) |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * G = Group Policy ID present.
 *
 * D = Don't Learn bit. When set, this bit indicates that the egress
 *     VTEP MUST NOT learn the source address of the encapsulated frame.
 *
 * A = Indicates that the group policy has already been applied to
 *     this packet. Policies MUST NOT be applied by devices when the
 *     A bit is set.
 *
 * https://tools.ietf.org/html/draft-smith-vxlan-group-policy
 */
struct vxlanhdr_gbp {
	u8	vx_flags;
#ifdef __LITTLE_ENDIAN_BITFIELD
	u8	reserved_flags1:3,
		policy_applied:1,
		reserved_flags2:2,
		dont_learn:1,
		reserved_flags3:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8	reserved_flags1:1,
		dont_learn:1,
		reserved_flags2:2,
		policy_applied:1,
		reserved_flags3:3;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	__be16	policy_id;
	__be32	vx_vni;
};

/* VXLAN-GBP header flags. */
#define VXLAN_HF_GBP	cpu_to_be32(BIT(31))

#define VXLAN_GBP_USED_BITS (VXLAN_HF_GBP | cpu_to_be32(0xFFFFFF))

/* skb->mark mapping
 *
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |R|R|R|R|R|R|R|R|R|D|R|R|A|R|R|R|        Group Policy ID        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define VXLAN_GBP_DONT_LEARN		(BIT(6) << 16)
#define VXLAN_GBP_POLICY_APPLIED	(BIT(3) << 16)
#define VXLAN_GBP_ID_MASK		(0xFFFF)

/*
 * VXLAN Generic Protocol Extension (VXLAN_F_GPE):
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |R|R|Ver|I|P|R|O|       Reserved                |Next Protocol  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                VXLAN Network Identifier (VNI) |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Ver = Version. Indicates VXLAN GPE protocol version.
 *
 * P = Next Protocol Bit. The P bit is set to indicate that the
 *     Next Protocol field is present.
 *
 * O = OAM Flag Bit. The O bit is set to indicate that the packet
 *     is an OAM packet.
 *
 * Next Protocol = This 8 bit field indicates the protocol header
 * immediately following the VXLAN GPE header.
 *
 * https://tools.ietf.org/html/draft-ietf-nvo3-vxlan-gpe-01
 */

struct vxlanhdr_gpe {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8	oam_flag:1,
		reserved_flags1:1,
		np_applied:1,
		instance_applied:1,
		version:2,
		reserved_flags2:2;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8	reserved_flags2:2,
		version:2,
		instance_applied:1,
		np_applied:1,
		reserved_flags1:1,
		oam_flag:1;
#endif
	u8	reserved_flags3;
	u8	reserved_flags4;
	u8	next_protocol;
	__be32	vx_vni;
};

/* VXLAN-GPE header flags. */
#define VXLAN_HF_VER	cpu_to_be32(BIT(29) | BIT(28))
#define VXLAN_HF_NP	cpu_to_be32(BIT(26))
#define VXLAN_HF_OAM	cpu_to_be32(BIT(24))

#define VXLAN_GPE_USED_BITS (VXLAN_HF_VER | VXLAN_HF_NP | VXLAN_HF_OAM | \
			     cpu_to_be32(0xff))

struct vxlan_metadata {
	u32		gbp;
};

/* per UDP socket information */
struct vxlan_sock {
	struct hlist_node hlist;
	struct socket	 *sock;
	struct hlist_head vni_list[VNI_HASH_SIZE];
	refcount_t	  refcnt;
	u32		  flags;
};

union vxlan_addr {
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr sa;
};

struct vxlan_rdst {
	union vxlan_addr	 remote_ip;
	__be16			 remote_port;
	u8			 offloaded:1;
	__be32			 remote_vni;
	u32			 remote_ifindex;
	struct net_device	 *remote_dev;
	struct list_head	 list;
	struct rcu_head		 rcu;
	struct dst_cache	 dst_cache;
};

struct vxlan_config {
	union vxlan_addr	remote_ip;
	union vxlan_addr	saddr;
	__be32			vni;
	int			remote_ifindex;
	int			mtu;
	__be16			dst_port;
	u16			port_min;
	u16			port_max;
	u8			tos;
	u8			ttl;
	__be32			label;
	u32			flags;
	unsigned long		age_interval;
	unsigned int		addrmax;
	bool			no_share;
	enum ifla_vxlan_df	df;
};

struct vxlan_dev_node {
	struct hlist_node hlist;
	struct vxlan_dev *vxlan;
};

/* Pseudo network device */
struct vxlan_dev {
	struct vxlan_dev_node hlist4;	/* vni hash table for IPv4 socket */
#if IS_ENABLED(CONFIG_IPV6)
	struct vxlan_dev_node hlist6;	/* vni hash table for IPv6 socket */
#endif
	struct list_head  next;		/* vxlan's per namespace list */
	struct vxlan_sock __rcu *vn4_sock;	/* listening socket for IPv4 */
#if IS_ENABLED(CONFIG_IPV6)
	struct vxlan_sock __rcu *vn6_sock;	/* listening socket for IPv6 */
#endif
	struct net_device *dev;
	struct net	  *net;		/* netns for packet i/o */
	struct vxlan_rdst default_dst;	/* default destination */

	struct timer_list age_timer;
	spinlock_t	  hash_lock[FDB_HASH_SIZE];
	unsigned int	  addrcnt;
	struct gro_cells  gro_cells;

	struct vxlan_config	cfg;

	struct hlist_head fdb_head[FDB_HASH_SIZE];
};

#define VXLAN_F_LEARN			0x01
#define VXLAN_F_PROXY			0x02
#define VXLAN_F_RSC			0x04
#define VXLAN_F_L2MISS			0x08
#define VXLAN_F_L3MISS			0x10
#define VXLAN_F_IPV6			0x20
#define VXLAN_F_UDP_ZERO_CSUM_TX	0x40
#define VXLAN_F_UDP_ZERO_CSUM6_TX	0x80
#define VXLAN_F_UDP_ZERO_CSUM6_RX	0x100
#define VXLAN_F_REMCSUM_TX		0x200
#define VXLAN_F_REMCSUM_RX		0x400
#define VXLAN_F_GBP			0x800
#define VXLAN_F_REMCSUM_NOPARTIAL	0x1000
#define VXLAN_F_COLLECT_METADATA	0x2000
#define VXLAN_F_GPE			0x4000
#define VXLAN_F_IPV6_LINKLOCAL		0x8000
#define VXLAN_F_TTL_INHERIT		0x10000

/* Flags that are used in the receive path. These flags must match in
 * order for a socket to be shareable
 */
#define VXLAN_F_RCV_FLAGS		(VXLAN_F_GBP |			\
					 VXLAN_F_GPE |			\
					 VXLAN_F_UDP_ZERO_CSUM6_RX |	\
					 VXLAN_F_REMCSUM_RX |		\
					 VXLAN_F_REMCSUM_NOPARTIAL |	\
					 VXLAN_F_COLLECT_METADATA)

/* Flags that can be set together with VXLAN_F_GPE. */
#define VXLAN_F_ALLOWED_GPE		(VXLAN_F_GPE |			\
					 VXLAN_F_IPV6 |			\
					 VXLAN_F_IPV6_LINKLOCAL |	\
					 VXLAN_F_UDP_ZERO_CSUM_TX |	\
					 VXLAN_F_UDP_ZERO_CSUM6_TX |	\
					 VXLAN_F_UDP_ZERO_CSUM6_RX |	\
					 VXLAN_F_COLLECT_METADATA)

struct net_device *vxlan_dev_create(struct net *net, const char *name,
				    u8 name_assign_type, struct vxlan_config *conf);

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
		return features;
	}

	if ((l4_hdr == IPPROTO_UDP) &&
	    (skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
	     skb->inner_protocol != htons(ETH_P_TEB) ||
	     (skb_inner_mac_header(skb) - skb_transport_header(skb) !=
	      sizeof(struct udphdr) + sizeof(struct vxlanhdr)) ||
	     (skb->ip_summed != CHECKSUM_NONE &&
	      !can_checksum_protocol(features, inner_eth_hdr(skb)->h_proto))))
		return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);

	return features;
}

/* IP header + UDP + VXLAN + Ethernet header */
#define VXLAN_HEADROOM (20 + 8 + 8 + 14)
/* IPv6 header + UDP + VXLAN + Ethernet header */
#define VXLAN6_HEADROOM (40 + 8 + 8 + 14)

static inline struct vxlanhdr *vxlan_hdr(struct sk_buff *skb)
{
	return (struct vxlanhdr *)(udp_hdr(skb) + 1);
}

static inline __be32 vxlan_vni(__be32 vni_field)
{
#if defined(__BIG_ENDIAN)
	return (__force __be32)((__force u32)vni_field >> 8);
#else
	return (__force __be32)((__force u32)(vni_field & VXLAN_VNI_MASK) << 8);
#endif
}

static inline __be32 vxlan_vni_field(__be32 vni)
{
#if defined(__BIG_ENDIAN)
	return (__force __be32)((__force u32)vni << 8);
#else
	return (__force __be32)((__force u32)vni >> 8);
#endif
}

static inline size_t vxlan_rco_start(__be32 vni_field)
{
	return be32_to_cpu(vni_field & VXLAN_RCO_MASK) << VXLAN_RCO_SHIFT;
}

static inline size_t vxlan_rco_offset(__be32 vni_field)
{
	return (vni_field & VXLAN_RCO_UDP) ?
		offsetof(struct udphdr, check) :
		offsetof(struct tcphdr, check);
}

static inline __be32 vxlan_compute_rco(unsigned int start, unsigned int offset)
{
	__be32 vni_field = cpu_to_be32(start >> VXLAN_RCO_SHIFT);

	if (offset == offsetof(struct udphdr, check))
		vni_field |= VXLAN_RCO_UDP;
	return vni_field;
}

static inline unsigned short vxlan_get_sk_family(struct vxlan_sock *vs)
{
	return vs->sock->sk->sk_family;
}

#if IS_ENABLED(CONFIG_IPV6)

static inline bool vxlan_addr_any(const union vxlan_addr *ipa)
{
	if (ipa->sa.sa_family == AF_INET6)
		return ipv6_addr_any(&ipa->sin6.sin6_addr);
	else
		return ipa->sin.sin_addr.s_addr == htonl(INADDR_ANY);
}

static inline bool vxlan_addr_multicast(const union vxlan_addr *ipa)
{
	if (ipa->sa.sa_family == AF_INET6)
		return ipv6_addr_is_multicast(&ipa->sin6.sin6_addr);
	else
		return ipv4_is_multicast(ipa->sin.sin_addr.s_addr);
}

#else /* !IS_ENABLED(CONFIG_IPV6) */

static inline bool vxlan_addr_any(const union vxlan_addr *ipa)
{
	return ipa->sin.sin_addr.s_addr == htonl(INADDR_ANY);
}

static inline bool vxlan_addr_multicast(const union vxlan_addr *ipa)
{
	return ipv4_is_multicast(ipa->sin.sin_addr.s_addr);
}

#endif /* IS_ENABLED(CONFIG_IPV6) */

static inline bool netif_is_vxlan(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "vxlan");
}

struct switchdev_notifier_vxlan_fdb_info {
	struct switchdev_notifier_info info; /* must be first */
	union vxlan_addr remote_ip;
	__be16 remote_port;
	__be32 remote_vni;
	u32 remote_ifindex;
	u8 eth_addr[ETH_ALEN];
	__be32 vni;
	bool offloaded;
	bool added_by_user;
};

#if IS_ENABLED(CONFIG_VXLAN)
int vxlan_fdb_find_uc(struct net_device *dev, const u8 *mac, __be32 vni,
		      struct switchdev_notifier_vxlan_fdb_info *fdb_info);
int vxlan_fdb_replay(const struct net_device *dev, __be32 vni,
		     struct notifier_block *nb,
		     struct netlink_ext_ack *extack);
void vxlan_fdb_clear_offload(const struct net_device *dev, __be32 vni);

#else
static inline int
vxlan_fdb_find_uc(struct net_device *dev, const u8 *mac, __be32 vni,
		  struct switchdev_notifier_vxlan_fdb_info *fdb_info)
{
	return -ENOENT;
}

static inline int vxlan_fdb_replay(const struct net_device *dev, __be32 vni,
				   struct notifier_block *nb,
				   struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline void
vxlan_fdb_clear_offload(const struct net_device *dev, __be32 vni)
{
}
#endif

static inline void vxlan_flag_attr_error(int attrtype,
					 struct netlink_ext_ack *extack)
{
#define VXLAN_FLAG(flg) \
	case IFLA_VXLAN_##flg: \
		NL_SET_ERR_MSG_MOD(extack, \
				   "cannot change " #flg " flag"); \
		break
	switch (attrtype) {
	VXLAN_FLAG(TTL_INHERIT);
	VXLAN_FLAG(LEARNING);
	VXLAN_FLAG(PROXY);
	VXLAN_FLAG(RSC);
	VXLAN_FLAG(L2MISS);
	VXLAN_FLAG(L3MISS);
	VXLAN_FLAG(COLLECT_METADATA);
	VXLAN_FLAG(UDP_ZERO_CSUM6_TX);
	VXLAN_FLAG(UDP_ZERO_CSUM6_RX);
	VXLAN_FLAG(REMCSUM_TX);
	VXLAN_FLAG(REMCSUM_RX);
	VXLAN_FLAG(GBP);
	VXLAN_FLAG(GPE);
	VXLAN_FLAG(REMCSUM_NOPARTIAL);
	default:
		NL_SET_ERR_MSG_MOD(extack, \
				   "cannot change flag");
		break;
	}
#undef VXLAN_FLAG
}

static inline bool vxlan_fdb_nh_path_select(struct nexthop *nh,
					    int hash,
					    struct vxlan_rdst *rdst)
{
	struct fib_nh_common *nhc;

	nhc = nexthop_path_fdb_result(nh, hash);
	if (unlikely(!nhc))
		return false;

	switch (nhc->nhc_gw_family) {
	case AF_INET:
		rdst->remote_ip.sin.sin_addr.s_addr = nhc->nhc_gw.ipv4;
		rdst->remote_ip.sa.sa_family = AF_INET;
		break;
	case AF_INET6:
		rdst->remote_ip.sin6.sin6_addr = nhc->nhc_gw.ipv6;
		rdst->remote_ip.sa.sa_family = AF_INET6;
		break;
	}

	return true;
}

#endif
