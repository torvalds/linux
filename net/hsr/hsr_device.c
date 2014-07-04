/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 *
 * This file contains device methods for creating, using and destroying
 * virtual HSR devices.
 */

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include "hsr_device.h"
#include "hsr_framereg.h"
#include "hsr_main.h"


static bool is_admin_up(struct net_device *dev)
{
	return dev && (dev->flags & IFF_UP);
}

static bool is_slave_up(struct net_device *dev)
{
	return dev && is_admin_up(dev) && netif_oper_up(dev);
}

static void __hsr_set_operstate(struct net_device *dev, int transition)
{
	write_lock_bh(&dev_base_lock);
	if (dev->operstate != transition) {
		dev->operstate = transition;
		write_unlock_bh(&dev_base_lock);
		netdev_state_change(dev);
	} else {
		write_unlock_bh(&dev_base_lock);
	}
}

void hsr_set_operstate(struct net_device *hsr_dev, struct net_device *slave1,
		       struct net_device *slave2)
{
	if (!is_admin_up(hsr_dev)) {
		__hsr_set_operstate(hsr_dev, IF_OPER_DOWN);
		return;
	}

	if (is_slave_up(slave1) || is_slave_up(slave2))
		__hsr_set_operstate(hsr_dev, IF_OPER_UP);
	else
		__hsr_set_operstate(hsr_dev, IF_OPER_LOWERLAYERDOWN);
}

void hsr_set_carrier(struct net_device *hsr_dev, struct net_device *slave1,
		     struct net_device *slave2)
{
	if (is_slave_up(slave1) || is_slave_up(slave2))
		netif_carrier_on(hsr_dev);
	else
		netif_carrier_off(hsr_dev);
}


void hsr_check_announce(struct net_device *hsr_dev, int old_operstate)
{
	struct hsr_priv *hsr;

	hsr = netdev_priv(hsr_dev);

	if ((hsr_dev->operstate == IF_OPER_UP) && (old_operstate != IF_OPER_UP)) {
		/* Went up */
		hsr->announce_count = 0;
		hsr->announce_timer.expires = jiffies +
				msecs_to_jiffies(HSR_ANNOUNCE_INTERVAL);
		add_timer(&hsr->announce_timer);
	}

	if ((hsr_dev->operstate != IF_OPER_UP) && (old_operstate == IF_OPER_UP))
		/* Went down */
		del_timer(&hsr->announce_timer);
}


int hsr_get_max_mtu(struct hsr_priv *hsr)
{
	int mtu_max;

	if (hsr->slave[0] && hsr->slave[1])
		mtu_max = min(hsr->slave[0]->mtu, hsr->slave[1]->mtu);
	else if (hsr->slave[0])
		mtu_max = hsr->slave[0]->mtu;
	else if (hsr->slave[1])
		mtu_max = hsr->slave[1]->mtu;
	else
		mtu_max = HSR_HLEN;

	return mtu_max - HSR_HLEN;
}

static int hsr_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	struct hsr_priv *hsr;

	hsr = netdev_priv(dev);

	if (new_mtu > hsr_get_max_mtu(hsr)) {
		netdev_info(hsr->dev, "A HSR master's MTU cannot be greater than the smallest MTU of its slaves minus the HSR Tag length (%d octets).\n",
			    HSR_HLEN);
		return -EINVAL;
	}

	dev->mtu = new_mtu;

	return 0;
}

static int hsr_dev_open(struct net_device *dev)
{
	struct hsr_priv *hsr;
	int i;
	char *slave_name;

	hsr = netdev_priv(dev);

	for (i = 0; i < HSR_MAX_SLAVE; i++) {
		if (hsr->slave[i])
			slave_name = hsr->slave[i]->name;
		else
			slave_name = "null";

		if (!is_slave_up(hsr->slave[i]))
			netdev_warn(dev, "Slave %c (%s) is not up; please bring it up to get a working HSR network\n",
				    'A' + i, slave_name);
	}

	return 0;
}

static int hsr_dev_close(struct net_device *dev)
{
	/* Nothing to do here. We could try to restore the state of the slaves
	 * to what they were before being changed by the hsr master dev's state,
	 * but they might have been changed manually in the mean time too, so
	 * taking them up or down here might be confusing and is probably not a
	 * good idea.
	 */
	return 0;
}


static void hsr_fill_tag(struct hsr_ethhdr *hsr_ethhdr, struct hsr_priv *hsr)
{
	unsigned long irqflags;

	/* IEC 62439-1:2010, p 48, says the 4-bit "path" field can take values
	 * between 0001-1001 ("ring identifier", for regular HSR frames),
	 * or 1111 ("HSR management", supervision frames). Unfortunately, the
	 * spec writers forgot to explain what a "ring identifier" is, or
	 * how it is used. So we just set this to 0001 for regular frames,
	 * and 1111 for supervision frames.
	 */
	set_hsr_tag_path(&hsr_ethhdr->hsr_tag, 0x1);

	/* IEC 62439-1:2010, p 12: "The link service data unit in an Ethernet
	 * frame is the content of the frame located between the Length/Type
	 * field and the Frame Check Sequence."
	 *
	 * IEC 62439-3, p 48, specifies the "original LPDU" to include the
	 * original "LT" field (what "LT" means is not explained anywhere as
	 * far as I can see - perhaps "Length/Type"?). So LSDU_size might
	 * equal original length + 2.
	 *   Also, the fact that this field is not used anywhere (might be used
	 * by a RedBox connecting HSR and PRP nets?) means I cannot test its
	 * correctness. Instead of guessing, I set this to 0 here, to make any
	 * problems immediately apparent. Anyone using this driver with PRP/HSR
	 * RedBoxes might need to fix this...
	 */
	set_hsr_tag_LSDU_size(&hsr_ethhdr->hsr_tag, 0);

	spin_lock_irqsave(&hsr->seqnr_lock, irqflags);
	hsr_ethhdr->hsr_tag.sequence_nr = htons(hsr->sequence_nr);
	hsr->sequence_nr++;
	spin_unlock_irqrestore(&hsr->seqnr_lock, irqflags);

	hsr_ethhdr->hsr_tag.encap_proto = hsr_ethhdr->ethhdr.h_proto;

	hsr_ethhdr->ethhdr.h_proto = htons(ETH_P_PRP);
}

static int slave_xmit(struct sk_buff *skb, struct hsr_priv *hsr,
		      enum hsr_dev_idx dev_idx)
{
	struct hsr_ethhdr *hsr_ethhdr;

	hsr_ethhdr = (struct hsr_ethhdr *) skb->data;

	skb->dev = hsr->slave[dev_idx];

	hsr_addr_subst_dest(hsr, &hsr_ethhdr->ethhdr, dev_idx);

	/* Address substitution (IEC62439-3 pp 26, 50): replace mac
	 * address of outgoing frame with that of the outgoing slave's.
	 */
	ether_addr_copy(hsr_ethhdr->ethhdr.h_source, skb->dev->dev_addr);

	return dev_queue_xmit(skb);
}


static int hsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hsr_priv *hsr;
	struct hsr_ethhdr *hsr_ethhdr;
	struct sk_buff *skb2;
	int res1, res2;

	hsr = netdev_priv(dev);
	hsr_ethhdr = (struct hsr_ethhdr *) skb->data;

	if ((skb->protocol != htons(ETH_P_PRP)) ||
	    (hsr_ethhdr->ethhdr.h_proto != htons(ETH_P_PRP))) {
		hsr_fill_tag(hsr_ethhdr, hsr);
		skb->protocol = htons(ETH_P_PRP);
	}

	skb2 = pskb_copy(skb, GFP_ATOMIC);

	res1 = NET_XMIT_DROP;
	if (likely(hsr->slave[HSR_DEV_SLAVE_A]))
		res1 = slave_xmit(skb, hsr, HSR_DEV_SLAVE_A);

	res2 = NET_XMIT_DROP;
	if (likely(skb2 && hsr->slave[HSR_DEV_SLAVE_B]))
		res2 = slave_xmit(skb2, hsr, HSR_DEV_SLAVE_B);

	if (likely(res1 == NET_XMIT_SUCCESS || res1 == NET_XMIT_CN ||
		   res2 == NET_XMIT_SUCCESS || res2 == NET_XMIT_CN)) {
		hsr->dev->stats.tx_packets++;
		hsr->dev->stats.tx_bytes += skb->len;
	} else {
		hsr->dev->stats.tx_dropped++;
	}

	return NETDEV_TX_OK;
}


static int hsr_header_create(struct sk_buff *skb, struct net_device *dev,
			     unsigned short type, const void *daddr,
			     const void *saddr, unsigned int len)
{
	int res;

	/* Make room for the HSR tag now. We will fill it in later (in
	 * hsr_dev_xmit)
	 */
	if (skb_headroom(skb) < HSR_HLEN + ETH_HLEN)
		return -ENOBUFS;
	skb_push(skb, HSR_HLEN);

	/* To allow VLAN/HSR combos we should probably use
	 * res = dev_hard_header(skb, dev, type, daddr, saddr, len + HSR_HLEN);
	 * here instead. It would require other changes too, though - e.g.
	 * separate headers for each slave etc...
	 */
	res = eth_header(skb, dev, type, daddr, saddr, len + HSR_HLEN);
	if (res <= 0)
		return res;
	skb_reset_mac_header(skb);

	return res + HSR_HLEN;
}


static const struct header_ops hsr_header_ops = {
	.create	 = hsr_header_create,
	.parse	 = eth_header_parse,
};


/* HSR:2010 supervision frames should be padded so that the whole frame,
 * including headers and FCS, is 64 bytes (without VLAN).
 */
static int hsr_pad(int size)
{
	const int min_size = ETH_ZLEN - HSR_HLEN - ETH_HLEN;

	if (size >= min_size)
		return size;
	return min_size;
}

static void send_hsr_supervision_frame(struct net_device *hsr_dev, u8 type)
{
	struct hsr_priv *hsr;
	struct sk_buff *skb;
	int hlen, tlen;
	struct hsr_sup_tag *hsr_stag;
	struct hsr_sup_payload *hsr_sp;
	unsigned long irqflags;

	hlen = LL_RESERVED_SPACE(hsr_dev);
	tlen = hsr_dev->needed_tailroom;
	skb = alloc_skb(hsr_pad(sizeof(struct hsr_sup_payload)) + hlen + tlen,
			GFP_ATOMIC);

	if (skb == NULL)
		return;

	hsr = netdev_priv(hsr_dev);

	skb_reserve(skb, hlen);

	skb->dev = hsr_dev;
	skb->protocol = htons(ETH_P_PRP);
	skb->priority = TC_PRIO_CONTROL;

	if (dev_hard_header(skb, skb->dev, ETH_P_PRP,
			    hsr->sup_multicast_addr,
			    skb->dev->dev_addr, skb->len) < 0)
		goto out;

	skb_pull(skb, sizeof(struct ethhdr));
	hsr_stag = (typeof(hsr_stag)) skb->data;

	set_hsr_stag_path(hsr_stag, 0xf);
	set_hsr_stag_HSR_Ver(hsr_stag, 0);

	spin_lock_irqsave(&hsr->seqnr_lock, irqflags);
	hsr_stag->sequence_nr = htons(hsr->sequence_nr);
	hsr->sequence_nr++;
	spin_unlock_irqrestore(&hsr->seqnr_lock, irqflags);

	hsr_stag->HSR_TLV_Type = type;
	hsr_stag->HSR_TLV_Length = 12;

	skb_push(skb, sizeof(struct ethhdr));

	/* Payload: MacAddressA */
	hsr_sp = (typeof(hsr_sp)) skb_put(skb, sizeof(*hsr_sp));
	ether_addr_copy(hsr_sp->MacAddressA, hsr_dev->dev_addr);

	dev_queue_xmit(skb);
	return;

out:
	kfree_skb(skb);
}


/* Announce (supervision frame) timer function
 */
static void hsr_announce(unsigned long data)
{
	struct hsr_priv *hsr;

	hsr = (struct hsr_priv *) data;

	if (hsr->announce_count < 3) {
		send_hsr_supervision_frame(hsr->dev, HSR_TLV_ANNOUNCE);
		hsr->announce_count++;
	} else {
		send_hsr_supervision_frame(hsr->dev, HSR_TLV_LIFE_CHECK);
	}

	if (hsr->announce_count < 3)
		hsr->announce_timer.expires = jiffies +
				msecs_to_jiffies(HSR_ANNOUNCE_INTERVAL);
	else
		hsr->announce_timer.expires = jiffies +
				msecs_to_jiffies(HSR_LIFE_CHECK_INTERVAL);

	if (is_admin_up(hsr->dev))
		add_timer(&hsr->announce_timer);
}


static void restore_slaves(struct net_device *hsr_dev)
{
	struct hsr_priv *hsr;
	int i;
	int res;

	hsr = netdev_priv(hsr_dev);

	rtnl_lock();

	/* Restore promiscuity */
	for (i = 0; i < HSR_MAX_SLAVE; i++) {
		if (!hsr->slave[i])
			continue;
		res = dev_set_promiscuity(hsr->slave[i], -1);
		if (res)
			netdev_info(hsr_dev,
				    "Cannot restore slave promiscuity (%s, %d)\n",
				    hsr->slave[i]->name, res);
	}

	rtnl_unlock();
}

static void reclaim_hsr_dev(struct rcu_head *rh)
{
	struct hsr_priv *hsr;

	hsr = container_of(rh, struct hsr_priv, rcu_head);
	free_netdev(hsr->dev);
}


/* According to comments in the declaration of struct net_device, this function
 * is "Called from unregister, can be used to call free_netdev". Ok then...
 */
static void hsr_dev_destroy(struct net_device *hsr_dev)
{
	struct hsr_priv *hsr;

	hsr = netdev_priv(hsr_dev);

	del_timer(&hsr->announce_timer);
	unregister_hsr_master(hsr);    /* calls list_del_rcu on hsr */
	restore_slaves(hsr_dev);
	call_rcu(&hsr->rcu_head, reclaim_hsr_dev);   /* reclaim hsr */
}

static const struct net_device_ops hsr_device_ops = {
	.ndo_change_mtu = hsr_dev_change_mtu,
	.ndo_open = hsr_dev_open,
	.ndo_stop = hsr_dev_close,
	.ndo_start_xmit = hsr_dev_xmit,
};


void hsr_dev_setup(struct net_device *dev)
{
	random_ether_addr(dev->dev_addr);

	ether_setup(dev);
	dev->header_ops		 = &hsr_header_ops;
	dev->netdev_ops		 = &hsr_device_ops;
	dev->tx_queue_len	 = 0;

	dev->destructor = hsr_dev_destroy;
}


/* Return true if dev is a HSR master; return false otherwise.
 */
bool is_hsr_master(struct net_device *dev)
{
	return (dev->netdev_ops->ndo_start_xmit == hsr_dev_xmit);
}

static int check_slave_ok(struct net_device *dev)
{
	/* Don't allow HSR on non-ethernet like devices */
	if ((dev->flags & IFF_LOOPBACK) || (dev->type != ARPHRD_ETHER) ||
	    (dev->addr_len != ETH_ALEN)) {
		netdev_info(dev, "Cannot use loopback or non-ethernet device as HSR slave.\n");
		return -EINVAL;
	}

	/* Don't allow enslaving hsr devices */
	if (is_hsr_master(dev)) {
		netdev_info(dev, "Cannot create trees of HSR devices.\n");
		return -EINVAL;
	}

	if (is_hsr_slave(dev)) {
		netdev_info(dev, "This device is already a HSR slave.\n");
		return -EINVAL;
	}

	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		netdev_info(dev, "HSR on top of VLAN is not yet supported in this driver.\n");
		return -EINVAL;
	}

	/* HSR over bonded devices has not been tested, but I'm not sure it
	 * won't work...
	 */

	return 0;
}


/* Default multicast address for HSR Supervision frames */
static const unsigned char def_multicast_addr[ETH_ALEN] __aligned(2) = {
	0x01, 0x15, 0x4e, 0x00, 0x01, 0x00
};

int hsr_dev_finalize(struct net_device *hsr_dev, struct net_device *slave[2],
		     unsigned char multicast_spec)
{
	struct hsr_priv *hsr;
	int i;
	int res;

	hsr = netdev_priv(hsr_dev);
	hsr->dev = hsr_dev;
	INIT_LIST_HEAD(&hsr->node_db);
	INIT_LIST_HEAD(&hsr->self_node_db);
	for (i = 0; i < HSR_MAX_SLAVE; i++)
		hsr->slave[i] = slave[i];

	spin_lock_init(&hsr->seqnr_lock);
	/* Overflow soon to find bugs easier: */
	hsr->sequence_nr = USHRT_MAX - 1024;

	init_timer(&hsr->announce_timer);
	hsr->announce_timer.function = hsr_announce;
	hsr->announce_timer.data = (unsigned long) hsr;

	ether_addr_copy(hsr->sup_multicast_addr, def_multicast_addr);
	hsr->sup_multicast_addr[ETH_ALEN - 1] = multicast_spec;

/* FIXME: should I modify the value of these?
 *
 * - hsr_dev->flags - i.e.
 *			IFF_MASTER/SLAVE?
 * - hsr_dev->priv_flags - i.e.
 *			IFF_EBRIDGE?
 *			IFF_TX_SKB_SHARING?
 *			IFF_HSR_MASTER/SLAVE?
 */

	for (i = 0; i < HSR_MAX_SLAVE; i++) {
		res = check_slave_ok(slave[i]);
		if (res)
			return res;
	}

	hsr_dev->features = slave[0]->features & slave[1]->features;
	/* Prevent recursive tx locking */
	hsr_dev->features |= NETIF_F_LLTX;
	/* VLAN on top of HSR needs testing and probably some work on
	 * hsr_header_create() etc.
	 */
	hsr_dev->features |= NETIF_F_VLAN_CHALLENGED;

	/* Set hsr_dev's MAC address to that of mac_slave1 */
	ether_addr_copy(hsr_dev->dev_addr, hsr->slave[0]->dev_addr);

	/* Set required header length */
	for (i = 0; i < HSR_MAX_SLAVE; i++) {
		if (slave[i]->hard_header_len + HSR_HLEN >
						hsr_dev->hard_header_len)
			hsr_dev->hard_header_len =
					slave[i]->hard_header_len + HSR_HLEN;
	}

	/* MTU */
	for (i = 0; i < HSR_MAX_SLAVE; i++)
		if (slave[i]->mtu - HSR_HLEN < hsr_dev->mtu)
			hsr_dev->mtu = slave[i]->mtu - HSR_HLEN;

	/* Make sure the 1st call to netif_carrier_on() gets through */
	netif_carrier_off(hsr_dev);

	/* Promiscuity */
	for (i = 0; i < HSR_MAX_SLAVE; i++) {
		res = dev_set_promiscuity(slave[i], 1);
		if (res) {
			netdev_info(hsr_dev, "Cannot set slave promiscuity (%s, %d)\n",
				    slave[i]->name, res);
			goto fail;
		}
	}

	/* Make sure we recognize frames from ourselves in hsr_rcv() */
	res = hsr_create_self_node(&hsr->self_node_db, hsr_dev->dev_addr,
				   hsr->slave[1]->dev_addr);
	if (res < 0)
		goto fail;

	res = register_netdevice(hsr_dev);
	if (res)
		goto fail;

	register_hsr_master(hsr);

	return 0;

fail:
	restore_slaves(hsr_dev);
	return res;
}
