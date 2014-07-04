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
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include "hsr_device.h"
#include "hsr_slave.h"
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

static void hsr_set_operstate(struct hsr_port *master, bool has_carrier)
{
	if (!is_admin_up(master->dev)) {
		__hsr_set_operstate(master->dev, IF_OPER_DOWN);
		return;
	}

	if (has_carrier)
		__hsr_set_operstate(master->dev, IF_OPER_UP);
	else
		__hsr_set_operstate(master->dev, IF_OPER_LOWERLAYERDOWN);
}

static bool hsr_check_carrier(struct hsr_port *master)
{
	struct hsr_port *port;
	bool has_carrier;

	has_carrier = false;

	rcu_read_lock();
	hsr_for_each_port(master->hsr, port)
		if ((port->type != HSR_PT_MASTER) && is_slave_up(port->dev)) {
			has_carrier = true;
			break;
		}
	rcu_read_unlock();

	if (has_carrier)
		netif_carrier_on(master->dev);
	else
		netif_carrier_off(master->dev);

	return has_carrier;
}


static void hsr_check_announce(struct net_device *hsr_dev,
			       unsigned char old_operstate)
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

void hsr_check_carrier_and_operstate(struct hsr_priv *hsr)
{
	struct hsr_port *master;
	unsigned char old_operstate;
	bool has_carrier;

	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);
	/* netif_stacked_transfer_operstate() cannot be used here since
	 * it doesn't set IF_OPER_LOWERLAYERDOWN (?)
	 */
	old_operstate = master->dev->operstate;
	has_carrier = hsr_check_carrier(master);
	hsr_set_operstate(master, has_carrier);
	hsr_check_announce(master->dev, old_operstate);
}

int hsr_get_max_mtu(struct hsr_priv *hsr)
{
	unsigned int mtu_max;
	struct hsr_port *port;

	mtu_max = ETH_DATA_LEN;
	rcu_read_lock();
	hsr_for_each_port(hsr, port)
		if (port->type != HSR_PT_MASTER)
			mtu_max = min(port->dev->mtu, mtu_max);
	rcu_read_unlock();

	if (mtu_max < HSR_HLEN)
		return 0;
	return mtu_max - HSR_HLEN;
}


static int hsr_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	struct hsr_priv *hsr;
	struct hsr_port *master;

	hsr = netdev_priv(dev);
	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);

	if (new_mtu > hsr_get_max_mtu(hsr)) {
		netdev_info(master->dev, "A HSR master's MTU cannot be greater than the smallest MTU of its slaves minus the HSR Tag length (%d octets).\n",
			    HSR_HLEN);
		return -EINVAL;
	}

	dev->mtu = new_mtu;

	return 0;
}

static int hsr_dev_open(struct net_device *dev)
{
	struct hsr_priv *hsr;
	struct hsr_port *port;
	char designation;

	hsr = netdev_priv(dev);
	designation = '\0';

	rcu_read_lock();
	hsr_for_each_port(hsr, port) {
		if (port->type == HSR_PT_MASTER)
			continue;
		switch (port->type) {
		case HSR_PT_SLAVE_A:
			designation = 'A';
			break;
		case HSR_PT_SLAVE_B:
			designation = 'B';
			break;
		default:
			designation = '?';
		}
		if (!is_slave_up(port->dev))
			netdev_warn(dev, "Slave %c (%s) is not up; please bring it up to get a fully working HSR network\n",
				    designation, port->dev->name);
	}
	rcu_read_unlock();

	if (designation == '\0')
		netdev_warn(dev, "No slave devices configured\n");

	return 0;
}


static int hsr_dev_close(struct net_device *dev)
{
	/* Nothing to do here. */
	return 0;
}


static netdev_features_t hsr_features_recompute(struct hsr_priv *hsr,
						netdev_features_t features)
{
	netdev_features_t mask;
	struct hsr_port *port;

	mask = features;

	/* Mask out all features that, if supported by one device, should be
	 * enabled for all devices (see NETIF_F_ONE_FOR_ALL).
	 *
	 * Anything that's off in mask will not be enabled - so only things
	 * that were in features originally, and also is in NETIF_F_ONE_FOR_ALL,
	 * may become enabled.
	 */
	features &= ~NETIF_F_ONE_FOR_ALL;
	hsr_for_each_port(hsr, port)
		features = netdev_increment_features(features,
						     port->dev->features,
						     mask);

	return features;
}

static netdev_features_t hsr_fix_features(struct net_device *dev,
					  netdev_features_t features)
{
	struct hsr_priv *hsr = netdev_priv(dev);

	return hsr_features_recompute(hsr, features);
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

static int slave_xmit(struct hsr_priv *hsr, struct sk_buff *skb,
		      enum hsr_port_type type)
{
	struct hsr_port *port;
	struct hsr_ethhdr *hsr_ethhdr;

	hsr_ethhdr = (struct hsr_ethhdr *) skb->data;

	rcu_read_lock();
	port = hsr_port_get_hsr(hsr, type);
	if (!port) {
		rcu_read_unlock();
		return NET_XMIT_DROP;
	}
	skb->dev = port->dev;

	hsr_addr_subst_dest(port->hsr, &hsr_ethhdr->ethhdr, port);
	rcu_read_unlock();

	/* Address substitution (IEC62439-3 pp 26, 50): replace mac
	 * address of outgoing frame with that of the outgoing slave's.
	 */
	ether_addr_copy(hsr_ethhdr->ethhdr.h_source, skb->dev->dev_addr);

	return dev_queue_xmit(skb);
}

static int hsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hsr_priv *hsr;
	struct hsr_port *master;
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

	res1 = slave_xmit(hsr, skb, HSR_PT_SLAVE_A);
	if (skb2)
		res2 = slave_xmit(hsr, skb2, HSR_PT_SLAVE_B);
	else
		res2 = NET_XMIT_DROP;

	rcu_read_lock();
	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);
	if (likely(res1 == NET_XMIT_SUCCESS || res1 == NET_XMIT_CN ||
		   res2 == NET_XMIT_SUCCESS || res2 == NET_XMIT_CN)) {
		master->dev->stats.tx_packets++;
		master->dev->stats.tx_bytes += skb->len;
	} else {
		master->dev->stats.tx_dropped++;
	}
	rcu_read_unlock();

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
	struct hsr_port *master;
	struct sk_buff *skb;
	int hlen, tlen;
	struct hsr_sup_tag *hsr_stag;
	struct hsr_sup_payload *hsr_sp;
	unsigned long irqflags;
	int res;

	hsr = netdev_priv(hsr_dev);
	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);

	hlen = LL_RESERVED_SPACE(master->dev);
	tlen = master->dev->needed_tailroom;
	skb = alloc_skb(hsr_pad(sizeof(struct hsr_sup_payload)) + hlen + tlen,
			GFP_ATOMIC);

	if (skb == NULL)
		return;

	skb_reserve(skb, hlen);

	skb->dev = master->dev;
	skb->protocol = htons(ETH_P_PRP);
	skb->priority = TC_PRIO_CONTROL;

	res = dev_hard_header(skb, skb->dev, ETH_P_PRP,
			      hsr->sup_multicast_addr,
			      skb->dev->dev_addr, skb->len);
	if (res <= 0)
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
	ether_addr_copy(hsr_sp->MacAddressA, master->dev->dev_addr);

	dev_queue_xmit(skb);
	return;

out:
	WARN_ON_ONCE("HSR: Could not send supervision frame\n");
	kfree_skb(skb);
}


/* Announce (supervision frame) timer function
 */
static void hsr_announce(unsigned long data)
{
	struct hsr_priv *hsr;
	struct hsr_port *master;

	hsr = (struct hsr_priv *) data;
	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);

	if (hsr->announce_count < 3) {
		send_hsr_supervision_frame(master->dev, HSR_TLV_ANNOUNCE);
		hsr->announce_count++;
	} else {
		send_hsr_supervision_frame(master->dev, HSR_TLV_LIFE_CHECK);
	}

	if (hsr->announce_count < 3)
		hsr->announce_timer.expires = jiffies +
				msecs_to_jiffies(HSR_ANNOUNCE_INTERVAL);
	else
		hsr->announce_timer.expires = jiffies +
				msecs_to_jiffies(HSR_LIFE_CHECK_INTERVAL);

	if (is_admin_up(master->dev))
		add_timer(&hsr->announce_timer);
}


/* According to comments in the declaration of struct net_device, this function
 * is "Called from unregister, can be used to call free_netdev". Ok then...
 */
static void hsr_dev_destroy(struct net_device *hsr_dev)
{
	struct hsr_priv *hsr;
	struct hsr_port *port;

	hsr = netdev_priv(hsr_dev);
	hsr_for_each_port(hsr, port)
		hsr_del_port(port);

	del_timer_sync(&hsr->prune_timer);
	del_timer_sync(&hsr->announce_timer);

	synchronize_rcu();
	free_netdev(hsr_dev);
}

static const struct net_device_ops hsr_device_ops = {
	.ndo_change_mtu = hsr_dev_change_mtu,
	.ndo_open = hsr_dev_open,
	.ndo_stop = hsr_dev_close,
	.ndo_start_xmit = hsr_dev_xmit,
	.ndo_fix_features = hsr_fix_features,
};


void hsr_dev_setup(struct net_device *dev)
{
	random_ether_addr(dev->dev_addr);

	ether_setup(dev);
	dev->header_ops		 = &hsr_header_ops;
	dev->netdev_ops		 = &hsr_device_ops;
	dev->tx_queue_len	 = 0;

	dev->destructor = hsr_dev_destroy;

	dev->hw_features = NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HIGHDMA |
			   NETIF_F_GSO_MASK | NETIF_F_HW_CSUM |
			   NETIF_F_HW_VLAN_CTAG_TX;

	dev->features = dev->hw_features;

	/* Prevent recursive tx locking */
	dev->features |= NETIF_F_LLTX;
	/* VLAN on top of HSR needs testing and probably some work on
	 * hsr_header_create() etc.
	 */
	dev->features |= NETIF_F_VLAN_CHALLENGED;
}


/* Return true if dev is a HSR master; return false otherwise.
 */
inline bool is_hsr_master(struct net_device *dev)
{
	return (dev->netdev_ops->ndo_start_xmit == hsr_dev_xmit);
}

/* Default multicast address for HSR Supervision frames */
static const unsigned char def_multicast_addr[ETH_ALEN] __aligned(2) = {
	0x01, 0x15, 0x4e, 0x00, 0x01, 0x00
};

int hsr_dev_finalize(struct net_device *hsr_dev, struct net_device *slave[2],
		     unsigned char multicast_spec)
{
	struct hsr_priv *hsr;
	struct hsr_port *port;
	int res;

	hsr = netdev_priv(hsr_dev);
	INIT_LIST_HEAD(&hsr->ports);
	INIT_LIST_HEAD(&hsr->node_db);
	INIT_LIST_HEAD(&hsr->self_node_db);

	ether_addr_copy(hsr_dev->dev_addr, slave[0]->dev_addr);

	/* Make sure we recognize frames from ourselves in hsr_rcv() */
	res = hsr_create_self_node(&hsr->self_node_db, hsr_dev->dev_addr,
				   slave[1]->dev_addr);
	if (res < 0)
		return res;

	spin_lock_init(&hsr->seqnr_lock);
	/* Overflow soon to find bugs easier: */
	hsr->sequence_nr = USHRT_MAX - 1024;

	init_timer(&hsr->announce_timer);
	hsr->announce_timer.function = hsr_announce;
	hsr->announce_timer.data = (unsigned long) hsr;

	init_timer(&hsr->prune_timer);
	hsr->prune_timer.function = hsr_prune_nodes;
	hsr->prune_timer.data = (unsigned long) hsr;

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

	/* Make sure the 1st call to netif_carrier_on() gets through */
	netif_carrier_off(hsr_dev);

	res = hsr_add_port(hsr, hsr_dev, HSR_PT_MASTER);
	if (res)
		return res;

	res = register_netdevice(hsr_dev);
	if (res)
		goto fail;

	res = hsr_add_port(hsr, slave[0], HSR_PT_SLAVE_A);
	if (res)
		goto fail;
	res = hsr_add_port(hsr, slave[1], HSR_PT_SLAVE_B);
	if (res)
		goto fail;

	hsr->prune_timer.expires = jiffies + msecs_to_jiffies(PRUNE_PERIOD);
	add_timer(&hsr->prune_timer);

	return 0;

fail:
	hsr_for_each_port(hsr, port)
		hsr_del_port(port);

	return res;
}
