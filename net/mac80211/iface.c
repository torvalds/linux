/*
 * Interface handling
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
#include <linux/slab.h>
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
#include "rate.h"

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

bool __ieee80211_recalc_txpower(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	int power;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (!chanctx_conf) {
		rcu_read_unlock();
		return false;
	}

	power = chanctx_conf->def.chan->max_power;
	rcu_read_unlock();

	if (sdata->user_power_level != IEEE80211_UNSET_POWER_LEVEL)
		power = min(power, sdata->user_power_level);

	if (sdata->ap_power_level != IEEE80211_UNSET_POWER_LEVEL)
		power = min(power, sdata->ap_power_level);

	if (power != sdata->vif.bss_conf.txpower) {
		sdata->vif.bss_conf.txpower = power;
		ieee80211_hw_config(sdata->local, 0);
		return true;
	}

	return false;
}

void ieee80211_recalc_txpower(struct ieee80211_sub_if_data *sdata)
{
	if (__ieee80211_recalc_txpower(sdata))
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_TXPOWER);
}

u32 ieee80211_idle_off(struct ieee80211_local *local)
{
	if (!(local->hw.conf.flags & IEEE80211_CONF_IDLE))
		return 0;

	local->hw.conf.flags &= ~IEEE80211_CONF_IDLE;
	return IEEE80211_CONF_CHANGE_IDLE;
}

static u32 ieee80211_idle_on(struct ieee80211_local *local)
{
	if (local->hw.conf.flags & IEEE80211_CONF_IDLE)
		return 0;

	ieee80211_flush_queues(local, NULL);

	local->hw.conf.flags |= IEEE80211_CONF_IDLE;
	return IEEE80211_CONF_CHANGE_IDLE;
}

void ieee80211_recalc_idle(struct ieee80211_local *local)
{
	bool working = false, scanning, active;
	unsigned int led_trig_start = 0, led_trig_stop = 0;
	struct ieee80211_roc_work *roc;
	u32 change;

	lockdep_assert_held(&local->mtx);

	active = !list_empty(&local->chanctx_list) || local->monitors;

	if (!local->ops->remain_on_channel) {
		list_for_each_entry(roc, &local->roc_list, list) {
			working = true;
			break;
		}
	}

	scanning = test_bit(SCAN_SW_SCANNING, &local->scanning) ||
		   test_bit(SCAN_ONCHANNEL_SCANNING, &local->scanning);

	if (working || scanning)
		led_trig_start |= IEEE80211_TPT_LEDTRIG_FL_WORK;
	else
		led_trig_stop |= IEEE80211_TPT_LEDTRIG_FL_WORK;

	if (active)
		led_trig_start |= IEEE80211_TPT_LEDTRIG_FL_CONNECTED;
	else
		led_trig_stop |= IEEE80211_TPT_LEDTRIG_FL_CONNECTED;

	ieee80211_mod_tpt_led_trig(local, led_trig_start, led_trig_stop);

	if (working || scanning || active)
		change = ieee80211_idle_off(local);
	else
		change = ieee80211_idle_on(local);
	if (change)
		ieee80211_hw_config(local, change);
}

static int ieee80211_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 256 || new_mtu > IEEE80211_MAX_DATA_LEN)
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static int ieee80211_verify_mac(struct ieee80211_local *local, u8 *addr)
{
	struct ieee80211_sub_if_data *sdata;
	u64 new, mask, tmp;
	u8 *m;
	int ret = 0;

	if (is_zero_ether_addr(local->hw.wiphy->addr_mask))
		return 0;

	m = addr;
	new =	((u64)m[0] << 5*8) | ((u64)m[1] << 4*8) |
		((u64)m[2] << 3*8) | ((u64)m[3] << 2*8) |
		((u64)m[4] << 1*8) | ((u64)m[5] << 0*8);

	m = local->hw.wiphy->addr_mask;
	mask =	((u64)m[0] << 5*8) | ((u64)m[1] << 4*8) |
		((u64)m[2] << 3*8) | ((u64)m[3] << 2*8) |
		((u64)m[4] << 1*8) | ((u64)m[5] << 0*8);


	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type == NL80211_IFTYPE_MONITOR)
			continue;

		m = sdata->vif.addr;
		tmp =	((u64)m[0] << 5*8) | ((u64)m[1] << 4*8) |
			((u64)m[2] << 3*8) | ((u64)m[3] << 2*8) |
			((u64)m[4] << 1*8) | ((u64)m[5] << 0*8);

		if ((new & ~mask) != (tmp & ~mask)) {
			ret = -EINVAL;
			break;
		}
	}
	mutex_unlock(&local->iflist_mtx);

	return ret;
}

static int ieee80211_change_mac(struct net_device *dev, void *addr)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sockaddr *sa = addr;
	int ret;

	if (ieee80211_sdata_running(sdata))
		return -EBUSY;

	ret = ieee80211_verify_mac(sdata->local, sa->sa_data);
	if (ret)
		return ret;

	ret = eth_mac_addr(dev, sa);

	if (ret == 0)
		memcpy(sdata->vif.addr, sa->sa_data, ETH_ALEN);

	return ret;
}

static inline int identical_mac_addr_allowed(int type1, int type2)
{
	return type1 == NL80211_IFTYPE_MONITOR ||
		type2 == NL80211_IFTYPE_MONITOR ||
		type1 == NL80211_IFTYPE_P2P_DEVICE ||
		type2 == NL80211_IFTYPE_P2P_DEVICE ||
		(type1 == NL80211_IFTYPE_AP && type2 == NL80211_IFTYPE_WDS) ||
		(type1 == NL80211_IFTYPE_WDS &&
			(type2 == NL80211_IFTYPE_WDS ||
			 type2 == NL80211_IFTYPE_AP)) ||
		(type1 == NL80211_IFTYPE_AP && type2 == NL80211_IFTYPE_AP_VLAN) ||
		(type1 == NL80211_IFTYPE_AP_VLAN &&
			(type2 == NL80211_IFTYPE_AP ||
			 type2 == NL80211_IFTYPE_AP_VLAN));
}

static int ieee80211_check_concurrent_iface(struct ieee80211_sub_if_data *sdata,
					    enum nl80211_iftype iftype)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *nsdata;

	ASSERT_RTNL();

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(nsdata, &local->interfaces, list) {
		if (nsdata != sdata && ieee80211_sdata_running(nsdata)) {
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
			if (iftype == NL80211_IFTYPE_ADHOC &&
			    nsdata->vif.type == NL80211_IFTYPE_ADHOC)
				return -EBUSY;

			/*
			 * The remaining checks are only performed for interfaces
			 * with the same MAC address.
			 */
			if (!ether_addr_equal(sdata->vif.addr,
					      nsdata->vif.addr))
				continue;

			/*
			 * check whether it may have the same address
			 */
			if (!identical_mac_addr_allowed(iftype,
							nsdata->vif.type))
				return -ENOTUNIQ;

			/*
			 * can only add VLANs to enabled APs
			 */
			if (iftype == NL80211_IFTYPE_AP_VLAN &&
			    nsdata->vif.type == NL80211_IFTYPE_AP)
				sdata->bss = &nsdata->u.ap;
		}
	}

	return 0;
}

static int ieee80211_check_queues(struct ieee80211_sub_if_data *sdata)
{
	int n_queues = sdata->local->hw.queues;
	int i;

	if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE) {
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			if (WARN_ON_ONCE(sdata->vif.hw_queue[i] ==
					 IEEE80211_INVAL_HW_QUEUE))
				return -EINVAL;
			if (WARN_ON_ONCE(sdata->vif.hw_queue[i] >=
					 n_queues))
				return -EINVAL;
		}
	}

	if ((sdata->vif.type != NL80211_IFTYPE_AP &&
	     sdata->vif.type != NL80211_IFTYPE_MESH_POINT) ||
	    !(sdata->local->hw.flags & IEEE80211_HW_QUEUE_CONTROL)) {
		sdata->vif.cab_queue = IEEE80211_INVAL_HW_QUEUE;
		return 0;
	}

	if (WARN_ON_ONCE(sdata->vif.cab_queue == IEEE80211_INVAL_HW_QUEUE))
		return -EINVAL;

	if (WARN_ON_ONCE(sdata->vif.cab_queue >= n_queues))
		return -EINVAL;

	return 0;
}

void ieee80211_adjust_monitor_flags(struct ieee80211_sub_if_data *sdata,
				    const int offset)
{
	struct ieee80211_local *local = sdata->local;
	u32 flags = sdata->u.mntr_flags;

#define ADJUST(_f, _s)	do {					\
	if (flags & MONITOR_FLAG_##_f)				\
		local->fif_##_s += offset;			\
	} while (0)

	ADJUST(FCSFAIL, fcsfail);
	ADJUST(PLCPFAIL, plcpfail);
	ADJUST(CONTROL, control);
	ADJUST(CONTROL, pspoll);
	ADJUST(OTHER_BSS, other_bss);

#undef ADJUST
}

static void ieee80211_set_default_queues(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		if (local->hw.flags & IEEE80211_HW_QUEUE_CONTROL)
			sdata->vif.hw_queue[i] = IEEE80211_INVAL_HW_QUEUE;
		else if (local->hw.queues >= IEEE80211_NUM_ACS)
			sdata->vif.hw_queue[i] = i;
		else
			sdata->vif.hw_queue[i] = 0;
	}
	sdata->vif.cab_queue = IEEE80211_INVAL_HW_QUEUE;
}

int ieee80211_add_virtual_monitor(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	int ret;

	if (!(local->hw.flags & IEEE80211_HW_WANT_MONITOR_VIF))
		return 0;

	ASSERT_RTNL();

	if (local->monitor_sdata)
		return 0;

	sdata = kzalloc(sizeof(*sdata) + local->hw.vif_data_size, GFP_KERNEL);
	if (!sdata)
		return -ENOMEM;

	/* set up data */
	sdata->local = local;
	sdata->vif.type = NL80211_IFTYPE_MONITOR;
	snprintf(sdata->name, IFNAMSIZ, "%s-monitor",
		 wiphy_name(local->hw.wiphy));

	ieee80211_set_default_queues(sdata);

	ret = drv_add_interface(local, sdata);
	if (WARN_ON(ret)) {
		/* ok .. stupid driver, it asked for this! */
		kfree(sdata);
		return ret;
	}

	ret = ieee80211_check_queues(sdata);
	if (ret) {
		kfree(sdata);
		return ret;
	}

	ret = ieee80211_vif_use_channel(sdata, &local->monitor_chandef,
					IEEE80211_CHANCTX_EXCLUSIVE);
	if (ret) {
		drv_remove_interface(local, sdata);
		kfree(sdata);
		return ret;
	}

	mutex_lock(&local->iflist_mtx);
	rcu_assign_pointer(local->monitor_sdata, sdata);
	mutex_unlock(&local->iflist_mtx);

	return 0;
}

void ieee80211_del_virtual_monitor(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	if (!(local->hw.flags & IEEE80211_HW_WANT_MONITOR_VIF))
		return;

	ASSERT_RTNL();

	mutex_lock(&local->iflist_mtx);

	sdata = rcu_dereference_protected(local->monitor_sdata,
					  lockdep_is_held(&local->iflist_mtx));
	if (!sdata) {
		mutex_unlock(&local->iflist_mtx);
		return;
	}

	rcu_assign_pointer(local->monitor_sdata, NULL);
	mutex_unlock(&local->iflist_mtx);

	synchronize_net();

	ieee80211_vif_release_channel(sdata);

	drv_remove_interface(local, sdata);

	kfree(sdata);
}

/*
 * NOTE: Be very careful when changing this function, it must NOT return
 * an error on interface type changes that have been pre-checked, so most
 * checks should be in ieee80211_check_concurrent_iface.
 */
int ieee80211_do_open(struct wireless_dev *wdev, bool coming_up)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct net_device *dev = wdev->netdev;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	u32 changed = 0;
	int res;
	u32 hw_reconf_flags = 0;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_WDS:
		if (!is_valid_ether_addr(sdata->u.wds.remote_addr))
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_AP_VLAN: {
		struct ieee80211_sub_if_data *master;

		if (!sdata->bss)
			return -ENOLINK;

		list_add(&sdata->u.vlan.list, &sdata->bss->vlans);

		master = container_of(sdata->bss,
				      struct ieee80211_sub_if_data, u.ap);
		sdata->control_port_protocol =
			master->control_port_protocol;
		sdata->control_port_no_encrypt =
			master->control_port_no_encrypt;
		break;
		}
	case NL80211_IFTYPE_AP:
		sdata->bss = &sdata->u.ap;
		break;
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_P2P_DEVICE:
		/* no special treatment */
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
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
		ieee80211_mod_tpt_led_trig(local,
					   IEEE80211_TPT_LEDTRIG_FL_RADIO, 0);
	}

	/*
	 * Copy the hopefully now-present MAC address to
	 * this interface, if it has the special null one.
	 */
	if (dev && is_zero_ether_addr(dev->dev_addr)) {
		memcpy(dev->dev_addr,
		       local->hw.wiphy->perm_addr,
		       ETH_ALEN);
		memcpy(dev->perm_addr, dev->dev_addr, ETH_ALEN);

		if (!is_valid_ether_addr(dev->dev_addr)) {
			res = -EADDRNOTAVAIL;
			goto err_stop;
		}
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		/* no need to tell driver, but set carrier and chanctx */
		if (rtnl_dereference(sdata->bss->beacon)) {
			ieee80211_vif_vlan_copy_chanctx(sdata);
			netif_carrier_on(dev);
		} else {
			netif_carrier_off(dev);
		}
		break;
	case NL80211_IFTYPE_MONITOR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs++;
			break;
		}

		if (local->monitors == 0 && local->open_count == 0) {
			res = ieee80211_add_virtual_monitor(local);
			if (res)
				goto err_stop;
		}

		/* must be before the call to ieee80211_configure_filter */
		local->monitors++;
		if (local->monitors == 1) {
			local->hw.conf.flags |= IEEE80211_CONF_MONITOR;
			hw_reconf_flags |= IEEE80211_CONF_CHANGE_MONITOR;
		}

		ieee80211_adjust_monitor_flags(sdata, 1);
		ieee80211_configure_filter(local);
		mutex_lock(&local->mtx);
		ieee80211_recalc_idle(local);
		mutex_unlock(&local->mtx);

		netif_carrier_on(dev);
		break;
	default:
		if (coming_up) {
			ieee80211_del_virtual_monitor(local);

			res = drv_add_interface(local, sdata);
			if (res)
				goto err_stop;
			res = ieee80211_check_queues(sdata);
			if (res)
				goto err_del_interface;
		}

		if (sdata->vif.type == NL80211_IFTYPE_AP) {
			local->fif_pspoll++;
			local->fif_probe_req++;

			ieee80211_configure_filter(local);
		} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
			local->fif_probe_req++;
		}

		if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE)
			changed |= ieee80211_reset_erp_info(sdata);
		ieee80211_bss_info_change_notify(sdata, changed);

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_MESH_POINT:
			netif_carrier_off(dev);
			break;
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_P2P_DEVICE:
			break;
		default:
			/* not reached */
			WARN_ON(1);
		}

		/*
		 * set default queue parameters so drivers don't
		 * need to initialise the hardware if the hardware
		 * doesn't start up with sane defaults
		 */
		ieee80211_set_wmm_default(sdata, true);
	}

	set_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (sdata->vif.type == NL80211_IFTYPE_WDS) {
		/* Create STA entry for the WDS peer */
		sta = sta_info_alloc(sdata, sdata->u.wds.remote_addr,
				     GFP_KERNEL);
		if (!sta) {
			res = -ENOMEM;
			goto err_del_interface;
		}

		sta_info_pre_move_state(sta, IEEE80211_STA_AUTH);
		sta_info_pre_move_state(sta, IEEE80211_STA_ASSOC);
		sta_info_pre_move_state(sta, IEEE80211_STA_AUTHORIZED);

		res = sta_info_insert(sta);
		if (res) {
			/* STA has been freed */
			goto err_del_interface;
		}

		rate_control_rate_init(sta);
		netif_carrier_on(dev);
	} else if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE) {
		rcu_assign_pointer(local->p2p_sdata, sdata);
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

	if (coming_up)
		local->open_count++;

	if (hw_reconf_flags)
		ieee80211_hw_config(local, hw_reconf_flags);

	ieee80211_recalc_ps(local, -1);

	if (dev) {
		unsigned long flags;
		int n_acs = IEEE80211_NUM_ACS;
		int ac;

		if (local->hw.queues < IEEE80211_NUM_ACS)
			n_acs = 1;

		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
		if (sdata->vif.cab_queue == IEEE80211_INVAL_HW_QUEUE ||
		    (local->queue_stop_reasons[sdata->vif.cab_queue] == 0 &&
		     skb_queue_empty(&local->pending[sdata->vif.cab_queue]))) {
			for (ac = 0; ac < n_acs; ac++) {
				int ac_queue = sdata->vif.hw_queue[ac];

				if (local->queue_stop_reasons[ac_queue] == 0 &&
				    skb_queue_empty(&local->pending[ac_queue]))
					netif_start_subqueue(dev, ac);
			}
		}
		spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	}

	return 0;
 err_del_interface:
	drv_remove_interface(local, sdata);
 err_stop:
	if (!local->open_count)
		drv_stop(local);
 err_del_bss:
	sdata->bss = NULL;
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		list_del(&sdata->u.vlan.list);
	/* might already be clear but that doesn't matter */
	clear_bit(SDATA_STATE_RUNNING, &sdata->state);
	return res;
}

static int ieee80211_open(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int err;

	/* fail early if user set an invalid address */
	if (!is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	err = ieee80211_check_concurrent_iface(sdata, sdata->vif.type);
	if (err)
		return err;

	return ieee80211_do_open(&sdata->wdev, true);
}

static void ieee80211_do_stop(struct ieee80211_sub_if_data *sdata,
			      bool going_down)
{
	struct ieee80211_local *local = sdata->local;
	unsigned long flags;
	struct sk_buff *skb, *tmp;
	u32 hw_reconf_flags = 0;
	int i, flushed;
	struct ps_data *ps;

	clear_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (rcu_access_pointer(local->scan_sdata) == sdata)
		ieee80211_scan_cancel(local);

	/*
	 * Stop TX on this interface first.
	 */
	if (sdata->dev)
		netif_tx_stop_all_queues(sdata->dev);

	ieee80211_roc_purge(local, sdata);

	if (sdata->vif.type == NL80211_IFTYPE_STATION)
		ieee80211_mgd_stop(sdata);

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
	 * This is relevant only in WDS mode, in all other modes we've
	 * already removed all stations when disconnecting or similar,
	 * so warn otherwise.
	 *
	 * We call sta_info_flush_cleanup() later, to combine RCU waits.
	 */
	flushed = sta_info_flush_defer(sdata);
	WARN_ON_ONCE((sdata->vif.type != NL80211_IFTYPE_WDS && flushed > 0) ||
		     (sdata->vif.type == NL80211_IFTYPE_WDS && flushed != 1));

	/* don't count this interface for promisc/allmulti while it is down */
	if (sdata->flags & IEEE80211_SDATA_ALLMULTI)
		atomic_dec(&local->iff_allmultis);

	if (sdata->flags & IEEE80211_SDATA_PROMISC)
		atomic_dec(&local->iff_promiscs);

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		local->fif_pspoll--;
		local->fif_probe_req--;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		local->fif_probe_req--;
	}

	if (sdata->dev) {
		netif_addr_lock_bh(sdata->dev);
		spin_lock_bh(&local->filter_lock);
		__hw_addr_unsync(&local->mc_list, &sdata->dev->mc,
				 sdata->dev->addr_len);
		spin_unlock_bh(&local->filter_lock);
		netif_addr_unlock_bh(sdata->dev);
	}

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	cancel_work_sync(&sdata->recalc_smps);

	cancel_delayed_work_sync(&sdata->dfs_cac_timer_work);

	if (sdata->wdev.cac_started) {
		WARN_ON(local->suspended);
		mutex_lock(&local->iflist_mtx);
		ieee80211_vif_release_channel(sdata);
		mutex_unlock(&local->iflist_mtx);
		cfg80211_cac_event(sdata->dev, NL80211_RADAR_CAC_ABORTED,
				   GFP_KERNEL);
	}

	/* APs need special treatment */
	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		struct ieee80211_sub_if_data *vlan, *tmpsdata;

		/* down all dependent devices, that is VLANs */
		list_for_each_entry_safe(vlan, tmpsdata, &sdata->u.ap.vlans,
					 u.vlan.list)
			dev_close(vlan->dev);
		WARN_ON(!list_empty(&sdata->u.ap.vlans));
	} else if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		/* remove all packets in parent bc_buf pointing to this dev */
		ps = &sdata->bss->ps;

		spin_lock_irqsave(&ps->bc_buf.lock, flags);
		skb_queue_walk_safe(&ps->bc_buf, skb, tmp) {
			if (skb->dev == sdata->dev) {
				__skb_unlink(skb, &ps->bc_buf);
				local->total_ps_buffered--;
				ieee80211_free_txskb(&local->hw, skb);
			}
		}
		spin_unlock_irqrestore(&ps->bc_buf.lock, flags);
	}

	if (going_down)
		local->open_count--;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		list_del(&sdata->u.vlan.list);
		rcu_assign_pointer(sdata->vif.chanctx_conf, NULL);
		/* no need to tell driver */
		break;
	case NL80211_IFTYPE_MONITOR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs--;
			break;
		}

		local->monitors--;
		if (local->monitors == 0) {
			local->hw.conf.flags &= ~IEEE80211_CONF_MONITOR;
			hw_reconf_flags |= IEEE80211_CONF_CHANGE_MONITOR;
		}

		ieee80211_adjust_monitor_flags(sdata, -1);
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		/* relies on synchronize_rcu() below */
		rcu_assign_pointer(local->p2p_sdata, NULL);
		/* fall through */
	default:
		cancel_work_sync(&sdata->work);
		/*
		 * When we get here, the interface is marked down.
		 *
		 * sta_info_flush_cleanup() requires rcu_barrier()
		 * first to wait for the station call_rcu() calls
		 * to complete, here we need at least sychronize_rcu()
		 * it to wait for the RX path in case it is using the
		 * interface and enqueuing frames at this very time on
		 * another CPU.
		 */
		rcu_barrier();
		sta_info_flush_cleanup(sdata);

		/*
		 * Free all remaining keys, there shouldn't be any,
		 * except maybe in WDS mode?
		 */
		ieee80211_free_keys(sdata);

		/* fall through */
	case NL80211_IFTYPE_AP:
		skb_queue_purge(&sdata->skb_queue);
	}

	sdata->bss = NULL;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	for (i = 0; i < IEEE80211_MAX_QUEUES; i++) {
		skb_queue_walk_safe(&local->pending[i], skb, tmp) {
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
			if (info->control.vif == &sdata->vif) {
				__skb_unlink(skb, &local->pending[i]);
				ieee80211_free_txskb(&local->hw, skb);
			}
		}
	}
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);

	if (local->open_count == 0)
		ieee80211_clear_tx_pending(local);

	/*
	 * If the interface goes down while suspended, presumably because
	 * the device was unplugged and that happens before our resume,
	 * then the driver is already unconfigured and the remainder of
	 * this function isn't needed.
	 * XXX: what about WoWLAN? If the device has software state, e.g.
	 *	memory allocated, it might expect teardown commands from
	 *	mac80211 here?
	 */
	if (local->suspended) {
		WARN_ON(local->wowlan);
		WARN_ON(rtnl_dereference(local->monitor_sdata));
		return;
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		break;
	case NL80211_IFTYPE_MONITOR:
		if (local->monitors == 0)
			ieee80211_del_virtual_monitor(local);

		mutex_lock(&local->mtx);
		ieee80211_recalc_idle(local);
		mutex_unlock(&local->mtx);
		break;
	default:
		if (going_down)
			drv_remove_interface(local, sdata);
	}

	ieee80211_recalc_ps(local, -1);

	if (local->open_count == 0) {
		ieee80211_stop_device(local);

		/* no reconfiguring after stop! */
		return;
	}

	/* do after stop to avoid reconfiguring when we stop anyway */
	ieee80211_configure_filter(local);
	ieee80211_hw_config(local, hw_reconf_flags);

	if (local->monitors == local->open_count)
		ieee80211_add_virtual_monitor(local);
}

static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_do_stop(sdata, true);

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

	/*
	 * TODO: If somebody needs this on AP interfaces,
	 *	 it can be enabled easily but multicast
	 *	 addresses from VLANs need to be synced.
	 */
	if (sdata->vif.type != NL80211_IFTYPE_MONITOR &&
	    sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
	    sdata->vif.type != NL80211_IFTYPE_AP)
		drv_set_multicast_list(local, sdata, &dev->mc);

	spin_lock_bh(&local->filter_lock);
	__hw_addr_sync(&local->mc_list, &dev->mc, dev->addr_len);
	spin_unlock_bh(&local->filter_lock);
	ieee80211_queue_work(&local->hw, &local->reconfig_filter);
}

/*
 * Called when the netdev is removed or, by the code below, before
 * the interface type changes.
 */
static void ieee80211_teardown_sdata(struct ieee80211_sub_if_data *sdata)
{
	int flushed;
	int i;

	/* free extra data */
	ieee80211_free_keys(sdata);

	ieee80211_debugfs_remove_netdev(sdata);

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		__skb_queue_purge(&sdata->fragments[i].skb_list);
	sdata->fragment_next = 0;

	if (ieee80211_vif_is_mesh(&sdata->vif))
		mesh_rmc_free(sdata);

	flushed = sta_info_flush(sdata);
	WARN_ON(flushed);
}

static void ieee80211_uninit(struct net_device *dev)
{
	ieee80211_teardown_sdata(IEEE80211_DEV_TO_SUB_IF(dev));
}

static u16 ieee80211_netdev_select_queue(struct net_device *dev,
					 struct sk_buff *skb)
{
	return ieee80211_select_queue(IEEE80211_DEV_TO_SUB_IF(dev), skb);
}

static const struct net_device_ops ieee80211_dataif_ops = {
	.ndo_open		= ieee80211_open,
	.ndo_stop		= ieee80211_stop,
	.ndo_uninit		= ieee80211_uninit,
	.ndo_start_xmit		= ieee80211_subif_start_xmit,
	.ndo_set_rx_mode	= ieee80211_set_multicast_list,
	.ndo_change_mtu 	= ieee80211_change_mtu,
	.ndo_set_mac_address 	= ieee80211_change_mac,
	.ndo_select_queue	= ieee80211_netdev_select_queue,
};

static u16 ieee80211_monitor_select_queue(struct net_device *dev,
					  struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hdr *hdr;
	struct ieee80211_radiotap_header *rtap = (void *)skb->data;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return 0;

	if (skb->len < 4 ||
	    skb->len < le16_to_cpu(rtap->it_len) + 2 /* frame control */)
		return 0; /* doesn't matter, frame will be dropped */

	hdr = (void *)((u8 *)skb->data + le16_to_cpu(rtap->it_len));

	return ieee80211_select_queue_80211(sdata, skb, hdr);
}

static const struct net_device_ops ieee80211_monitorif_ops = {
	.ndo_open		= ieee80211_open,
	.ndo_stop		= ieee80211_stop,
	.ndo_uninit		= ieee80211_uninit,
	.ndo_start_xmit		= ieee80211_monitor_start_xmit,
	.ndo_set_rx_mode	= ieee80211_set_multicast_list,
	.ndo_change_mtu 	= ieee80211_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_select_queue	= ieee80211_monitor_select_queue,
};

static void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->netdev_ops = &ieee80211_dataif_ops;
	dev->destructor = free_netdev;
}

static void ieee80211_iface_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, work);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct sta_info *sta;
	struct ieee80211_ra_tid *ra_tid;

	if (!ieee80211_sdata_running(sdata))
		return;

	if (local->scanning)
		return;

	/*
	 * ieee80211_queue_work() should have picked up most cases,
	 * here we'll pick the rest.
	 */
	if (WARN(local->suspended,
		 "interface work scheduled while going to suspend\n"))
		return;

	/* first process frames */
	while ((skb = skb_dequeue(&sdata->skb_queue))) {
		struct ieee80211_mgmt *mgmt = (void *)skb->data;

		if (skb->pkt_type == IEEE80211_SDATA_QUEUE_AGG_START) {
			ra_tid = (void *)&skb->cb;
			ieee80211_start_tx_ba_cb(&sdata->vif, ra_tid->ra,
						 ra_tid->tid);
		} else if (skb->pkt_type == IEEE80211_SDATA_QUEUE_AGG_STOP) {
			ra_tid = (void *)&skb->cb;
			ieee80211_stop_tx_ba_cb(&sdata->vif, ra_tid->ra,
						ra_tid->tid);
		} else if (ieee80211_is_action(mgmt->frame_control) &&
			   mgmt->u.action.category == WLAN_CATEGORY_BACK) {
			int len = skb->len;

			mutex_lock(&local->sta_mtx);
			sta = sta_info_get_bss(sdata, mgmt->sa);
			if (sta) {
				switch (mgmt->u.action.u.addba_req.action_code) {
				case WLAN_ACTION_ADDBA_REQ:
					ieee80211_process_addba_request(
							local, sta, mgmt, len);
					break;
				case WLAN_ACTION_ADDBA_RESP:
					ieee80211_process_addba_resp(local, sta,
								     mgmt, len);
					break;
				case WLAN_ACTION_DELBA:
					ieee80211_process_delba(sdata, sta,
								mgmt, len);
					break;
				default:
					WARN_ON(1);
					break;
				}
			}
			mutex_unlock(&local->sta_mtx);
		} else if (ieee80211_is_data_qos(mgmt->frame_control)) {
			struct ieee80211_hdr *hdr = (void *)mgmt;
			/*
			 * So the frame isn't mgmt, but frame_control
			 * is at the right place anyway, of course, so
			 * the if statement is correct.
			 *
			 * Warn if we have other data frame types here,
			 * they must not get here.
			 */
			WARN_ON(hdr->frame_control &
					cpu_to_le16(IEEE80211_STYPE_NULLFUNC));
			WARN_ON(!(hdr->seq_ctrl &
					cpu_to_le16(IEEE80211_SCTL_FRAG)));
			/*
			 * This was a fragment of a frame, received while
			 * a block-ack session was active. That cannot be
			 * right, so terminate the session.
			 */
			mutex_lock(&local->sta_mtx);
			sta = sta_info_get_bss(sdata, mgmt->sa);
			if (sta) {
				u16 tid = *ieee80211_get_qos_ctl(hdr) &
						IEEE80211_QOS_CTL_TID_MASK;

				__ieee80211_stop_rx_ba_session(
					sta, tid, WLAN_BACK_RECIPIENT,
					WLAN_REASON_QSTA_REQUIRE_SETUP,
					true);
			}
			mutex_unlock(&local->sta_mtx);
		} else switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			ieee80211_sta_rx_queued_mgmt(sdata, skb);
			break;
		case NL80211_IFTYPE_ADHOC:
			ieee80211_ibss_rx_queued_mgmt(sdata, skb);
			break;
		case NL80211_IFTYPE_MESH_POINT:
			if (!ieee80211_vif_is_mesh(&sdata->vif))
				break;
			ieee80211_mesh_rx_queued_mgmt(sdata, skb);
			break;
		default:
			WARN(1, "frame for unexpected interface type");
			break;
		}

		kfree_skb(skb);
	}

	/* then other type-dependent work */
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		ieee80211_sta_work(sdata);
		break;
	case NL80211_IFTYPE_ADHOC:
		ieee80211_ibss_work(sdata);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (!ieee80211_vif_is_mesh(&sdata->vif))
			break;
		ieee80211_mesh_work(sdata);
		break;
	default:
		break;
	}
}

static void ieee80211_recalc_smps_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, recalc_smps);

	ieee80211_recalc_smps(sdata);
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
	sdata->vif.p2p = false;
	sdata->wdev.iftype = type;

	sdata->control_port_protocol = cpu_to_be16(ETH_P_PAE);
	sdata->control_port_no_encrypt = false;

	sdata->noack_map = 0;

	/* only monitor/p2p-device differ */
	if (sdata->dev) {
		sdata->dev->netdev_ops = &ieee80211_dataif_ops;
		sdata->dev->type = ARPHRD_ETHER;
	}

	skb_queue_head_init(&sdata->skb_queue);
	INIT_WORK(&sdata->work, ieee80211_iface_work);
	INIT_WORK(&sdata->recalc_smps, ieee80211_recalc_smps_work);

	switch (type) {
	case NL80211_IFTYPE_P2P_GO:
		type = NL80211_IFTYPE_AP;
		sdata->vif.type = type;
		sdata->vif.p2p = true;
		/* fall through */
	case NL80211_IFTYPE_AP:
		skb_queue_head_init(&sdata->u.ap.ps.bc_buf);
		INIT_LIST_HEAD(&sdata->u.ap.vlans);
		sdata->vif.bss_conf.bssid = sdata->vif.addr;
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		type = NL80211_IFTYPE_STATION;
		sdata->vif.type = type;
		sdata->vif.p2p = true;
		/* fall through */
	case NL80211_IFTYPE_STATION:
		sdata->vif.bss_conf.bssid = sdata->u.mgd.bssid;
		ieee80211_sta_setup_sdata(sdata);
		break;
	case NL80211_IFTYPE_ADHOC:
		sdata->vif.bss_conf.bssid = sdata->u.ibss.bssid;
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
		sdata->vif.bss_conf.bssid = NULL;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		sdata->vif.bss_conf.bssid = sdata->vif.addr;
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
		BUG();
		break;
	}

	ieee80211_debugfs_add_netdev(sdata);
}

static int ieee80211_runtime_change_iftype(struct ieee80211_sub_if_data *sdata,
					   enum nl80211_iftype type)
{
	struct ieee80211_local *local = sdata->local;
	int ret, err;
	enum nl80211_iftype internal_type = type;
	bool p2p = false;

	ASSERT_RTNL();

	if (!local->ops->change_interface)
		return -EBUSY;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
		/*
		 * Could maybe also all others here?
		 * Just not sure how that interacts
		 * with the RX/config path e.g. for
		 * mesh.
		 */
		break;
	default:
		return -EBUSY;
	}

	switch (type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
		/*
		 * Could probably support everything
		 * but WDS here (WDS do_open can fail
		 * under memory pressure, which this
		 * code isn't prepared to handle).
		 */
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		p2p = true;
		internal_type = NL80211_IFTYPE_STATION;
		break;
	case NL80211_IFTYPE_P2P_GO:
		p2p = true;
		internal_type = NL80211_IFTYPE_AP;
		break;
	default:
		return -EBUSY;
	}

	ret = ieee80211_check_concurrent_iface(sdata, internal_type);
	if (ret)
		return ret;

	ieee80211_do_stop(sdata, false);

	ieee80211_teardown_sdata(sdata);

	ret = drv_change_interface(local, sdata, internal_type, p2p);
	if (ret)
		type = sdata->vif.type;

	/*
	 * Ignore return value here, there's not much we can do since
	 * the driver changed the interface type internally already.
	 * The warnings will hopefully make driver authors fix it :-)
	 */
	ieee80211_check_queues(sdata);

	ieee80211_setup_sdata(sdata, type);

	err = ieee80211_do_open(&sdata->wdev, false);
	WARN(err, "type change: do_open returned %d", err);

	return ret;
}

int ieee80211_if_change_type(struct ieee80211_sub_if_data *sdata,
			     enum nl80211_iftype type)
{
	int ret;

	ASSERT_RTNL();

	if (type == ieee80211_vif_type_p2p(&sdata->vif))
		return 0;

	if (ieee80211_sdata_running(sdata)) {
		ret = ieee80211_runtime_change_iftype(sdata, type);
		if (ret)
			return ret;
	} else {
		/* Purge and reset type-dependent state. */
		ieee80211_teardown_sdata(sdata);
		ieee80211_setup_sdata(sdata, type);
	}

	/* reset some values that shouldn't be kept across type changes */
	sdata->drop_unencrypted = 0;
	if (type == NL80211_IFTYPE_STATION)
		sdata->u.mgd.use_4addr = false;

	return 0;
}

static void ieee80211_assign_perm_addr(struct ieee80211_local *local,
				       u8 *perm_addr, enum nl80211_iftype type)
{
	struct ieee80211_sub_if_data *sdata;
	u64 mask, start, addr, val, inc;
	u8 *m;
	u8 tmp_addr[ETH_ALEN];
	int i;

	/* default ... something at least */
	memcpy(perm_addr, local->hw.wiphy->perm_addr, ETH_ALEN);

	if (is_zero_ether_addr(local->hw.wiphy->addr_mask) &&
	    local->hw.wiphy->n_addresses <= 1)
		return;

	mutex_lock(&local->iflist_mtx);

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
		/* doesn't matter */
		break;
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_AP_VLAN:
		/* match up with an AP interface */
		list_for_each_entry(sdata, &local->interfaces, list) {
			if (sdata->vif.type != NL80211_IFTYPE_AP)
				continue;
			memcpy(perm_addr, sdata->vif.addr, ETH_ALEN);
			break;
		}
		/* keep default if no AP interface present */
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
		if (local->hw.flags & IEEE80211_HW_P2P_DEV_ADDR_FOR_INTF) {
			list_for_each_entry(sdata, &local->interfaces, list) {
				if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE)
					continue;
				if (!ieee80211_sdata_running(sdata))
					continue;
				memcpy(perm_addr, sdata->vif.addr, ETH_ALEN);
				goto out_unlock;
			}
		}
		/* otherwise fall through */
	default:
		/* assign a new address if possible -- try n_addresses first */
		for (i = 0; i < local->hw.wiphy->n_addresses; i++) {
			bool used = false;

			list_for_each_entry(sdata, &local->interfaces, list) {
				if (memcmp(local->hw.wiphy->addresses[i].addr,
					   sdata->vif.addr, ETH_ALEN) == 0) {
					used = true;
					break;
				}
			}

			if (!used) {
				memcpy(perm_addr,
				       local->hw.wiphy->addresses[i].addr,
				       ETH_ALEN);
				break;
			}
		}

		/* try mask if available */
		if (is_zero_ether_addr(local->hw.wiphy->addr_mask))
			break;

		m = local->hw.wiphy->addr_mask;
		mask =	((u64)m[0] << 5*8) | ((u64)m[1] << 4*8) |
			((u64)m[2] << 3*8) | ((u64)m[3] << 2*8) |
			((u64)m[4] << 1*8) | ((u64)m[5] << 0*8);

		if (__ffs64(mask) + hweight64(mask) != fls64(mask)) {
			/* not a contiguous mask ... not handled now! */
			pr_info("not contiguous\n");
			break;
		}

		m = local->hw.wiphy->perm_addr;
		start = ((u64)m[0] << 5*8) | ((u64)m[1] << 4*8) |
			((u64)m[2] << 3*8) | ((u64)m[3] << 2*8) |
			((u64)m[4] << 1*8) | ((u64)m[5] << 0*8);

		inc = 1ULL<<__ffs64(mask);
		val = (start & mask);
		addr = (start & ~mask) | (val & mask);
		do {
			bool used = false;

			tmp_addr[5] = addr >> 0*8;
			tmp_addr[4] = addr >> 1*8;
			tmp_addr[3] = addr >> 2*8;
			tmp_addr[2] = addr >> 3*8;
			tmp_addr[1] = addr >> 4*8;
			tmp_addr[0] = addr >> 5*8;

			val += inc;

			list_for_each_entry(sdata, &local->interfaces, list) {
				if (memcmp(tmp_addr, sdata->vif.addr,
							ETH_ALEN) == 0) {
					used = true;
					break;
				}
			}

			if (!used) {
				memcpy(perm_addr, tmp_addr, ETH_ALEN);
				break;
			}
			addr = (start & ~mask) | (val & mask);
		} while (addr != start);

		break;
	}

 out_unlock:
	mutex_unlock(&local->iflist_mtx);
}

static void ieee80211_cleanup_sdata_stas_wk(struct work_struct *wk)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = container_of(wk, struct ieee80211_sub_if_data, cleanup_stations_wk);

	ieee80211_cleanup_sdata_stas(sdata);
}

int ieee80211_if_add(struct ieee80211_local *local, const char *name,
		     struct wireless_dev **new_wdev, enum nl80211_iftype type,
		     struct vif_params *params)
{
	struct net_device *ndev = NULL;
	struct ieee80211_sub_if_data *sdata = NULL;
	int ret, i;
	int txqs = 1;

	ASSERT_RTNL();

	if (type == NL80211_IFTYPE_P2P_DEVICE) {
		struct wireless_dev *wdev;

		sdata = kzalloc(sizeof(*sdata) + local->hw.vif_data_size,
				GFP_KERNEL);
		if (!sdata)
			return -ENOMEM;
		wdev = &sdata->wdev;

		sdata->dev = NULL;
		strlcpy(sdata->name, name, IFNAMSIZ);
		ieee80211_assign_perm_addr(local, wdev->address, type);
		memcpy(sdata->vif.addr, wdev->address, ETH_ALEN);
	} else {
		if (local->hw.queues >= IEEE80211_NUM_ACS)
			txqs = IEEE80211_NUM_ACS;

		ndev = alloc_netdev_mqs(sizeof(*sdata) +
					local->hw.vif_data_size,
					name, ieee80211_if_setup, txqs, 1);
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
		if (ret < 0) {
			free_netdev(ndev);
			return ret;
		}

		ieee80211_assign_perm_addr(local, ndev->perm_addr, type);
		memcpy(ndev->dev_addr, ndev->perm_addr, ETH_ALEN);
		SET_NETDEV_DEV(ndev, wiphy_dev(local->hw.wiphy));

		/* don't use IEEE80211_DEV_TO_SUB_IF -- it checks too much */
		sdata = netdev_priv(ndev);
		ndev->ieee80211_ptr = &sdata->wdev;
		memcpy(sdata->vif.addr, ndev->dev_addr, ETH_ALEN);
		memcpy(sdata->name, ndev->name, IFNAMSIZ);

		sdata->dev = ndev;
	}

	/* initialise type-independent data */
	sdata->wdev.wiphy = local->hw.wiphy;
	sdata->local = local;

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		skb_queue_head_init(&sdata->fragments[i].skb_list);

	INIT_LIST_HEAD(&sdata->key_list);

	spin_lock_init(&sdata->cleanup_stations_lock);
	INIT_LIST_HEAD(&sdata->cleanup_stations);
	INIT_WORK(&sdata->cleanup_stations_wk, ieee80211_cleanup_sdata_stas_wk);
	INIT_DELAYED_WORK(&sdata->dfs_cac_timer_work,
			  ieee80211_dfs_cac_timer_work);
	INIT_DELAYED_WORK(&sdata->dec_tailroom_needed_wk,
			  ieee80211_delayed_tailroom_dec);

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		struct ieee80211_supported_band *sband;
		sband = local->hw.wiphy->bands[i];
		sdata->rc_rateidx_mask[i] =
			sband ? (1 << sband->n_bitrates) - 1 : 0;
		if (sband)
			memcpy(sdata->rc_rateidx_mcs_mask[i],
			       sband->ht_cap.mcs.rx_mask,
			       sizeof(sdata->rc_rateidx_mcs_mask[i]));
		else
			memset(sdata->rc_rateidx_mcs_mask[i], 0,
			       sizeof(sdata->rc_rateidx_mcs_mask[i]));
	}

	ieee80211_set_default_queues(sdata);

	sdata->ap_power_level = IEEE80211_UNSET_POWER_LEVEL;
	sdata->user_power_level = local->user_power_level;

	/* setup type-dependent data */
	ieee80211_setup_sdata(sdata, type);

	if (ndev) {
		if (params) {
			ndev->ieee80211_ptr->use_4addr = params->use_4addr;
			if (type == NL80211_IFTYPE_STATION)
				sdata->u.mgd.use_4addr = params->use_4addr;
		}

		ndev->features |= local->hw.netdev_features;

		ret = register_netdevice(ndev);
		if (ret) {
			free_netdev(ndev);
			return ret;
		}
	}

	mutex_lock(&local->iflist_mtx);
	list_add_tail_rcu(&sdata->list, &local->interfaces);
	mutex_unlock(&local->iflist_mtx);

	if (new_wdev)
		*new_wdev = &sdata->wdev;

	return 0;
}

void ieee80211_if_remove(struct ieee80211_sub_if_data *sdata)
{
	ASSERT_RTNL();

	mutex_lock(&sdata->local->iflist_mtx);
	list_del_rcu(&sdata->list);
	mutex_unlock(&sdata->local->iflist_mtx);

	synchronize_rcu();

	if (sdata->dev) {
		unregister_netdevice(sdata->dev);
	} else {
		cfg80211_unregister_wdev(&sdata->wdev);
		kfree(sdata);
	}
}

void ieee80211_sdata_stop(struct ieee80211_sub_if_data *sdata)
{
	if (WARN_ON_ONCE(!test_bit(SDATA_STATE_RUNNING, &sdata->state)))
		return;
	ieee80211_do_stop(sdata, true);
	ieee80211_teardown_sdata(sdata);
}

/*
 * Remove all interfaces, may only be called at hardware unregistration
 * time because it doesn't do RCU-safe list removals.
 */
void ieee80211_remove_interfaces(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *tmp;
	LIST_HEAD(unreg_list);
	LIST_HEAD(wdev_list);

	ASSERT_RTNL();

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry_safe(sdata, tmp, &local->interfaces, list) {
		list_del(&sdata->list);

		if (sdata->dev)
			unregister_netdevice_queue(sdata->dev, &unreg_list);
		else
			list_add(&sdata->list, &wdev_list);
	}
	mutex_unlock(&local->iflist_mtx);
	unregister_netdevice_many(&unreg_list);
	list_del(&unreg_list);

	list_for_each_entry_safe(sdata, tmp, &wdev_list, list) {
		list_del(&sdata->list);
		cfg80211_unregister_wdev(&sdata->wdev);
		kfree(sdata);
	}
}

static int netdev_notify(struct notifier_block *nb,
			 unsigned long state,
			 void *ndev)
{
	struct net_device *dev = ndev;
	struct ieee80211_sub_if_data *sdata;

	if (state != NETDEV_CHANGENAME)
		return 0;

	if (!dev->ieee80211_ptr || !dev->ieee80211_ptr->wiphy)
		return 0;

	if (dev->ieee80211_ptr->wiphy->privid != mac80211_wiphy_privid)
		return 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	memcpy(sdata->name, dev->name, IFNAMSIZ);

	ieee80211_debugfs_rename_netdev(sdata);
	return 0;
}

static struct notifier_block mac80211_netdev_notifier = {
	.notifier_call = netdev_notify,
};

int ieee80211_iface_init(void)
{
	return register_netdevice_notifier(&mac80211_netdev_notifier);
}

void ieee80211_iface_exit(void)
{
	unregister_netdevice_notifier(&mac80211_netdev_notifier);
}
