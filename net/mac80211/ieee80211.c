/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/rtnetlink.h>
#include <linux/bitmap.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>

#include "ieee80211_i.h"
#include "ieee80211_rate.h"
#include "wep.h"
#include "wme.h"
#include "aes_ccm.h"
#include "ieee80211_led.h"
#include "cfg.h"
#include "debugfs.h"
#include "debugfs_netdev.h"

/*
 * For seeing transmitted packets on monitor interfaces
 * we have a radiotap header too.
 */
struct ieee80211_tx_status_rtap_hdr {
	struct ieee80211_radiotap_header hdr;
	__le16 tx_flags;
	u8 data_retries;
} __attribute__ ((packed));

/* common interface routines */

static int header_parse_80211(const struct sk_buff *skb, unsigned char *haddr)
{
	memcpy(haddr, skb_mac_header(skb) + 10, ETH_ALEN); /* addr2 */
	return ETH_ALEN;
}

/* must be called under mdev tx lock */
static void ieee80211_configure_filter(struct ieee80211_local *local)
{
	unsigned int changed_flags;
	unsigned int new_flags = 0;

	if (atomic_read(&local->iff_promiscs))
		new_flags |= FIF_PROMISC_IN_BSS;

	if (atomic_read(&local->iff_allmultis))
		new_flags |= FIF_ALLMULTI;

	if (local->monitors)
		new_flags |= FIF_CONTROL |
			     FIF_OTHER_BSS |
			     FIF_BCN_PRBRESP_PROMISC;

	changed_flags = local->filter_flags ^ new_flags;

	/* be a bit nasty */
	new_flags |= (1<<31);

	local->ops->configure_filter(local_to_hw(local),
				     changed_flags, &new_flags,
				     local->mdev->mc_count,
				     local->mdev->mc_list);

	WARN_ON(new_flags & (1<<31));

	local->filter_flags = new_flags & ~(1<<31);
}

/* master interface */

static int ieee80211_master_open(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	int res = -EOPNOTSUPP;

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->dev != dev && netif_running(sdata->dev)) {
			res = 0;
			break;
		}
	}
	return res;
}

static int ieee80211_master_stop(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(sdata, &local->interfaces, list)
		if (sdata->dev != dev && netif_running(sdata->dev))
			dev_close(sdata->dev);

	return 0;
}

static void ieee80211_master_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	ieee80211_configure_filter(local);
}

/* regular interfaces */

static int ieee80211_change_mtu(struct net_device *dev, int new_mtu)
{
	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.3 frames. */
	if (new_mtu < 256 || new_mtu > IEEE80211_MAX_DATA_LEN - 24 - 6) {
		printk(KERN_WARNING "%s: invalid MTU %d\n",
		       dev->name, new_mtu);
		return -EINVAL;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: setting MTU %d\n", dev->name, new_mtu);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	dev->mtu = new_mtu;
	return 0;
}

static inline int identical_mac_addr_allowed(int type1, int type2)
{
	return (type1 == IEEE80211_IF_TYPE_MNTR ||
		type2 == IEEE80211_IF_TYPE_MNTR ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_WDS) ||
		(type1 == IEEE80211_IF_TYPE_WDS &&
		 (type2 == IEEE80211_IF_TYPE_WDS ||
		  type2 == IEEE80211_IF_TYPE_AP)) ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_VLAN) ||
		(type1 == IEEE80211_IF_TYPE_VLAN &&
		 (type2 == IEEE80211_IF_TYPE_AP ||
		  type2 == IEEE80211_IF_TYPE_VLAN)));
}

static int ieee80211_open(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata, *nsdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_init_conf conf;
	int res;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(nsdata, &local->interfaces, list) {
		struct net_device *ndev = nsdata->dev;

		if (ndev != dev && ndev != local->mdev && netif_running(ndev) &&
		    compare_ether_addr(dev->dev_addr, ndev->dev_addr) == 0) {
			/*
			 * check whether it may have the same address
			 */
			if (!identical_mac_addr_allowed(sdata->type,
							nsdata->type))
				return -ENOTUNIQ;

			/*
			 * can only add VLANs to enabled APs
			 */
			if (sdata->type == IEEE80211_IF_TYPE_VLAN &&
			    nsdata->type == IEEE80211_IF_TYPE_AP &&
			    netif_running(nsdata->dev))
				sdata->u.vlan.ap = nsdata;
		}
	}

	switch (sdata->type) {
	case IEEE80211_IF_TYPE_WDS:
		if (is_zero_ether_addr(sdata->u.wds.remote_addr))
			return -ENOLINK;
		break;
	case IEEE80211_IF_TYPE_VLAN:
		if (!sdata->u.vlan.ap)
			return -ENOLINK;
		break;
	case IEEE80211_IF_TYPE_AP:
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_MNTR:
	case IEEE80211_IF_TYPE_IBSS:
		/* no special treatment */
		break;
	case IEEE80211_IF_TYPE_INVALID:
		/* cannot happen */
		WARN_ON(1);
		break;
	}

	if (local->open_count == 0) {
		res = 0;
		if (local->ops->start)
			res = local->ops->start(local_to_hw(local));
		if (res)
			return res;
	}

	switch (sdata->type) {
	case IEEE80211_IF_TYPE_VLAN:
		list_add(&sdata->u.vlan.list, &sdata->u.vlan.ap->u.ap.vlans);
		/* no need to tell driver */
		break;
	case IEEE80211_IF_TYPE_MNTR:
		/* must be before the call to ieee80211_configure_filter */
		local->monitors++;
		if (local->monitors == 1) {
			netif_tx_lock_bh(local->mdev);
			ieee80211_configure_filter(local);
			netif_tx_unlock_bh(local->mdev);

			local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;
			ieee80211_hw_config(local);
		}
		break;
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.flags &= ~IEEE80211_STA_PREV_BSSID_SET;
		/* fall through */
	default:
		conf.if_id = dev->ifindex;
		conf.type = sdata->type;
		conf.mac_addr = dev->dev_addr;
		res = local->ops->add_interface(local_to_hw(local), &conf);
		if (res && !local->open_count && local->ops->stop)
			local->ops->stop(local_to_hw(local));
		if (res)
			return res;

		ieee80211_if_config(dev);
		ieee80211_reset_erp_info(dev);
		ieee80211_enable_keys(sdata);

		if (sdata->type == IEEE80211_IF_TYPE_STA &&
		    !(sdata->flags & IEEE80211_SDATA_USERSPACE_MLME))
			netif_carrier_off(dev);
		else
			netif_carrier_on(dev);
	}

	if (local->open_count == 0) {
		res = dev_open(local->mdev);
		WARN_ON(res);
		tasklet_enable(&local->tx_pending_tasklet);
		tasklet_enable(&local->tasklet);
	}

	local->open_count++;

	netif_start_queue(dev);

	return 0;
}

static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_init_conf conf;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	netif_stop_queue(dev);

	dev_mc_unsync(local->mdev, dev);

	/* down all dependent devices, that is VLANs */
	if (sdata->type == IEEE80211_IF_TYPE_AP) {
		struct ieee80211_sub_if_data *vlan, *tmp;

		list_for_each_entry_safe(vlan, tmp, &sdata->u.ap.vlans,
					 u.vlan.list)
			dev_close(vlan->dev);
		WARN_ON(!list_empty(&sdata->u.ap.vlans));
	}

	local->open_count--;

	switch (sdata->type) {
	case IEEE80211_IF_TYPE_VLAN:
		list_del(&sdata->u.vlan.list);
		sdata->u.vlan.ap = NULL;
		/* no need to tell driver */
		break;
	case IEEE80211_IF_TYPE_MNTR:
		local->monitors--;
		if (local->monitors == 0) {
			netif_tx_lock_bh(local->mdev);
			ieee80211_configure_filter(local);
			netif_tx_unlock_bh(local->mdev);

			local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;
			ieee80211_hw_config(local);
		}
		break;
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.state = IEEE80211_DISABLED;
		del_timer_sync(&sdata->u.sta.timer);
		/*
		 * When we get here, the interface is marked down.
		 * Call synchronize_rcu() to wait for the RX path
		 * should it be using the interface and enqueuing
		 * frames at this very time on another CPU.
		 */
		synchronize_rcu();
		skb_queue_purge(&sdata->u.sta.skb_queue);

		if (!local->ops->hw_scan &&
		    local->scan_dev == sdata->dev) {
			local->sta_scanning = 0;
			cancel_delayed_work(&local->scan_work);
		}
		flush_workqueue(local->hw.workqueue);
		/* fall through */
	default:
		conf.if_id = dev->ifindex;
		conf.type = sdata->type;
		conf.mac_addr = dev->dev_addr;
		/* disable all keys for as long as this netdev is down */
		ieee80211_disable_keys(sdata);
		local->ops->remove_interface(local_to_hw(local), &conf);
	}

	if (local->open_count == 0) {
		if (netif_running(local->mdev))
			dev_close(local->mdev);

		if (local->ops->stop)
			local->ops->stop(local_to_hw(local));

		tasklet_disable(&local->tx_pending_tasklet);
		tasklet_disable(&local->tasklet);
	}

	return 0;
}

static void ieee80211_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int allmulti, promisc, sdata_allmulti, sdata_promisc;

	allmulti = !!(dev->flags & IFF_ALLMULTI);
	promisc = !!(dev->flags & IFF_PROMISC);
	sdata_allmulti = sdata->flags & IEEE80211_SDATA_ALLMULTI;
	sdata_promisc = sdata->flags & IEEE80211_SDATA_PROMISC;

	if (allmulti != sdata_allmulti) {
		if (dev->flags & IFF_ALLMULTI)
			atomic_inc(&local->iff_allmultis);
		else
			atomic_dec(&local->iff_allmultis);
		sdata->flags ^= IEEE80211_SDATA_ALLMULTI;
	}

	if (promisc != sdata_promisc) {
		if (dev->flags & IFF_PROMISC)
			atomic_inc(&local->iff_promiscs);
		else
			atomic_dec(&local->iff_promiscs);
		sdata->flags ^= IEEE80211_SDATA_PROMISC;
	}

	dev_mc_sync(local->mdev, dev);
}

static const struct header_ops ieee80211_header_ops = {
	.create		= eth_header,
	.parse		= header_parse_80211,
	.rebuild	= eth_rebuild_header,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
};

/* Must not be called for mdev */
void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->header_ops = &ieee80211_header_ops;
	dev->hard_start_xmit = ieee80211_subif_start_xmit;
	dev->wireless_handlers = &ieee80211_iw_handler_def;
	dev->set_multicast_list = ieee80211_set_multicast_list;
	dev->change_mtu = ieee80211_change_mtu;
	dev->open = ieee80211_open;
	dev->stop = ieee80211_stop;
	dev->destructor = ieee80211_if_free;
}

/* WDS specialties */

int ieee80211_if_update_wds(struct net_device *dev, u8 *remote_addr)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;
	DECLARE_MAC_BUF(mac);

	if (compare_ether_addr(remote_addr, sdata->u.wds.remote_addr) == 0)
		return 0;

	/* Create STA entry for the new peer */
	sta = sta_info_add(local, dev, remote_addr, GFP_KERNEL);
	if (!sta)
		return -ENOMEM;
	sta_info_put(sta);

	/* Remove STA entry for the old peer */
	sta = sta_info_get(local, sdata->u.wds.remote_addr);
	if (sta) {
		sta_info_free(sta);
		sta_info_put(sta);
	} else {
		printk(KERN_DEBUG "%s: could not find STA entry for WDS link "
		       "peer %s\n",
		       dev->name, print_mac(mac, sdata->u.wds.remote_addr));
	}

	/* Update WDS link data */
	memcpy(&sdata->u.wds.remote_addr, remote_addr, ETH_ALEN);

	return 0;
}

/* everything else */

static int __ieee80211_if_config(struct net_device *dev,
				 struct sk_buff *beacon,
				 struct ieee80211_tx_control *control)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_conf conf;

	if (!local->ops->config_interface || !netif_running(dev))
		return 0;

	memset(&conf, 0, sizeof(conf));
	conf.type = sdata->type;
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		conf.bssid = sdata->u.sta.bssid;
		conf.ssid = sdata->u.sta.ssid;
		conf.ssid_len = sdata->u.sta.ssid_len;
	} else if (sdata->type == IEEE80211_IF_TYPE_AP) {
		conf.ssid = sdata->u.ap.ssid;
		conf.ssid_len = sdata->u.ap.ssid_len;
		conf.beacon = beacon;
		conf.beacon_control = control;
	}
	return local->ops->config_interface(local_to_hw(local),
					   dev->ifindex, &conf);
}

int ieee80211_if_config(struct net_device *dev)
{
	return __ieee80211_if_config(dev, NULL, NULL);
}

int ieee80211_if_config_beacon(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_control control;
	struct sk_buff *skb;

	if (!(local->hw.flags & IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE))
		return 0;
	skb = ieee80211_beacon_get(local_to_hw(local), dev->ifindex, &control);
	if (!skb)
		return -ENOMEM;
	return __ieee80211_if_config(dev, skb, &control);
}

int ieee80211_hw_config(struct ieee80211_local *local)
{
	struct ieee80211_hw_mode *mode;
	struct ieee80211_channel *chan;
	int ret = 0;

	if (local->sta_scanning) {
		chan = local->scan_channel;
		mode = local->scan_hw_mode;
	} else {
		chan = local->oper_channel;
		mode = local->oper_hw_mode;
	}

	local->hw.conf.channel = chan->chan;
	local->hw.conf.channel_val = chan->val;
	if (!local->hw.conf.power_level) {
		local->hw.conf.power_level = chan->power_level;
	} else {
		local->hw.conf.power_level = min(chan->power_level,
						 local->hw.conf.power_level);
	}
	local->hw.conf.freq = chan->freq;
	local->hw.conf.phymode = mode->mode;
	local->hw.conf.antenna_max = chan->antenna_max;
	local->hw.conf.chan = chan;
	local->hw.conf.mode = mode;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "HW CONFIG: channel=%d freq=%d "
	       "phymode=%d\n", local->hw.conf.channel, local->hw.conf.freq,
	       local->hw.conf.phymode);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

	if (local->open_count)
		ret = local->ops->config(local_to_hw(local), &local->hw.conf);

	return ret;
}

void ieee80211_erp_info_change_notify(struct net_device *dev, u8 changes)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (local->ops->erp_ie_changed)
		local->ops->erp_ie_changed(local_to_hw(local), changes,
			!!(sdata->flags & IEEE80211_SDATA_USE_PROTECTION),
			!(sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE));
}

void ieee80211_reset_erp_info(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sdata->flags &= ~(IEEE80211_SDATA_USE_PROTECTION |
			IEEE80211_SDATA_SHORT_PREAMBLE);
	ieee80211_erp_info_change_notify(dev,
					 IEEE80211_ERP_CHANGE_PROTECTION |
					 IEEE80211_ERP_CHANGE_PREAMBLE);
}

void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb,
				 struct ieee80211_tx_status *status)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_status *saved;
	int tmp;

	skb->dev = local->mdev;
	saved = kmalloc(sizeof(struct ieee80211_tx_status), GFP_ATOMIC);
	if (unlikely(!saved)) {
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping tx status", skb->dev->name);
		/* should be dev_kfree_skb_irq, but due to this function being
		 * named _irqsafe instead of just _irq we can't be sure that
		 * people won't call it from non-irq contexts */
		dev_kfree_skb_any(skb);
		return;
	}
	memcpy(saved, status, sizeof(struct ieee80211_tx_status));
	/* copy pointer to saved status into skb->cb for use by tasklet */
	memcpy(skb->cb, &saved, sizeof(saved));

	skb->pkt_type = IEEE80211_TX_STATUS_MSG;
	skb_queue_tail(status->control.flags & IEEE80211_TXCTL_REQ_TX_STATUS ?
		       &local->skb_queue : &local->skb_queue_unreliable, skb);
	tmp = skb_queue_len(&local->skb_queue) +
		skb_queue_len(&local->skb_queue_unreliable);
	while (tmp > IEEE80211_IRQSAFE_QUEUE_LIMIT &&
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		memcpy(&saved, skb->cb, sizeof(saved));
		kfree(saved);
		dev_kfree_skb_irq(skb);
		tmp--;
		I802_DEBUG_INC(local->tx_status_drop);
	}
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_tx_status_irqsafe);

static void ieee80211_tasklet_handler(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sk_buff *skb;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_tx_status *tx_status;

	while ((skb = skb_dequeue(&local->skb_queue)) ||
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		switch (skb->pkt_type) {
		case IEEE80211_RX_MSG:
			/* status is in skb->cb */
			memcpy(&rx_status, skb->cb, sizeof(rx_status));
			/* Clear skb->type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			__ieee80211_rx(local_to_hw(local), skb, &rx_status);
			break;
		case IEEE80211_TX_STATUS_MSG:
			/* get pointer to saved status out of skb->cb */
			memcpy(&tx_status, skb->cb, sizeof(tx_status));
			skb->pkt_type = 0;
			ieee80211_tx_status(local_to_hw(local),
					    skb, tx_status);
			kfree(tx_status);
			break;
		default: /* should never get here! */
			printk(KERN_ERR "%s: Unknown message type (%d)\n",
			       wiphy_name(local->hw.wiphy), skb->pkt_type);
			dev_kfree_skb(skb);
			break;
		}
	}
}

/* Remove added headers (e.g., QoS control), encryption header/MIC, etc. to
 * make a prepared TX frame (one that has been given to hw) to look like brand
 * new IEEE 802.11 frame that is ready to go through TX processing again.
 * Also, tx_packet_data in cb is restored from tx_control. */
static void ieee80211_remove_tx_extra(struct ieee80211_local *local,
				      struct ieee80211_key *key,
				      struct sk_buff *skb,
				      struct ieee80211_tx_control *control)
{
	int hdrlen, iv_len, mic_len;
	struct ieee80211_tx_packet_data *pkt_data;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	pkt_data->ifindex = control->ifindex;
	pkt_data->flags = 0;
	if (control->flags & IEEE80211_TXCTL_REQ_TX_STATUS)
		pkt_data->flags |= IEEE80211_TXPD_REQ_TX_STATUS;
	if (control->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT)
		pkt_data->flags |= IEEE80211_TXPD_DO_NOT_ENCRYPT;
	if (control->flags & IEEE80211_TXCTL_REQUEUE)
		pkt_data->flags |= IEEE80211_TXPD_REQUEUE;
	pkt_data->queue = control->queue;

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);

	if (!key)
		goto no_key;

	switch (key->conf.alg) {
	case ALG_WEP:
		iv_len = WEP_IV_LEN;
		mic_len = WEP_ICV_LEN;
		break;
	case ALG_TKIP:
		iv_len = TKIP_IV_LEN;
		mic_len = TKIP_ICV_LEN;
		break;
	case ALG_CCMP:
		iv_len = CCMP_HDR_LEN;
		mic_len = CCMP_MIC_LEN;
		break;
	default:
		goto no_key;
	}

	if (skb->len >= mic_len &&
	    !(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		skb_trim(skb, skb->len - mic_len);
	if (skb->len >= iv_len && skb->len > hdrlen) {
		memmove(skb->data + iv_len, skb->data, hdrlen);
		skb_pull(skb, iv_len);
	}

no_key:
	{
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		u16 fc = le16_to_cpu(hdr->frame_control);
		if ((fc & 0x8C) == 0x88) /* QoS Control Field */ {
			fc &= ~IEEE80211_STYPE_QOS_DATA;
			hdr->frame_control = cpu_to_le16(fc);
			memmove(skb->data + 2, skb->data, hdrlen - 2);
			skb_pull(skb, 2);
		}
	}
}

void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb,
			 struct ieee80211_tx_status *status)
{
	struct sk_buff *skb2;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = hw_to_local(hw);
	u16 frag, type;
	struct ieee80211_tx_status_rtap_hdr *rthdr;
	struct ieee80211_sub_if_data *sdata;
	int monitors;

	if (!status) {
		printk(KERN_ERR
		       "%s: ieee80211_tx_status called with NULL status\n",
		       wiphy_name(local->hw.wiphy));
		dev_kfree_skb(skb);
		return;
	}

	if (status->excessive_retries) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			if (sta->flags & WLAN_STA_PS) {
				/* The STA is in power save mode, so assume
				 * that this TX packet failed because of that.
				 */
				status->excessive_retries = 0;
				status->flags |= IEEE80211_TX_STATUS_TX_FILTERED;
			}
			sta_info_put(sta);
		}
	}

	if (status->flags & IEEE80211_TX_STATUS_TX_FILTERED) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			sta->tx_filtered_count++;

			/* Clear the TX filter mask for this STA when sending
			 * the next packet. If the STA went to power save mode,
			 * this will happen when it is waking up for the next
			 * time. */
			sta->clear_dst_mask = 1;

			/* TODO: Is the WLAN_STA_PS flag always set here or is
			 * the race between RX and TX status causing some
			 * packets to be filtered out before 80211.o gets an
			 * update for PS status? This seems to be the case, so
			 * no changes are likely to be needed. */
			if (sta->flags & WLAN_STA_PS &&
			    skb_queue_len(&sta->tx_filtered) <
			    STA_MAX_TX_BUFFER) {
				ieee80211_remove_tx_extra(local, sta->key,
							  skb,
							  &status->control);
				skb_queue_tail(&sta->tx_filtered, skb);
			} else if (!(sta->flags & WLAN_STA_PS) &&
				   !(status->control.flags & IEEE80211_TXCTL_REQUEUE)) {
				/* Software retry the packet once */
				status->control.flags |= IEEE80211_TXCTL_REQUEUE;
				ieee80211_remove_tx_extra(local, sta->key,
							  skb,
							  &status->control);
				dev_queue_xmit(skb);
			} else {
				if (net_ratelimit()) {
					printk(KERN_DEBUG "%s: dropped TX "
					       "filtered frame queue_len=%d "
					       "PS=%d @%lu\n",
					       wiphy_name(local->hw.wiphy),
					       skb_queue_len(
						       &sta->tx_filtered),
					       !!(sta->flags & WLAN_STA_PS),
					       jiffies);
				}
				dev_kfree_skb(skb);
			}
			sta_info_put(sta);
			return;
		}
	} else {
		/* FIXME: STUPID to call this with both local and local->mdev */
		rate_control_tx_status(local, local->mdev, skb, status);
	}

	ieee80211_led_tx(local, 0);

	/* SNMP counters
	 * Fragments are passed to low-level drivers as separate skbs, so these
	 * are actually fragments, not frames. Update frame counters only for
	 * the first fragment of the frame. */

	frag = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
	type = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_FTYPE;

	if (status->flags & IEEE80211_TX_STATUS_ACK) {
		if (frag == 0) {
			local->dot11TransmittedFrameCount++;
			if (is_multicast_ether_addr(hdr->addr1))
				local->dot11MulticastTransmittedFrameCount++;
			if (status->retry_count > 0)
				local->dot11RetryCount++;
			if (status->retry_count > 1)
				local->dot11MultipleRetryCount++;
		}

		/* This counter shall be incremented for an acknowledged MPDU
		 * with an individual address in the address 1 field or an MPDU
		 * with a multicast address in the address 1 field of type Data
		 * or Management. */
		if (!is_multicast_ether_addr(hdr->addr1) ||
		    type == IEEE80211_FTYPE_DATA ||
		    type == IEEE80211_FTYPE_MGMT)
			local->dot11TransmittedFragmentCount++;
	} else {
		if (frag == 0)
			local->dot11FailedCount++;
	}

	/* this was a transmitted frame, but now we want to reuse it */
	skb_orphan(skb);

	if (!local->monitors) {
		dev_kfree_skb(skb);
		return;
	}

	/* send frame to monitor interfaces now */

	if (skb_headroom(skb) < sizeof(*rthdr)) {
		printk(KERN_ERR "ieee80211_tx_status: headroom too small\n");
		dev_kfree_skb(skb);
		return;
	}

	rthdr = (struct ieee80211_tx_status_rtap_hdr*)
				skb_push(skb, sizeof(*rthdr));

	memset(rthdr, 0, sizeof(*rthdr));
	rthdr->hdr.it_len = cpu_to_le16(sizeof(*rthdr));
	rthdr->hdr.it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_TX_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_DATA_RETRIES));

	if (!(status->flags & IEEE80211_TX_STATUS_ACK) &&
	    !is_multicast_ether_addr(hdr->addr1))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_FAIL);

	if ((status->control.flags & IEEE80211_TXCTL_USE_RTS_CTS) &&
	    (status->control.flags & IEEE80211_TXCTL_USE_CTS_PROTECT))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_CTS);
	else if (status->control.flags & IEEE80211_TXCTL_USE_RTS_CTS)
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_RTS);

	rthdr->data_retries = status->retry_count;

	rcu_read_lock();
	monitors = local->monitors;
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		/*
		 * Using the monitors counter is possibly racy, but
		 * if the value is wrong we simply either clone the skb
		 * once too much or forget sending it to one monitor iface
		 * The latter case isn't nice but fixing the race is much
		 * more complicated.
		 */
		if (!monitors || !skb)
			goto out;

		if (sdata->type == IEEE80211_IF_TYPE_MNTR) {
			if (!netif_running(sdata->dev))
				continue;
			monitors--;
			if (monitors)
				skb2 = skb_clone(skb, GFP_ATOMIC);
			else
				skb2 = NULL;
			skb->dev = sdata->dev;
			/* XXX: is this sufficient for BPF? */
			skb_set_mac_header(skb, 0);
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->pkt_type = PACKET_OTHERHOST;
			skb->protocol = htons(ETH_P_802_2);
			memset(skb->cb, 0, sizeof(skb->cb));
			netif_rx(skb);
			skb = skb2;
		}
	}
 out:
	rcu_read_unlock();
	if (skb)
		dev_kfree_skb(skb);
}
EXPORT_SYMBOL(ieee80211_tx_status);

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops)
{
	struct net_device *mdev;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	int priv_size;
	struct wiphy *wiphy;

	/* Ensure 32-byte alignment of our private data and hw private data.
	 * We use the wiphy priv data for both our ieee80211_local and for
	 * the driver's private data
	 *
	 * In memory it'll be like this:
	 *
	 * +-------------------------+
	 * | struct wiphy	    |
	 * +-------------------------+
	 * | struct ieee80211_local  |
	 * +-------------------------+
	 * | driver's private data   |
	 * +-------------------------+
	 *
	 */
	priv_size = ((sizeof(struct ieee80211_local) +
		      NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST) +
		    priv_data_len;

	wiphy = wiphy_new(&mac80211_config_ops, priv_size);

	if (!wiphy)
		return NULL;

	wiphy->privid = mac80211_wiphy_privid;

	local = wiphy_priv(wiphy);
	local->hw.wiphy = wiphy;

	local->hw.priv = (char *)local +
			 ((sizeof(struct ieee80211_local) +
			   NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST);

	BUG_ON(!ops->tx);
	BUG_ON(!ops->start);
	BUG_ON(!ops->stop);
	BUG_ON(!ops->config);
	BUG_ON(!ops->add_interface);
	BUG_ON(!ops->remove_interface);
	BUG_ON(!ops->configure_filter);
	local->ops = ops;

	/* for now, mdev needs sub_if_data :/ */
	mdev = alloc_netdev(sizeof(struct ieee80211_sub_if_data),
			    "wmaster%d", ether_setup);
	if (!mdev) {
		wiphy_free(wiphy);
		return NULL;
	}

	sdata = IEEE80211_DEV_TO_SUB_IF(mdev);
	mdev->ieee80211_ptr = &sdata->wdev;
	sdata->wdev.wiphy = wiphy;

	local->hw.queues = 1; /* default */

	local->mdev = mdev;
	local->rx_pre_handlers = ieee80211_rx_pre_handlers;
	local->rx_handlers = ieee80211_rx_handlers;
	local->tx_handlers = ieee80211_tx_handlers;

	local->bridge_packets = 1;

	local->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	local->fragmentation_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	local->short_retry_limit = 7;
	local->long_retry_limit = 4;
	local->hw.conf.radio_enabled = 1;

	local->enabled_modes = ~0;

	INIT_LIST_HEAD(&local->modes_list);

	INIT_LIST_HEAD(&local->interfaces);

	INIT_DELAYED_WORK(&local->scan_work, ieee80211_sta_scan_work);
	ieee80211_rx_bss_list_init(mdev);

	sta_info_init(local);

	mdev->hard_start_xmit = ieee80211_master_start_xmit;
	mdev->open = ieee80211_master_open;
	mdev->stop = ieee80211_master_stop;
	mdev->type = ARPHRD_IEEE80211;
	mdev->header_ops = &ieee80211_header_ops;
	mdev->set_multicast_list = ieee80211_master_set_multicast_list;

	sdata->type = IEEE80211_IF_TYPE_AP;
	sdata->dev = mdev;
	sdata->local = local;
	sdata->u.ap.force_unicast_rateidx = -1;
	sdata->u.ap.max_ratectrl_rateidx = -1;
	ieee80211_if_sdata_init(sdata);
	/* no RCU needed since we're still during init phase */
	list_add_tail(&sdata->list, &local->interfaces);

	tasklet_init(&local->tx_pending_tasklet, ieee80211_tx_pending,
		     (unsigned long)local);
	tasklet_disable(&local->tx_pending_tasklet);

	tasklet_init(&local->tasklet,
		     ieee80211_tasklet_handler,
		     (unsigned long) local);
	tasklet_disable(&local->tasklet);

	skb_queue_head_init(&local->skb_queue);
	skb_queue_head_init(&local->skb_queue_unreliable);

	return local_to_hw(local);
}
EXPORT_SYMBOL(ieee80211_alloc_hw);

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	const char *name;
	int result;

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		return result;

	name = wiphy_dev(local->hw.wiphy)->driver->name;
	local->hw.workqueue = create_singlethread_workqueue(name);
	if (!local->hw.workqueue) {
		result = -ENOMEM;
		goto fail_workqueue;
	}

	/*
	 * The hardware needs headroom for sending the frame,
	 * and we need some headroom for passing the frame to monitor
	 * interfaces, but never both at the same time.
	 */
	local->tx_headroom = max_t(unsigned int , local->hw.extra_tx_headroom,
				   sizeof(struct ieee80211_tx_status_rtap_hdr));

	debugfs_hw_add(local);

	local->hw.conf.beacon_int = 1000;

	local->wstats_flags |= local->hw.max_rssi ?
			       IW_QUAL_LEVEL_UPDATED : IW_QUAL_LEVEL_INVALID;
	local->wstats_flags |= local->hw.max_signal ?
			       IW_QUAL_QUAL_UPDATED : IW_QUAL_QUAL_INVALID;
	local->wstats_flags |= local->hw.max_noise ?
			       IW_QUAL_NOISE_UPDATED : IW_QUAL_NOISE_INVALID;
	if (local->hw.max_rssi < 0 || local->hw.max_noise < 0)
		local->wstats_flags |= IW_QUAL_DBM;

	result = sta_info_start(local);
	if (result < 0)
		goto fail_sta_info;

	rtnl_lock();
	result = dev_alloc_name(local->mdev, local->mdev->name);
	if (result < 0)
		goto fail_dev;

	memcpy(local->mdev->dev_addr, local->hw.wiphy->perm_addr, ETH_ALEN);
	SET_NETDEV_DEV(local->mdev, wiphy_dev(local->hw.wiphy));

	result = register_netdevice(local->mdev);
	if (result < 0)
		goto fail_dev;

	ieee80211_debugfs_add_netdev(IEEE80211_DEV_TO_SUB_IF(local->mdev));
	ieee80211_if_set_type(local->mdev, IEEE80211_IF_TYPE_AP);

	result = ieee80211_init_rate_ctrl_alg(local, NULL);
	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize rate control "
		       "algorithm\n", wiphy_name(local->hw.wiphy));
		goto fail_rate;
	}

	result = ieee80211_wep_init(local);

	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize wep\n",
		       wiphy_name(local->hw.wiphy));
		goto fail_wep;
	}

	ieee80211_install_qdisc(local->mdev);

	/* add one default STA interface */
	result = ieee80211_if_add(local->mdev, "wlan%d", NULL,
				  IEEE80211_IF_TYPE_STA);
	if (result)
		printk(KERN_WARNING "%s: Failed to add default virtual iface\n",
		       wiphy_name(local->hw.wiphy));

	local->reg_state = IEEE80211_DEV_REGISTERED;
	rtnl_unlock();

	ieee80211_led_init(local);

	return 0;

fail_wep:
	rate_control_deinitialize(local);
fail_rate:
	ieee80211_debugfs_remove_netdev(IEEE80211_DEV_TO_SUB_IF(local->mdev));
	unregister_netdevice(local->mdev);
fail_dev:
	rtnl_unlock();
	sta_info_stop(local);
fail_sta_info:
	debugfs_hw_del(local);
	destroy_workqueue(local->hw.workqueue);
fail_workqueue:
	wiphy_unregister(local->hw.wiphy);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

int ieee80211_register_hwmode(struct ieee80211_hw *hw,
			      struct ieee80211_hw_mode *mode)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	int i;

	INIT_LIST_HEAD(&mode->list);
	list_add_tail(&mode->list, &local->modes_list);

	local->hw_modes |= (1 << mode->mode);
	for (i = 0; i < mode->num_rates; i++) {
		rate = &(mode->rates[i]);
		rate->rate_inv = CHAN_UTIL_RATE_LCM / rate->rate;
	}
	ieee80211_prepare_rates(local, mode);

	if (!local->oper_hw_mode) {
		/* Default to this mode */
		local->hw.conf.phymode = mode->mode;
		local->oper_hw_mode = local->scan_hw_mode = mode;
		local->oper_channel = local->scan_channel = &mode->channels[0];
		local->hw.conf.mode = local->oper_hw_mode;
		local->hw.conf.chan = local->oper_channel;
	}

	if (!(hw->flags & IEEE80211_HW_DEFAULT_REG_DOMAIN_CONFIGURED))
		ieee80211_set_default_regdomain(mode);

	return 0;
}
EXPORT_SYMBOL(ieee80211_register_hwmode);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata, *tmp;
	int i;

	tasklet_kill(&local->tx_pending_tasklet);
	tasklet_kill(&local->tasklet);

	rtnl_lock();

	BUG_ON(local->reg_state != IEEE80211_DEV_REGISTERED);

	local->reg_state = IEEE80211_DEV_UNREGISTERED;

	/*
	 * At this point, interface list manipulations are fine
	 * because the driver cannot be handing us frames any
	 * more and the tasklet is killed.
	 */

	/*
	 * First, we remove all non-master interfaces. Do this because they
	 * may have bss pointer dependency on the master, and when we free
	 * the master these would be freed as well, breaking our list
	 * iteration completely.
	 */
	list_for_each_entry_safe(sdata, tmp, &local->interfaces, list) {
		if (sdata->dev == local->mdev)
			continue;
		list_del(&sdata->list);
		__ieee80211_if_del(local, sdata);
	}

	/* then, finally, remove the master interface */
	__ieee80211_if_del(local, IEEE80211_DEV_TO_SUB_IF(local->mdev));

	rtnl_unlock();

	ieee80211_rx_bss_list_deinit(local->mdev);
	ieee80211_clear_tx_pending(local);
	sta_info_stop(local);
	rate_control_deinitialize(local);
	debugfs_hw_del(local);

	for (i = 0; i < NUM_IEEE80211_MODES; i++) {
		kfree(local->supp_rates[i]);
		kfree(local->basic_rates[i]);
	}

	if (skb_queue_len(&local->skb_queue)
			|| skb_queue_len(&local->skb_queue_unreliable))
		printk(KERN_WARNING "%s: skb_queue not empty\n",
		       wiphy_name(local->hw.wiphy));
	skb_queue_purge(&local->skb_queue);
	skb_queue_purge(&local->skb_queue_unreliable);

	destroy_workqueue(local->hw.workqueue);
	wiphy_unregister(local->hw.wiphy);
	ieee80211_wep_free(local);
	ieee80211_led_exit(local);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	ieee80211_if_free(local->mdev);
	wiphy_free(local->hw.wiphy);
}
EXPORT_SYMBOL(ieee80211_free_hw);

static int __init ieee80211_init(void)
{
	struct sk_buff *skb;
	int ret;

	BUILD_BUG_ON(sizeof(struct ieee80211_tx_packet_data) > sizeof(skb->cb));

	ret = ieee80211_wme_register();
	if (ret) {
		printk(KERN_DEBUG "ieee80211_init: failed to "
		       "initialize WME (err=%d)\n", ret);
		return ret;
	}

	ieee80211_debugfs_netdev_init();
	ieee80211_regdomain_init();

	return 0;
}

static void __exit ieee80211_exit(void)
{
	ieee80211_wme_unregister();
	ieee80211_debugfs_netdev_exit();
}


subsys_initcall(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
