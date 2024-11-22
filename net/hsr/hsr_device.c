// SPDX-License-Identifier: GPL-2.0
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 * This file contains device methods for creating, using and destroying
 * virtual HSR or PRP devices.
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
#include "hsr_forward.h"

static bool is_admin_up(struct net_device *dev)
{
	return dev && (dev->flags & IFF_UP);
}

static bool is_slave_up(struct net_device *dev)
{
	return dev && is_admin_up(dev) && netif_oper_up(dev);
}

static void hsr_set_operstate(struct hsr_port *master, bool has_carrier)
{
	struct net_device *dev = master->dev;

	if (!is_admin_up(dev)) {
		netdev_set_operstate(dev, IF_OPER_DOWN);
		return;
	}

	if (has_carrier)
		netdev_set_operstate(dev, IF_OPER_UP);
	else
		netdev_set_operstate(dev, IF_OPER_LOWERLAYERDOWN);
}

static bool hsr_check_carrier(struct hsr_port *master)
{
	struct hsr_port *port;

	ASSERT_RTNL();

	hsr_for_each_port(master->hsr, port) {
		if (port->type != HSR_PT_MASTER && is_slave_up(port->dev)) {
			netif_carrier_on(master->dev);
			return true;
		}
	}

	netif_carrier_off(master->dev);

	return false;
}

static void hsr_check_announce(struct net_device *hsr_dev)
{
	struct hsr_priv *hsr;

	hsr = netdev_priv(hsr_dev);
	if (netif_running(hsr_dev) && netif_oper_up(hsr_dev)) {
		/* Enable announce timer and start sending supervisory frames */
		if (!timer_pending(&hsr->announce_timer)) {
			hsr->announce_count = 0;
			mod_timer(&hsr->announce_timer, jiffies +
				  msecs_to_jiffies(HSR_ANNOUNCE_INTERVAL));
		}

		if (hsr->redbox && !timer_pending(&hsr->announce_proxy_timer))
			mod_timer(&hsr->announce_proxy_timer, jiffies +
				  msecs_to_jiffies(HSR_ANNOUNCE_INTERVAL) / 2);
	} else {
		/* Deactivate the announce timer  */
		timer_delete(&hsr->announce_timer);
		if (hsr->redbox)
			timer_delete(&hsr->announce_proxy_timer);
	}
}

void hsr_check_carrier_and_operstate(struct hsr_priv *hsr)
{
	struct hsr_port *master;
	bool has_carrier;

	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);
	/* netif_stacked_transfer_operstate() cannot be used here since
	 * it doesn't set IF_OPER_LOWERLAYERDOWN (?)
	 */
	has_carrier = hsr_check_carrier(master);
	hsr_set_operstate(master, has_carrier);
	hsr_check_announce(master->dev);
}

int hsr_get_max_mtu(struct hsr_priv *hsr)
{
	unsigned int mtu_max;
	struct hsr_port *port;

	mtu_max = ETH_DATA_LEN;
	hsr_for_each_port(hsr, port)
		if (port->type != HSR_PT_MASTER)
			mtu_max = min(port->dev->mtu, mtu_max);

	if (mtu_max < HSR_HLEN)
		return 0;
	return mtu_max - HSR_HLEN;
}

static int hsr_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	struct hsr_priv *hsr;

	hsr = netdev_priv(dev);

	if (new_mtu > hsr_get_max_mtu(hsr)) {
		netdev_info(dev, "A HSR master's MTU cannot be greater than the smallest MTU of its slaves minus the HSR Tag length (%d octets).\n",
			    HSR_HLEN);
		return -EINVAL;
	}

	WRITE_ONCE(dev->mtu, new_mtu);

	return 0;
}

static int hsr_dev_open(struct net_device *dev)
{
	struct hsr_priv *hsr;
	struct hsr_port *port;
	const char *designation = NULL;

	hsr = netdev_priv(dev);

	hsr_for_each_port(hsr, port) {
		if (port->type == HSR_PT_MASTER)
			continue;
		switch (port->type) {
		case HSR_PT_SLAVE_A:
			designation = "Slave A";
			break;
		case HSR_PT_SLAVE_B:
			designation = "Slave B";
			break;
		case HSR_PT_INTERLINK:
			designation = "Interlink";
			break;
		default:
			designation = "Unknown";
		}
		if (!is_slave_up(port->dev))
			netdev_warn(dev, "%s (%s) is not up; please bring it up to get a fully working HSR network\n",
				    designation, port->dev->name);
	}

	if (!designation)
		netdev_warn(dev, "No slave devices configured\n");

	return 0;
}

static int hsr_dev_close(struct net_device *dev)
{
	struct hsr_port *port;
	struct hsr_priv *hsr;

	hsr = netdev_priv(dev);
	hsr_for_each_port(hsr, port) {
		if (port->type == HSR_PT_MASTER)
			continue;
		switch (port->type) {
		case HSR_PT_SLAVE_A:
		case HSR_PT_SLAVE_B:
			dev_uc_unsync(port->dev, dev);
			dev_mc_unsync(port->dev, dev);
			break;
		default:
			break;
		}
	}

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

static netdev_tx_t hsr_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hsr_priv *hsr = netdev_priv(dev);
	struct hsr_port *master;

	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);
	if (master) {
		skb->dev = master->dev;
		skb_reset_mac_header(skb);
		skb_reset_mac_len(skb);
		spin_lock_bh(&hsr->seqnr_lock);
		hsr_forward_skb(skb, master);
		spin_unlock_bh(&hsr->seqnr_lock);
	} else {
		dev_core_stats_tx_dropped_inc(dev);
		dev_kfree_skb_any(skb);
	}
	return NETDEV_TX_OK;
}

static const struct header_ops hsr_header_ops = {
	.create	 = eth_header,
	.parse	 = eth_header_parse,
};

static struct sk_buff *hsr_init_skb(struct hsr_port *master)
{
	struct hsr_priv *hsr = master->hsr;
	struct sk_buff *skb;
	int hlen, tlen;

	hlen = LL_RESERVED_SPACE(master->dev);
	tlen = master->dev->needed_tailroom;
	/* skb size is same for PRP/HSR frames, only difference
	 * being, for PRP it is a trailer and for HSR it is a
	 * header
	 */
	skb = dev_alloc_skb(sizeof(struct hsr_sup_tag) +
			    sizeof(struct hsr_sup_payload) + hlen + tlen);

	if (!skb)
		return skb;

	skb_reserve(skb, hlen);
	skb->dev = master->dev;
	skb->priority = TC_PRIO_CONTROL;

	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	if (dev_hard_header(skb, skb->dev, ETH_P_PRP,
			    hsr->sup_multicast_addr,
			    skb->dev->dev_addr, skb->len) <= 0)
		goto out;

	skb_reset_mac_header(skb);
	skb_reset_mac_len(skb);

	return skb;
out:
	kfree_skb(skb);

	return NULL;
}

static void send_hsr_supervision_frame(struct hsr_port *port,
				       unsigned long *interval,
				       const unsigned char *addr)
{
	struct hsr_priv *hsr = port->hsr;
	__u8 type = HSR_TLV_LIFE_CHECK;
	struct hsr_sup_payload *hsr_sp;
	struct hsr_sup_tlv *hsr_stlv;
	struct hsr_sup_tag *hsr_stag;
	struct sk_buff *skb;

	*interval = msecs_to_jiffies(HSR_LIFE_CHECK_INTERVAL);
	if (hsr->announce_count < 3 && hsr->prot_version == 0) {
		type = HSR_TLV_ANNOUNCE;
		*interval = msecs_to_jiffies(HSR_ANNOUNCE_INTERVAL);
		hsr->announce_count++;
	}

	skb = hsr_init_skb(port);
	if (!skb) {
		netdev_warn_once(port->dev, "HSR: Could not send supervision frame\n");
		return;
	}

	hsr_stag = skb_put(skb, sizeof(struct hsr_sup_tag));
	set_hsr_stag_path(hsr_stag, (hsr->prot_version ? 0x0 : 0xf));
	set_hsr_stag_HSR_ver(hsr_stag, hsr->prot_version);

	/* From HSRv1 on we have separate supervision sequence numbers. */
	spin_lock_bh(&hsr->seqnr_lock);
	if (hsr->prot_version > 0) {
		hsr_stag->sequence_nr = htons(hsr->sup_sequence_nr);
		hsr->sup_sequence_nr++;
	} else {
		hsr_stag->sequence_nr = htons(hsr->sequence_nr);
		hsr->sequence_nr++;
	}

	hsr_stag->tlv.HSR_TLV_type = type;
	/* TODO: Why 12 in HSRv0? */
	hsr_stag->tlv.HSR_TLV_length = hsr->prot_version ?
				sizeof(struct hsr_sup_payload) : 12;

	/* Payload: MacAddressA / SAN MAC from ProxyNodeTable */
	hsr_sp = skb_put(skb, sizeof(struct hsr_sup_payload));
	ether_addr_copy(hsr_sp->macaddress_A, addr);

	if (hsr->redbox &&
	    hsr_is_node_in_db(&hsr->proxy_node_db, addr)) {
		hsr_stlv = skb_put(skb, sizeof(struct hsr_sup_tlv));
		hsr_stlv->HSR_TLV_type = PRP_TLV_REDBOX_MAC;
		hsr_stlv->HSR_TLV_length = sizeof(struct hsr_sup_payload);

		/* Payload: MacAddressRedBox */
		hsr_sp = skb_put(skb, sizeof(struct hsr_sup_payload));
		ether_addr_copy(hsr_sp->macaddress_A, hsr->macaddress_redbox);
	}

	if (skb_put_padto(skb, ETH_ZLEN)) {
		spin_unlock_bh(&hsr->seqnr_lock);
		return;
	}

	hsr_forward_skb(skb, port);
	spin_unlock_bh(&hsr->seqnr_lock);
	return;
}

static void send_prp_supervision_frame(struct hsr_port *master,
				       unsigned long *interval,
				       const unsigned char *addr)
{
	struct hsr_priv *hsr = master->hsr;
	struct hsr_sup_payload *hsr_sp;
	struct hsr_sup_tag *hsr_stag;
	struct sk_buff *skb;

	skb = hsr_init_skb(master);
	if (!skb) {
		netdev_warn_once(master->dev, "PRP: Could not send supervision frame\n");
		return;
	}

	*interval = msecs_to_jiffies(HSR_LIFE_CHECK_INTERVAL);
	hsr_stag = skb_put(skb, sizeof(struct hsr_sup_tag));
	set_hsr_stag_path(hsr_stag, (hsr->prot_version ? 0x0 : 0xf));
	set_hsr_stag_HSR_ver(hsr_stag, (hsr->prot_version ? 1 : 0));

	/* From HSRv1 on we have separate supervision sequence numbers. */
	spin_lock_bh(&hsr->seqnr_lock);
	hsr_stag->sequence_nr = htons(hsr->sup_sequence_nr);
	hsr->sup_sequence_nr++;
	hsr_stag->tlv.HSR_TLV_type = PRP_TLV_LIFE_CHECK_DD;
	hsr_stag->tlv.HSR_TLV_length = sizeof(struct hsr_sup_payload);

	/* Payload: MacAddressA */
	hsr_sp = skb_put(skb, sizeof(struct hsr_sup_payload));
	ether_addr_copy(hsr_sp->macaddress_A, master->dev->dev_addr);

	if (skb_put_padto(skb, ETH_ZLEN)) {
		spin_unlock_bh(&hsr->seqnr_lock);
		return;
	}

	hsr_forward_skb(skb, master);
	spin_unlock_bh(&hsr->seqnr_lock);
}

/* Announce (supervision frame) timer function
 */
static void hsr_announce(struct timer_list *t)
{
	struct hsr_priv *hsr;
	struct hsr_port *master;
	unsigned long interval;

	hsr = from_timer(hsr, t, announce_timer);

	rcu_read_lock();
	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);
	hsr->proto_ops->send_sv_frame(master, &interval, master->dev->dev_addr);

	if (is_admin_up(master->dev))
		mod_timer(&hsr->announce_timer, jiffies + interval);

	rcu_read_unlock();
}

/* Announce (supervision frame) timer function for RedBox
 */
static void hsr_proxy_announce(struct timer_list *t)
{
	struct hsr_priv *hsr = from_timer(hsr, t, announce_proxy_timer);
	struct hsr_port *interlink;
	unsigned long interval = 0;
	struct hsr_node *node;

	rcu_read_lock();
	/* RedBOX sends supervisory frames to HSR network with MAC addresses
	 * of SAN nodes stored in ProxyNodeTable.
	 */
	interlink = hsr_port_get_hsr(hsr, HSR_PT_INTERLINK);
	if (!interlink)
		goto done;

	list_for_each_entry_rcu(node, &hsr->proxy_node_db, mac_list) {
		if (hsr_addr_is_redbox(hsr, node->macaddress_A))
			continue;
		hsr->proto_ops->send_sv_frame(interlink, &interval,
					      node->macaddress_A);
	}

	if (is_admin_up(interlink->dev)) {
		if (!interval)
			interval = msecs_to_jiffies(HSR_ANNOUNCE_INTERVAL);

		mod_timer(&hsr->announce_proxy_timer, jiffies + interval);
	}

done:
	rcu_read_unlock();
}

void hsr_del_ports(struct hsr_priv *hsr)
{
	struct hsr_port *port;

	port = hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);
	if (port)
		hsr_del_port(port);

	port = hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);
	if (port)
		hsr_del_port(port);

	port = hsr_port_get_hsr(hsr, HSR_PT_INTERLINK);
	if (port)
		hsr_del_port(port);

	port = hsr_port_get_hsr(hsr, HSR_PT_MASTER);
	if (port)
		hsr_del_port(port);
}

static void hsr_set_rx_mode(struct net_device *dev)
{
	struct hsr_port *port;
	struct hsr_priv *hsr;

	hsr = netdev_priv(dev);

	hsr_for_each_port(hsr, port) {
		if (port->type == HSR_PT_MASTER)
			continue;
		switch (port->type) {
		case HSR_PT_SLAVE_A:
		case HSR_PT_SLAVE_B:
			dev_mc_sync_multiple(port->dev, dev);
			dev_uc_sync_multiple(port->dev, dev);
			break;
		default:
			break;
		}
	}
}

static void hsr_change_rx_flags(struct net_device *dev, int change)
{
	struct hsr_port *port;
	struct hsr_priv *hsr;

	hsr = netdev_priv(dev);

	hsr_for_each_port(hsr, port) {
		if (port->type == HSR_PT_MASTER)
			continue;
		switch (port->type) {
		case HSR_PT_SLAVE_A:
		case HSR_PT_SLAVE_B:
			if (change & IFF_ALLMULTI)
				dev_set_allmulti(port->dev,
						 dev->flags &
						 IFF_ALLMULTI ? 1 : -1);
			break;
		default:
			break;
		}
	}
}

static const struct net_device_ops hsr_device_ops = {
	.ndo_change_mtu = hsr_dev_change_mtu,
	.ndo_open = hsr_dev_open,
	.ndo_stop = hsr_dev_close,
	.ndo_start_xmit = hsr_dev_xmit,
	.ndo_change_rx_flags = hsr_change_rx_flags,
	.ndo_fix_features = hsr_fix_features,
	.ndo_set_rx_mode = hsr_set_rx_mode,
};

static const struct device_type hsr_type = {
	.name = "hsr",
};

static struct hsr_proto_ops hsr_ops = {
	.send_sv_frame = send_hsr_supervision_frame,
	.create_tagged_frame = hsr_create_tagged_frame,
	.get_untagged_frame = hsr_get_untagged_frame,
	.drop_frame = hsr_drop_frame,
	.fill_frame_info = hsr_fill_frame_info,
	.invalid_dan_ingress_frame = hsr_invalid_dan_ingress_frame,
};

static struct hsr_proto_ops prp_ops = {
	.send_sv_frame = send_prp_supervision_frame,
	.create_tagged_frame = prp_create_tagged_frame,
	.get_untagged_frame = prp_get_untagged_frame,
	.drop_frame = prp_drop_frame,
	.fill_frame_info = prp_fill_frame_info,
	.handle_san_frame = prp_handle_san_frame,
	.update_san_info = prp_update_san_info,
};

void hsr_dev_setup(struct net_device *dev)
{
	eth_hw_addr_random(dev);

	ether_setup(dev);
	dev->min_mtu = 0;
	dev->header_ops = &hsr_header_ops;
	dev->netdev_ops = &hsr_device_ops;
	SET_NETDEV_DEVTYPE(dev, &hsr_type);
	dev->priv_flags |= IFF_NO_QUEUE | IFF_DISABLE_NETPOLL;
	/* Prevent recursive tx locking */
	dev->lltx = true;
	/* Not sure about this. Taken from bridge code. netdevice.h says
	 * it means "Does not change network namespaces".
	 */
	dev->netns_local = true;

	dev->needs_free_netdev = true;

	dev->hw_features = NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HIGHDMA |
			   NETIF_F_GSO_MASK | NETIF_F_HW_CSUM |
			   NETIF_F_HW_VLAN_CTAG_TX;

	dev->features = dev->hw_features;

	/* VLAN on top of HSR needs testing and probably some work on
	 * hsr_header_create() etc.
	 */
	dev->features |= NETIF_F_VLAN_CHALLENGED;
}

/* Return true if dev is a HSR master; return false otherwise.
 */
bool is_hsr_master(struct net_device *dev)
{
	return (dev->netdev_ops->ndo_start_xmit == hsr_dev_xmit);
}
EXPORT_SYMBOL(is_hsr_master);

/* Default multicast address for HSR Supervision frames */
static const unsigned char def_multicast_addr[ETH_ALEN] __aligned(2) = {
	0x01, 0x15, 0x4e, 0x00, 0x01, 0x00
};

int hsr_dev_finalize(struct net_device *hsr_dev, struct net_device *slave[2],
		     struct net_device *interlink, unsigned char multicast_spec,
		     u8 protocol_version, struct netlink_ext_ack *extack)
{
	bool unregister = false;
	struct hsr_priv *hsr;
	int res;

	hsr = netdev_priv(hsr_dev);
	INIT_LIST_HEAD(&hsr->ports);
	INIT_LIST_HEAD(&hsr->node_db);
	INIT_LIST_HEAD(&hsr->proxy_node_db);
	spin_lock_init(&hsr->list_lock);

	eth_hw_addr_set(hsr_dev, slave[0]->dev_addr);

	/* initialize protocol specific functions */
	if (protocol_version == PRP_V1) {
		/* For PRP, lan_id has most significant 3 bits holding
		 * the net_id of PRP_LAN_ID
		 */
		hsr->net_id = PRP_LAN_ID << 1;
		hsr->proto_ops = &prp_ops;
	} else {
		hsr->proto_ops = &hsr_ops;
	}

	/* Make sure we recognize frames from ourselves in hsr_rcv() */
	res = hsr_create_self_node(hsr, hsr_dev->dev_addr,
				   slave[1]->dev_addr);
	if (res < 0)
		return res;

	spin_lock_init(&hsr->seqnr_lock);
	/* Overflow soon to find bugs easier: */
	hsr->sequence_nr = HSR_SEQNR_START;
	hsr->sup_sequence_nr = HSR_SUP_SEQNR_START;

	timer_setup(&hsr->announce_timer, hsr_announce, 0);
	timer_setup(&hsr->prune_timer, hsr_prune_nodes, 0);
	timer_setup(&hsr->prune_proxy_timer, hsr_prune_proxy_nodes, 0);
	timer_setup(&hsr->announce_proxy_timer, hsr_proxy_announce, 0);

	ether_addr_copy(hsr->sup_multicast_addr, def_multicast_addr);
	hsr->sup_multicast_addr[ETH_ALEN - 1] = multicast_spec;

	hsr->prot_version = protocol_version;

	/* Make sure the 1st call to netif_carrier_on() gets through */
	netif_carrier_off(hsr_dev);

	res = hsr_add_port(hsr, hsr_dev, HSR_PT_MASTER, extack);
	if (res)
		goto err_add_master;

	/* HSR forwarding offload supported in lower device? */
	if ((slave[0]->features & NETIF_F_HW_HSR_FWD) &&
	    (slave[1]->features & NETIF_F_HW_HSR_FWD))
		hsr->fwd_offloaded = true;

	res = register_netdevice(hsr_dev);
	if (res)
		goto err_unregister;

	unregister = true;

	res = hsr_add_port(hsr, slave[0], HSR_PT_SLAVE_A, extack);
	if (res)
		goto err_unregister;

	res = hsr_add_port(hsr, slave[1], HSR_PT_SLAVE_B, extack);
	if (res)
		goto err_unregister;

	if (interlink) {
		res = hsr_add_port(hsr, interlink, HSR_PT_INTERLINK, extack);
		if (res)
			goto err_unregister;

		hsr->redbox = true;
		ether_addr_copy(hsr->macaddress_redbox, interlink->dev_addr);
		mod_timer(&hsr->prune_proxy_timer,
			  jiffies + msecs_to_jiffies(PRUNE_PROXY_PERIOD));
	}

	hsr_debugfs_init(hsr, hsr_dev);
	mod_timer(&hsr->prune_timer, jiffies + msecs_to_jiffies(PRUNE_PERIOD));

	return 0;

err_unregister:
	hsr_del_ports(hsr);
err_add_master:
	hsr_del_self_node(hsr);

	if (unregister)
		unregister_netdevice(hsr_dev);
	return res;
}
