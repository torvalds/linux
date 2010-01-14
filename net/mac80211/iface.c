/*
 * Interface handling (except master interface)
 *
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "debugfs_netdev.h"
#include "mesh.h"
#include "led.h"
#include "driver-ops.h"
#include "wme.h"

/**
 * DOC: Interface list locking
 *
 * The interface list in each struct ieee80211_local is protected
 * three-fold:
 *
 * (1) modifications may only be done under the RTNL
 * (2) modifications and readers are protected against each other by
 *     the iflist_mtx.
 * (3) modifications are done in an RCU manner so atomic readers
 *     can traverse the list in RCU-safe blocks.
 *
 * As a consequence, reads (traversals) of the list can be protected
 * by either the RTNL, the iflist_mtx or RCU.
 */


static int ieee80211_change_mtu(struct net_device *dev, int new_mtu)
{
	int meshhdrlen;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	meshhdrlen = (sdata->vif.type == NL80211_IFTYPE_MESH_POINT) ? 5 : 0;

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
	return type1 == NL80211_IFTYPE_MONITOR ||
		type2 == NL80211_IFTYPE_MONITOR ||
		(type1 == NL80211_IFTYPE_AP && type2 == NL80211_IFTYPE_WDS) ||
		(type1 == NL80211_IFTYPE_WDS &&
			(type2 == NL80211_IFTYPE_WDS ||
			 type2 == NL80211_IFTYPE_AP)) ||
		(type1 == NL80211_IFTYPE_AP && type2 == NL80211_IFTYPE_AP_VLAN) ||
		(type1 == NL80211_IFTYPE_AP_VLAN &&
			(type2 == NL80211_IFTYPE_AP ||
			 type2 == NL80211_IFTYPE_AP_VLAN));
}

static int ieee80211_open(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_sub_if_data *nsdata;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct ieee80211_if_init_conf conf;
	u32 changed = 0;
	int res;
	u32 hw_reconf_flags = 0;
	u8 null_addr[ETH_ALEN] = {0};

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
			if (sdata->vif.type == NL80211_IFTYPE_ADHOC &&
			    nsdata->vif.type == NL80211_IFTYPE_ADHOC)
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
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN &&
			    nsdata->vif.type == NL80211_IFTYPE_AP)
				sdata->bss = &nsdata->u.ap;
		}
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_WDS:
		if (!is_valid_ether_addr(sdata->u.wds.remote_addr))
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		if (!sdata->bss)
			return -ENOLINK;
		list_add(&sdata->u.vlan.list, &sdata->bss->vlans);
		break;
	case NL80211_IFTYPE_AP:
		sdata->bss = &sdata->u.ap;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (!ieee80211_vif_is_mesh(&sdata->vif))
			break;
		/* mesh ifaces must set allmulti to forward mcast traffic */
		atomic_inc(&local->iff_allmultis);
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_ADHOC:
		/* no special treatment */
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case __NL80211_IFTYPE_AFTER_LAST:
		/* cannot happen */
		WARN_ON(1);
		break;
	}

	if (local->open_count == 0) {
		res = drv_start(local);
		if (res)
			goto err_del_bss;
		/* we're brought up, everything changes */
		hw_reconf_flags = ~0;
		ieee80211_led_radio(local, true);
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

	/*
	 * Validate the MAC address for this device.
	 */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		if (!local->open_count)
			drv_stop(local);
		return -EADDRNOTAVAIL;
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		/* no need to tell driver */
		break;
	case NL80211_IFTYPE_MONITOR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs++;
			break;
		}

		/* must be before the call to ieee80211_configure_filter */
		local->monitors++;
		if (local->monitors == 1) {
			local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;
			hw_reconf_flags |= IEEE80211_CONF_CHANGE_RADIOTAP;
		}

		if (sdata->u.mntr_flags & MONITOR_FLAG_FCSFAIL)
			local->fif_fcsfail++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_PLCPFAIL)
			local->fif_plcpfail++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_CONTROL) {
			local->fif_control++;
			local->fif_pspoll++;
		}
		if (sdata->u.mntr_flags & MONITOR_FLAG_OTHER_BSS)
			local->fif_other_bss++;

		ieee80211_configure_filter(local);
		break;
	default:
		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
		conf.mac_addr = dev->dev_addr;
		res = drv_add_interface(local, &conf);
		if (res)
			goto err_stop;

		if (ieee80211_vif_is_mesh(&sdata->vif)) {
			local->fif_other_bss++;
			ieee80211_configure_filter(local);

			ieee80211_start_mesh(sdata);
		} else if (sdata->vif.type == NL80211_IFTYPE_AP) {
			local->fif_pspoll++;

			ieee80211_configure_filter(local);
		}

		changed |= ieee80211_reset_erp_info(sdata);
		ieee80211_bss_info_change_notify(sdata, changed);
		ieee80211_enable_keys(sdata);

		if (sdata->vif.type == NL80211_IFTYPE_STATION)
			netif_carrier_off(dev);
		else
			netif_carrier_on(dev);
	}

	if (sdata->vif.type == NL80211_IFTYPE_WDS) {
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

	/*
	 * set_multicast_list will be invoked by the networking core
	 * which will check whether any increments here were done in
	 * error and sync them down to the hardware as filter flags.
	 */
	if (sdata->flags & IEEE80211_SDATA_ALLMULTI)
		atomic_inc(&local->iff_allmultis);

	if (sdata->flags & IEEE80211_SDATA_PROMISC)
		atomic_inc(&local->iff_promiscs);

	hw_reconf_flags |= __ieee80211_recalc_idle(local);

	local->open_count++;
	if (hw_reconf_flags) {
		ieee80211_hw_config(local, hw_reconf_flags);
		/*
		 * set default queue parameters so drivers don't
		 * need to initialise the hardware if the hardware
		 * doesn't start up with sane defaults
		 */
		ieee80211_set_wmm_default(sdata);
	}

	ieee80211_recalc_ps(local, -1);

	/*
	 * ieee80211_sta_work is disabled while network interface
	 * is down. Therefore, some configuration changes may not
	 * yet be effective. Trigger execution of ieee80211_sta_work
	 * to fix this.
	 */
	if (sdata->vif.type == NL80211_IFTYPE_STATION)
		ieee80211_queue_work(&local->hw, &sdata->u.mgd.work);

	netif_tx_start_all_queues(dev);

	return 0;
 err_del_interface:
	drv_remove_interface(local, &conf);
 err_stop:
	if (!local->open_count)
		drv_stop(local);
 err_del_bss:
	sdata->bss = NULL;
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		list_del(&sdata->u.vlan.list);
	return res;
}

static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_init_conf conf;
	struct sta_info *sta;
	unsigned long flags;
	struct sk_buff *skb, *tmp;
	u32 hw_reconf_flags = 0;
	int i;

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
			ieee80211_sta_tear_down_BA_sessions(sta);
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

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		local->fif_pspoll--;

	netif_addr_lock_bh(dev);
	spin_lock_bh(&local->filter_lock);
	__dev_addr_unsync(&local->mc_list, &local->mc_count,
			  &dev->mc_list, &dev->mc_count);
	spin_unlock_bh(&local->filter_lock);
	netif_addr_unlock_bh(dev);

	ieee80211_configure_filter(local);

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	/* APs need special treatment */
	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		struct ieee80211_sub_if_data *vlan, *tmpsdata;
		struct beacon_data *old_beacon = sdata->u.ap.beacon;

		/* remove beacon */
		rcu_assign_pointer(sdata->u.ap.beacon, NULL);
		synchronize_rcu();
		kfree(old_beacon);

		/* down all dependent devices, that is VLANs */
		list_for_each_entry_safe(vlan, tmpsdata, &sdata->u.ap.vlans,
					 u.vlan.list)
			dev_close(vlan->dev);
		WARN_ON(!list_empty(&sdata->u.ap.vlans));
	}

	local->open_count--;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		list_del(&sdata->u.vlan.list);
		/* no need to tell driver */
		break;
	case NL80211_IFTYPE_MONITOR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs--;
			break;
		}

		local->monitors--;
		if (local->monitors == 0) {
			local->hw.conf.flags &= ~IEEE80211_CONF_RADIOTAP;
			hw_reconf_flags |= IEEE80211_CONF_CHANGE_RADIOTAP;
		}

		if (sdata->u.mntr_flags & MONITOR_FLAG_FCSFAIL)
			local->fif_fcsfail--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_PLCPFAIL)
			local->fif_plcpfail--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_CONTROL) {
			local->fif_pspoll--;
			local->fif_control--;
		}
		if (sdata->u.mntr_flags & MONITOR_FLAG_OTHER_BSS)
			local->fif_other_bss--;

		ieee80211_configure_filter(local);
		break;
	case NL80211_IFTYPE_STATION:
		del_timer_sync(&sdata->u.mgd.chswitch_timer);
		del_timer_sync(&sdata->u.mgd.timer);
		del_timer_sync(&sdata->u.mgd.conn_mon_timer);
		del_timer_sync(&sdata->u.mgd.bcn_mon_timer);
		/*
		 * If any of the timers fired while we waited for it, it will
		 * have queued its work. Now the work will be running again
		 * but will not rearm the timer again because it checks
		 * whether the interface is running, which, at this point,
		 * it no longer is.
		 */
		cancel_work_sync(&sdata->u.mgd.work);
		cancel_work_sync(&sdata->u.mgd.chswitch_work);
		cancel_work_sync(&sdata->u.mgd.monitor_work);
		cancel_work_sync(&sdata->u.mgd.beacon_loss_work);

		/*
		 * When we get here, the interface is marked down.
		 * Call synchronize_rcu() to wait for the RX path
		 * should it be using the interface and enqueuing
		 * frames at this very time on another CPU.
		 */
		synchronize_rcu();
		skb_queue_purge(&sdata->u.mgd.skb_queue);
		/* fall through */
	case NL80211_IFTYPE_ADHOC:
		if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
			del_timer_sync(&sdata->u.ibss.timer);
			cancel_work_sync(&sdata->u.ibss.work);
			synchronize_rcu();
			skb_queue_purge(&sdata->u.ibss.skb_queue);
		}
		/* fall through */
	case NL80211_IFTYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif)) {
			/* other_bss and allmulti are always set on mesh
			 * ifaces */
			local->fif_other_bss--;
			atomic_dec(&local->iff_allmultis);

			ieee80211_configure_filter(local);

			ieee80211_stop_mesh(sdata);
		}
		/* fall through */
	default:
		if (local->scan_sdata == sdata)
			ieee80211_scan_cancel(local);

		/*
		 * Disable beaconing for AP and mesh, IBSS can't
		 * still be joined to a network at this point.
		 */
		if (sdata->vif.type == NL80211_IFTYPE_AP ||
		    sdata->vif.type == NL80211_IFTYPE_MESH_POINT) {
			ieee80211_bss_info_change_notify(sdata,
				BSS_CHANGED_BEACON_ENABLED);
		}

		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
		conf.mac_addr = dev->dev_addr;
		/* disable all keys for as long as this netdev is down */
		ieee80211_disable_keys(sdata);
		drv_remove_interface(local, &conf);
	}

	sdata->bss = NULL;

	hw_reconf_flags |= __ieee80211_recalc_idle(local);

	ieee80211_recalc_ps(local, -1);

	if (local->open_count == 0) {
		ieee80211_clear_tx_pending(local);
		ieee80211_stop_device(local);

		/* no reconfiguring after stop! */
		hw_reconf_flags = 0;
	}

	/* do after stop to avoid reconfiguring when we stop anyway */
	if (hw_reconf_flags)
		ieee80211_hw_config(local, hw_reconf_flags);

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	for (i = 0; i < IEEE80211_MAX_QUEUES; i++) {
		skb_queue_walk_safe(&local->pending[i], skb, tmp) {
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
			if (info->control.vif == &sdata->vif) {
				__skb_unlink(skb, &local->pending[i]);
				dev_kfree_skb_irq(skb);
			}
		}
	}
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);

	return 0;
}

static void ieee80211_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
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
	spin_lock_bh(&local->filter_lock);
	__dev_addr_sync(&local->mc_list, &local->mc_count,
			&dev->mc_list, &dev->mc_count);
	spin_unlock_bh(&local->filter_lock);
	ieee80211_queue_work(&local->hw, &local->reconfig_filter);
}

/*
 * Called when the netdev is removed or, by the code below, before
 * the interface type changes.
 */
static void ieee80211_teardown_sdata(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct beacon_data *beacon;
	struct sk_buff *skb;
	int flushed;
	int i;

	/* free extra data */
	ieee80211_free_keys(sdata);

	ieee80211_debugfs_remove_netdev(sdata);

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		__skb_queue_purge(&sdata->fragments[i].skb_list);
	sdata->fragment_next = 0;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
		beacon = sdata->u.ap.beacon;
		rcu_assign_pointer(sdata->u.ap.beacon, NULL);
		synchronize_rcu();
		kfree(beacon);

		while ((skb = skb_dequeue(&sdata->u.ap.ps_bc_buf))) {
			local->total_ps_buffered--;
			dev_kfree_skb(skb);
		}

		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif))
			mesh_rmc_free(sdata);
		break;
	case NL80211_IFTYPE_ADHOC:
		if (WARN_ON(sdata->u.ibss.presp))
			kfree_skb(sdata->u.ibss.presp);
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_MONITOR:
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case __NL80211_IFTYPE_AFTER_LAST:
		BUG();
		break;
	}

	flushed = sta_info_flush(local, sdata);
	WARN_ON(flushed);
}

static u16 ieee80211_netdev_select_queue(struct net_device *dev,
					 struct sk_buff *skb)
{
	return ieee80211_select_queue(IEEE80211_DEV_TO_SUB_IF(dev), skb);
}

static const struct net_device_ops ieee80211_dataif_ops = {
	.ndo_open		= ieee80211_open,
	.ndo_stop		= ieee80211_stop,
	.ndo_uninit		= ieee80211_teardown_sdata,
	.ndo_start_xmit		= ieee80211_subif_start_xmit,
	.ndo_set_multicast_list = ieee80211_set_multicast_list,
	.ndo_change_mtu 	= ieee80211_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_select_queue	= ieee80211_netdev_select_queue,
};

static u16 ieee80211_monitor_select_queue(struct net_device *dev,
					  struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hdr *hdr;
	struct ieee80211_radiotap_header *rtap = (void *)skb->data;

	if (local->hw.queues < 4)
		return 0;

	if (skb->len < 4 ||
	    skb->len < le16_to_cpu(rtap->it_len) + 2 /* frame control */)
		return 0; /* doesn't matter, frame will be dropped */

	hdr = (void *)((u8 *)skb->data + le16_to_cpu(rtap->it_len));

	if (!ieee80211_is_data(hdr->frame_control)) {
		skb->priority = 7;
		return ieee802_1d_to_ac[skb->priority];
	}

	skb->priority = 0;
	return ieee80211_downgrade_queue(local, skb);
}

static const struct net_device_ops ieee80211_monitorif_ops = {
	.ndo_open		= ieee80211_open,
	.ndo_stop		= ieee80211_stop,
	.ndo_uninit		= ieee80211_teardown_sdata,
	.ndo_start_xmit		= ieee80211_monitor_start_xmit,
	.ndo_set_multicast_list = ieee80211_set_multicast_list,
	.ndo_change_mtu 	= ieee80211_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_select_queue	= ieee80211_monitor_select_queue,
};

static void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &ieee80211_dataif_ops;
	dev->destructor = free_netdev;
}

/*
 * Helper function to initialise an interface to a specific type.
 */
static void ieee80211_setup_sdata(struct ieee80211_sub_if_data *sdata,
				  enum nl80211_iftype type)
{
	/* clear type-dependent union */
	memset(&sdata->u, 0, sizeof(sdata->u));

	/* and set some type-dependent values */
	sdata->vif.type = type;
	sdata->dev->netdev_ops = &ieee80211_dataif_ops;
	sdata->wdev.iftype = type;

	/* only monitor differs */
	sdata->dev->type = ARPHRD_ETHER;

	switch (type) {
	case NL80211_IFTYPE_AP:
		skb_queue_head_init(&sdata->u.ap.ps_bc_buf);
		INIT_LIST_HEAD(&sdata->u.ap.vlans);
		break;
	case NL80211_IFTYPE_STATION:
		ieee80211_sta_setup_sdata(sdata);
		break;
	case NL80211_IFTYPE_ADHOC:
		ieee80211_ibss_setup_sdata(sdata);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif))
			ieee80211_mesh_init_sdata(sdata);
		break;
	case NL80211_IFTYPE_MONITOR:
		sdata->dev->type = ARPHRD_IEEE80211_RADIOTAP;
		sdata->dev->netdev_ops = &ieee80211_monitorif_ops;
		sdata->u.mntr_flags = MONITOR_FLAG_CONTROL |
				      MONITOR_FLAG_OTHER_BSS;
		break;
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_AP_VLAN:
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case __NL80211_IFTYPE_AFTER_LAST:
		BUG();
		break;
	}

	ieee80211_debugfs_add_netdev(sdata);
}

int ieee80211_if_change_type(struct ieee80211_sub_if_data *sdata,
			     enum nl80211_iftype type)
{
	ASSERT_RTNL();

	if (type == sdata->vif.type)
		return 0;

	/* Setting ad-hoc mode on non-IBSS channel is not supported. */
	if (sdata->local->oper_channel->flags & IEEE80211_CHAN_NO_IBSS &&
	    type == NL80211_IFTYPE_ADHOC)
		return -EOPNOTSUPP;

	/*
	 * We could, here, on changes between IBSS/STA/MESH modes,
	 * invoke an MLME function instead that disassociates etc.
	 * and goes into the requested mode.
	 */

	if (netif_running(sdata->dev))
		return -EBUSY;

	/* Purge and reset type-dependent state. */
	ieee80211_teardown_sdata(sdata->dev);
	ieee80211_setup_sdata(sdata, type);

	/* reset some values that shouldn't be kept across type changes */
	sdata->vif.bss_conf.basic_rates =
		ieee80211_mandatory_rates(sdata->local,
			sdata->local->hw.conf.channel->band);
	sdata->drop_unencrypted = 0;

	return 0;
}

static struct device_type wiphy_type = {
	.name	= "wlan",
};

int ieee80211_if_add(struct ieee80211_local *local, const char *name,
		     struct net_device **new_dev, enum nl80211_iftype type,
		     struct vif_params *params)
{
	struct net_device *ndev;
	struct ieee80211_sub_if_data *sdata = NULL;
	int ret, i;

	ASSERT_RTNL();

	ndev = alloc_netdev_mq(sizeof(*sdata) + local->hw.vif_data_size,
			       name, ieee80211_if_setup, local->hw.queues);
	if (!ndev)
		return -ENOMEM;
	dev_net_set(ndev, wiphy_net(local->hw.wiphy));

	ndev->needed_headroom = local->tx_headroom +
				4*6 /* four MAC addresses */
				+ 2 + 2 + 2 + 2 /* ctl, dur, seq, qos */
				+ 6 /* mesh */
				+ 8 /* rfc1042/bridge tunnel */
				- ETH_HLEN /* ethernet hard_header_len */
				+ IEEE80211_ENCRYPT_HEADROOM;
	ndev->needed_tailroom = IEEE80211_ENCRYPT_TAILROOM;

	ret = dev_alloc_name(ndev, ndev->name);
	if (ret < 0)
		goto fail;

	memcpy(ndev->dev_addr, local->hw.wiphy->perm_addr, ETH_ALEN);
	SET_NETDEV_DEV(ndev, wiphy_dev(local->hw.wiphy));
	SET_NETDEV_DEVTYPE(ndev, &wiphy_type);

	/* don't use IEEE80211_DEV_TO_SUB_IF because it checks too much */
	sdata = netdev_priv(ndev);
	ndev->ieee80211_ptr = &sdata->wdev;

	/* initialise type-independent data */
	sdata->wdev.wiphy = local->hw.wiphy;
	sdata->local = local;
	sdata->dev = ndev;

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		skb_queue_head_init(&sdata->fragments[i].skb_list);

	INIT_LIST_HEAD(&sdata->key_list);

	sdata->force_unicast_rateidx = -1;
	sdata->max_ratectrl_rateidx = -1;

	/* setup type-dependent data */
	ieee80211_setup_sdata(sdata, type);

	ret = register_netdevice(ndev);
	if (ret)
		goto fail;

	if (ieee80211_vif_is_mesh(&sdata->vif) &&
	    params && params->mesh_id_len)
		ieee80211_sdata_set_mesh_id(sdata,
					    params->mesh_id_len,
					    params->mesh_id);

	mutex_lock(&local->iflist_mtx);
	list_add_tail_rcu(&sdata->list, &local->interfaces);
	mutex_unlock(&local->iflist_mtx);

	if (new_dev)
		*new_dev = ndev;

	return 0;

 fail:
	free_netdev(ndev);
	return ret;
}

void ieee80211_if_remove(struct ieee80211_sub_if_data *sdata)
{
	ASSERT_RTNL();

	mutex_lock(&sdata->local->iflist_mtx);
	list_del_rcu(&sdata->list);
	mutex_unlock(&sdata->local->iflist_mtx);

	synchronize_rcu();
	unregister_netdevice(sdata->dev);
}

/*
 * Remove all interfaces, may only be called at hardware unregistration
 * time because it doesn't do RCU-safe list removals.
 */
void ieee80211_remove_interfaces(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *tmp;

	ASSERT_RTNL();

	list_for_each_entry_safe(sdata, tmp, &local->interfaces, list) {
		/*
		 * we cannot hold the iflist_mtx across unregister_netdevice,
		 * but we only need to hold it for list modifications to lock
		 * out readers since we're under the RTNL here as all other
		 * writers.
		 */
		mutex_lock(&local->iflist_mtx);
		list_del(&sdata->list);
		mutex_unlock(&local->iflist_mtx);

		unregister_netdevice(sdata->dev);
	}
}

static u32 ieee80211_idle_off(struct ieee80211_local *local,
			      const char *reason)
{
	if (!(local->hw.conf.flags & IEEE80211_CONF_IDLE))
		return 0;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: device no longer idle - %s\n",
	       wiphy_name(local->hw.wiphy), reason);
#endif

	local->hw.conf.flags &= ~IEEE80211_CONF_IDLE;
	return IEEE80211_CONF_CHANGE_IDLE;
}

static u32 ieee80211_idle_on(struct ieee80211_local *local)
{
	if (local->hw.conf.flags & IEEE80211_CONF_IDLE)
		return 0;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: device now idle\n",
	       wiphy_name(local->hw.wiphy));
#endif

	local->hw.conf.flags |= IEEE80211_CONF_IDLE;
	return IEEE80211_CONF_CHANGE_IDLE;
}

u32 __ieee80211_recalc_idle(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	int count = 0;

	if (local->scanning)
		return ieee80211_idle_off(local, "scanning");

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;
		/* do not count disabled managed interfaces */
		if (sdata->vif.type == NL80211_IFTYPE_STATION &&
		    !sdata->u.mgd.associated &&
		    list_empty(&sdata->u.mgd.work_list))
			continue;
		/* do not count unused IBSS interfaces */
		if (sdata->vif.type == NL80211_IFTYPE_ADHOC &&
		    !sdata->u.ibss.ssid_len)
			continue;
		/* count everything else */
		count++;
	}

	if (!count)
		return ieee80211_idle_on(local);
	else
		return ieee80211_idle_off(local, "in use");

	return 0;
}

void ieee80211_recalc_idle(struct ieee80211_local *local)
{
	u32 chg;

	mutex_lock(&local->iflist_mtx);
	chg = __ieee80211_recalc_idle(local);
	mutex_unlock(&local->iflist_mtx);
	if (chg)
		ieee80211_hw_config(local, chg);
}
