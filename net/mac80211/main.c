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

#define SUPP_MCS_SET_LEN 16

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
	int meshhdrlen;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	meshhdrlen = (sdata->vif.type == IEEE80211_IF_TYPE_MESH_POINT) ? 5 : 0;

	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.3 frames. */
	if (new_mtu < 256 ||
		new_mtu > IEEE80211_MAX_DATA_LEN - 24 - 6 - meshhdrlen) {
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
	bool need_hw_reconfig = 0;
	struct sta_info *sta;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(nsdata, &local->interfaces, list) {
		struct net_device *ndev = nsdata->dev;

		if (ndev != dev && ndev != local->mdev && netif_running(ndev)) {
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
			 * Disallow multiple IBSS/STA mode interfaces.
			 *
			 * This is a technical restriction, it is possible although
			 * most likely not IEEE 802.11 compliant to have multiple
			 * STAs with just a single hardware (the TSF timer will not
			 * be adjusted properly.)
			 *
			 * However, because mac80211 uses the master device's BSS
			 * information for each STA/IBSS interface, doing this will
			 * currently corrupt that BSS information completely, unless,
			 * a not very useful case, both STAs are associated to the
			 * same BSS.
			 *
			 * To remove this restriction, the BSS information needs to
			 * be embedded in the STA/IBSS mode sdata instead of using
			 * the master device's BSS structure.
			 */
			if ((sdata->vif.type == IEEE80211_IF_TYPE_STA ||
			     sdata->vif.type == IEEE80211_IF_TYPE_IBSS) &&
			    (nsdata->vif.type == IEEE80211_IF_TYPE_STA ||
			     nsdata->vif.type == IEEE80211_IF_TYPE_IBSS))
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
				sdata->u.vlan.ap = nsdata;
		}
	}

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_WDS:
		if (!is_valid_ether_addr(sdata->u.wds.remote_addr))
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
	case IEEE80211_IF_TYPE_MESH_POINT:
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
		need_hw_reconfig = 1;
		ieee80211_led_radio(local, local->hw.conf.radio_enabled);
	}

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_VLAN:
		list_add(&sdata->u.vlan.list, &sdata->u.vlan.ap->u.ap.vlans);
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

		netif_tx_lock_bh(local->mdev);
		ieee80211_configure_filter(local);
		netif_tx_unlock_bh(local->mdev);
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

		ieee80211_if_config(dev);
		ieee80211_reset_erp_info(dev);
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
	if (need_hw_reconfig)
		ieee80211_hw_config(local);

	/*
	 * ieee80211_sta_work is disabled while network interface
	 * is down. Therefore, some configuration changes may not
	 * yet be effective. Trigger execution of ieee80211_sta_work
	 * to fix this.
	 */
	if(sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	   sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		struct ieee80211_if_sta *ifsta = &sdata->u.sta;
		queue_work(local->hw.workqueue, &ifsta->work);
	}

	netif_start_queue(dev);

	return 0;
 err_del_interface:
	local->ops->remove_interface(local_to_hw(local), &conf);
 err_stop:
	if (!local->open_count && local->ops->stop)
		local->ops->stop(local_to_hw(local));
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
	netif_stop_queue(dev);

	/*
	 * Now delete all active aggregation sessions.
	 */
	rcu_read_lock();

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sta->sdata == sdata)
			ieee80211_sta_tear_down_BA_sessions(dev, sta->addr);
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
		sdata->u.vlan.ap = NULL;
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

		netif_tx_lock_bh(local->mdev);
		ieee80211_configure_filter(local);
		netif_tx_unlock_bh(local->mdev);
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
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

		if (local->scan_dev == sdata->dev) {
			if (!local->ops->hw_scan) {
				local->sta_sw_scanning = 0;
				cancel_delayed_work(&local->scan_work);
			} else
				local->sta_hw_scanning = 0;
		}

		flush_workqueue(local->hw.workqueue);

		sdata->u.sta.flags &= ~IEEE80211_STA_PRIVACY_INVOKED;
		kfree(sdata->u.sta.extra_ie);
		sdata->u.sta.extra_ie = NULL;
		sdata->u.sta.extra_ie_len = 0;
		/* fall through */
	default:
		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
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

		ieee80211_led_radio(local, 0);

		tasklet_disable(&local->tx_pending_tasklet);
		tasklet_disable(&local->tasklet);
	}

	return 0;
}

int ieee80211_start_tx_ba_session(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;
	u16 start_seq_num = 0;
	u8 *state;
	int ret;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM)
		return -EINVAL;

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Open BA session requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();

	sta = sta_info_get(local, ra);
	if (!sta) {
		printk(KERN_DEBUG "Could not find the station\n");
		rcu_read_unlock();
		return -ENOENT;
	}

	spin_lock_bh(&sta->ampdu_mlme.ampdu_tx);

	/* we have tried too many times, receiver does not want A-MPDU */
	if (sta->ampdu_mlme.addba_req_num[tid] > HT_AGG_MAX_RETRIES) {
		ret = -EBUSY;
		goto start_ba_exit;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	/* check if the TID is not in aggregation flow already */
	if (*state != HT_AGG_STATE_IDLE) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - session is not "
				 "idle on tid %u\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		ret = -EAGAIN;
		goto start_ba_exit;
	}

	/* prepare A-MPDU MLME for Tx aggregation */
	sta->ampdu_mlme.tid_tx[tid] =
			kmalloc(sizeof(struct tid_ampdu_tx), GFP_ATOMIC);
	if (!sta->ampdu_mlme.tid_tx[tid]) {
		if (net_ratelimit())
			printk(KERN_ERR "allocate tx mlme to tid %d failed\n",
					tid);
		ret = -ENOMEM;
		goto start_ba_exit;
	}
	/* Tx timer */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.function =
			sta_addba_resp_timer_expired;
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.data =
			(unsigned long)&sta->timer_to_tid[tid];
	init_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);

	/* ensure that TX flow won't interrupt us
	 * until the end of the call to requeue function */
	spin_lock_bh(&local->mdev->queue_lock);

	/* create a new queue for this aggregation */
	ret = ieee80211_ht_agg_queue_add(local, sta, tid);

	/* case no queue is available to aggregation
	 * don't switch to aggregation */
	if (ret) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - queue unavailable for"
					" tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		goto start_ba_err;
	}
	sdata = sta->sdata;

	/* Ok, the Addba frame hasn't been sent yet, but if the driver calls the
	 * call back right away, it must see that the flow has begun */
	*state |= HT_ADDBA_REQUESTED_MSK;

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_TX_START,
						ra, tid, &start_seq_num);

	if (ret) {
		/* No need to requeue the packets in the agg queue, since we
		 * held the tx lock: no packet could be enqueued to the newly
		 * allocated queue */
		 ieee80211_ht_agg_queue_remove(local, sta, tid, 0);
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - HW unavailable for"
					" tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		*state = HT_AGG_STATE_IDLE;
		goto start_ba_err;
	}

	/* Will put all the packets in the new SW queue */
	ieee80211_requeue(local, ieee802_1d_to_ac[tid]);
	spin_unlock_bh(&local->mdev->queue_lock);

	/* send an addBA request */
	sta->ampdu_mlme.dialog_token_allocator++;
	sta->ampdu_mlme.tid_tx[tid]->dialog_token =
			sta->ampdu_mlme.dialog_token_allocator;
	sta->ampdu_mlme.tid_tx[tid]->ssn = start_seq_num;

	ieee80211_send_addba_request(sta->sdata->dev, ra, tid,
			 sta->ampdu_mlme.tid_tx[tid]->dialog_token,
			 sta->ampdu_mlme.tid_tx[tid]->ssn,
			 0x40, 5000);

	/* activate the timer for the recipient's addBA response */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.expires =
				jiffies + ADDBA_RESP_INTERVAL;
	add_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);
	printk(KERN_DEBUG "activated addBA response timer on tid %d\n", tid);
	goto start_ba_exit;

start_ba_err:
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;
	spin_unlock_bh(&local->mdev->queue_lock);
	ret = -EBUSY;
start_ba_exit:
	spin_unlock_bh(&sta->ampdu_mlme.ampdu_tx);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_session);

int ieee80211_stop_tx_ba_session(struct ieee80211_hw *hw,
				 u8 *ra, u16 tid,
				 enum ieee80211_back_parties initiator)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	int ret = 0;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM)
		return -EINVAL;

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* check if the TID is in aggregation */
	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->ampdu_mlme.ampdu_tx);

	if (*state != HT_AGG_STATE_OPERATIONAL) {
		ret = -ENOENT;
		goto stop_BA_exit;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Tx BA session stop requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	ieee80211_stop_queue(hw, sta->tid_to_tx_q[tid]);

	*state = HT_AGG_STATE_REQ_STOP_BA_MSK |
		(initiator << HT_AGG_STATE_INITIATOR_SHIFT);

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_TX_STOP,
						ra, tid, NULL);

	/* case HW denied going back to legacy */
	if (ret) {
		WARN_ON(ret != -EBUSY);
		*state = HT_AGG_STATE_OPERATIONAL;
		ieee80211_wake_queue(hw, sta->tid_to_tx_q[tid]);
		goto stop_BA_exit;
	}

stop_BA_exit:
	spin_unlock_bh(&sta->ampdu_mlme.ampdu_tx);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_session);

void ieee80211_start_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM) {
		printk(KERN_DEBUG "Bad TID value: tid = %d (>= %d)\n",
				tid, STA_TID_NUM);
		return;
	}

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
		printk(KERN_DEBUG "Could not find station: %s\n",
				print_mac(mac, ra));
		return;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->ampdu_mlme.ampdu_tx);

	if (!(*state & HT_ADDBA_REQUESTED_MSK)) {
		printk(KERN_DEBUG "addBA was not requested yet, state is %d\n",
				*state);
		spin_unlock_bh(&sta->ampdu_mlme.ampdu_tx);
		rcu_read_unlock();
		return;
	}

	WARN_ON_ONCE(*state & HT_ADDBA_DRV_READY_MSK);

	*state |= HT_ADDBA_DRV_READY_MSK;

	if (*state == HT_AGG_STATE_OPERATIONAL) {
		printk(KERN_DEBUG "Aggregation is on for tid %d \n", tid);
		ieee80211_wake_queue(hw, sta->tid_to_tx_q[tid]);
	}
	spin_unlock_bh(&sta->ampdu_mlme.ampdu_tx);
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb);

void ieee80211_stop_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u8 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	int agg_queue;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM) {
		printk(KERN_DEBUG "Bad TID value: tid = %d (>= %d)\n",
				tid, STA_TID_NUM);
		return;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Stopping Tx BA session for %s tid %d\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		printk(KERN_DEBUG "Could not find station: %s\n",
				print_mac(mac, ra));
		rcu_read_unlock();
		return;
	}
	state = &sta->ampdu_mlme.tid_state_tx[tid];

	spin_lock_bh(&sta->ampdu_mlme.ampdu_tx);
	if ((*state & HT_AGG_STATE_REQ_STOP_BA_MSK) == 0) {
		printk(KERN_DEBUG "unexpected callback to A-MPDU stop\n");
		spin_unlock_bh(&sta->ampdu_mlme.ampdu_tx);
		rcu_read_unlock();
		return;
	}

	if (*state & HT_AGG_STATE_INITIATOR_MSK)
		ieee80211_send_delba(sta->sdata->dev, ra, tid,
			WLAN_BACK_INITIATOR, WLAN_REASON_QSTA_NOT_USE);

	agg_queue = sta->tid_to_tx_q[tid];

	/* avoid ordering issues: we are the only one that can modify
	 * the content of the qdiscs */
	spin_lock_bh(&local->mdev->queue_lock);
	/* remove the queue for this aggregation */
	ieee80211_ht_agg_queue_remove(local, sta, tid, 1);
	spin_unlock_bh(&local->mdev->queue_lock);

	/* we just requeued the all the frames that were in the removed
	 * queue, and since we might miss a softirq we do netif_schedule.
	 * ieee80211_wake_queue is not used here as this queue is not
	 * necessarily stopped */
	netif_schedule(local->mdev);
	*state = HT_AGG_STATE_IDLE;
	sta->ampdu_mlme.addba_req_num[tid] = 0;
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;
	spin_unlock_bh(&sta->ampdu_mlme.ampdu_tx);

	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb);

void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_hw *hw,
				      const u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb)) {
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping start BA session", skb->dev->name);
		return;
	}
	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_ADDBA_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb_irqsafe);

void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_hw *hw,
				     const u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb)) {
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping stop BA session", skb->dev->name);
		return;
	}
	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_DELBA_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb_irqsafe);

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

/* Must not be called for mdev */
void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->hard_start_xmit = ieee80211_subif_start_xmit;
	dev->wireless_handlers = &ieee80211_iw_handler_def;
	dev->set_multicast_list = ieee80211_set_multicast_list;
	dev->change_mtu = ieee80211_change_mtu;
	dev->open = ieee80211_open;
	dev->stop = ieee80211_stop;
	dev->destructor = ieee80211_if_free;
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
	conf.type = sdata->vif.type;
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		conf.bssid = sdata->u.sta.bssid;
		conf.ssid = sdata->u.sta.ssid;
		conf.ssid_len = sdata->u.sta.ssid_len;
	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		conf.beacon = beacon;
		conf.beacon_control = control;
		ieee80211_start_mesh(dev);
	} else if (sdata->vif.type == IEEE80211_IF_TYPE_AP) {
		conf.ssid = sdata->u.ap.ssid;
		conf.ssid_len = sdata->u.ap.ssid_len;
		conf.beacon = beacon;
		conf.beacon_control = control;
	}
	return local->ops->config_interface(local_to_hw(local),
					    &sdata->vif, &conf);
}

int ieee80211_if_config(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	if (sdata->vif.type == IEEE80211_IF_TYPE_MESH_POINT &&
	    (local->hw.flags & IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE))
		return ieee80211_if_config_beacon(dev);
	return __ieee80211_if_config(dev, NULL, NULL);
}

int ieee80211_if_config_beacon(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_control control;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sk_buff *skb;

	if (!(local->hw.flags & IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE))
		return 0;
	skb = ieee80211_beacon_get(local_to_hw(local), &sdata->vif,
				   &control);
	if (!skb)
		return -ENOMEM;
	return __ieee80211_if_config(dev, skb, &control);
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
	int i;
	u32 changed = 0;

	sband = local->hw.wiphy->bands[conf->channel->band];

	/* HT is not supported */
	if (!sband->ht_info.ht_supported) {
		conf->flags &= ~IEEE80211_CONF_SUPPORT_HT_MODE;
		return 0;
	}

	memset(&ht_conf, 0, sizeof(struct ieee80211_ht_info));
	memset(&ht_bss_conf, 0, sizeof(struct ieee80211_ht_bss_info));

	if (enable_ht) {
		if (!(conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE))
			changed |= BSS_CHANGED_HT;

		conf->flags |= IEEE80211_CONF_SUPPORT_HT_MODE;
		ht_conf.ht_supported = 1;

		ht_conf.cap = req_ht_cap->cap & sband->ht_info.cap;
		ht_conf.cap &= ~(IEEE80211_HT_CAP_MIMO_PS);
		ht_conf.cap |= sband->ht_info.cap & IEEE80211_HT_CAP_MIMO_PS;

		for (i = 0; i < SUPP_MCS_SET_LEN; i++)
			ht_conf.supp_mcs_set[i] =
					sband->ht_info.supp_mcs_set[i] &
					req_ht_cap->supp_mcs_set[i];

		ht_bss_conf.primary_channel = req_bss_cap->primary_channel;
		ht_bss_conf.bss_cap = req_bss_cap->bss_cap;
		ht_bss_conf.bss_op_mode = req_bss_cap->bss_op_mode;

		ht_conf.ampdu_factor = req_ht_cap->ampdu_factor;
		ht_conf.ampdu_density = req_ht_cap->ampdu_density;

		/* if bss configuration changed store the new one */
		if (memcmp(&conf->ht_conf, &ht_conf, sizeof(ht_conf)) ||
		    memcmp(&conf->ht_bss_conf, &ht_bss_conf, sizeof(ht_bss_conf))) {
			changed |= BSS_CHANGED_HT;
			memcpy(&conf->ht_conf, &ht_conf, sizeof(ht_conf));
			memcpy(&conf->ht_bss_conf, &ht_bss_conf, sizeof(ht_bss_conf));
		}
	} else {
		if (conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE)
			changed |= BSS_CHANGED_HT;
		conf->flags &= ~IEEE80211_CONF_SUPPORT_HT_MODE;
	}

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

void ieee80211_reset_erp_info(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sdata->bss_conf.use_cts_prot = 0;
	sdata->bss_conf.use_short_preamble = 0;
	ieee80211_bss_info_change_notify(sdata,
					 BSS_CHANGED_ERP_CTS_PROT |
					 BSS_CHANGED_ERP_PREAMBLE);
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
			/* get pointer to saved status out of skb->cb */
			memcpy(&tx_status, skb->cb, sizeof(tx_status));
			skb->pkt_type = 0;
			ieee80211_tx_status(local_to_hw(local),
					    skb, tx_status);
			kfree(tx_status);
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
	pkt_data->ifindex = vif_to_sdata(control->vif)->dev->ifindex;
	pkt_data->flags = 0;
	if (control->flags & IEEE80211_TXCTL_REQ_TX_STATUS)
		pkt_data->flags |= IEEE80211_TXPD_REQ_TX_STATUS;
	if (control->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT)
		pkt_data->flags |= IEEE80211_TXPD_DO_NOT_ENCRYPT;
	if (control->flags & IEEE80211_TXCTL_REQUEUE)
		pkt_data->flags |= IEEE80211_TXPD_REQUEUE;
	if (control->flags & IEEE80211_TXCTL_EAPOL_FRAME)
		pkt_data->flags |= IEEE80211_TXPD_EAPOL_FRAME;
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

static void ieee80211_handle_filtered_frame(struct ieee80211_local *local,
					    struct sta_info *sta,
					    struct sk_buff *skb,
					    struct ieee80211_tx_status *status)
{
	sta->tx_filtered_count++;

	/*
	 * Clear the TX filter mask for this STA when sending the next
	 * packet. If the STA went to power save mode, this will happen
	 * happen when it wakes up for the next time.
	 */
	sta->flags |= WLAN_STA_CLEAR_PS_FILT;

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
	if (sta->flags & WLAN_STA_PS &&
	    skb_queue_len(&sta->tx_filtered) < STA_MAX_TX_BUFFER) {
		ieee80211_remove_tx_extra(local, sta->key, skb,
					  &status->control);
		skb_queue_tail(&sta->tx_filtered, skb);
		return;
	}

	if (!(sta->flags & WLAN_STA_PS) &&
	    !(status->control.flags & IEEE80211_TXCTL_REQUEUE)) {
		/* Software retry the packet once */
		status->control.flags |= IEEE80211_TXCTL_REQUEUE;
		ieee80211_remove_tx_extra(local, sta->key, skb,
					  &status->control);
		dev_queue_xmit(skb);
		return;
	}

	if (net_ratelimit())
		printk(KERN_DEBUG "%s: dropped TX filtered frame, "
		       "queue_len=%d PS=%d @%lu\n",
		       wiphy_name(local->hw.wiphy),
		       skb_queue_len(&sta->tx_filtered),
		       !!(sta->flags & WLAN_STA_PS), jiffies);
	dev_kfree_skb(skb);
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
	struct net_device *prev_dev = NULL;

	if (!status) {
		printk(KERN_ERR
		       "%s: ieee80211_tx_status called with NULL status\n",
		       wiphy_name(local->hw.wiphy));
		dev_kfree_skb(skb);
		return;
	}

	rcu_read_lock();

	if (status->excessive_retries) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			if (sta->flags & WLAN_STA_PS) {
				/*
				 * The STA is in power save mode, so assume
				 * that this TX packet failed because of that.
				 */
				status->excessive_retries = 0;
				status->flags |= IEEE80211_TX_STATUS_TX_FILTERED;
				ieee80211_handle_filtered_frame(local, sta,
								skb, status);
				rcu_read_unlock();
				return;
			}
		}
	}

	if (status->flags & IEEE80211_TX_STATUS_TX_FILTERED) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			ieee80211_handle_filtered_frame(local, sta, skb,
							status);
			rcu_read_unlock();
			return;
		}
	} else
		rate_control_tx_status(local->mdev, skb, status);

	rcu_read_unlock();

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
	struct ieee80211_sub_if_data *sdata;

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

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		return result;

	/* for now, mdev needs sub_if_data :/ */
	mdev = alloc_netdev(sizeof(struct ieee80211_sub_if_data),
			    "wmaster%d", ether_setup);
	if (!mdev)
		goto fail_mdev_alloc;

	sdata = IEEE80211_DEV_TO_SUB_IF(mdev);
	mdev->ieee80211_ptr = &sdata->wdev;
	sdata->wdev.wiphy = local->hw.wiphy;

	local->mdev = mdev;

	ieee80211_rx_bss_list_init(mdev);

	mdev->hard_start_xmit = ieee80211_master_start_xmit;
	mdev->open = ieee80211_master_open;
	mdev->stop = ieee80211_master_stop;
	mdev->type = ARPHRD_IEEE80211;
	mdev->header_ops = &ieee80211_header_ops;
	mdev->set_multicast_list = ieee80211_master_set_multicast_list;

	sdata->vif.type = IEEE80211_IF_TYPE_AP;
	sdata->dev = mdev;
	sdata->local = local;
	sdata->u.ap.force_unicast_rateidx = -1;
	sdata->u.ap.max_ratectrl_rateidx = -1;
	ieee80211_if_sdata_init(sdata);

	/* no RCU needed since we're still during init phase */
	list_add_tail(&sdata->list, &local->interfaces);

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

	result = ieee80211_init_rate_ctrl_alg(local,
					      hw->rate_control_algorithm);
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
				  IEEE80211_IF_TYPE_STA, NULL);
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
	ieee80211_if_free(local->mdev);
	local->mdev = NULL;
fail_mdev_alloc:
	wiphy_unregister(local->hw.wiphy);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata, *tmp;

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
	ieee80211_if_free(local->mdev);
	local->mdev = NULL;
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

	BUILD_BUG_ON(sizeof(struct ieee80211_tx_packet_data) > sizeof(skb->cb));

	ret = rc80211_pid_init();
	if (ret)
		goto out;

	ret = ieee80211_wme_register();
	if (ret) {
		printk(KERN_DEBUG "ieee80211_init: failed to "
		       "initialize WME (err=%d)\n", ret);
		goto out_cleanup_pid;
	}

	ieee80211_debugfs_netdev_init();

	return 0;

 out_cleanup_pid:
	rc80211_pid_exit();
 out:
	return ret;
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

	ieee80211_wme_unregister();
	ieee80211_debugfs_netdev_exit();
}


subsys_initcall(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
