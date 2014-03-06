/*
 * Copyright 2011, Siemens AG
 * written by Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

/*
 * Based on patches from Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2011 Jon Smirl <jonsmirl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Jon's code is based on 6lowpan implementation for Contiki which is:
 * Copyright (c) 2008, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/bitops.h>
#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <net/af_ieee802154.h>
#include <net/ieee802154.h>
#include <net/ieee802154_netdev.h>
#include <net/ipv6.h>

#include "6lowpan.h"

static LIST_HEAD(lowpan_devices);

/* private device info */
struct lowpan_dev_info {
	struct net_device	*real_dev; /* real WPAN device ptr */
	struct mutex		dev_list_mtx; /* mutex for list ops */
	unsigned short		fragment_tag;
};

struct lowpan_dev_record {
	struct net_device *ldev;
	struct list_head list;
};

struct lowpan_fragment {
	struct sk_buff		*skb;		/* skb to be assembled */
	u16			length;		/* length to be assemled */
	u32			bytes_rcv;	/* bytes received */
	u16			tag;		/* current fragment tag */
	struct timer_list	timer;		/* assembling timer */
	struct list_head	list;		/* fragments list */
};

static LIST_HEAD(lowpan_fragments);
static DEFINE_SPINLOCK(flist_lock);

static inline struct
lowpan_dev_info *lowpan_dev_info(const struct net_device *dev)
{
	return netdev_priv(dev);
}

static inline void lowpan_address_flip(u8 *src, u8 *dest)
{
	int i;
	for (i = 0; i < IEEE802154_ADDR_LEN; i++)
		(dest)[IEEE802154_ADDR_LEN - i - 1] = (src)[i];
}

static int lowpan_header_create(struct sk_buff *skb,
			   struct net_device *dev,
			   unsigned short type, const void *_daddr,
			   const void *_saddr, unsigned int len)
{
	struct ipv6hdr *hdr;
	const u8 *saddr = _saddr;
	const u8 *daddr = _daddr;
	struct ieee802154_addr sa, da;

	/* TODO:
	 * if this package isn't ipv6 one, where should it be routed?
	 */
	if (type != ETH_P_IPV6)
		return 0;

	hdr = ipv6_hdr(skb);

	if (!saddr)
		saddr = dev->dev_addr;

	raw_dump_inline(__func__, "saddr", (unsigned char *)saddr, 8);
	raw_dump_inline(__func__, "daddr", (unsigned char *)daddr, 8);

	lowpan_header_compress(skb, dev, type, daddr, saddr, len);

	/*
	 * NOTE1: I'm still unsure about the fact that compression and WPAN
	 * header are created here and not later in the xmit. So wait for
	 * an opinion of net maintainers.
	 */
	/*
	 * NOTE2: to be absolutely correct, we must derive PANid information
	 * from MAC subif of the 'dev' and 'real_dev' network devices, but
	 * this isn't implemented in mainline yet, so currently we assign 0xff
	 */
	mac_cb(skb)->flags = IEEE802154_FC_TYPE_DATA;
	mac_cb(skb)->seq = ieee802154_mlme_ops(dev)->get_dsn(dev);

	/* prepare wpan address data */
	sa.addr_type = IEEE802154_ADDR_LONG;
	sa.pan_id = ieee802154_mlme_ops(dev)->get_pan_id(dev);

	memcpy(&(sa.hwaddr), saddr, 8);
	/* intra-PAN communications */
	da.pan_id = ieee802154_mlme_ops(dev)->get_pan_id(dev);

	/*
	 * if the destination address is the broadcast address, use the
	 * corresponding short address
	 */
	if (lowpan_is_addr_broadcast(daddr)) {
		da.addr_type = IEEE802154_ADDR_SHORT;
		da.short_addr = IEEE802154_ADDR_BROADCAST;
	} else {
		da.addr_type = IEEE802154_ADDR_LONG;
		memcpy(&(da.hwaddr), daddr, IEEE802154_ADDR_LEN);

		/* request acknowledgment */
		mac_cb(skb)->flags |= MAC_CB_FLAG_ACKREQ;
	}

	return dev_hard_header(skb, lowpan_dev_info(dev)->real_dev,
			type, (void *)&da, (void *)&sa, skb->len);
}

static int lowpan_give_skb_to_devices(struct sk_buff *skb,
					struct net_device *dev)
{
	struct lowpan_dev_record *entry;
	struct sk_buff *skb_cp;
	int stat = NET_RX_SUCCESS;

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &lowpan_devices, list)
		if (lowpan_dev_info(entry->ldev)->real_dev == skb->dev) {
			skb_cp = skb_copy(skb, GFP_ATOMIC);
			if (!skb_cp) {
				stat = -ENOMEM;
				break;
			}

			skb_cp->dev = entry->ldev;
			stat = netif_rx(skb_cp);
		}
	rcu_read_unlock();

	return stat;
}

static void lowpan_fragment_timer_expired(unsigned long entry_addr)
{
	struct lowpan_fragment *entry = (struct lowpan_fragment *)entry_addr;

	pr_debug("timer expired for frame with tag %d\n", entry->tag);

	list_del(&entry->list);
	dev_kfree_skb(entry->skb);
	kfree(entry);
}

static struct lowpan_fragment *
lowpan_alloc_new_frame(struct sk_buff *skb, u16 len, u16 tag)
{
	struct lowpan_fragment *frame;

	frame = kzalloc(sizeof(struct lowpan_fragment),
			GFP_ATOMIC);
	if (!frame)
		goto frame_err;

	INIT_LIST_HEAD(&frame->list);

	frame->length = len;
	frame->tag = tag;

	/* allocate buffer for frame assembling */
	frame->skb = netdev_alloc_skb_ip_align(skb->dev, frame->length +
					       sizeof(struct ipv6hdr));

	if (!frame->skb)
		goto skb_err;

	frame->skb->priority = skb->priority;

	/* reserve headroom for uncompressed ipv6 header */
	skb_reserve(frame->skb, sizeof(struct ipv6hdr));
	skb_put(frame->skb, frame->length);

	/* copy the first control block to keep a
	 * trace of the link-layer addresses in case
	 * of a link-local compressed address
	 */
	memcpy(frame->skb->cb, skb->cb, sizeof(skb->cb));

	init_timer(&frame->timer);
	/* time out is the same as for ipv6 - 60 sec */
	frame->timer.expires = jiffies + LOWPAN_FRAG_TIMEOUT;
	frame->timer.data = (unsigned long)frame;
	frame->timer.function = lowpan_fragment_timer_expired;

	add_timer(&frame->timer);

	list_add_tail(&frame->list, &lowpan_fragments);

	return frame;

skb_err:
	kfree(frame);
frame_err:
	return NULL;
}

static int process_data(struct sk_buff *skb)
{
	u8 iphc0, iphc1;
	const struct ieee802154_addr *_saddr, *_daddr;

	raw_dump_table(__func__, "raw skb data dump", skb->data, skb->len);
	/* at least two bytes will be used for the encoding */
	if (skb->len < 2)
		goto drop;

	if (lowpan_fetch_skb_u8(skb, &iphc0))
		goto drop;

	/* fragments assembling */
	switch (iphc0 & LOWPAN_DISPATCH_MASK) {
	case LOWPAN_DISPATCH_FRAG1:
	case LOWPAN_DISPATCH_FRAGN:
	{
		struct lowpan_fragment *frame;
		/* slen stores the rightmost 8 bits of the 11 bits length */
		u8 slen, offset = 0;
		u16 len, tag;
		bool found = false;

		if (lowpan_fetch_skb_u8(skb, &slen) || /* frame length */
		    lowpan_fetch_skb_u16(skb, &tag))  /* fragment tag */
			goto drop;

		/* adds the 3 MSB to the 8 LSB to retrieve the 11 bits length */
		len = ((iphc0 & 7) << 8) | slen;

		if ((iphc0 & LOWPAN_DISPATCH_MASK) == LOWPAN_DISPATCH_FRAG1) {
			pr_debug("%s received a FRAG1 packet (tag: %d, "
				 "size of the entire IP packet: %d)",
				 __func__, tag, len);
		} else { /* FRAGN */
			if (lowpan_fetch_skb_u8(skb, &offset))
				goto unlock_and_drop;
			pr_debug("%s received a FRAGN packet (tag: %d, "
				 "size of the entire IP packet: %d, "
				 "offset: %d)", __func__, tag, len, offset * 8);
		}

		/*
		 * check if frame assembling with the same tag is
		 * already in progress
		 */
		spin_lock_bh(&flist_lock);

		list_for_each_entry(frame, &lowpan_fragments, list)
			if (frame->tag == tag) {
				found = true;
				break;
			}

		/* alloc new frame structure */
		if (!found) {
			pr_debug("%s first fragment received for tag %d, "
				 "begin packet reassembly", __func__, tag);
			frame = lowpan_alloc_new_frame(skb, len, tag);
			if (!frame)
				goto unlock_and_drop;
		}

		/* if payload fits buffer, copy it */
		if (likely((offset * 8 + skb->len) <= frame->length))
			skb_copy_to_linear_data_offset(frame->skb, offset * 8,
							skb->data, skb->len);
		else
			goto unlock_and_drop;

		frame->bytes_rcv += skb->len;

		/* frame assembling complete */
		if ((frame->bytes_rcv == frame->length) &&
		     frame->timer.expires > jiffies) {
			/* if timer haven't expired - first of all delete it */
			del_timer_sync(&frame->timer);
			list_del(&frame->list);
			spin_unlock_bh(&flist_lock);

			pr_debug("%s successfully reassembled fragment "
				 "(tag %d)", __func__, tag);

			dev_kfree_skb(skb);
			skb = frame->skb;
			kfree(frame);

			if (lowpan_fetch_skb_u8(skb, &iphc0))
				goto drop;

			break;
		}
		spin_unlock_bh(&flist_lock);

		return kfree_skb(skb), 0;
	}
	default:
		break;
	}

	if (lowpan_fetch_skb_u8(skb, &iphc1))
		goto drop;

	_saddr = &mac_cb(skb)->sa;
	_daddr = &mac_cb(skb)->da;

	return lowpan_process_data(skb, skb->dev, (u8 *)_saddr->hwaddr,
				_saddr->addr_type, IEEE802154_ADDR_LEN,
				(u8 *)_daddr->hwaddr, _daddr->addr_type,
				IEEE802154_ADDR_LEN, iphc0, iphc1,
				lowpan_give_skb_to_devices);

unlock_and_drop:
	spin_unlock_bh(&flist_lock);
drop:
	kfree_skb(skb);
	return -EINVAL;
}

static int lowpan_set_address(struct net_device *dev, void *p)
{
	struct sockaddr *sa = p;

	if (netif_running(dev))
		return -EBUSY;

	/* TODO: validate addr */
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	return 0;
}

static int
lowpan_fragment_xmit(struct sk_buff *skb, u8 *head,
			int mlen, int plen, int offset, int type)
{
	struct sk_buff *frag;
	int hlen;

	hlen = (type == LOWPAN_DISPATCH_FRAG1) ?
			LOWPAN_FRAG1_HEAD_SIZE : LOWPAN_FRAGN_HEAD_SIZE;

	raw_dump_inline(__func__, "6lowpan fragment header", head, hlen);

	frag = netdev_alloc_skb(skb->dev,
				hlen + mlen + plen + IEEE802154_MFR_SIZE);
	if (!frag)
		return -ENOMEM;

	frag->priority = skb->priority;

	/* copy header, MFR and payload */
	skb_put(frag, mlen);
	skb_copy_to_linear_data(frag, skb_mac_header(skb), mlen);

	skb_put(frag, hlen);
	skb_copy_to_linear_data_offset(frag, mlen, head, hlen);

	skb_put(frag, plen);
	skb_copy_to_linear_data_offset(frag, mlen + hlen,
				       skb_network_header(skb) + offset, plen);

	raw_dump_table(__func__, " raw fragment dump", frag->data, frag->len);

	return dev_queue_xmit(frag);
}

static int
lowpan_skb_fragmentation(struct sk_buff *skb, struct net_device *dev)
{
	int  err, header_length, payload_length, tag, offset = 0;
	u8 head[5];

	header_length = skb->mac_len;
	payload_length = skb->len - header_length;
	tag = lowpan_dev_info(dev)->fragment_tag++;

	/* first fragment header */
	head[0] = LOWPAN_DISPATCH_FRAG1 | ((payload_length >> 8) & 0x7);
	head[1] = payload_length & 0xff;
	head[2] = tag >> 8;
	head[3] = tag & 0xff;

	err = lowpan_fragment_xmit(skb, head, header_length, LOWPAN_FRAG_SIZE,
				   0, LOWPAN_DISPATCH_FRAG1);

	if (err) {
		pr_debug("%s unable to send FRAG1 packet (tag: %d)",
			 __func__, tag);
		goto exit;
	}

	offset = LOWPAN_FRAG_SIZE;

	/* next fragment header */
	head[0] &= ~LOWPAN_DISPATCH_FRAG1;
	head[0] |= LOWPAN_DISPATCH_FRAGN;

	while (payload_length - offset > 0) {
		int len = LOWPAN_FRAG_SIZE;

		head[4] = offset / 8;

		if (payload_length - offset < len)
			len = payload_length - offset;

		err = lowpan_fragment_xmit(skb, head, header_length,
					   len, offset, LOWPAN_DISPATCH_FRAGN);
		if (err) {
			pr_debug("%s unable to send a subsequent FRAGN packet "
				 "(tag: %d, offset: %d", __func__, tag, offset);
			goto exit;
		}

		offset += len;
	}

exit:
	return err;
}

static netdev_tx_t lowpan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int err = -1;

	pr_debug("package xmit\n");

	skb->dev = lowpan_dev_info(dev)->real_dev;
	if (skb->dev == NULL) {
		pr_debug("ERROR: no real wpan device found\n");
		goto error;
	}

	/* Send directly if less than the MTU minus the 2 checksum bytes. */
	if (skb->len <= IEEE802154_MTU - IEEE802154_MFR_SIZE) {
		err = dev_queue_xmit(skb);
		goto out;
	}

	pr_debug("frame is too big, fragmentation is needed\n");
	err = lowpan_skb_fragmentation(skb, dev);
error:
	dev_kfree_skb(skb);
out:
	if (err)
		pr_debug("ERROR: xmit failed\n");

	return (err < 0) ? NET_XMIT_DROP : err;
}

static struct wpan_phy *lowpan_get_phy(const struct net_device *dev)
{
	struct net_device *real_dev = lowpan_dev_info(dev)->real_dev;
	return ieee802154_mlme_ops(real_dev)->get_phy(real_dev);
}

static u16 lowpan_get_pan_id(const struct net_device *dev)
{
	struct net_device *real_dev = lowpan_dev_info(dev)->real_dev;
	return ieee802154_mlme_ops(real_dev)->get_pan_id(real_dev);
}

static u16 lowpan_get_short_addr(const struct net_device *dev)
{
	struct net_device *real_dev = lowpan_dev_info(dev)->real_dev;
	return ieee802154_mlme_ops(real_dev)->get_short_addr(real_dev);
}

static u8 lowpan_get_dsn(const struct net_device *dev)
{
	struct net_device *real_dev = lowpan_dev_info(dev)->real_dev;
	return ieee802154_mlme_ops(real_dev)->get_dsn(real_dev);
}

static struct header_ops lowpan_header_ops = {
	.create	= lowpan_header_create,
};

static const struct net_device_ops lowpan_netdev_ops = {
	.ndo_start_xmit		= lowpan_xmit,
	.ndo_set_mac_address	= lowpan_set_address,
};

static struct ieee802154_mlme_ops lowpan_mlme = {
	.get_pan_id = lowpan_get_pan_id,
	.get_phy = lowpan_get_phy,
	.get_short_addr = lowpan_get_short_addr,
	.get_dsn = lowpan_get_dsn,
};

static void lowpan_setup(struct net_device *dev)
{
	dev->addr_len		= IEEE802154_ADDR_LEN;
	memset(dev->broadcast, 0xff, IEEE802154_ADDR_LEN);
	dev->type		= ARPHRD_IEEE802154;
	/* Frame Control + Sequence Number + Address fields + Security Header */
	dev->hard_header_len	= 2 + 1 + 20 + 14;
	dev->needed_tailroom	= 2; /* FCS */
	dev->mtu		= 1281;
	dev->tx_queue_len	= 0;
	dev->flags		= IFF_BROADCAST | IFF_MULTICAST;
	dev->watchdog_timeo	= 0;

	dev->netdev_ops		= &lowpan_netdev_ops;
	dev->header_ops		= &lowpan_header_ops;
	dev->ml_priv		= &lowpan_mlme;
	dev->destructor		= free_netdev;
}

static int lowpan_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != IEEE802154_ADDR_LEN)
			return -EINVAL;
	}
	return 0;
}

static int lowpan_rcv(struct sk_buff *skb, struct net_device *dev,
	struct packet_type *pt, struct net_device *orig_dev)
{
	struct sk_buff *local_skb;

	if (!netif_running(dev))
		goto drop;

	if (dev->type != ARPHRD_IEEE802154)
		goto drop;

	/* check that it's our buffer */
	if (skb->data[0] == LOWPAN_DISPATCH_IPV6) {
		/* Copy the packet so that the IPv6 header is
		 * properly aligned.
		 */
		local_skb = skb_copy_expand(skb, NET_SKB_PAD - 1,
					    skb_tailroom(skb), GFP_ATOMIC);
		if (!local_skb)
			goto drop;

		local_skb->protocol = htons(ETH_P_IPV6);
		local_skb->pkt_type = PACKET_HOST;

		/* Pull off the 1-byte of 6lowpan header. */
		skb_pull(local_skb, 1);

		lowpan_give_skb_to_devices(local_skb, NULL);

		kfree_skb(local_skb);
		kfree_skb(skb);
	} else {
		switch (skb->data[0] & 0xe0) {
		case LOWPAN_DISPATCH_IPHC:	/* ipv6 datagram */
		case LOWPAN_DISPATCH_FRAG1:	/* first fragment header */
		case LOWPAN_DISPATCH_FRAGN:	/* next fragments headers */
			local_skb = skb_clone(skb, GFP_ATOMIC);
			if (!local_skb)
				goto drop;
			process_data(local_skb);

			kfree_skb(skb);
			break;
		default:
			break;
		}
	}

	return NET_RX_SUCCESS;

drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static int lowpan_newlink(struct net *src_net, struct net_device *dev,
			  struct nlattr *tb[], struct nlattr *data[])
{
	struct net_device *real_dev;
	struct lowpan_dev_record *entry;

	pr_debug("adding new link\n");

	if (!tb[IFLA_LINK])
		return -EINVAL;
	/* find and hold real wpan device */
	real_dev = dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (!real_dev)
		return -ENODEV;
	if (real_dev->type != ARPHRD_IEEE802154) {
		dev_put(real_dev);
		return -EINVAL;
	}

	lowpan_dev_info(dev)->real_dev = real_dev;
	lowpan_dev_info(dev)->fragment_tag = 0;
	mutex_init(&lowpan_dev_info(dev)->dev_list_mtx);

	entry = kzalloc(sizeof(struct lowpan_dev_record), GFP_KERNEL);
	if (!entry) {
		dev_put(real_dev);
		lowpan_dev_info(dev)->real_dev = NULL;
		return -ENOMEM;
	}

	entry->ldev = dev;

	/* Set the lowpan harware address to the wpan hardware address. */
	memcpy(dev->dev_addr, real_dev->dev_addr, IEEE802154_ADDR_LEN);

	mutex_lock(&lowpan_dev_info(dev)->dev_list_mtx);
	INIT_LIST_HEAD(&entry->list);
	list_add_tail(&entry->list, &lowpan_devices);
	mutex_unlock(&lowpan_dev_info(dev)->dev_list_mtx);

	register_netdevice(dev);

	return 0;
}

static void lowpan_dellink(struct net_device *dev, struct list_head *head)
{
	struct lowpan_dev_info *lowpan_dev = lowpan_dev_info(dev);
	struct net_device *real_dev = lowpan_dev->real_dev;
	struct lowpan_dev_record *entry, *tmp;

	ASSERT_RTNL();

	mutex_lock(&lowpan_dev_info(dev)->dev_list_mtx);
	list_for_each_entry_safe(entry, tmp, &lowpan_devices, list) {
		if (entry->ldev == dev) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
	mutex_unlock(&lowpan_dev_info(dev)->dev_list_mtx);

	mutex_destroy(&lowpan_dev_info(dev)->dev_list_mtx);

	unregister_netdevice_queue(dev, head);

	dev_put(real_dev);
}

static struct rtnl_link_ops lowpan_link_ops __read_mostly = {
	.kind		= "lowpan",
	.priv_size	= sizeof(struct lowpan_dev_info),
	.setup		= lowpan_setup,
	.newlink	= lowpan_newlink,
	.dellink	= lowpan_dellink,
	.validate	= lowpan_validate,
};

static inline int __init lowpan_netlink_init(void)
{
	return rtnl_link_register(&lowpan_link_ops);
}

static inline void lowpan_netlink_fini(void)
{
	rtnl_link_unregister(&lowpan_link_ops);
}

static int lowpan_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	LIST_HEAD(del_list);
	struct lowpan_dev_record *entry, *tmp;

	if (dev->type != ARPHRD_IEEE802154)
		goto out;

	if (event == NETDEV_UNREGISTER) {
		list_for_each_entry_safe(entry, tmp, &lowpan_devices, list) {
			if (lowpan_dev_info(entry->ldev)->real_dev == dev)
				lowpan_dellink(entry->ldev, &del_list);
		}

		unregister_netdevice_many(&del_list);
	}

out:
	return NOTIFY_DONE;
}

static struct notifier_block lowpan_dev_notifier = {
	.notifier_call = lowpan_device_event,
};

static struct packet_type lowpan_packet_type = {
	.type = __constant_htons(ETH_P_IEEE802154),
	.func = lowpan_rcv,
};

static int __init lowpan_init_module(void)
{
	int err = 0;

	err = lowpan_netlink_init();
	if (err < 0)
		goto out;

	dev_add_pack(&lowpan_packet_type);

	err = register_netdevice_notifier(&lowpan_dev_notifier);
	if (err < 0) {
		dev_remove_pack(&lowpan_packet_type);
		lowpan_netlink_fini();
	}
out:
	return err;
}

static void __exit lowpan_cleanup_module(void)
{
	struct lowpan_fragment *frame, *tframe;

	lowpan_netlink_fini();

	dev_remove_pack(&lowpan_packet_type);

	unregister_netdevice_notifier(&lowpan_dev_notifier);

	/* Now 6lowpan packet_type is removed, so no new fragments are
	 * expected on RX, therefore that's the time to clean incomplete
	 * fragments.
	 */
	spin_lock_bh(&flist_lock);
	list_for_each_entry_safe(frame, tframe, &lowpan_fragments, list) {
		del_timer_sync(&frame->timer);
		list_del(&frame->list);
		dev_kfree_skb(frame->skb);
		kfree(frame);
	}
	spin_unlock_bh(&flist_lock);
}

module_init(lowpan_init_module);
module_exit(lowpan_cleanup_module);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("lowpan");
