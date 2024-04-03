/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * VLAN		An implementation of 802.1Q VLAN tagging.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 */
#ifndef _LINUX_IF_VLAN_H_
#define _LINUX_IF_VLAN_H_

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/bug.h>
#include <uapi/linux/if_vlan.h>

#define VLAN_HLEN	4		/* The additional bytes required by VLAN
					 * (in addition to the Ethernet header)
					 */
#define VLAN_ETH_HLEN	18		/* Total octets in header.	 */
#define VLAN_ETH_ZLEN	64		/* Min. octets in frame sans FCS */

/*
 * According to 802.3ac, the packet can be 4 bytes longer. --Klika Jan
 */
#define VLAN_ETH_DATA_LEN	1500	/* Max. octets in payload	 */
#define VLAN_ETH_FRAME_LEN	1518	/* Max. octets in frame sans FCS */

#define VLAN_MAX_DEPTH	8		/* Max. number of nested VLAN tags parsed */

/*
 * 	struct vlan_hdr - vlan header
 * 	@h_vlan_TCI: priority and VLAN ID
 *	@h_vlan_encapsulated_proto: packet type ID or len
 */
struct vlan_hdr {
	__be16	h_vlan_TCI;
	__be16	h_vlan_encapsulated_proto;
};

/**
 *	struct vlan_ethhdr - vlan ethernet header (ethhdr + vlan_hdr)
 *	@h_dest: destination ethernet address
 *	@h_source: source ethernet address
 *	@h_vlan_proto: ethernet protocol
 *	@h_vlan_TCI: priority and VLAN ID
 *	@h_vlan_encapsulated_proto: packet type ID or len
 */
struct vlan_ethhdr {
	struct_group(addrs,
		unsigned char	h_dest[ETH_ALEN];
		unsigned char	h_source[ETH_ALEN];
	);
	__be16		h_vlan_proto;
	__be16		h_vlan_TCI;
	__be16		h_vlan_encapsulated_proto;
};

#include <linux/skbuff.h>

static inline struct vlan_ethhdr *vlan_eth_hdr(const struct sk_buff *skb)
{
	return (struct vlan_ethhdr *)skb_mac_header(skb);
}

/* Prefer this version in TX path, instead of
 * skb_reset_mac_header() + vlan_eth_hdr()
 */
static inline struct vlan_ethhdr *skb_vlan_eth_hdr(const struct sk_buff *skb)
{
	return (struct vlan_ethhdr *)skb->data;
}

#define VLAN_PRIO_MASK		0xe000 /* Priority Code Point */
#define VLAN_PRIO_SHIFT		13
#define VLAN_CFI_MASK		0x1000 /* Canonical Format Indicator / Drop Eligible Indicator */
#define VLAN_VID_MASK		0x0fff /* VLAN Identifier */
#define VLAN_N_VID		4096

/* found in socket.c */
extern void vlan_ioctl_set(int (*hook)(struct net *, void __user *));

static inline bool is_vlan_dev(const struct net_device *dev)
{
        return dev->priv_flags & IFF_802_1Q_VLAN;
}

#define skb_vlan_tag_present(__skb)	(!!(__skb)->vlan_all)
#define skb_vlan_tag_get(__skb)		((__skb)->vlan_tci)
#define skb_vlan_tag_get_id(__skb)	((__skb)->vlan_tci & VLAN_VID_MASK)
#define skb_vlan_tag_get_cfi(__skb)	(!!((__skb)->vlan_tci & VLAN_CFI_MASK))
#define skb_vlan_tag_get_prio(__skb)	(((__skb)->vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT)

static inline int vlan_get_rx_ctag_filter_info(struct net_device *dev)
{
	ASSERT_RTNL();
	return notifier_to_errno(call_netdevice_notifiers(NETDEV_CVLAN_FILTER_PUSH_INFO, dev));
}

static inline void vlan_drop_rx_ctag_filter_info(struct net_device *dev)
{
	ASSERT_RTNL();
	call_netdevice_notifiers(NETDEV_CVLAN_FILTER_DROP_INFO, dev);
}

static inline int vlan_get_rx_stag_filter_info(struct net_device *dev)
{
	ASSERT_RTNL();
	return notifier_to_errno(call_netdevice_notifiers(NETDEV_SVLAN_FILTER_PUSH_INFO, dev));
}

static inline void vlan_drop_rx_stag_filter_info(struct net_device *dev)
{
	ASSERT_RTNL();
	call_netdevice_notifiers(NETDEV_SVLAN_FILTER_DROP_INFO, dev);
}

/**
 *	struct vlan_pcpu_stats - VLAN percpu rx/tx stats
 *	@rx_packets: number of received packets
 *	@rx_bytes: number of received bytes
 *	@rx_multicast: number of received multicast packets
 *	@tx_packets: number of transmitted packets
 *	@tx_bytes: number of transmitted bytes
 *	@syncp: synchronization point for 64bit counters
 *	@rx_errors: number of rx errors
 *	@tx_dropped: number of tx drops
 */
struct vlan_pcpu_stats {
	u64_stats_t		rx_packets;
	u64_stats_t		rx_bytes;
	u64_stats_t		rx_multicast;
	u64_stats_t		tx_packets;
	u64_stats_t		tx_bytes;
	struct u64_stats_sync	syncp;
	u32			rx_errors;
	u32			tx_dropped;
};

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)

extern struct net_device *__vlan_find_dev_deep_rcu(struct net_device *real_dev,
					       __be16 vlan_proto, u16 vlan_id);
extern int vlan_for_each(struct net_device *dev,
			 int (*action)(struct net_device *dev, int vid,
				       void *arg), void *arg);
extern struct net_device *vlan_dev_real_dev(const struct net_device *dev);
extern u16 vlan_dev_vlan_id(const struct net_device *dev);
extern __be16 vlan_dev_vlan_proto(const struct net_device *dev);

/**
 *	struct vlan_priority_tci_mapping - vlan egress priority mappings
 *	@priority: skb priority
 *	@vlan_qos: vlan priority: (skb->priority << 13) & 0xE000
 *	@next: pointer to next struct
 */
struct vlan_priority_tci_mapping {
	u32					priority;
	u16					vlan_qos;
	struct vlan_priority_tci_mapping	*next;
};

struct proc_dir_entry;
struct netpoll;

/**
 *	struct vlan_dev_priv - VLAN private device data
 *	@nr_ingress_mappings: number of ingress priority mappings
 *	@ingress_priority_map: ingress priority mappings
 *	@nr_egress_mappings: number of egress priority mappings
 *	@egress_priority_map: hash of egress priority mappings
 *	@vlan_proto: VLAN encapsulation protocol
 *	@vlan_id: VLAN identifier
 *	@flags: device flags
 *	@real_dev: underlying netdevice
 *	@dev_tracker: refcount tracker for @real_dev reference
 *	@real_dev_addr: address of underlying netdevice
 *	@dent: proc dir entry
 *	@vlan_pcpu_stats: ptr to percpu rx stats
 */
struct vlan_dev_priv {
	unsigned int				nr_ingress_mappings;
	u32					ingress_priority_map[8];
	unsigned int				nr_egress_mappings;
	struct vlan_priority_tci_mapping	*egress_priority_map[16];

	__be16					vlan_proto;
	u16					vlan_id;
	u16					flags;

	struct net_device			*real_dev;
	netdevice_tracker			dev_tracker;

	unsigned char				real_dev_addr[ETH_ALEN];

	struct proc_dir_entry			*dent;
	struct vlan_pcpu_stats __percpu		*vlan_pcpu_stats;
#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll				*netpoll;
#endif
};

static inline struct vlan_dev_priv *vlan_dev_priv(const struct net_device *dev)
{
	return netdev_priv(dev);
}

static inline u16
vlan_dev_get_egress_qos_mask(struct net_device *dev, u32 skprio)
{
	struct vlan_priority_tci_mapping *mp;

	smp_rmb(); /* coupled with smp_wmb() in vlan_dev_set_egress_priority() */

	mp = vlan_dev_priv(dev)->egress_priority_map[(skprio & 0xF)];
	while (mp) {
		if (mp->priority == skprio) {
			return mp->vlan_qos; /* This should already be shifted
					      * to mask correctly with the
					      * VLAN's TCI */
		}
		mp = mp->next;
	}
	return 0;
}

extern bool vlan_do_receive(struct sk_buff **skb);

extern int vlan_vid_add(struct net_device *dev, __be16 proto, u16 vid);
extern void vlan_vid_del(struct net_device *dev, __be16 proto, u16 vid);

extern int vlan_vids_add_by_dev(struct net_device *dev,
				const struct net_device *by_dev);
extern void vlan_vids_del_by_dev(struct net_device *dev,
				 const struct net_device *by_dev);

extern bool vlan_uses_dev(const struct net_device *dev);

#else
static inline struct net_device *
__vlan_find_dev_deep_rcu(struct net_device *real_dev,
		     __be16 vlan_proto, u16 vlan_id)
{
	return NULL;
}

static inline int
vlan_for_each(struct net_device *dev,
	      int (*action)(struct net_device *dev, int vid, void *arg),
	      void *arg)
{
	return 0;
}

static inline struct net_device *vlan_dev_real_dev(const struct net_device *dev)
{
	BUG();
	return NULL;
}

static inline u16 vlan_dev_vlan_id(const struct net_device *dev)
{
	BUG();
	return 0;
}

static inline __be16 vlan_dev_vlan_proto(const struct net_device *dev)
{
	BUG();
	return 0;
}

static inline u16 vlan_dev_get_egress_qos_mask(struct net_device *dev,
					       u32 skprio)
{
	return 0;
}

static inline bool vlan_do_receive(struct sk_buff **skb)
{
	return false;
}

static inline int vlan_vid_add(struct net_device *dev, __be16 proto, u16 vid)
{
	return 0;
}

static inline void vlan_vid_del(struct net_device *dev, __be16 proto, u16 vid)
{
}

static inline int vlan_vids_add_by_dev(struct net_device *dev,
				       const struct net_device *by_dev)
{
	return 0;
}

static inline void vlan_vids_del_by_dev(struct net_device *dev,
					const struct net_device *by_dev)
{
}

static inline bool vlan_uses_dev(const struct net_device *dev)
{
	return false;
}
#endif

/**
 * eth_type_vlan - check for valid vlan ether type.
 * @ethertype: ether type to check
 *
 * Returns true if the ether type is a vlan ether type.
 */
static inline bool eth_type_vlan(__be16 ethertype)
{
	switch (ethertype) {
	case htons(ETH_P_8021Q):
	case htons(ETH_P_8021AD):
		return true;
	default:
		return false;
	}
}

static inline bool vlan_hw_offload_capable(netdev_features_t features,
					   __be16 proto)
{
	if (proto == htons(ETH_P_8021Q) && features & NETIF_F_HW_VLAN_CTAG_TX)
		return true;
	if (proto == htons(ETH_P_8021AD) && features & NETIF_F_HW_VLAN_STAG_TX)
		return true;
	return false;
}

/**
 * __vlan_insert_inner_tag - inner VLAN tag inserting
 * @skb: skbuff to tag
 * @vlan_proto: VLAN encapsulation protocol
 * @vlan_tci: VLAN TCI to insert
 * @mac_len: MAC header length including outer vlan headers
 *
 * Inserts the VLAN tag into @skb as part of the payload at offset mac_len
 * Returns error if skb_cow_head fails.
 *
 * Does not change skb->protocol so this function can be used during receive.
 */
static inline int __vlan_insert_inner_tag(struct sk_buff *skb,
					  __be16 vlan_proto, u16 vlan_tci,
					  unsigned int mac_len)
{
	struct vlan_ethhdr *veth;

	if (skb_cow_head(skb, VLAN_HLEN) < 0)
		return -ENOMEM;

	skb_push(skb, VLAN_HLEN);

	/* Move the mac header sans proto to the beginning of the new header. */
	if (likely(mac_len > ETH_TLEN))
		memmove(skb->data, skb->data + VLAN_HLEN, mac_len - ETH_TLEN);
	if (skb_mac_header_was_set(skb))
		skb->mac_header -= VLAN_HLEN;

	veth = (struct vlan_ethhdr *)(skb->data + mac_len - ETH_HLEN);

	/* first, the ethernet type */
	if (likely(mac_len >= ETH_TLEN)) {
		/* h_vlan_encapsulated_proto should already be populated, and
		 * skb->data has space for h_vlan_proto
		 */
		veth->h_vlan_proto = vlan_proto;
	} else {
		/* h_vlan_encapsulated_proto should not be populated, and
		 * skb->data has no space for h_vlan_proto
		 */
		veth->h_vlan_encapsulated_proto = skb->protocol;
	}

	/* now, the TCI */
	veth->h_vlan_TCI = htons(vlan_tci);

	return 0;
}

/**
 * __vlan_insert_tag - regular VLAN tag inserting
 * @skb: skbuff to tag
 * @vlan_proto: VLAN encapsulation protocol
 * @vlan_tci: VLAN TCI to insert
 *
 * Inserts the VLAN tag into @skb as part of the payload
 * Returns error if skb_cow_head fails.
 *
 * Does not change skb->protocol so this function can be used during receive.
 */
static inline int __vlan_insert_tag(struct sk_buff *skb,
				    __be16 vlan_proto, u16 vlan_tci)
{
	return __vlan_insert_inner_tag(skb, vlan_proto, vlan_tci, ETH_HLEN);
}

/**
 * vlan_insert_inner_tag - inner VLAN tag inserting
 * @skb: skbuff to tag
 * @vlan_proto: VLAN encapsulation protocol
 * @vlan_tci: VLAN TCI to insert
 * @mac_len: MAC header length including outer vlan headers
 *
 * Inserts the VLAN tag into @skb as part of the payload at offset mac_len
 * Returns a VLAN tagged skb. This might change skb->head.
 *
 * Following the skb_unshare() example, in case of error, the calling function
 * doesn't have to worry about freeing the original skb.
 *
 * Does not change skb->protocol so this function can be used during receive.
 */
static inline struct sk_buff *vlan_insert_inner_tag(struct sk_buff *skb,
						    __be16 vlan_proto,
						    u16 vlan_tci,
						    unsigned int mac_len)
{
	int err;

	err = __vlan_insert_inner_tag(skb, vlan_proto, vlan_tci, mac_len);
	if (err) {
		dev_kfree_skb_any(skb);
		return NULL;
	}
	return skb;
}

/**
 * vlan_insert_tag - regular VLAN tag inserting
 * @skb: skbuff to tag
 * @vlan_proto: VLAN encapsulation protocol
 * @vlan_tci: VLAN TCI to insert
 *
 * Inserts the VLAN tag into @skb as part of the payload
 * Returns a VLAN tagged skb. This might change skb->head.
 *
 * Following the skb_unshare() example, in case of error, the calling function
 * doesn't have to worry about freeing the original skb.
 *
 * Does not change skb->protocol so this function can be used during receive.
 */
static inline struct sk_buff *vlan_insert_tag(struct sk_buff *skb,
					      __be16 vlan_proto, u16 vlan_tci)
{
	return vlan_insert_inner_tag(skb, vlan_proto, vlan_tci, ETH_HLEN);
}

/**
 * vlan_insert_tag_set_proto - regular VLAN tag inserting
 * @skb: skbuff to tag
 * @vlan_proto: VLAN encapsulation protocol
 * @vlan_tci: VLAN TCI to insert
 *
 * Inserts the VLAN tag into @skb as part of the payload
 * Returns a VLAN tagged skb. This might change skb->head.
 *
 * Following the skb_unshare() example, in case of error, the calling function
 * doesn't have to worry about freeing the original skb.
 */
static inline struct sk_buff *vlan_insert_tag_set_proto(struct sk_buff *skb,
							__be16 vlan_proto,
							u16 vlan_tci)
{
	skb = vlan_insert_tag(skb, vlan_proto, vlan_tci);
	if (skb)
		skb->protocol = vlan_proto;
	return skb;
}

/**
 * __vlan_hwaccel_clear_tag - clear hardware accelerated VLAN info
 * @skb: skbuff to clear
 *
 * Clears the VLAN information from @skb
 */
static inline void __vlan_hwaccel_clear_tag(struct sk_buff *skb)
{
	skb->vlan_all = 0;
}

/**
 * __vlan_hwaccel_copy_tag - copy hardware accelerated VLAN info from another skb
 * @dst: skbuff to copy to
 * @src: skbuff to copy from
 *
 * Copies VLAN information from @src to @dst (for branchless code)
 */
static inline void __vlan_hwaccel_copy_tag(struct sk_buff *dst, const struct sk_buff *src)
{
	dst->vlan_all = src->vlan_all;
}

/*
 * __vlan_hwaccel_push_inside - pushes vlan tag to the payload
 * @skb: skbuff to tag
 *
 * Pushes the VLAN tag from @skb->vlan_tci inside to the payload.
 *
 * Following the skb_unshare() example, in case of error, the calling function
 * doesn't have to worry about freeing the original skb.
 */
static inline struct sk_buff *__vlan_hwaccel_push_inside(struct sk_buff *skb)
{
	skb = vlan_insert_tag_set_proto(skb, skb->vlan_proto,
					skb_vlan_tag_get(skb));
	if (likely(skb))
		__vlan_hwaccel_clear_tag(skb);
	return skb;
}

/**
 * __vlan_hwaccel_put_tag - hardware accelerated VLAN inserting
 * @skb: skbuff to tag
 * @vlan_proto: VLAN encapsulation protocol
 * @vlan_tci: VLAN TCI to insert
 *
 * Puts the VLAN TCI in @skb->vlan_tci and lets the device do the rest
 */
static inline void __vlan_hwaccel_put_tag(struct sk_buff *skb,
					  __be16 vlan_proto, u16 vlan_tci)
{
	skb->vlan_proto = vlan_proto;
	skb->vlan_tci = vlan_tci;
}

/**
 * __vlan_get_tag - get the VLAN ID that is part of the payload
 * @skb: skbuff to query
 * @vlan_tci: buffer to store value
 *
 * Returns error if the skb is not of VLAN type
 */
static inline int __vlan_get_tag(const struct sk_buff *skb, u16 *vlan_tci)
{
	struct vlan_ethhdr *veth = skb_vlan_eth_hdr(skb);

	if (!eth_type_vlan(veth->h_vlan_proto))
		return -ENODATA;

	*vlan_tci = ntohs(veth->h_vlan_TCI);
	return 0;
}

/**
 * __vlan_hwaccel_get_tag - get the VLAN ID that is in @skb->cb[]
 * @skb: skbuff to query
 * @vlan_tci: buffer to store value
 *
 * Returns error if @skb->vlan_tci is not set correctly
 */
static inline int __vlan_hwaccel_get_tag(const struct sk_buff *skb,
					 u16 *vlan_tci)
{
	if (skb_vlan_tag_present(skb)) {
		*vlan_tci = skb_vlan_tag_get(skb);
		return 0;
	} else {
		*vlan_tci = 0;
		return -ENODATA;
	}
}

/**
 * vlan_get_tag - get the VLAN ID from the skb
 * @skb: skbuff to query
 * @vlan_tci: buffer to store value
 *
 * Returns error if the skb is not VLAN tagged
 */
static inline int vlan_get_tag(const struct sk_buff *skb, u16 *vlan_tci)
{
	if (skb->dev->features & NETIF_F_HW_VLAN_CTAG_TX) {
		return __vlan_hwaccel_get_tag(skb, vlan_tci);
	} else {
		return __vlan_get_tag(skb, vlan_tci);
	}
}

/**
 * vlan_get_protocol - get protocol EtherType.
 * @skb: skbuff to query
 * @type: first vlan protocol
 * @depth: buffer to store length of eth and vlan tags in bytes
 *
 * Returns the EtherType of the packet, regardless of whether it is
 * vlan encapsulated (normal or hardware accelerated) or not.
 */
static inline __be16 __vlan_get_protocol(const struct sk_buff *skb, __be16 type,
					 int *depth)
{
	unsigned int vlan_depth = skb->mac_len, parse_depth = VLAN_MAX_DEPTH;

	/* if type is 802.1Q/AD then the header should already be
	 * present at mac_len - VLAN_HLEN (if mac_len > 0), or at
	 * ETH_HLEN otherwise
	 */
	if (eth_type_vlan(type)) {
		if (vlan_depth) {
			if (WARN_ON(vlan_depth < VLAN_HLEN))
				return 0;
			vlan_depth -= VLAN_HLEN;
		} else {
			vlan_depth = ETH_HLEN;
		}
		do {
			struct vlan_hdr vhdr, *vh;

			vh = skb_header_pointer(skb, vlan_depth, sizeof(vhdr), &vhdr);
			if (unlikely(!vh || !--parse_depth))
				return 0;

			type = vh->h_vlan_encapsulated_proto;
			vlan_depth += VLAN_HLEN;
		} while (eth_type_vlan(type));
	}

	if (depth)
		*depth = vlan_depth;

	return type;
}

/**
 * vlan_get_protocol - get protocol EtherType.
 * @skb: skbuff to query
 *
 * Returns the EtherType of the packet, regardless of whether it is
 * vlan encapsulated (normal or hardware accelerated) or not.
 */
static inline __be16 vlan_get_protocol(const struct sk_buff *skb)
{
	return __vlan_get_protocol(skb, skb->protocol, NULL);
}

/* This version of __vlan_get_protocol() also pulls mac header in skb->head */
static inline __be16 vlan_get_protocol_and_depth(struct sk_buff *skb,
						 __be16 type, int *depth)
{
	int maclen;

	type = __vlan_get_protocol(skb, type, &maclen);

	if (type) {
		if (!pskb_may_pull(skb, maclen))
			type = 0;
		else if (depth)
			*depth = maclen;
	}
	return type;
}

/* A getter for the SKB protocol field which will handle VLAN tags consistently
 * whether VLAN acceleration is enabled or not.
 */
static inline __be16 skb_protocol(const struct sk_buff *skb, bool skip_vlan)
{
	if (!skip_vlan)
		/* VLAN acceleration strips the VLAN header from the skb and
		 * moves it to skb->vlan_proto
		 */
		return skb_vlan_tag_present(skb) ? skb->vlan_proto : skb->protocol;

	return vlan_get_protocol(skb);
}

static inline void vlan_set_encap_proto(struct sk_buff *skb,
					struct vlan_hdr *vhdr)
{
	__be16 proto;
	unsigned short *rawp;

	/*
	 * Was a VLAN packet, grab the encapsulated protocol, which the layer
	 * three protocols care about.
	 */

	proto = vhdr->h_vlan_encapsulated_proto;
	if (eth_proto_is_802_3(proto)) {
		skb->protocol = proto;
		return;
	}

	rawp = (unsigned short *)(vhdr + 1);
	if (*rawp == 0xFFFF)
		/*
		 * This is a magic hack to spot IPX packets. Older Novell
		 * breaks the protocol design and runs IPX over 802.3 without
		 * an 802.2 LLC layer. We look for FFFF which isn't a used
		 * 802.2 SSAP/DSAP. This won't work for fault tolerant netware
		 * but does for the rest.
		 */
		skb->protocol = htons(ETH_P_802_3);
	else
		/*
		 * Real 802.2 LLC
		 */
		skb->protocol = htons(ETH_P_802_2);
}

/**
 * vlan_remove_tag - remove outer VLAN tag from payload
 * @skb: skbuff to remove tag from
 * @vlan_tci: buffer to store value
 *
 * Expects the skb to contain a VLAN tag in the payload, and to have skb->data
 * pointing at the MAC header.
 *
 * Returns a new pointer to skb->data, or NULL on failure to pull.
 */
static inline void *vlan_remove_tag(struct sk_buff *skb, u16 *vlan_tci)
{
	struct vlan_hdr *vhdr = (struct vlan_hdr *)(skb->data + ETH_HLEN);

	*vlan_tci = ntohs(vhdr->h_vlan_TCI);

	memmove(skb->data + VLAN_HLEN, skb->data, 2 * ETH_ALEN);
	vlan_set_encap_proto(skb, vhdr);
	return __skb_pull(skb, VLAN_HLEN);
}

/**
 * skb_vlan_tagged - check if skb is vlan tagged.
 * @skb: skbuff to query
 *
 * Returns true if the skb is tagged, regardless of whether it is hardware
 * accelerated or not.
 */
static inline bool skb_vlan_tagged(const struct sk_buff *skb)
{
	if (!skb_vlan_tag_present(skb) &&
	    likely(!eth_type_vlan(skb->protocol)))
		return false;

	return true;
}

/**
 * skb_vlan_tagged_multi - check if skb is vlan tagged with multiple headers.
 * @skb: skbuff to query
 *
 * Returns true if the skb is tagged with multiple vlan headers, regardless
 * of whether it is hardware accelerated or not.
 */
static inline bool skb_vlan_tagged_multi(struct sk_buff *skb)
{
	__be16 protocol = skb->protocol;

	if (!skb_vlan_tag_present(skb)) {
		struct vlan_ethhdr *veh;

		if (likely(!eth_type_vlan(protocol)))
			return false;

		if (unlikely(!pskb_may_pull(skb, VLAN_ETH_HLEN)))
			return false;

		veh = skb_vlan_eth_hdr(skb);
		protocol = veh->h_vlan_encapsulated_proto;
	}

	if (!eth_type_vlan(protocol))
		return false;

	return true;
}

/**
 * vlan_features_check - drop unsafe features for skb with multiple tags.
 * @skb: skbuff to query
 * @features: features to be checked
 *
 * Returns features without unsafe ones if the skb has multiple tags.
 */
static inline netdev_features_t vlan_features_check(struct sk_buff *skb,
						    netdev_features_t features)
{
	if (skb_vlan_tagged_multi(skb)) {
		/* In the case of multi-tagged packets, use a direct mask
		 * instead of using netdev_interesect_features(), to make
		 * sure that only devices supporting NETIF_F_HW_CSUM will
		 * have checksum offloading support.
		 */
		features &= NETIF_F_SG | NETIF_F_HIGHDMA | NETIF_F_HW_CSUM |
			    NETIF_F_FRAGLIST | NETIF_F_HW_VLAN_CTAG_TX |
			    NETIF_F_HW_VLAN_STAG_TX;
	}

	return features;
}

/**
 * compare_vlan_header - Compare two vlan headers
 * @h1: Pointer to vlan header
 * @h2: Pointer to vlan header
 *
 * Compare two vlan headers, returns 0 if equal.
 *
 * Please note that alignment of h1 & h2 are only guaranteed to be 16 bits.
 */
static inline unsigned long compare_vlan_header(const struct vlan_hdr *h1,
						const struct vlan_hdr *h2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return *(u32 *)h1 ^ *(u32 *)h2;
#else
	return ((__force u32)h1->h_vlan_TCI ^ (__force u32)h2->h_vlan_TCI) |
	       ((__force u32)h1->h_vlan_encapsulated_proto ^
		(__force u32)h2->h_vlan_encapsulated_proto);
#endif
}
#endif /* !(_LINUX_IF_VLAN_H_) */
