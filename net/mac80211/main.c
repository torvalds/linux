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
#include "rate.h"
#include "mesh.h"
#include "wep.h"
#include "wme.h"
#include "aes_ccm.h"
#include "led.h"
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
		new_flags |= FIF_BCN_PRBRESP_PROMISC;

	if (local->fif_fcsfail)
		new_flags |= FIF_FCSFAIL;

	if (local->fif_plcpfail)
		new_flags |= FIF_PLCPFAIL;

	if (local->fif_control)
		new_flags |= FIF_CONTROL;

	if (local->fif_other_bss)
		new_flags |= FIF_OTHER_BSS;

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
		if (netif_running(sdata->dev)) {
			res = 0;
			break;
		}
	}

	if (res)
		return res;

	netif_tx_start_all_queues(local->mdev);

	return 0;
}

static int ieee80211_master_stop(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(sdata, &local->interfaces, list)
		if (netif_running(sdata->dev))
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
	int meshhdrlen;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	meshhdrlen = (sdata->vif.type == IEEE80211_IF_TYPE_MESH_POINT) ? 5 : 0;

	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.3 frames. */
	if (new_mtu < 256 ||
	    new_mtu > IEEE80211_MAX_DATA_LEN - 24 - 6 - meshhdrlen) {
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
	struct sta_info *sta;
	struct ieee80211_if_init_conf conf;
	u32 changed = 0;
	int res;
	bool need_hw_reconfig = 0;
	u8 null_addr[ETH_ALEN] = {0};

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	/* fail early if user set an invalid address */
	if (compare_ether_addr(dev->dev_addr, null_addr) &&
	    !is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(nsdata, &local->interfaces, list) {
		struct net_device *ndev = nsdata->dev;

		if (ndev != dev && netif_running(ndev)) {
			/*
			 * Allow only a single IBSS interface to be up at any
			 * time. This is restricted because beacon distribution
			 * cannot work properly if both are in the same IBSS.
			 *
			 * To remove this restriction we'd have to disallow them
			 * from setting the same SSID on different IBSS interfaces
			 * belonging to the same hardware. Then, however, we're
			 * faced with having to adopt two different TSF timers...
			 */
			if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS &&
			    nsdata->vif.type == IEEE80211_IF_TYPE_IBSS)
				return -EBUSY;

			/*
			 * The remaining checks are only performed for interfaces
			 * with the same MAC address.
			 */
			if (compare_ether_addr(dev->dev_addr, ndev->dev_addr))
				continue;

			/*
			 * check whether it may have the same address
			 */
			if (!identical_mac_addr_allowed(sdata->vif.type,
							nsdata->vif.type))
				return -ENOTUNIQ;

			/*
			 * can only add VLANs to enabled APs
			 */
			if (sdata->vif.type == IEEE80211_IF_TYPE_VLAN &&
			    nsdata->vif.type == IEEE80211_IF_TYPE_AP)
				sdata->bss = &nsdata->u.ap;
		}
	}

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_WDS:
		if (!is_valid_ether_addr(sdata->u.wds.remote_addr))
			return -ENOLINK;
		break;
	case IEEE80211_IF_TYPE_VLAN:
		if (!sdata->bss)
			return -ENOLINK;
		list_add(&sdata->u.vlan.list, &sdata->bss->vlans);
		break;
	case IEEE80211_IF_TYPE_AP:
		sdata->bss = &sdata->u.ap;
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
		if (!ieee80211_vif_is_mesh(&sdata->vif))
			break;
		/* mesh ifaces must set allmulti to forward mcast traffic */
		atomic_inc(&local->iff_allmultis);
		break;
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
			goto err_del_bss;
		need_hw_reconfig = 1;
		ieee80211_led_radio(local, local->hw.conf.radio_enabled);
	}

	/*
	 * Check all interfaces and copy the hopefully now-present
	 * MAC address to those that have the special null one.
	 */
	list_for_each_entry(nsdata, &local->interfaces, list) {
		struct net_device *ndev = nsdata->dev;

		/*
		 * No need to check netif_running since we do not allow
		 * it to start up with this invalid address.
		 */
		if (compare_ether_addr(null_addr, ndev->dev_addr) == 0)
			memcpy(ndev->dev_addr,
			       local->hw.wiphy->perm_addr,
			       ETH_ALEN);
	}

	if (compare_ether_addr(null_addr, local->mdev->dev_addr) == 0)
		memcpy(local->mdev->dev_addr, local->hw.wiphy->perm_addr,
		       ETH_ALEN);

	/*
	 * Validate the MAC address for this device.
	 */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		if (!local->open_count && local->ops->stop)
			local->ops->stop(local_to_hw(local));
		return -EADDRNOTAVAIL;
	}

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_VLAN:
		/* no need to tell driver */
		break;
	case IEEE80211_IF_TYPE_MNTR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs++;
			break;
		}

		/* must be before the call to ieee80211_configure_filter */
		local->monitors++;
		if (local->monitors == 1)
			local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;

		if (sdata->u.mntr_flags & MONITOR_FLAG_FCSFAIL)
			local->fif_fcsfail++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_PLCPFAIL)
			local->fif_plcpfail++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_CONTROL)
			local->fif_control++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_OTHER_BSS)
			local->fif_other_bss++;

		netif_addr_lock_bh(local->mdev);
		ieee80211_configure_filter(local);
		netif_addr_unlock_bh(local->mdev);
		break;
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.flags &= ~IEEE80211_STA_PREV_BSSID_SET;
		/* fall through */
	default:
		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
		conf.mac_addr = dev->dev_addr;
		res = local->ops->add_interface(local_to_hw(local), &conf);
		if (res)
			goto err_stop;

		if (ieee80211_vif_is_mesh(&sdata->vif))
			ieee80211_start_mesh(sdata);
		changed |= ieee80211_reset_erp_info(sdata);
		ieee80211_bss_info_change_notify(sdata, changed);
		ieee80211_enable_keys(sdata);

		if (sdata->vif.type == IEEE80211_IF_TYPE_STA &&
		    !(sdata->flags & IEEE80211_SDATA_USERSPACE_MLME))
			netif_carrier_off(dev);
		else
			netif_carrier_on(dev);
	}

	if (sdata->vif.type == IEEE80211_IF_TYPE_WDS) {
		/* Create STA entry for the WDS peer */
		sta = sta_info_alloc(sdata, sdata->u.wds.remote_addr,
				     GFP_KERNEL);
		if (!sta) {
			res = -ENOMEM;
			goto err_del_interface;
		}

		/* no locking required since STA is not live yet */
		sta->flags |= WLAN_STA_AUTHORIZED;

		res = sta_info_insert(sta);
		if (res) {
			/* STA has been freed */
			goto err_del_interface;
		}
	}

	if (local->open_count == 0) {
		res = dev_open(local->mdev);
		WARN_ON(res);
		if (res)
			goto err_del_interface;
		tasklet_enable(&local->tx_pending_tasklet);
		tasklet_enable(&local->tasklet);
	}

	/*
	 * set_multicast_list will be invoked by the networking core
	 * which will check whether any increments here were done in
	 * error and sync them down to the hardware as filter flags.
	 */
	if (sdata->flags & IEEE80211_SDATA_ALLMULTI)
		atomic_inc(&local->iff_allmultis);

	if (sdata->flags & IEEE80211_SDATA_PROMISC)
		atomic_inc(&local->iff_promiscs);

	local->open_count++;
	if (need_hw_reconfig) {
		ieee80211_hw_config(local);
		/*
		 * set default queue parameters so drivers don't
		 * need to initialise the hardware if the hardware
		 * doesn't start up with sane defaults
		 */
		ieee80211_set_wmm_default(sdata);
	}

	/*
	 * ieee80211_sta_work is disabled while network interface
	 * is down. Therefore, some configuration changes may not
	 * yet be effective. Trigger execution of ieee80211_sta_work
	 * to fix this.
	 */
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		struct ieee80211_if_sta *ifsta = &sdata->u.sta;
		queue_work(local->hw.workqueue, &ifsta->work);
	}

	netif_tx_start_all_queues(dev);

	return 0;
 err_del_interface:
	local->ops->remove_interface(local_to_hw(local), &conf);
 err_stop:
	if (!local->open_count && local->ops->stop)
		local->ops->stop(local_to_hw(local));
 err_del_bss:
	sdata->bss = NULL;
	if (sdata->vif.type == IEEE80211_IF_TYPE_VLAN)
		list_del(&sdata->u.vlan.list);
	return res;
}

static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_init_conf conf;
	struct sta_info *sta;

	/*
	 * Stop TX on this interface first.
	 */
	netif_tx_stop_all_queues(dev);

	/*
	 * Now delete all active aggregation sessions.
	 */
	rcu_read_lock();

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sta->sdata == sdata)
			ieee80211_sta_tear_down_BA_sessions(sdata, sta->addr);
	}

	rcu_read_unlock();

	/*
	 * Remove all stations associated with this interface.
	 *
	 * This must be done before calling ops->remove_interface()
	 * because otherwise we can later invoke ops->sta_notify()
	 * whenever the STAs are removed, and that invalidates driver
	 * assumptions about always getting a vif pointer that is valid
	 * (because if we remove a STA after ops->remove_interface()
	 * the driver will have removed the vif info already!)
	 *
	 * We could relax this and only unlink the stations from the
	 * hash table and list but keep them on a per-sdata list that
	 * will be inserted back again when the interface is brought
	 * up again, but I don't currently see a use case for that,
	 * except with WDS which gets a STA entry created when it is
	 * brought up.
	 */
	sta_info_flush(local, sdata);

	/*
	 * Don't count this interface for promisc/allmulti while it
	 * is down. dev_mc_unsync() will invoke set_multicast_list
	 * on the master interface which will sync these down to the
	 * hardware as filter flags.
	 */
	if (sdata->flags & IEEE80211_SDATA_ALLMULTI)
		atomic_dec(&local->iff_allmultis);

	if (sdata->flags & IEEE80211_SDATA_PROMISC)
		atomic_dec(&local->iff_promiscs);

	dev_mc_unsync(local->mdev, dev);

	/* APs need special treatment */
	if (sdata->vif.type == IEEE80211_IF_TYPE_AP) {
		struct ieee80211_sub_if_data *vlan, *tmp;
		struct beacon_data *old_beacon = sdata->u.ap.beacon;

		/* remove beacon */
		rcu_assign_pointer(sdata->u.ap.beacon, NULL);
		synchronize_rcu();
		kfree(old_beacon);

		/* down all dependent devices, that is VLANs */
		list_for_each_entry_safe(vlan, tmp, &sdata->u.ap.vlans,
					 u.vlan.list)
			dev_close(vlan->dev);
		WARN_ON(!list_empty(&sdata->u.ap.vlans));
	}

	local->open_count--;

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_VLAN:
		list_del(&sdata->u.vlan.list);
		/* no need to tell driver */
		break;
	case IEEE80211_IF_TYPE_MNTR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs--;
			break;
		}

		local->monitors--;
		if (local->monitors == 0)
			local->hw.conf.flags &= ~IEEE80211_CONF_RADIOTAP;

		if (sdata->u.mntr_flags & MONITOR_FLAG_FCSFAIL)
			local->fif_fcsfail--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_PLCPFAIL)
			local->fif_plcpfail--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_CONTROL)
			local->fif_control--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_OTHER_BSS)
			local->fif_other_bss--;

		netif_addr_lock_bh(local->mdev);
		ieee80211_configure_filter(local);
		netif_addr_unlock_bh(local->mdev);
		break;
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.state = IEEE80211_STA_MLME_DISABLED;
		memset(sdata->u.sta.bssid, 0, ETH_ALEN);
		del_timer_sync(&sdata->u.sta.timer);
		/*
		 * If the timer fired while we waited for it, it will have
		 * requeued the work. Now the work will be running again
		 * but will not rearm the timer again because it checks
		 * whether the interface is running, which, at this point,
		 * it no longer is.
		 */
		cancel_work_sync(&sdata->u.sta.work);
		/*
		 * When we get here, the interface is marked down.
		 * Call synchronize_rcu() to wait for the RX path
		 * should it be using the interface and enqueuing
		 * frames at this very time on another CPU.
		 */
		synchronize_rcu();
		skb_queue_purge(&sdata->u.sta.skb_queue);

		sdata->u.sta.flags &= ~IEEE80211_STA_PRIVACY_INVOKED;
		kfree(sdata->u.sta.extra_ie);
		sdata->u.sta.extra_ie = NULL;
		sdata->u.sta.extra_ie_len = 0;
		/* fall through */
	case IEEE80211_IF_TYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif)) {
			/* allmulti is always set on mesh ifaces */
			atomic_dec(&local->iff_allmultis);
			ieee80211_stop_mesh(sdata);
		}
		/* fall through */
	default:
		if (local->scan_sdata == sdata) {
			if (!local->ops->hw_scan)
				cancel_delayed_work_sync(&local->scan_work);
			/*
			 * The software scan can no longer run now, so we can
			 * clear out the scan_sdata reference. However, the
			 * hardware scan may still be running. The complete
			 * function must be prepared to handle a NULL value.
			 */
			local->scan_sdata = NULL;
			/*
			 * The memory barrier guarantees that another CPU
			 * that is hardware-scanning will now see the fact
			 * that this interface is gone.
			 */
			smp_mb();
			/*
			 * If software scanning, complete the scan but since
			 * the scan_sdata is NULL already don't send out a
			 * scan event to userspace -- the scan is incomplete.
			 */
			if (local->sta_sw_scanning)
				ieee80211_scan_completed(&local->hw);
		}

		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
		conf.mac_addr = dev->dev_addr;
		/* disable all keys for as long as this netdev is down */
		ieee80211_disable_keys(sdata);
		local->ops->remove_interface(local_to_hw(local), &conf);
	}

	sdata->bss = NULL;

	if (local->open_count == 0) {
		if (netif_running(local->mdev))
			dev_close(local->mdev);

		if (local->ops->stop)
			local->ops->stop(local_to_hw(local));

		ieee80211_led_radio(local, 0);

		flush_workqueue(local->hw.workqueue);

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
	sdata_allmulti = !!(sdata->flags & IEEE80211_SDATA_ALLMULTI);
	sdata_promisc = !!(sdata->flags & IEEE80211_SDATA_PROMISC);

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

void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->hard_start_xmit = ieee80211_subif_start_xmit;
	dev->wireless_handlers = &ieee80211_iw_handler_def;
	dev->set_multicast_list = ieee80211_set_multicast_list;
	dev->change_mtu = ieee80211_change_mtu;
	dev->open = ieee80211_open;
	dev->stop = ieee80211_stop;
	dev->destructor = free_netdev;
	/* we will validate the address ourselves in ->open */
	dev->validate_addr = NULL;
}

/* everything else */

int ieee80211_if_config(struct ieee80211_sub_if_data *sdata, u32 changed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_conf conf;

	if (WARN_ON(!netif_running(sdata->dev)))
		return 0;

	if (!local->ops->config_interface)
		return 0;

	memset(&conf, 0, sizeof(conf));
	conf.changed = changed;

	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		conf.bssid = sdata->u.sta.bssid;
		conf.ssid = sdata->u.sta.ssid;
		conf.ssid_len = sdata->u.sta.ssid_len;
	} else if (sdata->vif.type == IEEE80211_IF_TYPE_AP) {
		conf.bssid = sdata->dev->dev_addr;
		conf.ssid = sdata->u.ap.ssid;
		conf.ssid_len = sdata->u.ap.ssid_len;
	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		u8 zero[ETH_ALEN] = { 0 };
		conf.bssid = zero;
		conf.ssid = zero;
		conf.ssid_len = 0;
	} else {
		WARN_ON(1);
		return -EINVAL;
	}

	if (WARN_ON(!conf.bssid && (changed & IEEE80211_IFCC_BSSID)))
		return -EINVAL;

	if (WARN_ON(!conf.ssid && (changed & IEEE80211_IFCC_SSID)))
		return -EINVAL;

	return local->ops->config_interface(local_to_hw(local),
					    &sdata->vif, &conf);
}

int ieee80211_hw_config(struct ieee80211_local *local)
{
	struct ieee80211_channel *chan;
	int ret = 0;

	if (local->sta_sw_scanning)
		chan = local->scan_channel;
	else
		chan = local->oper_channel;

	local->hw.conf.channel = chan;

	if (!local->hw.conf.power_level)
		local->hw.conf.power_level = chan->max_power;
	else
		local->hw.conf.power_level = min(chan->max_power,
					       local->hw.conf.power_level);

	local->hw.conf.max_antenna_gain = chan->max_antenna_gain;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: HW CONFIG: freq=%d\n",
	       wiphy_name(local->hw.wiphy), chan->center_freq);
#endif

	if (local->open_count)
		ret = local->ops->config(local_to_hw(local), &local->hw.conf);

	return ret;
}

/**
 * ieee80211_handle_ht should be used only after legacy configuration
 * has been determined namely band, as ht configuration depends upon
 * the hardware's HT abilities for a _specific_ band.
 */
u32 ieee80211_handle_ht(struct ieee80211_local *local, int enable_ht,
			   struct ieee80211_ht_info *req_ht_cap,
			   struct ieee80211_ht_bss_info *req_bss_cap)
{
	struct ieee80211_conf *conf = &local->hw.conf;
	struct ieee80211_supported_band *sband;
	struct ieee80211_ht_info ht_conf;
	struct ieee80211_ht_bss_info ht_bss_conf;
	u32 changed = 0;
	int i;
	u8 max_tx_streams = IEEE80211_HT_CAP_MAX_STREAMS;
	u8 tx_mcs_set_cap;

	sband = local->hw.wiphy->bands[conf->channel->band];

	memset(&ht_conf, 0, sizeof(struct ieee80211_ht_info));
	memset(&ht_bss_conf, 0, sizeof(struct ieee80211_ht_bss_info));

	/* HT is not supported */
	if (!sband->ht_info.ht_supported) {
		conf->flags &= ~IEEE80211_CONF_SUPPORT_HT_MODE;
		goto out;
	}

	/* disable HT */
	if (!enable_ht) {
		if (conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE)
			changed |= BSS_CHANGED_HT;
		conf->flags &= ~IEEE80211_CONF_SUPPORT_HT_MODE;
		conf->ht_conf.ht_supported = 0;
		goto out;
	}


	if (!(conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE))
		changed |= BSS_CHANGED_HT;

	conf->flags |= IEEE80211_CONF_SUPPORT_HT_MODE;
	ht_conf.ht_supported = 1;

	ht_conf.cap = req_ht_cap->cap & sband->ht_info.cap;
	ht_conf.cap &= ~(IEEE80211_HT_CAP_SM_PS);
	ht_conf.cap |= sband->ht_info.cap & IEEE80211_HT_CAP_SM_PS;
	ht_bss_conf.primary_channel = req_bss_cap->primary_channel;
	ht_bss_conf.bss_cap = req_bss_cap->bss_cap;
	ht_bss_conf.bss_op_mode = req_bss_cap->bss_op_mode;

	ht_conf.ampdu_factor = req_ht_cap->ampdu_factor;
	ht_conf.ampdu_density = req_ht_cap->ampdu_density;

	/* Bits 96-100 */
	tx_mcs_set_cap = sband->ht_info.supp_mcs_set[12];

	/* configure suppoerted Tx MCS according to requested MCS
	 * (based in most cases on Rx capabilities of peer) and self
	 * Tx MCS capabilities (as defined by low level driver HW
	 * Tx capabilities) */
	if (!(tx_mcs_set_cap & IEEE80211_HT_CAP_MCS_TX_DEFINED))
		goto check_changed;

	/* Counting from 0 therfore + 1 */
	if (tx_mcs_set_cap & IEEE80211_HT_CAP_MCS_TX_RX_DIFF)
		max_tx_streams = ((tx_mcs_set_cap &
				IEEE80211_HT_CAP_MCS_TX_STREAMS) >> 2) + 1;

	for (i = 0; i < max_tx_streams; i++)
		ht_conf.supp_mcs_set[i] =
			sband->ht_info.supp_mcs_set[i] &
					req_ht_cap->supp_mcs_set[i];

	if (tx_mcs_set_cap & IEEE80211_HT_CAP_MCS_TX_UEQM)
		for (i = IEEE80211_SUPP_MCS_SET_UEQM;
		     i < IEEE80211_SUPP_MCS_SET_LEN; i++)
			ht_conf.supp_mcs_set[i] =
				sband->ht_info.supp_mcs_set[i] &
					req_ht_cap->supp_mcs_set[i];

check_changed:
	/* if bss configuration changed store the new one */
	if (memcmp(&conf->ht_conf, &ht_conf, sizeof(ht_conf)) ||
	    memcmp(&conf->ht_bss_conf, &ht_bss_conf, sizeof(ht_bss_conf))) {
		changed |= BSS_CHANGED_HT;
		memcpy(&conf->ht_conf, &ht_conf, sizeof(ht_conf));
		memcpy(&conf->ht_bss_conf, &ht_bss_conf, sizeof(ht_bss_conf));
	}
out:
	return changed;
}

void ieee80211_bss_info_change_notify(struct ieee80211_sub_if_data *sdata,
				      u32 changed)
{
	struct ieee80211_local *local = sdata->local;

	if (!changed)
		return;

	if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(local_to_hw(local),
					     &sdata->vif,
					     &sdata->bss_conf,
					     changed);
}

u32 ieee80211_reset_erp_info(struct ieee80211_sub_if_data *sdata)
{
	sdata->bss_conf.use_cts_prot = 0;
	sdata->bss_conf.use_short_preamble = 0;
	return BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_ERP_PREAMBLE;
}

void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int tmp;

	skb->dev = local->mdev;
	skb->pkt_type = IEEE80211_TX_STATUS_MSG;
	skb_queue_tail(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS ?
		       &local->skb_queue : &local->skb_queue_unreliable, skb);
	tmp = skb_queue_len(&local->skb_queue) +
		skb_queue_len(&local->skb_queue_unreliable);
	while (tmp > IEEE80211_IRQSAFE_QUEUE_LIMIT &&
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
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
	struct ieee80211_ra_tid *ra_tid;

	while ((skb = skb_dequeue(&local->skb_queue)) ||
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		switch (skb->pkt_type) {
		case IEEE80211_RX_MSG:
			/* status is in skb->cb */
			memcpy(&rx_status, skb->cb, sizeof(rx_status));
			/* Clear skb->pkt_type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			__ieee80211_rx(local_to_hw(local), skb, &rx_status);
			break;
		case IEEE80211_TX_STATUS_MSG:
			skb->pkt_type = 0;
			ieee80211_tx_status(local_to_hw(local), skb);
			break;
		case IEEE80211_DELBA_MSG:
			ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
			ieee80211_stop_tx_ba_cb(local_to_hw(local),
						ra_tid->ra, ra_tid->tid);
			dev_kfree_skb(skb);
			break;
		case IEEE80211_ADDBA_MSG:
			ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
			ieee80211_start_tx_ba_cb(local_to_hw(local),
						 ra_tid->ra, ra_tid->tid);
			dev_kfree_skb(skb);
			break ;
		default:
			WARN_ON(1);
			dev_kfree_skb(skb);
			break;
		}
	}
}

/* Remove added headers (e.g., QoS control), encryption header/MIC, etc. to
 * make a prepared TX frame (one that has been given to hw) to look like brand
 * new IEEE 802.11 frame that is ready to go through TX processing again.
 */
static void ieee80211_remove_tx_extra(struct ieee80211_local *local,
				      struct ieee80211_key *key,
				      struct sk_buff *skb)
{
	unsigned int hdrlen, iv_len, mic_len;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

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

	if (skb->len >= hdrlen + mic_len &&
	    !(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		skb_trim(skb, skb->len - mic_len);
	if (skb->len >= hdrlen + iv_len) {
		memmove(skb->data + iv_len, skb->data, hdrlen);
		hdr = (struct ieee80211_hdr *)skb_pull(skb, iv_len);
	}

no_key:
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		hdr->frame_control &= ~cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		memmove(skb->data + IEEE80211_QOS_CTL_LEN, skb->data,
			hdrlen - IEEE80211_QOS_CTL_LEN);
		skb_pull(skb, IEEE80211_QOS_CTL_LEN);
	}
}

static void ieee80211_handle_filtered_frame(struct ieee80211_local *local,
					    struct sta_info *sta,
					    struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	sta->tx_filtered_count++;

	/*
	 * Clear the TX filter mask for this STA when sending the next
	 * packet. If the STA went to power save mode, this will happen
	 * when it wakes up for the next time.
	 */
	set_sta_flags(sta, WLAN_STA_CLEAR_PS_FILT);

	/*
	 * This code races in the following way:
	 *
	 *  (1) STA sends frame indicating it will go to sleep and does so
	 *  (2) hardware/firmware adds STA to filter list, passes frame up
	 *  (3) hardware/firmware processes TX fifo and suppresses a frame
	 *  (4) we get TX status before having processed the frame and
	 *	knowing that the STA has gone to sleep.
	 *
	 * This is actually quite unlikely even when both those events are
	 * processed from interrupts coming in quickly after one another or
	 * even at the same time because we queue both TX status events and
	 * RX frames to be processed by a tasklet and process them in the
	 * same order that they were received or TX status last. Hence, there
	 * is no race as long as the frame RX is processed before the next TX
	 * status, which drivers can ensure, see below.
	 *
	 * Note that this can only happen if the hardware or firmware can
	 * actually add STAs to the filter list, if this is done by the
	 * driver in response to set_tim() (which will only reduce the race
	 * this whole filtering tries to solve, not completely solve it)
	 * this situation cannot happen.
	 *
	 * To completely solve this race drivers need to make sure that they
	 *  (a) don't mix the irq-safe/not irq-safe TX status/RX processing
	 *	functions and
	 *  (b) always process RX events before TX status events if ordering
	 *      can be unknown, for example with different interrupt status
	 *	bits.
	 */
	if (test_sta_flags(sta, WLAN_STA_PS) &&
	    skb_queue_len(&sta->tx_filtered) < STA_MAX_TX_BUFFER) {
		ieee80211_remove_tx_extra(local, sta->key, skb);
		skb_queue_tail(&sta->tx_filtered, skb);
		return;
	}

	if (!test_sta_flags(sta, WLAN_STA_PS) &&
	    !(info->flags & IEEE80211_TX_CTL_REQUEUE)) {
		/* Software retry the packet once */
		info->flags |= IEEE80211_TX_CTL_REQUEUE;
		ieee80211_remove_tx_extra(local, sta->key, skb);
		dev_queue_xmit(skb);
		return;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	if (net_ratelimit())
		printk(KERN_DEBUG "%s: dropped TX filtered frame, "
		       "queue_len=%d PS=%d @%lu\n",
		       wiphy_name(local->hw.wiphy),
		       skb_queue_len(&sta->tx_filtered),
		       !!test_sta_flags(sta, WLAN_STA_PS), jiffies);
#endif
	dev_kfree_skb(skb);
}

void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct sk_buff *skb2;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u16 frag, type;
	__le16 fc;
	struct ieee80211_tx_status_rtap_hdr *rthdr;
	struct ieee80211_sub_if_data *sdata;
	struct net_device *prev_dev = NULL;
	struct sta_info *sta;

	rcu_read_lock();

	if (info->status.excessive_retries) {
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			if (test_sta_flags(sta, WLAN_STA_PS)) {
				/*
				 * The STA is in power save mode, so assume
				 * that this TX packet failed because of that.
				 */
				ieee80211_handle_filtered_frame(local, sta, skb);
				rcu_read_unlock();
				return;
			}
		}
	}

	fc = hdr->frame_control;

	if ((info->flags & IEEE80211_TX_STAT_AMPDU_NO_BACK) &&
	    (ieee80211_is_data_qos(fc))) {
		u16 tid, ssn;
		u8 *qc;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;
			ssn = ((le16_to_cpu(hdr->seq_ctrl) + 0x10)
						& IEEE80211_SCTL_SEQ);
			ieee80211_send_bar(sta->sdata, hdr->addr1,
					   tid, ssn);
		}
	}

	if (info->flags & IEEE80211_TX_STAT_TX_FILTERED) {
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			ieee80211_handle_filtered_frame(local, sta, skb);
			rcu_read_unlock();
			return;
		}
	} else
		rate_control_tx_status(local->mdev, skb);

	rcu_read_unlock();

	ieee80211_led_tx(local, 0);

	/* SNMP counters
	 * Fragments are passed to low-level drivers as separate skbs, so these
	 * are actually fragments, not frames. Update frame counters only for
	 * the first fragment of the frame. */

	frag = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
	type = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_FTYPE;

	if (info->flags & IEEE80211_TX_STAT_ACK) {
		if (frag == 0) {
			local->dot11TransmittedFrameCount++;
			if (is_multicast_ether_addr(hdr->addr1))
				local->dot11MulticastTransmittedFrameCount++;
			if (info->status.retry_count > 0)
				local->dot11RetryCount++;
			if (info->status.retry_count > 1)
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

	/*
	 * This is a bit racy but we can avoid a lot of work
	 * with this test...
	 */
	if (!local->monitors && !local->cooked_mntrs) {
		dev_kfree_skb(skb);
		return;
	}

	/* send frame to monitor interfaces now */

	if (skb_headroom(skb) < sizeof(*rthdr)) {
		printk(KERN_ERR "ieee80211_tx_status: headroom too small\n");
		dev_kfree_skb(skb);
		return;
	}

	rthdr = (struct ieee80211_tx_status_rtap_hdr *)
				skb_push(skb, sizeof(*rthdr));

	memset(rthdr, 0, sizeof(*rthdr));
	rthdr->hdr.it_len = cpu_to_le16(sizeof(*rthdr));
	rthdr->hdr.it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_TX_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_DATA_RETRIES));

	if (!(info->flags & IEEE80211_TX_STAT_ACK) &&
	    !is_multicast_ether_addr(hdr->addr1))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_FAIL);

	if ((info->flags & IEEE80211_TX_CTL_USE_RTS_CTS) &&
	    (info->flags & IEEE80211_TX_CTL_USE_CTS_PROTECT))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_CTS);
	else if (info->flags & IEEE80211_TX_CTL_USE_RTS_CTS)
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_RTS);

	rthdr->data_retries = info->status.retry_count;

	/* XXX: is this sufficient for BPF? */
	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->vif.type == IEEE80211_IF_TYPE_MNTR) {
			if (!netif_running(sdata->dev))
				continue;

			if (prev_dev) {
				skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2) {
					skb2->dev = prev_dev;
					netif_rx(skb2);
				}
			}

			prev_dev = sdata->dev;
		}
	}
	if (prev_dev) {
		skb->dev = prev_dev;
		netif_rx(skb);
		skb = NULL;
	}
	rcu_read_unlock();
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL(ieee80211_tx_status);

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops)
{
	struct ieee80211_local *local;
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

	local->hw.queues = 1; /* default */

	local->bridge_packets = 1;

	local->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	local->fragmentation_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	local->short_retry_limit = 7;
	local->long_retry_limit = 4;
	local->hw.conf.radio_enabled = 1;

	INIT_LIST_HEAD(&local->interfaces);

	spin_lock_init(&local->key_lock);

	INIT_DELAYED_WORK(&local->scan_work, ieee80211_sta_scan_work);

	sta_info_init(local);

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
	enum ieee80211_band band;
	struct net_device *mdev;
	struct wireless_dev *mwdev;

	/*
	 * generic code guarantees at least one band,
	 * set this very early because much code assumes
	 * that hw.conf.channel is assigned
	 */
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[band];
		if (sband) {
			/* init channel we're on */
			local->hw.conf.channel =
			local->oper_channel =
			local->scan_channel = &sband->channels[0];
			break;
		}
	}

	/* if low-level driver supports AP, we also support VLAN */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_AP))
		local->hw.wiphy->interface_modes |= BIT(NL80211_IFTYPE_AP_VLAN);

	/* mac80211 always supports monitor */
	local->hw.wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		return result;

	/*
	 * We use the number of queues for feature tests (QoS, HT) internally
	 * so restrict them appropriately.
	 */
	if (hw->queues > IEEE80211_MAX_QUEUES)
		hw->queues = IEEE80211_MAX_QUEUES;
	if (hw->ampdu_queues > IEEE80211_MAX_AMPDU_QUEUES)
		hw->ampdu_queues = IEEE80211_MAX_AMPDU_QUEUES;
	if (hw->queues < 4)
		hw->ampdu_queues = 0;

	mdev = alloc_netdev_mq(sizeof(struct wireless_dev),
			       "wmaster%d", ether_setup,
			       ieee80211_num_queues(hw));
	if (!mdev)
		goto fail_mdev_alloc;

	mwdev = netdev_priv(mdev);
	mdev->ieee80211_ptr = mwdev;
	mwdev->wiphy = local->hw.wiphy;

	local->mdev = mdev;

	ieee80211_rx_bss_list_init(local);

	mdev->hard_start_xmit = ieee80211_master_start_xmit;
	mdev->open = ieee80211_master_open;
	mdev->stop = ieee80211_master_stop;
	mdev->type = ARPHRD_IEEE80211;
	mdev->header_ops = &ieee80211_header_ops;
	mdev->set_multicast_list = ieee80211_master_set_multicast_list;

	name = wiphy_dev(local->hw.wiphy)->driver->name;
	local->hw.workqueue = create_freezeable_workqueue(name);
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

	if (local->hw.conf.beacon_int < 10)
		local->hw.conf.beacon_int = 100;

	if (local->hw.max_listen_interval == 0)
		local->hw.max_listen_interval = 1;

	local->hw.conf.listen_interval = local->hw.max_listen_interval;

	local->wstats_flags |= local->hw.flags & (IEEE80211_HW_SIGNAL_UNSPEC |
						  IEEE80211_HW_SIGNAL_DB |
						  IEEE80211_HW_SIGNAL_DBM) ?
			       IW_QUAL_QUAL_UPDATED : IW_QUAL_QUAL_INVALID;
	local->wstats_flags |= local->hw.flags & IEEE80211_HW_NOISE_DBM ?
			       IW_QUAL_NOISE_UPDATED : IW_QUAL_NOISE_INVALID;
	if (local->hw.flags & IEEE80211_HW_SIGNAL_DBM)
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

	result = ieee80211_init_rate_ctrl_alg(local,
					      hw->rate_control_algorithm);
	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize rate control "
		       "algorithm\n", wiphy_name(local->hw.wiphy));
		goto fail_rate;
	}

	result = ieee80211_wep_init(local);

	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize wep: %d\n",
		       wiphy_name(local->hw.wiphy), result);
		goto fail_wep;
	}

	local->mdev->select_queue = ieee80211_select_queue;

	/* add one default STA interface */
	result = ieee80211_if_add(local, "wlan%d", NULL,
				  IEEE80211_IF_TYPE_STA, NULL);
	if (result)
		printk(KERN_WARNING "%s: Failed to add default virtual iface\n",
		       wiphy_name(local->hw.wiphy));

	rtnl_unlock();

	ieee80211_led_init(local);

	return 0;

fail_wep:
	rate_control_deinitialize(local);
fail_rate:
	unregister_netdevice(local->mdev);
	local->mdev = NULL;
fail_dev:
	rtnl_unlock();
	sta_info_stop(local);
fail_sta_info:
	debugfs_hw_del(local);
	destroy_workqueue(local->hw.workqueue);
fail_workqueue:
	if (local->mdev)
		free_netdev(local->mdev);
fail_mdev_alloc:
	wiphy_unregister(local->hw.wiphy);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	tasklet_kill(&local->tx_pending_tasklet);
	tasklet_kill(&local->tasklet);

	rtnl_lock();

	/*
	 * At this point, interface list manipulations are fine
	 * because the driver cannot be handing us frames any
	 * more and the tasklet is killed.
	 */

	/* First, we remove all virtual interfaces. */
	ieee80211_remove_interfaces(local);

	/* then, finally, remove the master interface */
	unregister_netdevice(local->mdev);

	rtnl_unlock();

	ieee80211_rx_bss_list_deinit(local);
	ieee80211_clear_tx_pending(local);
	sta_info_stop(local);
	rate_control_deinitialize(local);
	debugfs_hw_del(local);

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
	free_netdev(local->mdev);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	wiphy_free(local->hw.wiphy);
}
EXPORT_SYMBOL(ieee80211_free_hw);

static int __init ieee80211_init(void)
{
	struct sk_buff *skb;
	int ret;

	BUILD_BUG_ON(sizeof(struct ieee80211_tx_info) > sizeof(skb->cb));
	BUILD_BUG_ON(offsetof(struct ieee80211_tx_info, driver_data) +
	             IEEE80211_TX_INFO_DRIVER_DATA_SIZE > sizeof(skb->cb));

	ret = rc80211_pid_init();
	if (ret)
		return ret;

	ieee80211_debugfs_netdev_init();

	return 0;
}

static void __exit ieee80211_exit(void)
{
	rc80211_pid_exit();

	/*
	 * For key todo, it'll be empty by now but the work
	 * might still be scheduled.
	 */
	flush_scheduled_work();

	if (mesh_allocated)
		ieee80211s_stop();

	ieee80211_debugfs_netdev_exit();
}


subsys_initcall(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
