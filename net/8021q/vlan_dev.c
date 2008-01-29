/* -*- linux-c -*-
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: netdev@vger.kernel.org
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
static int vlan_dev_rebuild_header(struct sk_buff *skb)
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
		pr_debug("%s: unable to resolve type %X addresses.\n",
			 dev->name, ntohs(veth->h_vlan_encapsulated_proto));

		memcpy(veth->h_source, dev->dev_addr, ETH_ALEN);
		break;
	}

	return 0;
}

static inline struct sk_buff *vlan_check_reorder_header(struct sk_buff *skb)
{
	if (vlan_dev_info(skb->dev)->flags & VLAN_FLAG_REORDER_HDR) {
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

static inline void vlan_set_encap_proto(struct sk_buff *skb,
		struct vlan_hdr *vhdr)
{
	__be16 proto;
	unsigned char *rawp;

	/*
	 * Was a VLAN packet, grab the encapsulated protocol, which the layer
	 * three protocols care about.
	 */

	proto = vhdr->h_vlan_encapsulated_proto;
	if (ntohs(proto) >= 1536) {
		skb->protocol = proto;
		return;
	}

	rawp = skb->data;
	if (*(unsigned short *)rawp == 0xFFFF)
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
 *  SANITY NOTE 2a: According to Dave Miller & Alexey, it will always be
 *  		    aligned, so there doesn't need to be any of the unaligned
 *  		    stuff.  It has been commented out now...  --Ben
 *
 */
int vlan_skb_recv(struct sk_buff *skb, struct net_device *dev,
		  struct packet_type *ptype, struct net_device *orig_dev)
{
	struct vlan_hdr *vhdr;
	unsigned short vid;
	struct net_device_stats *stats;
	unsigned short vlan_TCI;

	if (dev->nd_net != &init_net)
		goto err_free;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		goto err_free;

	if (unlikely(!pskb_may_pull(skb, VLAN_HLEN)))
		goto err_free;

	vhdr = (struct vlan_hdr *)skb->data;
	vlan_TCI = ntohs(vhdr->h_vlan_TCI);
	vid = (vlan_TCI & VLAN_VID_MASK);

	rcu_read_lock();
	skb->dev = __find_vlan_dev(dev, vid);
	if (!skb->dev) {
		pr_debug("%s: ERROR: No net_device for VID: %u on dev: %s\n",
			 __FUNCTION__, (unsigned int)vid, dev->name);
		goto err_unlock;
	}

	skb->dev->last_rx = jiffies;

	stats = &skb->dev->stats;
	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	skb_pull_rcsum(skb, VLAN_HLEN);

	skb->priority = vlan_get_ingress_priority(skb->dev,
						  ntohs(vhdr->h_vlan_TCI));

	pr_debug("%s: priority: %u for TCI: %hu\n",
		 __FUNCTION__, skb->priority, ntohs(vhdr->h_vlan_TCI));

	switch (skb->pkt_type) {
	case PACKET_BROADCAST: /* Yeah, stats collect these together.. */
		/* stats->broadcast ++; // no such counter :-( */
		break;

	case PACKET_MULTICAST:
		stats->multicast++;
		break;

	case PACKET_OTHERHOST:
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the
		 * underlying device, and still route correctly.
		 */
		if (!compare_ether_addr(eth_hdr(skb)->h_dest,
					skb->dev->dev_addr))
			skb->pkt_type = PACKET_HOST;
		break;
	default:
		break;
	}

	vlan_set_encap_proto(skb, vhdr);

	skb = vlan_check_reorder_header(skb);
	if (!skb) {
		stats->rx_errors++;
		goto err_unlock;
	}

	netif_rx(skb);
	rcu_read_unlock();
	return NET_RX_SUCCESS;

err_unlock:
	rcu_read_unlock();
err_free:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static inline unsigned short
vlan_dev_get_egress_qos_mask(struct net_device *dev, struct sk_buff *skb)
{
	struct vlan_priority_tci_mapping *mp;

	mp = vlan_dev_info(dev)->egress_priority_map[(skb->priority & 0xF)];
	while (mp) {
		if (mp->priority == skb->priority) {
			return mp->vlan_qos; /* This should already be shifted
					      * to mask correctly with the
					      * VLAN's TCI */
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
static int vlan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
				unsigned short type,
				const void *daddr, const void *saddr,
				unsigned int len)
{
	struct vlan_hdr *vhdr;
	unsigned short veth_TCI = 0;
	int rc = 0;
	int build_vlan_header = 0;
	struct net_device *vdev = dev;

	pr_debug("%s: skb: %p type: %hx len: %u vlan_id: %hx, daddr: %p\n",
		 __FUNCTION__, skb, type, len, vlan_dev_info(dev)->vlan_id,
		 daddr);

	/* build vlan header only if re_order_header flag is NOT set.  This
	 * fixes some programs that get confused when they see a VLAN device
	 * sending a frame that is VLAN encoded (the consensus is that the VLAN
	 * device should look completely like an Ethernet device when the
	 * REORDER_HEADER flag is set)	The drawback to this is some extra
	 * header shuffling in the hard_start_xmit.  Users can turn off this
	 * REORDER behaviour with the vconfig tool.
	 */
	if (!(vlan_dev_info(dev)->flags & VLAN_FLAG_REORDER_HDR))
		build_vlan_header = 1;

	if (build_vlan_header) {
		vhdr = (struct vlan_hdr *) skb_push(skb, VLAN_HLEN);

		/* build the four bytes that make this a VLAN header. */

		/* Now, construct the second two bytes. This field looks
		 * something like:
		 * usr_priority: 3 bits	 (high bits)
		 * CFI		 1 bit
		 * VLAN ID	 12 bits (low bits)
		 *
		 */
		veth_TCI = vlan_dev_info(dev)->vlan_id;
		veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);

		vhdr->h_vlan_TCI = htons(veth_TCI);

		/*
		 *  Set the protocol type. For a packet of type ETH_P_802_3 we
		 *  put the length in here instead. It is up to the 802.2
		 *  layer to carry protocol information.
		 */

		if (type != ETH_P_802_3)
			vhdr->h_vlan_encapsulated_proto = htons(type);
		else
			vhdr->h_vlan_encapsulated_proto = htons(len);

		skb->protocol = htons(ETH_P_8021Q);
		skb_reset_network_header(skb);
	}

	/* Before delegating work to the lower layer, enter our MAC-address */
	if (saddr == NULL)
		saddr = dev->dev_addr;

	dev = vlan_dev_info(dev)->real_dev;

	/* MPLS can send us skbuffs w/out enough space.	This check will grow
	 * the skb if it doesn't have enough headroom. Not a beautiful solution,
	 * so I'll tick a counter so that users can know it's happening...
	 * If they care...
	 */

	/* NOTE: This may still break if the underlying device is not the final
	 * device (and thus there are more headers to add...) It should work for
	 * good-ole-ethernet though.
	 */
	if (skb_headroom(skb) < dev->hard_header_len) {
		struct sk_buff *sk_tmp = skb;
		skb = skb_realloc_headroom(sk_tmp, dev->hard_header_len);
		kfree_skb(sk_tmp);
		if (skb == NULL) {
			struct net_device_stats *stats = &vdev->stats;
			stats->tx_dropped++;
			return -ENOMEM;
		}
		vlan_dev_info(vdev)->cnt_inc_headroom_on_tx++;
		pr_debug("%s: %s: had to grow skb\n", __FUNCTION__, vdev->name);
	}

	if (build_vlan_header) {
		/* Now make the underlying real hard header */
		rc = dev_hard_header(skb, dev, ETH_P_8021Q, daddr, saddr,
				     len + VLAN_HLEN);
		if (rc > 0)
			rc += VLAN_HLEN;
		else if (rc < 0)
			rc -= VLAN_HLEN;
	} else
		/* If here, then we'll just make a normal looking ethernet
		 * frame, but, the hard_start_xmit method will insert the tag
		 * (it has to be able to do this for bridged and other skbs
		 * that don't come down the protocol stack in an orderly manner.
		 */
		rc = dev_hard_header(skb, dev, type, daddr, saddr, len);

	return rc;
}

static int vlan_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);

	/* Handle non-VLAN frames if they are sent to us, for example by DHCP.
	 *
	 * NOTE: THIS ASSUMES DIX ETHERNET, SPECIFICALLY NOT SUPPORTING
	 * OTHER THINGS LIKE FDDI/TokenRing/802.3 SNAPs...
	 */

	if (veth->h_vlan_proto != htons(ETH_P_8021Q) ||
		vlan_dev_info(dev)->flags & VLAN_FLAG_REORDER_HDR) {
		int orig_headroom = skb_headroom(skb);
		unsigned short veth_TCI;

		/* This is not a VLAN frame...but we can fix that! */
		vlan_dev_info(dev)->cnt_encap_on_xmit++;

		pr_debug("%s: proto to encap: 0x%hx\n",
			 __FUNCTION__, htons(veth->h_vlan_proto));
		/* Construct the second two bytes. This field looks something
		 * like:
		 * usr_priority: 3 bits	 (high bits)
		 * CFI		 1 bit
		 * VLAN ID	 12 bits (low bits)
		 */
		veth_TCI = vlan_dev_info(dev)->vlan_id;
		veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);

		skb = __vlan_put_tag(skb, veth_TCI);
		if (!skb) {
			stats->tx_dropped++;
			return 0;
		}

		if (orig_headroom < VLAN_HLEN)
			vlan_dev_info(dev)->cnt_inc_headroom_on_tx++;
	}

	pr_debug("%s: about to send skb: %p to dev: %s\n",
		__FUNCTION__, skb, skb->dev->name);
	pr_debug("  " MAC_FMT " " MAC_FMT " %4hx %4hx %4hx\n",
		 veth->h_dest[0], veth->h_dest[1], veth->h_dest[2],
		 veth->h_dest[3], veth->h_dest[4], veth->h_dest[5],
		 veth->h_source[0], veth->h_source[1], veth->h_source[2],
		 veth->h_source[3], veth->h_source[4], veth->h_source[5],
		 veth->h_vlan_proto, veth->h_vlan_TCI,
		 veth->h_vlan_encapsulated_proto);

	stats->tx_packets++; /* for statics only */
	stats->tx_bytes += skb->len;

	skb->dev = vlan_dev_info(dev)->real_dev;
	dev_queue_xmit(skb);

	return 0;
}

static int vlan_dev_hwaccel_hard_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	unsigned short veth_TCI;

	/* Construct the second two bytes. This field looks something
	 * like:
	 * usr_priority: 3 bits	 (high bits)
	 * CFI		 1 bit
	 * VLAN ID	 12 bits (low bits)
	 */
	veth_TCI = vlan_dev_info(dev)->vlan_id;
	veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);
	skb = __vlan_hwaccel_put_tag(skb, veth_TCI);

	stats->tx_packets++;
	stats->tx_bytes += skb->len;

	skb->dev = vlan_dev_info(dev)->real_dev;
	dev_queue_xmit(skb);

	return 0;
}

static int vlan_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	/* TODO: gotta make sure the underlying layer can handle it,
	 * maybe an IFF_VLAN_CAPABLE flag for devices?
	 */
	if (vlan_dev_info(dev)->real_dev->mtu < new_mtu)
		return -ERANGE;

	dev->mtu = new_mtu;

	return 0;
}

void vlan_dev_set_ingress_priority(const struct net_device *dev,
				   u32 skb_prio, short vlan_prio)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);

	if (vlan->ingress_priority_map[vlan_prio & 0x7] && !skb_prio)
		vlan->nr_ingress_mappings--;
	else if (!vlan->ingress_priority_map[vlan_prio & 0x7] && skb_prio)
		vlan->nr_ingress_mappings++;

	vlan->ingress_priority_map[vlan_prio & 0x7] = skb_prio;
}

int vlan_dev_set_egress_priority(const struct net_device *dev,
				 u32 skb_prio, short vlan_prio)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
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
		if (flag_val)
			vlan_dev_info(dev)->flags |= VLAN_FLAG_REORDER_HDR;
		else
			vlan_dev_info(dev)->flags &= ~VLAN_FLAG_REORDER_HDR;
		return 0;
	}
	return -EINVAL;
}

void vlan_dev_get_realdev_name(const struct net_device *dev, char *result)
{
	strncpy(result, vlan_dev_info(dev)->real_dev->name, 23);
}

void vlan_dev_get_vid(const struct net_device *dev, unsigned short *result)
{
	*result = vlan_dev_info(dev)->vlan_id;
}

static int vlan_dev_open(struct net_device *dev)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
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

	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, 1);
	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(real_dev, 1);

	return 0;
}

static int vlan_dev_stop(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;

	dev_mc_unsync(real_dev, dev);
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, -1);
	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(real_dev, -1);

	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr))
		dev_unicast_delete(real_dev, dev->dev_addr, dev->addr_len);

	return 0;
}

static int vlan_dev_set_mac_address(struct net_device *dev, void *p)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!(dev->flags & IFF_UP))
		goto out;

	if (compare_ether_addr(addr->sa_data, real_dev->dev_addr)) {
		err = dev_unicast_add(real_dev, addr->sa_data, ETH_ALEN);
		if (err < 0)
			return err;
	}

	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr))
		dev_unicast_delete(real_dev, dev->dev_addr, ETH_ALEN);

out:
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	return 0;
}

static int vlan_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;
	struct ifreq ifrr;
	int err = -EOPNOTSUPP;

	strncpy(ifrr.ifr_name, real_dev->name, IFNAMSIZ);
	ifrr.ifr_ifru = ifr->ifr_ifru;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		if (real_dev->do_ioctl && netif_device_present(real_dev))
			err = real_dev->do_ioctl(real_dev, &ifrr, cmd);
		break;
	}

	if (!err)
		ifr->ifr_ifru = ifrr.ifr_ifru;

	return err;
}

static void vlan_dev_change_rx_flags(struct net_device *dev, int change)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(real_dev, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void vlan_dev_set_multicast_list(struct net_device *vlan_dev)
{
	dev_mc_sync(vlan_dev_info(vlan_dev)->real_dev, vlan_dev);
}

/*
 * vlan network devices have devices nesting below it, and are a special
 * "super class" of normal network devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key vlan_netdev_xmit_lock_key;

static const struct header_ops vlan_header_ops = {
	.create	 = vlan_dev_hard_header,
	.rebuild = vlan_dev_rebuild_header,
	.parse	 = eth_header_parse,
};

static int vlan_dev_init(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;
	int subclass = 0;

	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	dev->flags  = real_dev->flags & ~IFF_UP;
	dev->iflink = real_dev->ifindex;
	dev->state  = (real_dev->state & ((1<<__LINK_STATE_NOCARRIER) |
					  (1<<__LINK_STATE_DORMANT))) |
		      (1<<__LINK_STATE_PRESENT);

	/* ipv6 shared card related stuff */
	dev->dev_id = real_dev->dev_id;

	if (is_zero_ether_addr(dev->dev_addr))
		memcpy(dev->dev_addr, real_dev->dev_addr, dev->addr_len);
	if (is_zero_ether_addr(dev->broadcast))
		memcpy(dev->broadcast, real_dev->broadcast, dev->addr_len);

	if (real_dev->features & NETIF_F_HW_VLAN_TX) {
		dev->header_ops      = real_dev->header_ops;
		dev->hard_header_len = real_dev->hard_header_len;
		dev->hard_start_xmit = vlan_dev_hwaccel_hard_start_xmit;
	} else {
		dev->header_ops      = &vlan_header_ops;
		dev->hard_header_len = real_dev->hard_header_len + VLAN_HLEN;
		dev->hard_start_xmit = vlan_dev_hard_start_xmit;
	}

	if (real_dev->priv_flags & IFF_802_1Q_VLAN)
		subclass = 1;

	lockdep_set_class_and_subclass(&dev->_xmit_lock,
				&vlan_netdev_xmit_lock_key, subclass);
	return 0;
}

void vlan_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->priv_flags		|= IFF_802_1Q_VLAN;
	dev->tx_queue_len	= 0;

	dev->change_mtu		= vlan_dev_change_mtu;
	dev->init		= vlan_dev_init;
	dev->open		= vlan_dev_open;
	dev->stop		= vlan_dev_stop;
	dev->set_mac_address	= vlan_dev_set_mac_address;
	dev->set_multicast_list	= vlan_dev_set_multicast_list;
	dev->change_rx_flags	= vlan_dev_change_rx_flags;
	dev->do_ioctl		= vlan_dev_ioctl;
	dev->destructor		= free_netdev;

	memset(dev->broadcast, 0, ETH_ALEN);
}
