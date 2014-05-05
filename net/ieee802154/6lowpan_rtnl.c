/* Copyright 2011, Siemens AG
 * written by Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

/* Based on patches from Jon Smirl <jonsmirl@gmail.com>
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
#include <net/6lowpan.h>
#include <net/ipv6.h>

#include "reassembly.h"

static LIST_HEAD(lowpan_devices);

/* private device info */
struct lowpan_dev_info {
	struct net_device	*real_dev; /* real WPAN device ptr */
	struct mutex		dev_list_mtx; /* mutex for list ops */
	__be16			fragment_tag;
};

struct lowpan_dev_record {
	struct net_device *ldev;
	struct list_head list;
};

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
	const u8 *saddr = _saddr;
	const u8 *daddr = _daddr;
	struct ieee802154_addr sa, da;

	/* TODO:
	 * if this package isn't ipv6 one, where should it be routed?
	 */
	if (type != ETH_P_IPV6)
		return 0;

	if (!saddr)
		saddr = dev->dev_addr;

	raw_dump_inline(__func__, "saddr", (unsigned char *)saddr, 8);
	raw_dump_inline(__func__, "daddr", (unsigned char *)daddr, 8);

	lowpan_header_compress(skb, dev, type, daddr, saddr, len);

	/* NOTE1: I'm still unsure about the fact that compression and WPAN
	 * header are created here and not later in the xmit. So wait for
	 * an opinion of net maintainers.
	 */
	/* NOTE2: to be absolutely correct, we must derive PANid information
	 * from MAC subif of the 'dev' and 'real_dev' network devices, but
	 * this isn't implemented in mainline yet, so currently we assign 0xff
	 */
	mac_cb(skb)->flags = IEEE802154_FC_TYPE_DATA;
	mac_cb(skb)->seq = ieee802154_mlme_ops(dev)->get_dsn(dev);

	/* prepare wpan address data */
	sa.mode = IEEE802154_ADDR_LONG;
	sa.pan_id = ieee802154_mlme_ops(dev)->get_pan_id(dev);
	sa.extended_addr = ieee802154_devaddr_from_raw(saddr);

	/* intra-PAN communications */
	da.pan_id = sa.pan_id;

	/* if the destination address is the broadcast address, use the
	 * corresponding short address
	 */
	if (lowpan_is_addr_broadcast(daddr)) {
		da.mode = IEEE802154_ADDR_SHORT;
		da.short_addr = cpu_to_le16(IEEE802154_ADDR_BROADCAST);
	} else {
		da.mode = IEEE802154_ADDR_LONG;
		da.extended_addr = ieee802154_devaddr_from_raw(daddr);

		/* request acknowledgment */
		mac_cb(skb)->flags |= MAC_CB_FLAG_ACKREQ;
	}

	return dev_hard_header(skb, lowpan_dev_info(dev)->real_dev,
			type, (void *)&da, (void *)&sa, 0);
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

static int process_data(struct sk_buff *skb, const struct ieee802154_hdr *hdr)
{
	u8 iphc0, iphc1;
	struct ieee802154_addr_sa sa, da;
	void *sap, *dap;

	raw_dump_table(__func__, "raw skb data dump", skb->data, skb->len);
	/* at least two bytes will be used for the encoding */
	if (skb->len < 2)
		goto drop;

	if (lowpan_fetch_skb_u8(skb, &iphc0))
		goto drop;

	if (lowpan_fetch_skb_u8(skb, &iphc1))
		goto drop;

	ieee802154_addr_to_sa(&sa, &hdr->source);
	ieee802154_addr_to_sa(&da, &hdr->dest);

	if (sa.addr_type == IEEE802154_ADDR_SHORT)
		sap = &sa.short_addr;
	else
		sap = &sa.hwaddr;

	if (da.addr_type == IEEE802154_ADDR_SHORT)
		dap = &da.short_addr;
	else
		dap = &da.hwaddr;

	return lowpan_process_data(skb, skb->dev, sap, sa.addr_type,
				   IEEE802154_ADDR_LEN, dap, da.addr_type,
				   IEEE802154_ADDR_LEN, iphc0, iphc1,
				   lowpan_give_skb_to_devices);

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
	int err;
	u16 dgram_offset, dgram_size, payload_length, header_length,
	    lowpan_size, frag_plen, offset;
	__be16 tag;
	u8 head[5];

	header_length = skb->mac_len;
	payload_length = skb->len - header_length;
	tag = lowpan_dev_info(dev)->fragment_tag++;
	lowpan_size = skb_network_header_len(skb);
	dgram_size = lowpan_uncompress_size(skb, &dgram_offset) -
		     header_length;

	/* first fragment header */
	head[0] = LOWPAN_DISPATCH_FRAG1 | ((dgram_size >> 8) & 0x7);
	head[1] = dgram_size & 0xff;
	memcpy(head + 2, &tag, sizeof(tag));

	/* calc the nearest payload length(divided to 8) for first fragment
	 * which fits into a IEEE802154_MTU
	 */
	frag_plen = round_down(IEEE802154_MTU - header_length -
			       LOWPAN_FRAG1_HEAD_SIZE - lowpan_size -
			       IEEE802154_MFR_SIZE, 8);

	err = lowpan_fragment_xmit(skb, head, header_length,
				   frag_plen + lowpan_size, 0,
				   LOWPAN_DISPATCH_FRAG1);
	if (err) {
		pr_debug("%s unable to send FRAG1 packet (tag: %d)",
			 __func__, tag);
		goto exit;
	}

	offset = lowpan_size + frag_plen;
	dgram_offset += frag_plen;

	/* next fragment header */
	head[0] &= ~LOWPAN_DISPATCH_FRAG1;
	head[0] |= LOWPAN_DISPATCH_FRAGN;

	frag_plen = round_down(IEEE802154_MTU - header_length -
			       LOWPAN_FRAGN_HEAD_SIZE - IEEE802154_MFR_SIZE, 8);

	while (payload_length - offset > 0) {
		int len = frag_plen;

		head[4] = dgram_offset >> 3;

		if (payload_length - offset < len)
			len = payload_length - offset;

		err = lowpan_fragment_xmit(skb, head, header_length, len,
					   offset, LOWPAN_DISPATCH_FRAGN);
		if (err) {
			pr_debug("%s unable to send a FRAGN packet. (tag: %d, offset: %d)\n",
				 __func__, tag, offset);
			goto exit;
		}

		offset += len;
		dgram_offset += len;
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

static __le16 lowpan_get_pan_id(const struct net_device *dev)
{
	struct net_device *real_dev = lowpan_dev_info(dev)->real_dev;
	return ieee802154_mlme_ops(real_dev)->get_pan_id(real_dev);
}

static __le16 lowpan_get_short_addr(const struct net_device *dev)
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

static struct lock_class_key lowpan_tx_busylock;
static struct lock_class_key lowpan_netdev_xmit_lock_key;

static void lowpan_set_lockdep_class_one(struct net_device *dev,
					 struct netdev_queue *txq,
					 void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock,
			  &lowpan_netdev_xmit_lock_key);
}


static int lowpan_dev_init(struct net_device *dev)
{
	netdev_for_each_tx_queue(dev, lowpan_set_lockdep_class_one, NULL);
	dev->qdisc_tx_busylock = &lowpan_tx_busylock;
	return 0;
}

static const struct net_device_ops lowpan_netdev_ops = {
	.ndo_init		= lowpan_dev_init,
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
	struct ieee802154_hdr hdr;
	int ret;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto drop;

	if (!netif_running(dev))
		goto drop_skb;

	if (dev->type != ARPHRD_IEEE802154)
		goto drop_skb;

	if (ieee802154_hdr_peek_addrs(skb, &hdr) < 0)
		goto drop_skb;

	/* check that it's our buffer */
	if (skb->data[0] == LOWPAN_DISPATCH_IPV6) {
		skb->protocol = htons(ETH_P_IPV6);
		skb->pkt_type = PACKET_HOST;

		/* Pull off the 1-byte of 6lowpan header. */
		skb_pull(skb, 1);

		ret = lowpan_give_skb_to_devices(skb, NULL);
		if (ret == NET_RX_DROP)
			goto drop;
	} else {
		switch (skb->data[0] & 0xe0) {
		case LOWPAN_DISPATCH_IPHC:	/* ipv6 datagram */
			ret = process_data(skb, &hdr);
			if (ret == NET_RX_DROP)
				goto drop;
			break;
		case LOWPAN_DISPATCH_FRAG1:	/* first fragment header */
			ret = lowpan_frag_rcv(skb, LOWPAN_DISPATCH_FRAG1);
			if (ret == 1) {
				ret = process_data(skb, &hdr);
				if (ret == NET_RX_DROP)
					goto drop;
			}
			break;
		case LOWPAN_DISPATCH_FRAGN:	/* next fragments headers */
			ret = lowpan_frag_rcv(skb, LOWPAN_DISPATCH_FRAGN);
			if (ret == 1) {
				ret = process_data(skb, &hdr);
				if (ret == NET_RX_DROP)
					goto drop;
			}
			break;
		default:
			break;
		}
	}

	return NET_RX_SUCCESS;
drop_skb:
	kfree_skb(skb);
drop:
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
	mutex_init(&lowpan_dev_info(dev)->dev_list_mtx);

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
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
	.type = htons(ETH_P_IEEE802154),
	.func = lowpan_rcv,
};

static int __init lowpan_init_module(void)
{
	int err = 0;

	err = lowpan_net_frag_init();
	if (err < 0)
		goto out;

	err = lowpan_netlink_init();
	if (err < 0)
		goto out_frag;

	dev_add_pack(&lowpan_packet_type);

	err = register_netdevice_notifier(&lowpan_dev_notifier);
	if (err < 0)
		goto out_pack;

	return 0;

out_pack:
	dev_remove_pack(&lowpan_packet_type);
	lowpan_netlink_fini();
out_frag:
	lowpan_net_frag_exit();
out:
	return err;
}

static void __exit lowpan_cleanup_module(void)
{
	lowpan_netlink_fini();

	dev_remove_pack(&lowpan_packet_type);

	lowpan_net_frag_exit();

	unregister_netdevice_notifier(&lowpan_dev_notifier);
}

module_init(lowpan_init_module);
module_exit(lowpan_cleanup_module);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("lowpan");
