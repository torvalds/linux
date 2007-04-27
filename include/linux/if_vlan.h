/*
 * VLAN		An implementation of 802.1Q VLAN tagging.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#ifndef _LINUX_IF_VLAN_H_
#define _LINUX_IF_VLAN_H_

#ifdef __KERNEL__

/* externally defined structs */
struct vlan_group;
struct net_device;
struct packet_type;
struct vlan_collection;
struct vlan_dev_info;
struct hlist_node;

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#define VLAN_HLEN	4		/* The additional bytes (on top of the Ethernet header)
					 * that VLAN requires.
					 */
#define VLAN_ETH_ALEN	6		/* Octets in one ethernet addr	 */
#define VLAN_ETH_HLEN	18		/* Total octets in header.	 */
#define VLAN_ETH_ZLEN	64		/* Min. octets in frame sans FCS */

/*
 * According to 802.3ac, the packet can be 4 bytes longer. --Klika Jan
 */
#define VLAN_ETH_DATA_LEN	1500	/* Max. octets in payload	 */
#define VLAN_ETH_FRAME_LEN	1518	/* Max. octets in frame sans FCS */

struct vlan_ethhdr {
   unsigned char	h_dest[ETH_ALEN];	   /* destination eth addr	*/
   unsigned char	h_source[ETH_ALEN];	   /* source ether addr	*/
   __be16               h_vlan_proto;              /* Should always be 0x8100 */
   __be16               h_vlan_TCI;                /* Encapsulates priority and VLAN ID */
   __be16		h_vlan_encapsulated_proto; /* packet type ID field (or len) */
};

#include <linux/skbuff.h>

static inline struct vlan_ethhdr *vlan_eth_hdr(const struct sk_buff *skb)
{
	return (struct vlan_ethhdr *)skb_mac_header(skb);
}

struct vlan_hdr {
   __be16               h_vlan_TCI;                /* Encapsulates priority and VLAN ID */
   __be16               h_vlan_encapsulated_proto; /* packet type ID field (or len) */
};

#define VLAN_VID_MASK	0xfff

/* found in socket.c */
extern void vlan_ioctl_set(int (*hook)(void __user *));

#define VLAN_NAME "vlan"

/* if this changes, algorithm will have to be reworked because this
 * depends on completely exhausting the VLAN identifier space.  Thus
 * it gives constant time look-up, but in many cases it wastes memory.
 */
#define VLAN_GROUP_ARRAY_LEN          4096
#define VLAN_GROUP_ARRAY_SPLIT_PARTS  8
#define VLAN_GROUP_ARRAY_PART_LEN     (VLAN_GROUP_ARRAY_LEN/VLAN_GROUP_ARRAY_SPLIT_PARTS)

struct vlan_group {
	int real_dev_ifindex; /* The ifindex of the ethernet(like) device the vlan is attached to. */
	struct hlist_node	hlist;	/* linked list */
	struct net_device **vlan_devices_arrays[VLAN_GROUP_ARRAY_SPLIT_PARTS];
	struct rcu_head		rcu;
};

static inline struct net_device *vlan_group_get_device(struct vlan_group *vg, int vlan_id)
{
	struct net_device **array;
	array = vg->vlan_devices_arrays[vlan_id / VLAN_GROUP_ARRAY_PART_LEN];
	return array[vlan_id % VLAN_GROUP_ARRAY_PART_LEN];
}

static inline void vlan_group_set_device(struct vlan_group *vg, int vlan_id,
					 struct net_device *dev)
{
	struct net_device **array;
	if (!vg)
		return;
	array = vg->vlan_devices_arrays[vlan_id / VLAN_GROUP_ARRAY_PART_LEN];
	array[vlan_id % VLAN_GROUP_ARRAY_PART_LEN] = dev;
}

struct vlan_priority_tci_mapping {
	unsigned long priority;
	unsigned short vlan_qos; /* This should be shifted when first set, so we only do it
				  * at provisioning time.
				  * ((skb->priority << 13) & 0xE000)
				  */
	struct vlan_priority_tci_mapping *next;
};

/* Holds information that makes sense if this device is a VLAN device. */
struct vlan_dev_info {
	/** This will be the mapping that correlates skb->priority to
	 * 3 bits of VLAN QOS tags...
	 */
	unsigned long ingress_priority_map[8];
	struct vlan_priority_tci_mapping *egress_priority_map[16]; /* hash table */

	unsigned short vlan_id;        /*  The VLAN Identifier for this interface. */
	unsigned short flags;          /* (1 << 0) re_order_header   This option will cause the
                                        *   VLAN code to move around the ethernet header on
                                        *   ingress to make the skb look **exactly** like it
                                        *   came in from an ethernet port.  This destroys some of
                                        *   the VLAN information in the skb, but it fixes programs
                                        *   like DHCP that use packet-filtering and don't understand
                                        *   802.1Q
                                        */
	struct dev_mc_list *old_mc_list;  /* old multi-cast list for the VLAN interface..
                                           * we save this so we can tell what changes were
                                           * made, in order to feed the right changes down
                                           * to the real hardware...
                                           */
	int old_allmulti;               /* similar to above. */
	int old_promiscuity;            /* similar to above. */
	struct net_device *real_dev;    /* the underlying device/interface */
	struct proc_dir_entry *dent;    /* Holds the proc data */
	unsigned long cnt_inc_headroom_on_tx; /* How many times did we have to grow the skb on TX. */
	unsigned long cnt_encap_on_xmit;      /* How many times did we have to encapsulate the skb on TX. */
	struct net_device_stats dev_stats; /* Device stats (rx-bytes, tx-pkts, etc...) */
};

#define VLAN_DEV_INFO(x) ((struct vlan_dev_info *)(x->priv))

/* inline functions */

static inline struct net_device_stats *vlan_dev_get_stats(struct net_device *dev)
{
	return &(VLAN_DEV_INFO(dev)->dev_stats);
}

static inline __u32 vlan_get_ingress_priority(struct net_device *dev,
					      unsigned short vlan_tag)
{
	struct vlan_dev_info *vip = VLAN_DEV_INFO(dev);

	return vip->ingress_priority_map[(vlan_tag >> 13) & 0x7];
}

/* VLAN tx hw acceleration helpers. */
struct vlan_skb_tx_cookie {
	u32	magic;
	u32	vlan_tag;
};

#define VLAN_TX_COOKIE_MAGIC	0x564c414e	/* "VLAN" in ascii. */
#define VLAN_TX_SKB_CB(__skb)	((struct vlan_skb_tx_cookie *)&((__skb)->cb[0]))
#define vlan_tx_tag_present(__skb) \
	(VLAN_TX_SKB_CB(__skb)->magic == VLAN_TX_COOKIE_MAGIC)
#define vlan_tx_tag_get(__skb)	(VLAN_TX_SKB_CB(__skb)->vlan_tag)

/* VLAN rx hw acceleration helper.  This acts like netif_{rx,receive_skb}(). */
static inline int __vlan_hwaccel_rx(struct sk_buff *skb,
				    struct vlan_group *grp,
				    unsigned short vlan_tag, int polling)
{
	struct net_device_stats *stats;

	if (skb_bond_should_drop(skb)) {
		dev_kfree_skb_any(skb);
		return NET_RX_DROP;
	}

	skb->dev = vlan_group_get_device(grp, vlan_tag & VLAN_VID_MASK);
	if (skb->dev == NULL) {
		dev_kfree_skb_any(skb);

		/* Not NET_RX_DROP, this is not being dropped
		 * due to congestion.
		 */
		return 0;
	}

	skb->dev->last_rx = jiffies;

	stats = vlan_dev_get_stats(skb->dev);
	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	skb->priority = vlan_get_ingress_priority(skb->dev, vlan_tag);
	switch (skb->pkt_type) {
	case PACKET_BROADCAST:
		break;

	case PACKET_MULTICAST:
		stats->multicast++;
		break;

	case PACKET_OTHERHOST:
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the underlying
		 * device, and still route correctly.
		 */
		if (!compare_ether_addr(eth_hdr(skb)->h_dest,
				       	skb->dev->dev_addr))
			skb->pkt_type = PACKET_HOST;
		break;
	};

	return (polling ? netif_receive_skb(skb) : netif_rx(skb));
}

static inline int vlan_hwaccel_rx(struct sk_buff *skb,
				  struct vlan_group *grp,
				  unsigned short vlan_tag)
{
	return __vlan_hwaccel_rx(skb, grp, vlan_tag, 0);
}

static inline int vlan_hwaccel_receive_skb(struct sk_buff *skb,
					   struct vlan_group *grp,
					   unsigned short vlan_tag)
{
	return __vlan_hwaccel_rx(skb, grp, vlan_tag, 1);
}

/**
 * __vlan_put_tag - regular VLAN tag inserting
 * @skb: skbuff to tag
 * @tag: VLAN tag to insert
 *
 * Inserts the VLAN tag into @skb as part of the payload
 * Returns a VLAN tagged skb. If a new skb is created, @skb is freed.
 * 
 * Following the skb_unshare() example, in case of error, the calling function
 * doesn't have to worry about freeing the original skb.
 */
static inline struct sk_buff *__vlan_put_tag(struct sk_buff *skb, unsigned short tag)
{
	struct vlan_ethhdr *veth;

	if (skb_headroom(skb) < VLAN_HLEN) {
		struct sk_buff *sk_tmp = skb;
		skb = skb_realloc_headroom(sk_tmp, VLAN_HLEN);
		kfree_skb(sk_tmp);
		if (!skb) {
			printk(KERN_ERR "vlan: failed to realloc headroom\n");
			return NULL;
		}
	} else {
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (!skb) {
			printk(KERN_ERR "vlan: failed to unshare skbuff\n");
			return NULL;
		}
	}

	veth = (struct vlan_ethhdr *)skb_push(skb, VLAN_HLEN);

	/* Move the mac addresses to the beginning of the new header. */
	memmove(skb->data, skb->data + VLAN_HLEN, 2 * VLAN_ETH_ALEN);

	/* first, the ethernet type */
	veth->h_vlan_proto = __constant_htons(ETH_P_8021Q);

	/* now, the tag */
	veth->h_vlan_TCI = htons(tag);

	skb->protocol = __constant_htons(ETH_P_8021Q);
	skb->mac_header -= VLAN_HLEN;
	skb->network_header -= VLAN_HLEN;

	return skb;
}

/**
 * __vlan_hwaccel_put_tag - hardware accelerated VLAN inserting
 * @skb: skbuff to tag
 * @tag: VLAN tag to insert
 *
 * Puts the VLAN tag in @skb->cb[] and lets the device do the rest
 */
static inline struct sk_buff *__vlan_hwaccel_put_tag(struct sk_buff *skb, unsigned short tag)
{
	struct vlan_skb_tx_cookie *cookie;

	cookie = VLAN_TX_SKB_CB(skb);
	cookie->magic = VLAN_TX_COOKIE_MAGIC;
	cookie->vlan_tag = tag;

	return skb;
}

#define HAVE_VLAN_PUT_TAG

/**
 * vlan_put_tag - inserts VLAN tag according to device features
 * @skb: skbuff to tag
 * @tag: VLAN tag to insert
 *
 * Assumes skb->dev is the target that will xmit this frame.
 * Returns a VLAN tagged skb.
 */
static inline struct sk_buff *vlan_put_tag(struct sk_buff *skb, unsigned short tag)
{
	if (skb->dev->features & NETIF_F_HW_VLAN_TX) {
		return __vlan_hwaccel_put_tag(skb, tag);
	} else {
		return __vlan_put_tag(skb, tag);
	}
}

/**
 * __vlan_get_tag - get the VLAN ID that is part of the payload
 * @skb: skbuff to query
 * @tag: buffer to store vlaue
 * 
 * Returns error if the skb is not of VLAN type
 */
static inline int __vlan_get_tag(struct sk_buff *skb, unsigned short *tag)
{
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)skb->data;

	if (veth->h_vlan_proto != __constant_htons(ETH_P_8021Q)) {
		return -EINVAL;
	}

	*tag = ntohs(veth->h_vlan_TCI);

	return 0;
}

/**
 * __vlan_hwaccel_get_tag - get the VLAN ID that is in @skb->cb[]
 * @skb: skbuff to query
 * @tag: buffer to store vlaue
 * 
 * Returns error if @skb->cb[] is not set correctly
 */
static inline int __vlan_hwaccel_get_tag(struct sk_buff *skb, unsigned short *tag)
{
	struct vlan_skb_tx_cookie *cookie;

	cookie = VLAN_TX_SKB_CB(skb);
	if (cookie->magic == VLAN_TX_COOKIE_MAGIC) {
		*tag = cookie->vlan_tag;
		return 0;
	} else {
		*tag = 0;
		return -EINVAL;
	}
}

#define HAVE_VLAN_GET_TAG

/**
 * vlan_get_tag - get the VLAN ID from the skb
 * @skb: skbuff to query
 * @tag: buffer to store vlaue
 * 
 * Returns error if the skb is not VLAN tagged
 */
static inline int vlan_get_tag(struct sk_buff *skb, unsigned short *tag)
{
	if (skb->dev->features & NETIF_F_HW_VLAN_TX) {
		return __vlan_hwaccel_get_tag(skb, tag);
	} else {
		return __vlan_get_tag(skb, tag);
	}
}

#endif /* __KERNEL__ */

/* VLAN IOCTLs are found in sockios.h */

/* Passed in vlan_ioctl_args structure to determine behaviour. */
enum vlan_ioctl_cmds {
	ADD_VLAN_CMD,
	DEL_VLAN_CMD,
	SET_VLAN_INGRESS_PRIORITY_CMD,
	SET_VLAN_EGRESS_PRIORITY_CMD,
	GET_VLAN_INGRESS_PRIORITY_CMD,
	GET_VLAN_EGRESS_PRIORITY_CMD,
	SET_VLAN_NAME_TYPE_CMD,
	SET_VLAN_FLAG_CMD,
	GET_VLAN_REALDEV_NAME_CMD, /* If this works, you know it's a VLAN device, btw */
	GET_VLAN_VID_CMD /* Get the VID of this VLAN (specified by name) */
};

enum vlan_name_types {
	VLAN_NAME_TYPE_PLUS_VID, /* Name will look like:  vlan0005 */
	VLAN_NAME_TYPE_RAW_PLUS_VID, /* name will look like:  eth1.0005 */
	VLAN_NAME_TYPE_PLUS_VID_NO_PAD, /* Name will look like:  vlan5 */
	VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD, /* Name will look like:  eth0.5 */
	VLAN_NAME_TYPE_HIGHEST
};

struct vlan_ioctl_args {
	int cmd; /* Should be one of the vlan_ioctl_cmds enum above. */
	char device1[24];

        union {
		char device2[24];
		int VID;
		unsigned int skb_priority;
		unsigned int name_type;
		unsigned int bind_type;
		unsigned int flag; /* Matches vlan_dev_info flags */
        } u;

	short vlan_qos;   
};

#endif /* !(_LINUX_IF_VLAN_H_) */
