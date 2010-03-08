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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/bitmap.h>
#include <linux/pm_qos_params.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wep.h"
#include "led.h"
#include "cfg.h"
#include "debugfs.h"


bool ieee80211_disable_40mhz_24ghz;
module_param(ieee80211_disable_40mhz_24ghz, bool, 0644);
MODULE_PARM_DESC(ieee80211_disable_40mhz_24ghz,
		 "Disable 40MHz support in the 2.4GHz band");

void ieee80211_configure_filter(struct ieee80211_local *local)
{
	u64 mc;
	unsigned int changed_flags;
	unsigned int new_flags = 0;

	if (atomic_read(&local->iff_promiscs))
		new_flags |= FIF_PROMISC_IN_BSS;

	if (atomic_read(&local->iff_allmultis))
		new_flags |= FIF_ALLMULTI;

	if (local->monitors || local->scanning)
		new_flags |= FIF_BCN_PRBRESP_PROMISC;

	if (local->fif_fcsfail)
		new_flags |= FIF_FCSFAIL;

	if (local->fif_plcpfail)
		new_flags |= FIF_PLCPFAIL;

	if (local->fif_control)
		new_flags |= FIF_CONTROL;

	if (local->fif_other_bss)
		new_flags |= FIF_OTHER_BSS;

	if (local->fif_pspoll)
		new_flags |= FIF_PSPOLL;

	spin_lock_bh(&local->filter_lock);
	changed_flags = local->filter_flags ^ new_flags;

	mc = drv_prepare_multicast(local, local->mc_count, local->mc_list);
	spin_unlock_bh(&local->filter_lock);

	/* be a bit nasty */
	new_flags |= (1<<31);

	drv_configure_filter(local, changed_flags, &new_flags, mc);

	WARN_ON(new_flags & (1<<31));

	local->filter_flags = new_flags & ~(1<<31);
}

static void ieee80211_reconfig_filter(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, reconfig_filter);

	ieee80211_configure_filter(local);
}

int ieee80211_hw_config(struct ieee80211_local *local, u32 changed)
{
	struct ieee80211_channel *chan, *scan_chan;
	int ret = 0;
	int power;
	enum nl80211_channel_type channel_type;

	might_sleep();

	scan_chan = local->scan_channel;

	if (scan_chan) {
		chan = scan_chan;
		channel_type = NL80211_CHAN_NO_HT;
	} else if (local->tmp_channel) {
		chan = scan_chan = local->tmp_channel;
		channel_type = local->tmp_channel_type;
	} else {
		chan = local->oper_channel;
		channel_type = local->oper_channel_type;
	}

	if (chan != local->hw.conf.channel ||
	    channel_type != local->hw.conf.channel_type) {
		local->hw.conf.channel = chan;
		local->hw.conf.channel_type = channel_type;
		changed |= IEEE80211_CONF_CHANGE_CHANNEL;
	}

	if (!conf_is_ht(&local->hw.conf)) {
		/*
		 * mac80211.h documents that this is only valid
		 * when the channel is set to an HT type, and
		 * that otherwise STATIC is used.
		 */
		local->hw.conf.smps_mode = IEEE80211_SMPS_STATIC;
	} else if (local->hw.conf.smps_mode != local->smps_mode) {
		local->hw.conf.smps_mode = local->smps_mode;
		changed |= IEEE80211_CONF_CHANGE_SMPS;
	}

	if (scan_chan)
		power = chan->max_power;
	else
		power = local->power_constr_level ?
			(chan->max_power - local->power_constr_level) :
			chan->max_power;

	if (local->user_power_level >= 0)
		power = min(power, local->user_power_level);

	if (local->hw.conf.power_level != power) {
		changed |= IEEE80211_CONF_CHANGE_POWER;
		local->hw.conf.power_level = power;
	}

	if (changed && local->open_count) {
		ret = drv_config(local, changed);
		/*
		 * Goal:
		 * HW reconfiguration should never fail, the driver has told
		 * us what it can support so it should live up to that promise.
		 *
		 * Current status:
		 * rfkill is not integrated with mac80211 and a
		 * configuration command can thus fail if hardware rfkill
		 * is enabled
		 *
		 * FIXME: integrate rfkill with mac80211 and then add this
		 * WARN_ON() back
		 *
		 */
		/* WARN_ON(ret); */
	}

	return ret;
}

void ieee80211_bss_info_change_notify(struct ieee80211_sub_if_data *sdata,
				      u32 changed)
{
	struct ieee80211_local *local = sdata->local;
	static const u8 zero[ETH_ALEN] = { 0 };

	if (!changed)
		return;

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		/*
		 * While not associated, claim a BSSID of all-zeroes
		 * so that drivers don't do any weird things with the
		 * BSSID at that time.
		 */
		if (sdata->vif.bss_conf.assoc)
			sdata->vif.bss_conf.bssid = sdata->u.mgd.bssid;
		else
			sdata->vif.bss_conf.bssid = zero;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		sdata->vif.bss_conf.bssid = sdata->u.ibss.bssid;
	else if (sdata->vif.type == NL80211_IFTYPE_AP)
		sdata->vif.bss_conf.bssid = sdata->vif.addr;
	else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		sdata->vif.bss_conf.bssid = zero;
	} else {
		WARN_ON(1);
		return;
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		break;
	default:
		/* do not warn to simplify caller in scan.c */
		changed &= ~BSS_CHANGED_BEACON_ENABLED;
		if (WARN_ON(changed & BSS_CHANGED_BEACON))
			return;
		break;
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		if (local->quiescing || !ieee80211_sdata_running(sdata) ||
		    test_bit(SCAN_SW_SCANNING, &local->scanning)) {
			sdata->vif.bss_conf.enable_beacon = false;
		} else {
			/*
			 * Beacon should be enabled, but AP mode must
			 * check whether there is a beacon configured.
			 */
			switch (sdata->vif.type) {
			case NL80211_IFTYPE_AP:
				sdata->vif.bss_conf.enable_beacon =
					!!rcu_dereference(sdata->u.ap.beacon);
				break;
			case NL80211_IFTYPE_ADHOC:
				sdata->vif.bss_conf.enable_beacon =
					!!rcu_dereference(sdata->u.ibss.presp);
				break;
			case NL80211_IFTYPE_MESH_POINT:
				sdata->vif.bss_conf.enable_beacon = true;
				break;
			default:
				/* not reached */
				WARN_ON(1);
				break;
			}
		}
	}

	drv_bss_info_changed(local, sdata, &sdata->vif.bss_conf, changed);
}

u32 ieee80211_reset_erp_info(struct ieee80211_sub_if_data *sdata)
{
	sdata->vif.bss_conf.use_cts_prot = false;
	sdata->vif.bss_conf.use_short_preamble = false;
	sdata->vif.bss_conf.use_short_slot = false;
	return BSS_CHANGED_ERP_CTS_PROT |
	       BSS_CHANGED_ERP_PREAMBLE |
	       BSS_CHANGED_ERP_SLOT;
}

static void ieee80211_tasklet_handler(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sk_buff *skb;
	struct ieee80211_ra_tid *ra_tid;

	while ((skb = skb_dequeue(&local->skb_queue)) ||
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		switch (skb->pkt_type) {
		case IEEE80211_RX_MSG:
			/* Clear skb->pkt_type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			ieee80211_rx(local_to_hw(local), skb);
			break;
		case IEEE80211_TX_STATUS_MSG:
			skb->pkt_type = 0;
			ieee80211_tx_status(local_to_hw(local), skb);
			break;
		case IEEE80211_DELBA_MSG:
			ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
			ieee80211_stop_tx_ba_cb(ra_tid->vif, ra_tid->ra,
						ra_tid->tid);
			dev_kfree_skb(skb);
			break;
		case IEEE80211_ADDBA_MSG:
			ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
			ieee80211_start_tx_ba_cb(ra_tid->vif, ra_tid->ra,
						 ra_tid->tid);
			dev_kfree_skb(skb);
			break ;
		default:
			WARN(1, "mac80211: Packet is of unknown type %d\n",
			     skb->pkt_type);
			dev_kfree_skb(skb);
			break;
		}
	}
}

static void ieee80211_restart_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, restart_work);

	rtnl_lock();
	ieee80211_reconfig(local);
	rtnl_unlock();
}

void ieee80211_restart_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	/* use this reason, __ieee80211_resume will unblock it */
	ieee80211_stop_queues_by_reason(hw,
		IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	schedule_work(&local->restart_work);
}
EXPORT_SYMBOL(ieee80211_restart_hw);

static void ieee80211_recalc_smps_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, recalc_smps);

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_smps(local, NULL);
	mutex_unlock(&local->iflist_mtx);
}

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops)
{
	struct ieee80211_local *local;
	int priv_size, i;
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
	priv_size = ALIGN(sizeof(*local), NETDEV_ALIGN) + priv_data_len;

	wiphy = wiphy_new(&mac80211_config_ops, priv_size);

	if (!wiphy)
		return NULL;

	wiphy->flags |= WIPHY_FLAG_NETNS_OK |
			WIPHY_FLAG_4ADDR_AP |
			WIPHY_FLAG_4ADDR_STATION;
	wiphy->privid = mac80211_wiphy_privid;

	wiphy->bss_priv_size = sizeof(struct ieee80211_bss);

	local = wiphy_priv(wiphy);

	local->hw.wiphy = wiphy;

	local->hw.priv = (char *)local + ALIGN(sizeof(*local), NETDEV_ALIGN);

	BUG_ON(!ops->tx);
	BUG_ON(!ops->start);
	BUG_ON(!ops->stop);
	BUG_ON(!ops->config);
	BUG_ON(!ops->add_interface);
	BUG_ON(!ops->remove_interface);
	BUG_ON(!ops->configure_filter);
	local->ops = ops;

	/* set up some defaults */
	local->hw.queues = 1;
	local->hw.max_rates = 1;
	local->hw.conf.long_frame_max_tx_count = wiphy->retry_long;
	local->hw.conf.short_frame_max_tx_count = wiphy->retry_short;
	local->user_power_level = -1;
	local->uapsd_queues = IEEE80211_DEFAULT_UAPSD_QUEUES;
	local->uapsd_max_sp_len = IEEE80211_DEFAULT_MAX_SP_LEN;

	INIT_LIST_HEAD(&local->interfaces);
	mutex_init(&local->iflist_mtx);
	mutex_init(&local->scan_mtx);

	spin_lock_init(&local->key_lock);
	spin_lock_init(&local->filter_lock);
	spin_lock_init(&local->queue_stop_reason_lock);

	INIT_DELAYED_WORK(&local->scan_work, ieee80211_scan_work);

	ieee80211_work_init(local);

	INIT_WORK(&local->restart_work, ieee80211_restart_work);

	INIT_WORK(&local->reconfig_filter, ieee80211_reconfig_filter);
	INIT_WORK(&local->recalc_smps, ieee80211_recalc_smps_work);
	local->smps_mode = IEEE80211_SMPS_OFF;

	INIT_WORK(&local->dynamic_ps_enable_work,
		  ieee80211_dynamic_ps_enable_work);
	INIT_WORK(&local->dynamic_ps_disable_work,
		  ieee80211_dynamic_ps_disable_work);
	setup_timer(&local->dynamic_ps_timer,
		    ieee80211_dynamic_ps_timer, (unsigned long) local);

	sta_info_init(local);

	for (i = 0; i < IEEE80211_MAX_QUEUES; i++)
		skb_queue_head_init(&local->pending[i]);
	tasklet_init(&local->tx_pending_tasklet, ieee80211_tx_pending,
		     (unsigned long)local);

	tasklet_init(&local->tasklet,
		     ieee80211_tasklet_handler,
		     (unsigned long) local);

	skb_queue_head_init(&local->skb_queue);
	skb_queue_head_init(&local->skb_queue_unreliable);

	spin_lock_init(&local->ampdu_lock);

	return local_to_hw(local);
}
EXPORT_SYMBOL(ieee80211_alloc_hw);

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	int result;
	enum ieee80211_band band;
	int channels, i, j, max_bitrates;
	bool supp_ht;
	static const u32 cipher_suites[] = {
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,

		/* keep last -- depends on hw flags! */
		WLAN_CIPHER_SUITE_AES_CMAC
	};

	/*
	 * generic code guarantees at least one band,
	 * set this very early because much code assumes
	 * that hw.conf.channel is assigned
	 */
	channels = 0;
	max_bitrates = 0;
	supp_ht = false;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[band];
		if (!sband)
			continue;
		if (!local->oper_channel) {
			/* init channel we're on */
			local->hw.conf.channel =
			local->oper_channel = &sband->channels[0];
			local->hw.conf.channel_type = NL80211_CHAN_NO_HT;
		}
		channels += sband->n_channels;

		if (max_bitrates < sband->n_bitrates)
			max_bitrates = sband->n_bitrates;
		supp_ht = supp_ht || sband->ht_cap.ht_supported;
	}

	local->int_scan_req = kzalloc(sizeof(*local->int_scan_req) +
				      sizeof(void *) * channels, GFP_KERNEL);
	if (!local->int_scan_req)
		return -ENOMEM;

	/* if low-level driver supports AP, we also support VLAN */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_AP))
		local->hw.wiphy->interface_modes |= BIT(NL80211_IFTYPE_AP_VLAN);

	/* mac80211 always supports monitor */
	local->hw.wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);

	if (local->hw.flags & IEEE80211_HW_SIGNAL_DBM)
		local->hw.wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	else if (local->hw.flags & IEEE80211_HW_SIGNAL_UNSPEC)
		local->hw.wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;

	WARN((local->hw.flags & IEEE80211_HW_SUPPORTS_UAPSD)
	     && (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK),
	     "U-APSD not supported with HW_PS_NULLFUNC_STACK\n");

	/*
	 * Calculate scan IE length -- we need this to alloc
	 * memory and to subtract from the driver limit. It
	 * includes the (extended) supported rates and HT
	 * information -- SSID is the driver's responsibility.
	 */
	local->scan_ies_len = 4 + max_bitrates; /* (ext) supp rates */
	if (supp_ht)
		local->scan_ies_len += 2 + sizeof(struct ieee80211_ht_cap);

	if (!local->ops->hw_scan) {
		/* For hw_scan, driver needs to set these up. */
		local->hw.wiphy->max_scan_ssids = 4;
		local->hw.wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	}

	/*
	 * If the driver supports any scan IEs, then assume the
	 * limit includes the IEs mac80211 will add, otherwise
	 * leave it at zero and let the driver sort it out; we
	 * still pass our IEs to the driver but userspace will
	 * not be allowed to in that case.
	 */
	if (local->hw.wiphy->max_scan_ie_len)
		local->hw.wiphy->max_scan_ie_len -= local->scan_ies_len;

	local->hw.wiphy->cipher_suites = cipher_suites;
	local->hw.wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);
	if (!(local->hw.flags & IEEE80211_HW_MFP_CAPABLE))
		local->hw.wiphy->n_cipher_suites--;

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		goto fail_wiphy_register;

	/*
	 * We use the number of queues for feature tests (QoS, HT) internally
	 * so restrict them appropriately.
	 */
	if (hw->queues > IEEE80211_MAX_QUEUES)
		hw->queues = IEEE80211_MAX_QUEUES;

	local->workqueue =
		create_singlethread_workqueue(wiphy_name(local->hw.wiphy));
	if (!local->workqueue) {
		result = -ENOMEM;
		goto fail_workqueue;
	}

	/*
	 * The hardware needs headroom for sending the frame,
	 * and we need some headroom for passing the frame to monitor
	 * interfaces, but never both at the same time.
	 */
	BUILD_BUG_ON(IEEE80211_TX_STATUS_HEADROOM !=
			sizeof(struct ieee80211_tx_status_rtap_hdr));
	local->tx_headroom = max_t(unsigned int , local->hw.extra_tx_headroom,
				   sizeof(struct ieee80211_tx_status_rtap_hdr));

	debugfs_hw_add(local);

	/*
	 * if the driver doesn't specify a max listen interval we
	 * use 5 which should be a safe default
	 */
	if (local->hw.max_listen_interval == 0)
		local->hw.max_listen_interval = 5;

	local->hw.conf.listen_interval = local->hw.max_listen_interval;

	result = sta_info_start(local);
	if (result < 0)
		goto fail_sta_info;

	result = ieee80211_wep_init(local);
	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize wep: %d\n",
		       wiphy_name(local->hw.wiphy), result);
		goto fail_wep;
	}

	rtnl_lock();

	result = ieee80211_init_rate_ctrl_alg(local,
					      hw->rate_control_algorithm);
	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize rate control "
		       "algorithm\n", wiphy_name(local->hw.wiphy));
		goto fail_rate;
	}

	/* add one default STA interface if supported */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_STATION)) {
		result = ieee80211_if_add(local, "wlan%d", NULL,
					  NL80211_IFTYPE_STATION, NULL);
		if (result)
			printk(KERN_WARNING "%s: Failed to add default virtual iface\n",
			       wiphy_name(local->hw.wiphy));
	}

	rtnl_unlock();

	ieee80211_led_init(local);

	/* alloc internal scan request */
	i = 0;
	local->int_scan_req->ssids = &local->scan_ssid;
	local->int_scan_req->n_ssids = 1;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (!hw->wiphy->bands[band])
			continue;
		for (j = 0; j < hw->wiphy->bands[band]->n_channels; j++) {
			local->int_scan_req->channels[i] =
				&hw->wiphy->bands[band]->channels[j];
			i++;
		}
	}
	local->int_scan_req->n_channels = i;

	local->network_latency_notifier.notifier_call =
		ieee80211_max_network_latency;
	result = pm_qos_add_notifier(PM_QOS_NETWORK_LATENCY,
				     &local->network_latency_notifier);

	if (result) {
		rtnl_lock();
		goto fail_pm_qos;
	}

	return 0;

 fail_pm_qos:
	ieee80211_led_exit(local);
	ieee80211_remove_interfaces(local);
 fail_rate:
	rtnl_unlock();
	ieee80211_wep_free(local);
 fail_wep:
	sta_info_stop(local);
 fail_sta_info:
	destroy_workqueue(local->workqueue);
 fail_workqueue:
	wiphy_unregister(local->hw.wiphy);
 fail_wiphy_register:
	kfree(local->int_scan_req);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	tasklet_kill(&local->tx_pending_tasklet);
	tasklet_kill(&local->tasklet);

	pm_qos_remove_notifier(PM_QOS_NETWORK_LATENCY,
			       &local->network_latency_notifier);

	rtnl_lock();

	/*
	 * At this point, interface list manipulations are fine
	 * because the driver cannot be handing us frames any
	 * more and the tasklet is killed.
	 */
	ieee80211_remove_interfaces(local);

	rtnl_unlock();

	cancel_work_sync(&local->reconfig_filter);

	ieee80211_clear_tx_pending(local);
	sta_info_stop(local);
	rate_control_deinitialize(local);

	if (skb_queue_len(&local->skb_queue) ||
	    skb_queue_len(&local->skb_queue_unreliable))
		printk(KERN_WARNING "%s: skb_queue not empty\n",
		       wiphy_name(local->hw.wiphy));
	skb_queue_purge(&local->skb_queue);
	skb_queue_purge(&local->skb_queue_unreliable);

	destroy_workqueue(local->workqueue);
	wiphy_unregister(local->hw.wiphy);
	ieee80211_wep_free(local);
	ieee80211_led_exit(local);
	kfree(local->int_scan_req);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	mutex_destroy(&local->iflist_mtx);
	mutex_destroy(&local->scan_mtx);

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

	ret = rc80211_minstrel_init();
	if (ret)
		return ret;

	ret = rc80211_pid_init();
	if (ret)
		goto err_pid;

	ret = ieee80211_iface_init();
	if (ret)
		goto err_netdev;

	return 0;
 err_netdev:
	rc80211_pid_exit();
 err_pid:
	rc80211_minstrel_exit();

	return ret;
}

static void __exit ieee80211_exit(void)
{
	rc80211_pid_exit();
	rc80211_minstrel_exit();

	/*
	 * For key todo, it'll be empty by now but the work
	 * might still be scheduled.
	 */
	flush_scheduled_work();

	if (mesh_allocated)
		ieee80211s_stop();

	ieee80211_iface_exit();
}


subsys_initcall(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
