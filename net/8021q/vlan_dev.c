/* -*- linux-c -*-
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: vlan@scry.wanfear.com
 *              VLAN Home Page: http://www.candelatech.com/~greear/vlan.html
 *
 * Fixes:       Mar 22 2001: Martin Bokaemper <mbokaemper@unispherenetworks.com>
 *                - reset skb->pkt_type on incoming packets when MAC was changed
 *                - see that changed MAC is saddr for outgoing packets
 *              Oct 20, 2001:  Ard van Breeman:
 *                - Fix MC-list, finally.
 *                - Flush MC-list on VLAN destroy.
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/init.h>
#include <asm/uaccess.h> /* for copy_from_user */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/datalink.h>
#include <net/p8022.h>
#include <net/arp.h>

#include "vlan.h"
#include "vlanproc.h"
#include <linux/if_vlan.h>
#include <net/ip.h>

/*
 *	Rebuild the Ethernet MAC header. This is called after an ARP
 *	(or in future other address resolution) has completed on this
 *	sk_buff. We now let ARP fill in the other fields.
 *
 *	This routine CANNOT use cached dst->neigh!
 *	Really, it is used only when dst->neigh is wrong.
 *
 * TODO:  This needs a checkup, I'm ignorant here. --BLG
 */
int vlan_dev_rebuild_header(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);

	switch (veth->h_vlan_encapsulated_proto) {
#ifdef CONFIG_INET
	case __constant_htons(ETH_P_IP):

		/* TODO:  Confirm this will work with VLAN headers... */
		return arp_find(veth->h_dest, skb);
#endif
	default:
		printk(VLAN_DBG
		       "%s: unable to resolve type %X addresses.\n",
		       dev->name, ntohs(veth->h_vlan_encapsulated_proto));

		memcpy(veth->h_source, dev->dev_addr, ETH_ALEN);
		break;
	}

	return 0;
}

static inline struct sk_buff *vlan_check_reorder_header(struct sk_buff *skb)
{
	if (VLAN_DEV_INFO(skb->dev)->flags & VLAN_FLAG_REORDER_HDR) {
		if (skb_shared(skb) || skb_cloned(skb)) {
			struct sk_buff *nskb = skb_copy(skb, GFP_ATOMIC);
			kfree_skb(skb);
			skb = nskb;
		}
		if (skb) {
			/* Lifted from Gleb's VLAN code... */
			memmove(skb->data - ETH_HLEN,
				skb->data - VLAN_ETH_HLEN, 12);
			skb->mac_header += VLAN_HLEN;
		}
	}

	return skb;
}

/*
 *	Determine the packet's protocol ID. The rule here is that we
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 *
 *  Also, at this point we assume that we ARE dealing exclusively with
 *  VLAN packets, or packets that should be made into VLAN packets based
 *  on a default VLAN ID.
 *
 *  NOTE:  Should be similar to ethernet/eth.c.
 *
 *  SANITY NOTE:  This method is called when a packet is moving up the stack
 *                towards userland.  To get here, it would have already passed
 *                through the ethernet/eth.c eth_type_trans() method.
 *  SANITY NOTE 2: We are referencing to the VLAN_HDR frields, which MAY be
 *                 stored UNALIGNED in the memory.  RISC systems don't like
 *                 such cases very much...
 *  SANITY NOTE 2a:  According to Dave Miller & Alexey, it will always be aligned,
 *                 so there doesn't need to be any of the unaligned stuff.  It has
 *                 been commented out now...  --Ben
 *
 */
int vlan_skb_recv(struct sk_buff *skb, struct net_device *dev,
		  struct packet_type* ptype, struct net_device *orig_dev)
{
	unsigned char *rawp = NULL;
	struct vlan_hdr *vhdr = (struct vlan_hdr *)(skb->data);
	unsigned short vid;
	struct net_device_stats *stats;
	unsigned short vlan_TCI;
	__be16 proto;

	/* vlan_TCI = ntohs(get_unaligned(&vhdr->h_vlan_TCI)); */
	vlan_TCI = ntohs(vhdr->h_vlan_TCI);

	vid = (vlan_TCI & VLAN_VID_MASK);

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: skb: %p vlan_id: %hx\n",
		__FUNCTION__, skb, vid);
#endif

	/* Ok, we will find the correct VLAN device, strip the header,
	 * and then go on as usual.
	 */

	/* We have 12 bits of vlan ID.
	 *
	 * We must not drop allow preempt until we hold a
	 * reference to the device (netif_rx does that) or we
	 * fail.
	 */

	rcu_read_lock();
	skb->dev = __find_vlan_dev(dev, vid);
	if (!skb->dev) {
		rcu_read_unlock();

#ifdef VLAN_DEBUG
		printk(VLAN_DBG "%s: ERROR: No net_device for VID: %i on dev: %s [%i]\n",
			__FUNCTION__, (unsigned int)(vid), dev->name, dev->ifindex);
#endif
		kfree_skb(skb);
		return -1;
	}

	skb->dev->last_rx = jiffies;

	/* Bump the rx counters for the VLAN device. */
	stats = vlan_dev_get_stats(skb->dev);
	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	/* Take off the VLAN header (4 bytes currently) */
	skb_pull_rcsum(skb, VLAN_HLEN);

	/* Ok, lets check to make sure the device (dev) we
	 * came in on is what this VLAN is attached to.
	 */

	if (dev != VLAN_DEV_INFO(skb->dev)->real_dev) {
		rcu_read_unlock();

#ifdef VLAN_DEBUG
		printk(VLAN_DBG "%s: dropping skb: %p because came in on wrong device, dev: %s  real_dev: %s, skb_dev: %s\n",
			__FUNCTION__, skb, dev->name,
			VLAN_DEV_INFO(skb->dev)->real_dev->name,
			skb->dev->name);
#endif
		kfree_skb(skb);
		stats->rx_errors++;
		return -1;
	}

	/*
	 * Deal with ingress priority mapping.
	 */
	skb->priority = vlan_get_ingress_priority(skb->dev, ntohs(vhdr->h_vlan_TCI));

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: priority: %lu  for TCI: %hu (hbo)\n",
		__FUNCTION__, (unsigned long)(skb->priority),
		ntohs(vhdr->h_vlan_TCI));
#endif

	/* The ethernet driver already did the pkt_type calculations
	 * for us...
	 */
	switch (skb->pkt_type) {
	case PACKET_BROADCAST: /* Yeah, stats collect these together.. */
		// stats->broadcast ++; // no such counter :-(
		break;

	case PACKET_MULTICAST:
		stats->multicast++;
		break;

	case PACKET_OTHERHOST:
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the underlying
		 * device, and still route correctly.
		 */
		if (!compare_ether_addr(eth_hdr(skb)->h_dest, skb->dev->dev_addr)) {
			/* It is for our (changed) MAC-address! */
			skb->pkt_type = PACKET_HOST;
		}
		break;
	default:
		break;
	}

	/*  Was a VLAN packet, grab the encapsulated protocol, which the layer
	 * three protocols care about.
	 */
	/* proto = get_unaligned(&vhdr->h_vlan_encapsulated_proto); */
	proto = vhdr->h_vlan_encapsulated_proto;

	skb->protocol = proto;
	if (ntohs(proto) >= 1536) {
		/* place it back on the queue to be handled by
		 * true layer 3 protocols.
		 */

		/* See if we are configured to re-write the VLAN header
		 * to make it look like ethernet...
		 */
		skb = vlan_check_reorder_header(skb);

		/* Can be null if skb-clone fails when re-ordering */
		if (skb) {
			netif_rx(skb);
		} else {
			/* TODO:  Add a more specific counter here. */
			stats->rx_errors++;
		}
		rcu_read_unlock();
		return 0;
	}

	rawp = skb->data;

	/*
	 * This is a magic hack to spot IPX packets. Older Novell breaks
	 * the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 * layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 * won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF) {
		skb->protocol = htons(ETH_P_802_3);
		/* place it back on the queue to be handled by true layer 3 protocols.
		 */

		/* See if we are configured to re-write the VLAN header
		 * to make it look like ethernet...
		 */
		skb = vlan_check_reorder_header(skb);

		/* Can be null if skb-clone fails when re-ordering */
		if (skb) {
			netif_rx(skb);
		} else {
			/* TODO:  Add a more specific counter here. */
			stats->rx_errors++;
		}
		rcu_read_unlock();
		return 0;
	}

	/*
	 *	Real 802.2 LLC
	 */
	skb->protocol = htons(ETH_P_802_2);
	/* place it back on the queue to be handled by upper layer protocols.
	 */

	/* See if we are configured to re-write the VLAN header
	 * to make it look like ethernet...
	 */
	skb = vlan_check_reorder_header(skb);

	/* Can be null if skb-clone fails when re-ordering */
	if (skb) {
		netif_rx(skb);
	} else {
		/* TODO:  Add a more specific counter here. */
		stats->rx_errors++;
	}
	rcu_read_unlock();
	return 0;
}

static inline unsigned short vlan_dev_get_egress_qos_mask(struct net_device* dev,
							  struct sk_buff* skb)
{
	struct vlan_priority_tci_mapping *mp =
		VLAN_DEV_INFO(dev)->egress_priority_map[(skb->priority & 0xF)];

	while (mp) {
		if (mp->priority == skb->priority) {
			return mp->vlan_qos; /* This should already be shifted to mask
					      * correctly with the VLAN's TCI
					      */
		}
		mp = mp->next;
	}
	return 0;
}

/*
 *	Create the VLAN header for an arbitrary protocol layer
 *
 *	saddr=NULL	means use device source address
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 *
 *  This is called when the SKB is moving down the stack towards the
 *  physical devices.
 */
int vlan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
			 unsigned short type, void *daddr, void *saddr,
			 unsigned len)
{
	struct vlan_hdr *vhdr;
	unsigned short veth_TCI = 0;
	int rc = 0;
	int build_vlan_header = 0;
	struct net_device *vdev = dev; /* save this for the bottom of the method */

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: skb: %p type: %hx len: %x vlan_id: %hx, daddr: %p\n",
		__FUNCTION__, skb, type, len, VLAN_DEV_INFO(dev)->vlan_id, daddr);
#endif

	/* build vlan header only if re_order_header flag is NOT set.  This
	 * fixes some programs that get confused when they see a VLAN device
	 * sending a frame that is VLAN encoded (the consensus is that the VLAN
	 * device should look completely like an Ethernet device when the
	 * REORDER_HEADER flag is set)	The drawback to this is some extra
	 * header shuffling in the hard_start_xmit.  Users can turn off this
	 * REORDER behaviour with the vconfig tool.
	 */
	if (!(VLAN_DEV_INFO(dev)->flags & VLAN_FLAG_REORDER_HDR))
		build_vlan_header = 1;

	if (build_vlan_header) {
		vhdr = (struct vlan_hdr *) skb_push(skb, VLAN_HLEN);

		/* build the four bytes that make this a VLAN header. */

		/* Now, construct the second two bytes. This field looks something
		 * like:
		 * usr_priority: 3 bits	 (high bits)
		 * CFI		 1 bit
		 * VLAN ID	 12 bits (low bits)
		 *
		 */
		veth_TCI = VLAN_DEV_INFO(dev)->vlan_id;
		veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);

		vhdr->h_vlan_TCI = htons(veth_TCI);

		/*
		 *  Set the protocol type.
		 *  For a packet of type ETH_P_802_3 we put the length in here instead.
		 *  It is up to the 802.2 layer to carry protocol information.
		 */

		if (type != ETH_P_802_3) {
			vhdr->h_vlan_encapsulated_proto = htons(type);
		} else {
			vhdr->h_vlan_encapsulated_proto = htons(len);
		}

		skb->protocol = htons(ETH_P_8021Q);
		skb_reset_network_header(skb);
	}

	/* Before delegating work to the lower layer, enter our MAC-address */
	if (saddr == NULL)
		saddr = dev->dev_addr;

	dev = VLAN_DEV_INFO(dev)->real_dev;

	/* MPLS can send us skbuffs w/out enough space.	 This check will grow the
	 * skb if it doesn't have enough headroom.  Not a beautiful solution, so
	 * I'll tick a counter so that users can know it's happening...	 If they
	 * care...
	 */

	/* NOTE:  This may still break if the underlying device is not the final
	 * device (and thus there are more headers to add...)  It should work for
	 * good-ole-ethernet though.
	 */
	if (skb_headroom(skb) < dev->hard_header_len) {
		struct sk_buff *sk_tmp = skb;
		skb = skb_realloc_headroom(sk_tmp, dev->hard_header_len);
		kfree_skb(sk_tmp);
		if (skb == NULL) {
			struct net_device_stats *stats = vlan_dev_get_stats(vdev);
			stats->tx_dropped++;
			return -ENOMEM;
		}
		VLAN_DEV_INFO(vdev)->cnt_inc_headroom_on_tx++;
#ifdef VLAN_DEBUG
		printk(VLAN_DBG "%s: %s: had to grow skb.\n", __FUNCTION__, vdev->name);
#endif
	}

	if (build_vlan_header) {
		/* Now make the underlying real hard header */
		rc = dev->hard_header(skb, dev, ETH_P_8021Q, daddr, saddr, len + VLAN_HLEN);

		if (rc > 0) {
			rc += VLAN_HLEN;
		} else if (rc < 0) {
			rc -= VLAN_HLEN;
		}
	} else {
		/* If here, then we'll just make a normal looking ethernet frame,
		 * but, the hard_start_xmit method will insert the tag (it has to
		 * be able to do this for bridged and other skbs that don't come
		 * down the protocol stack in an orderly manner.
		 */
		rc = dev->hard_header(skb, dev, type, daddr, saddr, len);
	}

	return rc;
}

int vlan_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = vlan_dev_get_stats(dev);
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);

	/* Handle non-VLAN frames if they are sent to us, for example by DHCP.
	 *
	 * NOTE: THIS ASSUMES DIX ETHERNET, SPECIFICALLY NOT SUPPORTING
	 * OTHER THINGS LIKE FDDI/TokenRing/802.3 SNAPs...
	 */

	if (veth->h_vlan_proto != htons(ETH_P_8021Q)) {
		int orig_headroom = skb_headroom(skb);
		unsigned short veth_TCI;

		/* This is not a VLAN frame...but we can fix that! */
		VLAN_DEV_INFO(dev)->cnt_encap_on_xmit++;

#ifdef VLAN_DEBUG
		printk(VLAN_DBG "%s: proto to encap: 0x%hx (hbo)\n",
			__FUNCTION__, htons(veth->h_vlan_proto));
#endif
		/* Construct the second two bytes. This field looks something
		 * like:
		 * usr_priority: 3 bits	 (high bits)
		 * CFI		 1 bit
		 * VLAN ID	 12 bits (low bits)
		 */
		veth_TCI = VLAN_DEV_INFO(dev)->vlan_id;
		veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);

		skb = __vlan_put_tag(skb, veth_TCI);
		if (!skb) {
			stats->tx_dropped++;
			return 0;
		}

		if (orig_headroom < VLAN_HLEN) {
			VLAN_DEV_INFO(dev)->cnt_inc_headroom_on_tx++;
		}
	}

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: about to send skb: %p to dev: %s\n",
		__FUNCTION__, skb, skb->dev->name);
	printk(VLAN_DBG "  %2hx.%2hx.%2hx.%2xh.%2hx.%2hx %2hx.%2hx.%2hx.%2hx.%2hx.%2hx %4hx %4hx %4hx\n",
	       veth->h_dest[0], veth->h_dest[1], veth->h_dest[2], veth->h_dest[3], veth->h_dest[4], veth->h_dest[5],
	       veth->h_source[0], veth->h_source[1], veth->h_source[2], veth->h_source[3], veth->h_source[4], veth->h_source[5],
	       veth->h_vlan_proto, veth->h_vlan_TCI, veth->h_vlan_encapsulated_proto);
#endif

	stats->tx_packets++; /* for statics only */
	stats->tx_bytes += skb->len;

	skb->dev = VLAN_DEV_INFO(dev)->real_dev;
	dev_queue_xmit(skb);

	return 0;
}

int vlan_dev_hwaccel_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = vlan_dev_get_stats(dev);
	unsigned short veth_TCI;

	/* Construct the second two bytes. This field looks something
	 * like:
	 * usr_priority: 3 bits	 (high bits)
	 * CFI		 1 bit
	 * VLAN ID	 12 bits (low bits)
	 */
	veth_TCI = VLAN_DEV_INFO(dev)->vlan_id;
	veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);
	skb = __vlan_hwaccel_put_tag(skb, veth_TCI);

	stats->tx_packets++;
	stats->tx_bytes += skb->len;

	skb->dev = VLAN_DEV_INFO(dev)->real_dev;
	dev_queue_xmit(skb);

	return 0;
}

int vlan_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	/* TODO: gotta make sure the underlying layer can handle it,
	 * maybe an IFF_VLAN_CAPABLE flag for devices?
	 */
	if (VLAN_DEV_INFO(dev)->real_dev->mtu < new_mtu)
		return -ERANGE;

	dev->mtu = new_mtu;

	return 0;
}

void vlan_dev_set_ingress_priority(const struct net_device *dev,
				   u32 skb_prio, short vlan_prio)
{
	struct vlan_dev_info *vlan = VLAN_DEV_INFO(dev);

	if (vlan->ingress_priority_map[vlan_prio & 0x7] && !skb_prio)
		vlan->nr_ingress_mappings--;
	else if (!vlan->ingress_priority_map[vlan_prio & 0x7] && skb_prio)
		vlan->nr_ingress_mappings++;

	vlan->ingress_priority_map[vlan_prio & 0x7] = skb_prio;
}

int vlan_dev_set_egress_priority(const struct net_device *dev,
				 u32 skb_prio, short vlan_prio)
{
	struct vlan_dev_info *vlan = VLAN_DEV_INFO(dev);
	struct vlan_priority_tci_mapping *mp = NULL;
	struct vlan_priority_tci_mapping *np;
	u32 vlan_qos = (vlan_prio << 13) & 0xE000;

	/* See if a priority mapping exists.. */
	mp = vlan->egress_priority_map[skb_prio & 0xF];
	while (mp) {
		if (mp->priority == skb_prio) {
			if (mp->vlan_qos && !vlan_qos)
				vlan->nr_egress_mappings--;
			else if (!mp->vlan_qos && vlan_qos)
				vlan->nr_egress_mappings++;
			mp->vlan_qos = vlan_qos;
			return 0;
		}
		mp = mp->next;
	}

	/* Create a new mapping then. */
	mp = vlan->egress_priority_map[skb_prio & 0xF];
	np = kmalloc(sizeof(struct vlan_priority_tci_mapping), GFP_KERNEL);
	if (!np)
		return -ENOBUFS;

	np->next = mp;
	np->priority = skb_prio;
	np->vlan_qos = vlan_qos;
	vlan->egress_priority_map[skb_prio & 0xF] = np;
	if (vlan_qos)
		vlan->nr_egress_mappings++;
	return 0;
}

/* Flags are defined in the vlan_flags enum in include/linux/if_vlan.h file. */
int vlan_dev_set_vlan_flag(const struct net_device *dev,
			   u32 flag, short flag_val)
{
	/* verify flag is supported */
	if (flag == VLAN_FLAG_REORDER_HDR) {
		if (flag_val) {
			VLAN_DEV_INFO(dev)->flags |= VLAN_FLAG_REORDER_HDR;
		} else {
			VLAN_DEV_INFO(dev)->flags &= ~VLAN_FLAG_REORDER_HDR;
		}
		return 0;
	}
	printk(KERN_ERR "%s: flag %i is not valid.\n", __FUNCTION__, flag);
	return -EINVAL;
}

void vlan_dev_get_realdev_name(const struct net_device *dev, char *result)
{
	strncpy(result, VLAN_DEV_INFO(dev)->real_dev->name, 23);
}

void vlan_dev_get_vid(const struct net_device *dev, unsigned short *result)
{
	*result = VLAN_DEV_INFO(dev)->vlan_id;
}

static inline int vlan_dmi_equals(struct dev_mc_list *dmi1,
				  struct dev_mc_list *dmi2)
{
	return ((dmi1->dmi_addrlen == dmi2->dmi_addrlen) &&
		(memcmp(dmi1->dmi_addr, dmi2->dmi_addr, dmi1->dmi_addrlen) == 0));
}

/** dmi is a single entry into a dev_mc_list, a single node.  mc_list is
 *  an entire list, and we'll iterate through it.
 */
static int vlan_should_add_mc(struct dev_mc_list *dmi, struct dev_mc_list *mc_list)
{
	struct dev_mc_list *idmi;

	for (idmi = mc_list; idmi != NULL; ) {
		if (vlan_dmi_equals(dmi, idmi)) {
			if (dmi->dmi_users > idmi->dmi_users)
				return 1;
			else
				return 0;
		} else {
			idmi = idmi->next;
		}
	}

	return 1;
}

static inline void vlan_destroy_mc_list(struct dev_mc_list *mc_list)
{
	struct dev_mc_list *dmi = mc_list;
	struct dev_mc_list *next;

	while(dmi) {
		next = dmi->next;
		kfree(dmi);
		dmi = next;
	}
}

static void vlan_copy_mc_list(struct dev_mc_list *mc_list, struct vlan_dev_info *vlan_info)
{
	struct dev_mc_list *dmi, *new_dmi;

	vlan_destroy_mc_list(vlan_info->old_mc_list);
	vlan_info->old_mc_list = NULL;

	for (dmi = mc_list; dmi != NULL; dmi = dmi->next) {
		new_dmi = kmalloc(sizeof(*new_dmi), GFP_ATOMIC);
		if (new_dmi == NULL) {
			printk(KERN_ERR "vlan: cannot allocate memory. "
			       "Multicast may not work properly from now.\n");
			return;
		}

		/* Copy whole structure, then make new 'next' pointer */
		*new_dmi = *dmi;
		new_dmi->next = vlan_info->old_mc_list;
		vlan_info->old_mc_list = new_dmi;
	}
}

static void vlan_flush_mc_list(struct net_device *dev)
{
	struct dev_mc_list *dmi = dev->mc_list;

	while (dmi) {
		printk(KERN_DEBUG "%s: del %.2x:%.2x:%.2x:%.2x:%.2x:%.2x mcast address from vlan interface\n",
		       dev->name,
		       dmi->dmi_addr[0],
		       dmi->dmi_addr[1],
		       dmi->dmi_addr[2],
		       dmi->dmi_addr[3],
		       dmi->dmi_addr[4],
		       dmi->dmi_addr[5]);
		dev_mc_delete(dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		dmi = dev->mc_list;
	}

	/* dev->mc_list is NULL by the time we get here. */
	vlan_destroy_mc_list(VLAN_DEV_INFO(dev)->old_mc_list);
	VLAN_DEV_INFO(dev)->old_mc_list = NULL;
}

int vlan_dev_open(struct net_device *dev)
{
	struct vlan_dev_info *vlan = VLAN_DEV_INFO(dev);
	struct net_device *real_dev = vlan->real_dev;
	int err;

	if (!(real_dev->flags & IFF_UP))
		return -ENETDOWN;

	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr)) {
		err = dev_unicast_add(real_dev, dev->dev_addr, ETH_ALEN);
		if (err < 0)
			return err;
	}
	memcpy(vlan->real_dev_addr, real_dev->dev_addr, ETH_ALEN);

	return 0;
}

int vlan_dev_stop(struct net_device *dev)
{
	struct net_device *real_dev = VLAN_DEV_INFO(dev)->real_dev;

	vlan_flush_mc_list(dev);

	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr))
		dev_unicast_delete(real_dev, dev->dev_addr, dev->addr_len);

	return 0;
}

int vlan_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct net_device *real_dev = VLAN_DEV_INFO(dev)->real_dev;
	struct ifreq ifrr;
	int err = -EOPNOTSUPP;

	strncpy(ifrr.ifr_name, real_dev->name, IFNAMSIZ);
	ifrr.ifr_ifru = ifr->ifr_ifru;

	switch(cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		if (real_dev->do_ioctl && netif_device_present(real_dev))
			err = real_dev->do_ioctl(real_dev, &ifrr, cmd);
		break;

	case SIOCETHTOOL:
		err = dev_ethtool(&ifrr);
	}

	if (!err)
		ifr->ifr_ifru = ifrr.ifr_ifru;

	return err;
}

/** Taken from Gleb + Lennert's VLAN code, and modified... */
void vlan_dev_set_multicast_list(struct net_device *vlan_dev)
{
	struct dev_mc_list *dmi;
	struct net_device *real_dev;
	int inc;

	if (vlan_dev && (vlan_dev->priv_flags & IFF_802_1Q_VLAN)) {
		/* Then it's a real vlan device, as far as we can tell.. */
		real_dev = VLAN_DEV_INFO(vlan_dev)->real_dev;

		/* compare the current promiscuity to the last promisc we had.. */
		inc = vlan_dev->promiscuity - VLAN_DEV_INFO(vlan_dev)->old_promiscuity;
		if (inc) {
			printk(KERN_INFO "%s: dev_set_promiscuity(master, %d)\n",
			       vlan_dev->name, inc);
			dev_set_promiscuity(real_dev, inc); /* found in dev.c */
			VLAN_DEV_INFO(vlan_dev)->old_promiscuity = vlan_dev->promiscuity;
		}

		inc = vlan_dev->allmulti - VLAN_DEV_INFO(vlan_dev)->old_allmulti;
		if (inc) {
			printk(KERN_INFO "%s: dev_set_allmulti(master, %d)\n",
			       vlan_dev->name, inc);
			dev_set_allmulti(real_dev, inc); /* dev.c */
			VLAN_DEV_INFO(vlan_dev)->old_allmulti = vlan_dev->allmulti;
		}

		/* looking for addresses to add to master's list */
		for (dmi = vlan_dev->mc_list; dmi != NULL; dmi = dmi->next) {
			if (vlan_should_add_mc(dmi, VLAN_DEV_INFO(vlan_dev)->old_mc_list)) {
				dev_mc_add(real_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
				printk(KERN_DEBUG "%s: add %.2x:%.2x:%.2x:%.2x:%.2x:%.2x mcast address to master interface\n",
				       vlan_dev->name,
				       dmi->dmi_addr[0],
				       dmi->dmi_addr[1],
				       dmi->dmi_addr[2],
				       dmi->dmi_addr[3],
				       dmi->dmi_addr[4],
				       dmi->dmi_addr[5]);
			}
		}

		/* looking for addresses to delete from master's list */
		for (dmi = VLAN_DEV_INFO(vlan_dev)->old_mc_list; dmi != NULL; dmi = dmi->next) {
			if (vlan_should_add_mc(dmi, vlan_dev->mc_list)) {
				/* if we think we should add it to the new list, then we should really
				 * delete it from the real list on the underlying device.
				 */
				dev_mc_delete(real_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
				printk(KERN_DEBUG "%s: del %.2x:%.2x:%.2x:%.2x:%.2x:%.2x mcast address from master interface\n",
				       vlan_dev->name,
				       dmi->dmi_addr[0],
				       dmi->dmi_addr[1],
				       dmi->dmi_addr[2],
				       dmi->dmi_addr[3],
				       dmi->dmi_addr[4],
				       dmi->dmi_addr[5]);
			}
		}

		/* save multicast list */
		vlan_copy_mc_list(vlan_dev->mc_list, VLAN_DEV_INFO(vlan_dev));
	}
}
