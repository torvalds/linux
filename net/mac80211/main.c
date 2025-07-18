// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2017     Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */

#include <net/mac80211.h>
#include <linux/module.h>
#include <linux/fips.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/bitmap.h>
#include <linux/inetdevice.h>
#include <net/net_namespace.h>
#include <net/dropreason.h>
#include <net/cfg80211.h>
#include <net/addrconf.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wep.h"
#include "led.h"
#include "debugfs.h"

void ieee80211_configure_filter(struct ieee80211_local *local)
{
	u64 mc;
	unsigned int changed_flags;
	unsigned int new_flags = 0;

	if (atomic_read(&local->iff_allmultis))
		new_flags |= FIF_ALLMULTI;

	if (local->monitors || test_bit(SCAN_SW_SCANNING, &local->scanning) ||
	    test_bit(SCAN_ONCHANNEL_SCANNING, &local->scanning))
		new_flags |= FIF_BCN_PRBRESP_PROMISC;

	if (local->fif_probe_req || local->probe_req_reg)
		new_flags |= FIF_PROBE_REQ;

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

	if (local->rx_mcast_action_reg)
		new_flags |= FIF_MCAST_ACTION;

	spin_lock_bh(&local->filter_lock);
	changed_flags = local->filter_flags ^ new_flags;

	mc = drv_prepare_multicast(local, &local->mc_list);
	spin_unlock_bh(&local->filter_lock);

	/* be a bit nasty */
	new_flags |= (1<<31);

	drv_configure_filter(local, changed_flags, &new_flags, mc);

	WARN_ON(new_flags & (1<<31));

	local->filter_flags = new_flags & ~(1<<31);
}

static void ieee80211_reconfig_filter(struct wiphy *wiphy,
				      struct wiphy_work *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, reconfig_filter);

	ieee80211_configure_filter(local);
}

static u32 ieee80211_calc_hw_conf_chan(struct ieee80211_local *local,
				       struct ieee80211_chanctx_conf *ctx)
{
	struct ieee80211_sub_if_data *sdata;
	struct cfg80211_chan_def chandef = {};
	struct cfg80211_chan_def *oper = NULL;
	enum ieee80211_smps_mode smps_mode = IEEE80211_SMPS_STATIC;
	u32 changed = 0;
	int power;
	u32 offchannel_flag;

	if (!local->emulate_chanctx)
		return 0;

	offchannel_flag = local->hw.conf.flags & IEEE80211_CONF_OFFCHANNEL;

	if (ctx && !WARN_ON(!ctx->def.chan)) {
		oper = &ctx->def;
		if (ctx->rx_chains_static > 1)
			smps_mode = IEEE80211_SMPS_OFF;
		else if (ctx->rx_chains_dynamic > 1)
			smps_mode = IEEE80211_SMPS_DYNAMIC;
		else
			smps_mode = IEEE80211_SMPS_STATIC;
	}

	if (local->scan_chandef.chan) {
		chandef = local->scan_chandef;
	} else if (local->tmp_channel) {
		chandef.chan = local->tmp_channel;
		chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		chandef.center_freq1 = chandef.chan->center_freq;
		chandef.freq1_offset = chandef.chan->freq_offset;
	} else if (oper) {
		chandef = *oper;
	} else {
		chandef = local->dflt_chandef;
	}

	if (WARN(!cfg80211_chandef_valid(&chandef),
		 "control:%d.%03d MHz width:%d center: %d.%03d/%d MHz",
		 chandef.chan ? chandef.chan->center_freq : -1,
		 chandef.chan ? chandef.chan->freq_offset : 0,
		 chandef.width, chandef.center_freq1, chandef.freq1_offset,
		 chandef.center_freq2))
		return 0;

	if (!oper || !cfg80211_chandef_identical(&chandef, oper))
		local->hw.conf.flags |= IEEE80211_CONF_OFFCHANNEL;
	else
		local->hw.conf.flags &= ~IEEE80211_CONF_OFFCHANNEL;

	offchannel_flag ^= local->hw.conf.flags & IEEE80211_CONF_OFFCHANNEL;

	/* force it also for scanning, since drivers might config differently */
	if (offchannel_flag || local->scanning || local->in_reconfig ||
	    !cfg80211_chandef_identical(&local->hw.conf.chandef, &chandef)) {
		local->hw.conf.chandef = chandef;
		changed |= IEEE80211_CONF_CHANGE_CHANNEL;
	}

	if (!conf_is_ht(&local->hw.conf)) {
		/*
		 * mac80211.h documents that this is only valid
		 * when the channel is set to an HT type, and
		 * that otherwise STATIC is used.
		 */
		local->hw.conf.smps_mode = IEEE80211_SMPS_STATIC;
	} else if (local->hw.conf.smps_mode != smps_mode) {
		local->hw.conf.smps_mode = smps_mode;
		changed |= IEEE80211_CONF_CHANGE_SMPS;
	}

	power = ieee80211_chandef_max_power(&chandef);
	if (local->user_power_level != IEEE80211_UNSET_POWER_LEVEL)
		power = min(local->user_power_level, power);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!rcu_access_pointer(sdata->vif.bss_conf.chanctx_conf))
			continue;
		if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			continue;
		if (sdata->vif.bss_conf.txpower == INT_MIN)
			continue;
		power = min(power, sdata->vif.bss_conf.txpower);
	}
	rcu_read_unlock();

	if (local->hw.conf.power_level != power) {
		changed |= IEEE80211_CONF_CHANGE_POWER;
		local->hw.conf.power_level = power;
	}

	return changed;
}

int ieee80211_hw_config(struct ieee80211_local *local, int radio_idx,
			u32 changed)
{
	int ret = 0;

	might_sleep();

	WARN_ON(changed & (IEEE80211_CONF_CHANGE_CHANNEL |
			   IEEE80211_CONF_CHANGE_POWER |
			   IEEE80211_CONF_CHANGE_SMPS));

	if (changed && local->open_count) {
		ret = drv_config(local, radio_idx, changed);
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

/* for scanning, offchannel and chanctx emulation only */
static int _ieee80211_hw_conf_chan(struct ieee80211_local *local,
				   struct ieee80211_chanctx_conf *ctx)
{
	u32 changed;

	if (!local->open_count)
		return 0;

	changed = ieee80211_calc_hw_conf_chan(local, ctx);
	if (!changed)
		return 0;

	return drv_config(local, -1, changed);
}

int ieee80211_hw_conf_chan(struct ieee80211_local *local)
{
	struct ieee80211_chanctx *ctx;

	ctx = list_first_entry_or_null(&local->chanctx_list,
				       struct ieee80211_chanctx,
				       list);

	return _ieee80211_hw_conf_chan(local, ctx ? &ctx->conf : NULL);
}

void ieee80211_hw_conf_init(struct ieee80211_local *local)
{
	u32 changed = ~(IEEE80211_CONF_CHANGE_CHANNEL |
			IEEE80211_CONF_CHANGE_POWER |
			IEEE80211_CONF_CHANGE_SMPS);

	if (WARN_ON(!local->open_count))
		return;

	if (local->emulate_chanctx) {
		struct ieee80211_chanctx *ctx;

		ctx = list_first_entry_or_null(&local->chanctx_list,
					       struct ieee80211_chanctx,
					       list);

		changed |= ieee80211_calc_hw_conf_chan(local,
						       ctx ? &ctx->conf : NULL);
	}

	WARN_ON(drv_config(local, -1, changed));
}

int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx)
{
	struct ieee80211_local *local = hw_to_local(hw);

	local->hw.conf.radar_enabled = ctx->radar_enabled;

	return _ieee80211_hw_conf_chan(local, ctx);
}
EXPORT_SYMBOL(ieee80211_emulate_add_chanctx);

void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx)
{
	struct ieee80211_local *local = hw_to_local(hw);

	local->hw.conf.radar_enabled = false;

	_ieee80211_hw_conf_chan(local, NULL);
}
EXPORT_SYMBOL(ieee80211_emulate_remove_chanctx);

void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx,
				      u32 changed)
{
	struct ieee80211_local *local = hw_to_local(hw);

	local->hw.conf.radar_enabled = ctx->radar_enabled;

	_ieee80211_hw_conf_chan(local, ctx);
}
EXPORT_SYMBOL(ieee80211_emulate_change_chanctx);

int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_vif_chanctx_switch *vifs,
					 int n_vifs,
					 enum ieee80211_chanctx_switch_mode mode)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (n_vifs <= 0)
		return -EINVAL;

	local->hw.conf.radar_enabled = vifs[0].new_ctx->radar_enabled;
	_ieee80211_hw_conf_chan(local, vifs[0].new_ctx);

	return 0;
}
EXPORT_SYMBOL(ieee80211_emulate_switch_vif_chanctx);

#define BSS_CHANGED_VIF_CFG_FLAGS (BSS_CHANGED_ASSOC |\
				   BSS_CHANGED_IDLE |\
				   BSS_CHANGED_PS |\
				   BSS_CHANGED_IBSS |\
				   BSS_CHANGED_ARP_FILTER |\
				   BSS_CHANGED_SSID |\
				   BSS_CHANGED_MLD_VALID_LINKS |\
				   BSS_CHANGED_MLD_TTLM)

void ieee80211_bss_info_change_notify(struct ieee80211_sub_if_data *sdata,
				      u64 changed)
{
	struct ieee80211_local *local = sdata->local;

	might_sleep();

	WARN_ON_ONCE(ieee80211_vif_is_mld(&sdata->vif));

	if (!changed || sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		return;

	if (WARN_ON_ONCE(changed & (BSS_CHANGED_BEACON |
				    BSS_CHANGED_BEACON_ENABLED) &&
			 sdata->vif.type != NL80211_IFTYPE_AP &&
			 sdata->vif.type != NL80211_IFTYPE_ADHOC &&
			 sdata->vif.type != NL80211_IFTYPE_MESH_POINT &&
			 sdata->vif.type != NL80211_IFTYPE_OCB))
		return;

	if (WARN_ON_ONCE(sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE ||
			 sdata->vif.type == NL80211_IFTYPE_NAN ||
			 (sdata->vif.type == NL80211_IFTYPE_MONITOR &&
			  !sdata->vif.bss_conf.mu_mimo_owner &&
			  !(changed & BSS_CHANGED_TXPOWER))))
		return;

	if (!check_sdata_in_driver(sdata))
		return;

	if (changed & BSS_CHANGED_VIF_CFG_FLAGS) {
		u64 ch = changed & BSS_CHANGED_VIF_CFG_FLAGS;

		trace_drv_vif_cfg_changed(local, sdata, changed);
		if (local->ops->vif_cfg_changed)
			local->ops->vif_cfg_changed(&local->hw, &sdata->vif, ch);
	}

	if (changed & ~BSS_CHANGED_VIF_CFG_FLAGS) {
		u64 ch = changed & ~BSS_CHANGED_VIF_CFG_FLAGS;

		trace_drv_link_info_changed(local, sdata, &sdata->vif.bss_conf,
					    changed);
		if (local->ops->link_info_changed)
			local->ops->link_info_changed(&local->hw, &sdata->vif,
						      &sdata->vif.bss_conf, ch);
	}

	if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(&local->hw, &sdata->vif,
					     &sdata->vif.bss_conf, changed);
	trace_drv_return_void(local);
}

void ieee80211_vif_cfg_change_notify(struct ieee80211_sub_if_data *sdata,
				     u64 changed)
{
	struct ieee80211_local *local = sdata->local;

	WARN_ON_ONCE(changed & ~BSS_CHANGED_VIF_CFG_FLAGS);

	if (!changed || sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		return;

	drv_vif_cfg_changed(local, sdata, changed);
}

void ieee80211_link_info_change_notify(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_link_data *link,
				       u64 changed)
{
	struct ieee80211_local *local = sdata->local;

	WARN_ON_ONCE(changed & BSS_CHANGED_VIF_CFG_FLAGS);

	if (!changed || sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		return;

	if (!check_sdata_in_driver(sdata))
		return;

	drv_link_info_changed(local, sdata, link->conf, link->link_id, changed);
}

u64 ieee80211_reset_erp_info(struct ieee80211_sub_if_data *sdata)
{
	sdata->vif.bss_conf.use_cts_prot = false;
	sdata->vif.bss_conf.use_short_preamble = false;
	sdata->vif.bss_conf.use_short_slot = false;
	return BSS_CHANGED_ERP_CTS_PROT |
	       BSS_CHANGED_ERP_PREAMBLE |
	       BSS_CHANGED_ERP_SLOT;
}

/* context: requires softirqs disabled */
void ieee80211_handle_queued_frames(struct ieee80211_local *local)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&local->skb_queue)) ||
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		switch (skb->pkt_type) {
		case IEEE80211_RX_MSG:
			/* Clear skb->pkt_type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			ieee80211_rx(&local->hw, skb);
			break;
		case IEEE80211_TX_STATUS_MSG:
			skb->pkt_type = 0;
			ieee80211_tx_status_skb(&local->hw, skb);
			break;
		default:
			WARN(1, "mac80211: Packet is of unknown type %d\n",
			     skb->pkt_type);
			dev_kfree_skb(skb);
			break;
		}
	}
}

static void ieee80211_tasklet_handler(struct tasklet_struct *t)
{
	struct ieee80211_local *local = from_tasklet(local, t, tasklet);

	ieee80211_handle_queued_frames(local);
}

static void ieee80211_restart_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, restart_work);
	struct ieee80211_sub_if_data *sdata;
	int ret;

	flush_workqueue(local->workqueue);

	rtnl_lock();
	/* we might do interface manipulations, so need both */
	wiphy_lock(local->hw.wiphy);
	wiphy_work_flush(local->hw.wiphy, NULL);

	WARN(test_bit(SCAN_HW_SCANNING, &local->scanning),
	     "%s called with hardware scan in progress\n", __func__);

	list_for_each_entry(sdata, &local->interfaces, list) {
		/*
		 * XXX: there may be more work for other vif types and even
		 * for station mode: a good thing would be to run most of
		 * the iface type's dependent _stop (ieee80211_mg_stop,
		 * ieee80211_ibss_stop) etc...
		 * For now, fix only the specific bug that was seen: race
		 * between csa_connection_drop_work and us.
		 */
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			/*
			 * This worker is scheduled from the iface worker that
			 * runs on mac80211's workqueue, so we can't be
			 * scheduling this worker after the cancel right here.
			 * The exception is ieee80211_chswitch_done.
			 * Then we can have a race...
			 */
			wiphy_work_cancel(local->hw.wiphy,
					  &sdata->u.mgd.csa_connection_drop_work);
			if (sdata->vif.bss_conf.csa_active)
				ieee80211_sta_connection_lost(sdata,
							      WLAN_REASON_UNSPECIFIED,
							      false);
		}
		wiphy_delayed_work_flush(local->hw.wiphy,
					 &sdata->dec_tailroom_needed_wk);
	}
	ieee80211_scan_cancel(local);

	/* make sure any new ROC will consider local->in_reconfig */
	wiphy_delayed_work_flush(local->hw.wiphy, &local->roc_work);
	wiphy_work_flush(local->hw.wiphy, &local->hw_roc_done);

	/* wait for all packet processing to be done */
	synchronize_net();

	ret = ieee80211_reconfig(local);
	wiphy_unlock(local->hw.wiphy);

	if (ret)
		cfg80211_shutdown_all_interfaces(local->hw.wiphy);

	rtnl_unlock();
}

void ieee80211_restart_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_restart_hw(local);

	wiphy_info(hw->wiphy,
		   "Hardware restart was requested\n");

	/* use this reason, ieee80211_reconfig will unblock it */
	ieee80211_stop_queues_by_reason(hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_SUSPEND,
					false);

	/*
	 * Stop all Rx during the reconfig. We don't want state changes
	 * or driver callbacks while this is in progress.
	 */
	local->in_reconfig = true;
	barrier();

	queue_work(system_freezable_wq, &local->restart_work);
}
EXPORT_SYMBOL(ieee80211_restart_hw);

#ifdef CONFIG_INET
static int ieee80211_ifa_changed(struct notifier_block *nb,
				 unsigned long data, void *arg)
{
	struct in_ifaddr *ifa = arg;
	struct ieee80211_local *local =
		container_of(nb, struct ieee80211_local,
			     ifa_notifier);
	struct net_device *ndev = ifa->ifa_dev->dev;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	struct in_device *idev;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_vif_cfg *vif_cfg;
	struct ieee80211_if_managed *ifmgd;
	int c = 0;

	/* Make sure it's our interface that got changed */
	if (!wdev)
		return NOTIFY_DONE;

	if (wdev->wiphy != local->hw.wiphy || !wdev->registered)
		return NOTIFY_DONE;

	sdata = IEEE80211_DEV_TO_SUB_IF(ndev);
	vif_cfg = &sdata->vif.cfg;

	/* ARP filtering is only supported in managed mode */
	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return NOTIFY_DONE;

	idev = __in_dev_get_rtnl(sdata->dev);
	if (!idev)
		return NOTIFY_DONE;

	ifmgd = &sdata->u.mgd;

	/*
	 * The nested here is needed to convince lockdep that this is
	 * all OK. Yes, we lock the wiphy mutex here while we already
	 * hold the notifier rwsem, that's the normal case. And yes,
	 * we also acquire the notifier rwsem again when unregistering
	 * a netdev while we already hold the wiphy mutex, so it does
	 * look like a typical ABBA deadlock.
	 *
	 * However, both of these things happen with the RTNL held
	 * already. Therefore, they can't actually happen, since the
	 * lock orders really are ABC and ACB, which is fine due to
	 * the RTNL (A).
	 *
	 * We still need to prevent recursion, which is accomplished
	 * by the !wdev->registered check above.
	 */
	mutex_lock_nested(&local->hw.wiphy->mtx, 1);
	__acquire(&local->hw.wiphy->mtx);

	/* Copy the addresses to the vif config list */
	ifa = rtnl_dereference(idev->ifa_list);
	while (ifa) {
		if (c < IEEE80211_BSS_ARP_ADDR_LIST_LEN)
			vif_cfg->arp_addr_list[c] = ifa->ifa_address;
		ifa = rtnl_dereference(ifa->ifa_next);
		c++;
	}

	vif_cfg->arp_addr_cnt = c;

	/* Configure driver only if associated (which also implies it is up) */
	if (ifmgd->associated)
		ieee80211_vif_cfg_change_notify(sdata, BSS_CHANGED_ARP_FILTER);

	wiphy_unlock(local->hw.wiphy);

	return NOTIFY_OK;
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int ieee80211_ifa6_changed(struct notifier_block *nb,
				  unsigned long data, void *arg)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)arg;
	struct inet6_dev *idev = ifa->idev;
	struct net_device *ndev = ifa->idev->dev;
	struct ieee80211_local *local =
		container_of(nb, struct ieee80211_local, ifa6_notifier);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	struct ieee80211_sub_if_data *sdata;

	/* Make sure it's our interface that got changed */
	if (!wdev || wdev->wiphy != local->hw.wiphy)
		return NOTIFY_DONE;

	sdata = IEEE80211_DEV_TO_SUB_IF(ndev);

	/*
	 * For now only support station mode. This is mostly because
	 * doing AP would have to handle AP_VLAN in some way ...
	 */
	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return NOTIFY_DONE;

	drv_ipv6_addr_change(local, sdata, idev);

	return NOTIFY_OK;
}
#endif

/* There isn't a lot of sense in it, but you can transmit anything you like */
static const struct ieee80211_txrx_stypes
ieee80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		/*
		 * To support Pre Association Security Negotiation (PASN) while
		 * already associated to one AP, allow user space to register to
		 * Rx authentication frames, so that the user space logic would
		 * be able to receive/handle authentication frames from a
		 * different AP as part of PASN.
		 * It is expected that user space would intelligently register
		 * for Rx authentication frames, i.e., only when PASN is used
		 * and configure a match filter only for PASN authentication
		 * algorithm, as otherwise the MLME functionality of mac80211
		 * would be broken.
		 */
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_ACTION >> 4),
	},
	[NL80211_IFTYPE_AP_VLAN] = {
		/* copy AP */
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_ACTION >> 4),
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_ACTION >> 4),
	},
	[NL80211_IFTYPE_MESH_POINT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4),
	},
	[NL80211_IFTYPE_P2P_DEVICE] = {
		.tx = 0xffff,
		/*
		 * To support P2P PASN pairing let user space register to rx
		 * also AUTH frames on P2P device interface.
		 */
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4),
	},
};

static const struct ieee80211_ht_cap mac80211_ht_capa_mod_mask = {
	.ampdu_params_info = IEEE80211_HT_AMPDU_PARM_FACTOR |
			     IEEE80211_HT_AMPDU_PARM_DENSITY,

	.cap_info = cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				IEEE80211_HT_CAP_MAX_AMSDU |
				IEEE80211_HT_CAP_SGI_20 |
				IEEE80211_HT_CAP_SGI_40 |
				IEEE80211_HT_CAP_TX_STBC |
				IEEE80211_HT_CAP_RX_STBC |
				IEEE80211_HT_CAP_LDPC_CODING |
				IEEE80211_HT_CAP_40MHZ_INTOLERANT),
	.mcs = {
		.rx_mask = { 0xff, 0xff, 0xff, 0xff, 0xff,
			     0xff, 0xff, 0xff, 0xff, 0xff, },
	},
};

static const struct ieee80211_vht_cap mac80211_vht_capa_mod_mask = {
	.vht_cap_info =
		cpu_to_le32(IEEE80211_VHT_CAP_RXLDPC |
			    IEEE80211_VHT_CAP_SHORT_GI_80 |
			    IEEE80211_VHT_CAP_SHORT_GI_160 |
			    IEEE80211_VHT_CAP_RXSTBC_MASK |
			    IEEE80211_VHT_CAP_TXSTBC |
			    IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
			    IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			    IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN |
			    IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN |
			    IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK),
	.supp_mcs = {
		.rx_mcs_map = cpu_to_le16(~0),
		.tx_mcs_map = cpu_to_le16(~0),
	},
};

struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len,
					   const struct ieee80211_ops *ops,
					   const char *requested_name)
{
	struct ieee80211_local *local;
	int priv_size, i;
	struct wiphy *wiphy;
	bool emulate_chanctx;

	if (WARN_ON(!ops->tx || !ops->start || !ops->stop || !ops->config ||
		    !ops->add_interface || !ops->remove_interface ||
		    !ops->configure_filter || !ops->wake_tx_queue))
		return NULL;

	if (WARN_ON(ops->sta_state && (ops->sta_add || ops->sta_remove)))
		return NULL;

	if (WARN_ON(!!ops->link_info_changed != !!ops->vif_cfg_changed ||
		    (ops->link_info_changed && ops->bss_info_changed)))
		return NULL;

	/* check all or no channel context operations exist */
	if (ops->add_chanctx == ieee80211_emulate_add_chanctx &&
	    ops->remove_chanctx == ieee80211_emulate_remove_chanctx &&
	    ops->change_chanctx == ieee80211_emulate_change_chanctx) {
		if (WARN_ON(ops->assign_vif_chanctx ||
			    ops->unassign_vif_chanctx))
			return NULL;
		emulate_chanctx = true;
	} else {
		if (WARN_ON(ops->add_chanctx == ieee80211_emulate_add_chanctx ||
			    ops->remove_chanctx == ieee80211_emulate_remove_chanctx ||
			    ops->change_chanctx == ieee80211_emulate_change_chanctx))
			return NULL;
		if (WARN_ON(!ops->add_chanctx ||
			    !ops->remove_chanctx ||
			    !ops->change_chanctx ||
			    !ops->assign_vif_chanctx ||
			    !ops->unassign_vif_chanctx))
			return NULL;
		emulate_chanctx = false;
	}

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

	wiphy = wiphy_new_nm(&mac80211_config_ops, priv_size, requested_name);

	if (!wiphy)
		return NULL;

	wiphy->mgmt_stypes = ieee80211_default_mgmt_stypes;

	wiphy->privid = mac80211_wiphy_privid;

	wiphy->flags |= WIPHY_FLAG_NETNS_OK |
			WIPHY_FLAG_4ADDR_AP |
			WIPHY_FLAG_4ADDR_STATION |
			WIPHY_FLAG_REPORTS_OBSS |
			WIPHY_FLAG_OFFCHAN_TX;

	if (emulate_chanctx || ops->remain_on_channel)
		wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;

	wiphy->features |= NL80211_FEATURE_SK_TX_STATUS |
			   NL80211_FEATURE_SAE |
			   NL80211_FEATURE_HT_IBSS |
			   NL80211_FEATURE_VIF_TXPOWER |
			   NL80211_FEATURE_MAC_ON_CREATE |
			   NL80211_FEATURE_USERSPACE_MPM |
			   NL80211_FEATURE_FULL_AP_CLIENT_STATE;
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_FILS_STA);
	wiphy_ext_feature_set(wiphy,
			      NL80211_EXT_FEATURE_CONTROL_PORT_OVER_NL80211);
	wiphy_ext_feature_set(wiphy,
			      NL80211_EXT_FEATURE_CONTROL_PORT_NO_PREAUTH);
	wiphy_ext_feature_set(wiphy,
			      NL80211_EXT_FEATURE_CONTROL_PORT_OVER_NL80211_TX_STATUS);
	wiphy_ext_feature_set(wiphy,
			      NL80211_EXT_FEATURE_SCAN_FREQ_KHZ);
	wiphy_ext_feature_set(wiphy,
			      NL80211_EXT_FEATURE_POWERED_ADDR_CHANGE);

	if (!ops->hw_scan) {
		wiphy->features |= NL80211_FEATURE_LOW_PRIORITY_SCAN |
				   NL80211_FEATURE_AP_SCAN;
		/*
		 * if the driver behaves correctly using the probe request
		 * (template) from mac80211, then both of these should be
		 * supported even with hw scan - but let drivers opt in.
		 */
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_SCAN_RANDOM_SN);
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_SCAN_MIN_PREQ_CONTENT);
	}

	if (!ops->set_key) {
		wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_SPP_AMSDU_SUPPORT);
	}

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_TXQS);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_RRM);

	wiphy->bss_priv_size = sizeof(struct ieee80211_bss);

	local = wiphy_priv(wiphy);

	if (sta_info_init(local))
		goto err_free;

	local->hw.wiphy = wiphy;

	local->hw.priv = (char *)local + ALIGN(sizeof(*local), NETDEV_ALIGN);

	local->ops = ops;
	local->emulate_chanctx = emulate_chanctx;

	if (emulate_chanctx)
		ieee80211_hw_set(&local->hw, CHANCTX_STA_CSA);

	/*
	 * We need a bit of data queued to build aggregates properly, so
	 * instruct the TCP stack to allow more than a single ms of data
	 * to be queued in the stack. The value is a bit-shift of 1
	 * second, so 7 is ~8ms of queued data. Only affects local TCP
	 * sockets.
	 * This is the default, anyhow - drivers may need to override it
	 * for local reasons (longer buffers, longer completion time, or
	 * similar).
	 */
	local->hw.tx_sk_pacing_shift = 7;

	/* set up some defaults */
	local->hw.queues = 1;
	local->hw.max_rates = 1;
	local->hw.max_report_rates = 0;
	local->hw.max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_HT;
	local->hw.max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_HT;
	local->hw.offchannel_tx_hw_queue = IEEE80211_INVAL_HW_QUEUE;
	local->hw.conf.long_frame_max_tx_count = wiphy->retry_long;
	local->hw.conf.short_frame_max_tx_count = wiphy->retry_short;
	local->hw.radiotap_mcs_details = IEEE80211_RADIOTAP_MCS_HAVE_MCS |
					 IEEE80211_RADIOTAP_MCS_HAVE_GI |
					 IEEE80211_RADIOTAP_MCS_HAVE_BW;
	local->hw.radiotap_vht_details = IEEE80211_RADIOTAP_VHT_KNOWN_GI |
					 IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
	local->hw.uapsd_queues = IEEE80211_DEFAULT_UAPSD_QUEUES;
	local->hw.uapsd_max_sp_len = IEEE80211_DEFAULT_MAX_SP_LEN;
	local->hw.max_mtu = IEEE80211_MAX_DATA_LEN;
	local->user_power_level = IEEE80211_UNSET_POWER_LEVEL;
	wiphy->ht_capa_mod_mask = &mac80211_ht_capa_mod_mask;
	wiphy->vht_capa_mod_mask = &mac80211_vht_capa_mod_mask;

	local->ext_capa[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF;

	wiphy->extended_capabilities = local->ext_capa;
	wiphy->extended_capabilities_mask = local->ext_capa;
	wiphy->extended_capabilities_len =
		ARRAY_SIZE(local->ext_capa);

	INIT_LIST_HEAD(&local->interfaces);
	INIT_LIST_HEAD(&local->mon_list);

	__hw_addr_init(&local->mc_list);

	mutex_init(&local->iflist_mtx);
	spin_lock_init(&local->filter_lock);
	spin_lock_init(&local->rx_path_lock);
	spin_lock_init(&local->queue_stop_reason_lock);

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		INIT_LIST_HEAD(&local->active_txqs[i]);
		spin_lock_init(&local->active_txq_lock[i]);
		local->aql_txq_limit_low[i] = IEEE80211_DEFAULT_AQL_TXQ_LIMIT_L;
		local->aql_txq_limit_high[i] =
			IEEE80211_DEFAULT_AQL_TXQ_LIMIT_H;
		atomic_set(&local->aql_ac_pending_airtime[i], 0);
	}

	local->airtime_flags = AIRTIME_USE_TX | AIRTIME_USE_RX;
	local->aql_threshold = IEEE80211_AQL_THRESHOLD;
	atomic_set(&local->aql_total_pending_airtime, 0);

	spin_lock_init(&local->handle_wake_tx_queue_lock);

	INIT_LIST_HEAD(&local->chanctx_list);

	wiphy_delayed_work_init(&local->scan_work, ieee80211_scan_work);

	INIT_WORK(&local->restart_work, ieee80211_restart_work);

	wiphy_work_init(&local->radar_detected_work,
			ieee80211_dfs_radar_detected_work);

	wiphy_work_init(&local->reconfig_filter, ieee80211_reconfig_filter);

	wiphy_work_init(&local->dynamic_ps_enable_work,
			ieee80211_dynamic_ps_enable_work);
	wiphy_work_init(&local->dynamic_ps_disable_work,
			ieee80211_dynamic_ps_disable_work);
	timer_setup(&local->dynamic_ps_timer, ieee80211_dynamic_ps_timer, 0);

	wiphy_work_init(&local->sched_scan_stopped_work,
			ieee80211_sched_scan_stopped_work);

	spin_lock_init(&local->ack_status_lock);
	idr_init(&local->ack_status_frames);

	for (i = 0; i < IEEE80211_MAX_QUEUES; i++) {
		skb_queue_head_init(&local->pending[i]);
		atomic_set(&local->agg_queue_stop[i], 0);
	}
	tasklet_setup(&local->tx_pending_tasklet, ieee80211_tx_pending);
	tasklet_setup(&local->wake_txqs_tasklet, ieee80211_wake_txqs);
	tasklet_setup(&local->tasklet, ieee80211_tasklet_handler);

	skb_queue_head_init(&local->skb_queue);
	skb_queue_head_init(&local->skb_queue_unreliable);

	ieee80211_alloc_led_names(local);

	ieee80211_roc_setup(local);

	local->hw.radiotap_timestamp.units_pos = -1;
	local->hw.radiotap_timestamp.accuracy = -1;

	return &local->hw;
 err_free:
	wiphy_free(wiphy);
	return NULL;
}
EXPORT_SYMBOL(ieee80211_alloc_hw_nm);

static int ieee80211_init_cipher_suites(struct ieee80211_local *local)
{
	bool have_mfp = ieee80211_hw_check(&local->hw, MFP_CAPABLE);
	static const u32 cipher_suites[] = {
		/* keep WEP and TKIP first, they may be removed below */
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
		WLAN_CIPHER_SUITE_CCMP_256,
		WLAN_CIPHER_SUITE_GCMP,
		WLAN_CIPHER_SUITE_GCMP_256,

		/* keep last -- depends on hw flags! */
		WLAN_CIPHER_SUITE_AES_CMAC,
		WLAN_CIPHER_SUITE_BIP_CMAC_256,
		WLAN_CIPHER_SUITE_BIP_GMAC_128,
		WLAN_CIPHER_SUITE_BIP_GMAC_256,
	};

	if (ieee80211_hw_check(&local->hw, SW_CRYPTO_CONTROL) && fips_enabled) {
		dev_err(local->hw.wiphy->dev.parent,
			"Drivers with SW_CRYPTO_CONTROL cannot work with FIPS\n");
		return -EINVAL;
	}

	if (WARN_ON(ieee80211_hw_check(&local->hw, SW_CRYPTO_CONTROL) &&
		    !local->hw.wiphy->cipher_suites))
		return -EINVAL;

	if (fips_enabled || !local->hw.wiphy->cipher_suites) {
		/* assign the (software supported and perhaps offloaded)
		 * cipher suites
		 */
		local->hw.wiphy->cipher_suites = cipher_suites;
		local->hw.wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

		if (!have_mfp)
			local->hw.wiphy->n_cipher_suites -= 4;

		/* FIPS does not permit the use of RC4 */
		if (fips_enabled) {
			local->hw.wiphy->cipher_suites += 3;
			local->hw.wiphy->n_cipher_suites -= 3;
		}
	}

	return 0;
}

static bool
ieee80211_ifcomb_check(const struct ieee80211_iface_combination *c, int n_comb)
{
	int i, j;

	for (i = 0; i < n_comb; i++, c++) {
		/* DFS is not supported with multi-channel combinations yet */
		if (c->radar_detect_widths &&
		    c->num_different_channels > 1)
			return false;

		/* mac80211 doesn't support more than one IBSS interface */
		for (j = 0; j < c->n_limits; j++)
			if ((c->limits[j].types & BIT(NL80211_IFTYPE_ADHOC)) &&
			    c->limits[j].max > 1)
				return false;
	}

	return true;
}

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	int result, i;
	enum nl80211_band band;
	int channels, max_bitrates;
	bool supp_ht, supp_vht, supp_he, supp_eht;
	struct cfg80211_chan_def dflt_chandef = {};

	if (ieee80211_hw_check(hw, QUEUE_CONTROL) &&
	    (local->hw.offchannel_tx_hw_queue == IEEE80211_INVAL_HW_QUEUE ||
	     local->hw.offchannel_tx_hw_queue >= local->hw.queues))
		return -EINVAL;

	if ((hw->wiphy->features & NL80211_FEATURE_TDLS_CHANNEL_SWITCH) &&
	    (!local->ops->tdls_channel_switch ||
	     !local->ops->tdls_cancel_channel_switch ||
	     !local->ops->tdls_recv_channel_switch))
		return -EOPNOTSUPP;

	if (WARN_ON(ieee80211_hw_check(hw, SUPPORTS_TX_FRAG) &&
		    !local->ops->set_frag_threshold))
		return -EINVAL;

	if (WARN_ON(local->hw.wiphy->interface_modes &
			BIT(NL80211_IFTYPE_NAN) &&
		    (!local->ops->start_nan || !local->ops->stop_nan)))
		return -EINVAL;

	if (hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_MLO) {
		/*
		 * For drivers capable of doing MLO, assume modern driver
		 * or firmware facilities, so software doesn't have to do
		 * as much, e.g. monitoring beacons would be hard if we
		 * might not even know which link is active at which time.
		 */
		if (WARN_ON(local->emulate_chanctx))
			return -EINVAL;

		if (WARN_ON(!local->ops->link_info_changed))
			return -EINVAL;

		if (WARN_ON(!ieee80211_hw_check(hw, HAS_RATE_CONTROL)))
			return -EINVAL;

		if (WARN_ON(!ieee80211_hw_check(hw, AMPDU_AGGREGATION)))
			return -EINVAL;

		if (WARN_ON(ieee80211_hw_check(hw, HOST_BROADCAST_PS_BUFFERING)))
			return -EINVAL;

		if (WARN_ON(ieee80211_hw_check(hw, SUPPORTS_PS) &&
			    (!ieee80211_hw_check(hw, SUPPORTS_DYNAMIC_PS) ||
			     ieee80211_hw_check(hw, PS_NULLFUNC_STACK))))
			return -EINVAL;

		if (WARN_ON(!ieee80211_hw_check(hw, MFP_CAPABLE)))
			return -EINVAL;

		if (WARN_ON(!ieee80211_hw_check(hw, CONNECTION_MONITOR)))
			return -EINVAL;

		if (WARN_ON(ieee80211_hw_check(hw, NEED_DTIM_BEFORE_ASSOC)))
			return -EINVAL;

		if (WARN_ON(ieee80211_hw_check(hw, TIMING_BEACON_ONLY)))
			return -EINVAL;

		if (WARN_ON(!ieee80211_hw_check(hw, AP_LINK_PS)))
			return -EINVAL;
	}

#ifdef CONFIG_PM
	if (hw->wiphy->wowlan && (!local->ops->suspend || !local->ops->resume))
		return -EINVAL;
#endif

	if (local->emulate_chanctx) {
		for (i = 0; i < local->hw.wiphy->n_iface_combinations; i++) {
			const struct ieee80211_iface_combination *comb;

			comb = &local->hw.wiphy->iface_combinations[i];

			if (comb->num_different_channels > 1)
				return -EINVAL;
		}
	}

	if (hw->wiphy->n_radio) {
		for (i = 0; i < hw->wiphy->n_radio; i++) {
			const struct wiphy_radio *radio = &hw->wiphy->radio[i];

			if (!ieee80211_ifcomb_check(radio->iface_combinations,
						    radio->n_iface_combinations))
				return -EINVAL;
		}
	} else {
		if (!ieee80211_ifcomb_check(hw->wiphy->iface_combinations,
					    hw->wiphy->n_iface_combinations))
			return -EINVAL;
	}

	/* Only HW csum features are currently compatible with mac80211 */
	if (WARN_ON(hw->netdev_features & ~MAC80211_SUPPORTED_FEATURES))
		return -EINVAL;

	if (hw->max_report_rates == 0)
		hw->max_report_rates = hw->max_rates;

	local->rx_chains = 1;

	/*
	 * generic code guarantees at least one band,
	 * set this very early because much code assumes
	 * that hw.conf.channel is assigned
	 */
	channels = 0;
	max_bitrates = 0;
	supp_ht = false;
	supp_vht = false;
	supp_he = false;
	supp_eht = false;
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		const struct ieee80211_sband_iftype_data *iftd;
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[band];
		if (!sband)
			continue;

		if (!dflt_chandef.chan) {
			/*
			 * Assign the first enabled channel to dflt_chandef
			 * from the list of channels
			 */
			for (i = 0; i < sband->n_channels; i++)
				if (!(sband->channels[i].flags &
						IEEE80211_CHAN_DISABLED))
					break;
			/* if none found then use the first anyway */
			if (i == sband->n_channels)
				i = 0;
			cfg80211_chandef_create(&dflt_chandef,
						&sband->channels[i],
						NL80211_CHAN_NO_HT);
			/* init channel we're on */
			local->monitor_chanreq.oper = dflt_chandef;
			if (local->emulate_chanctx) {
				local->dflt_chandef = dflt_chandef;
				local->hw.conf.chandef = dflt_chandef;
			}
		}

		channels += sband->n_channels;

		/*
		 * Due to the way the aggregation code handles this and it
		 * being an HT capability, we can't really support delayed
		 * BA in MLO (yet).
		 */
		if (WARN_ON(sband->ht_cap.ht_supported &&
			    (sband->ht_cap.cap & IEEE80211_HT_CAP_DELAY_BA) &&
			    hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_MLO))
			return -EINVAL;

		if (max_bitrates < sband->n_bitrates)
			max_bitrates = sband->n_bitrates;
		supp_ht = supp_ht || sband->ht_cap.ht_supported;
		supp_vht = supp_vht || sband->vht_cap.vht_supported;

		for_each_sband_iftype_data(sband, i, iftd) {
			u8 he_40_mhz_cap;

			supp_he = supp_he || iftd->he_cap.has_he;
			supp_eht = supp_eht || iftd->eht_cap.has_eht;

			if (band == NL80211_BAND_2GHZ)
				he_40_mhz_cap =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
			else
				he_40_mhz_cap =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;

			/* currently no support for HE client where HT has 40 MHz but not HT */
			if (iftd->he_cap.has_he &&
			    iftd->types_mask & (BIT(NL80211_IFTYPE_STATION) |
						BIT(NL80211_IFTYPE_P2P_CLIENT)) &&
			    sband->ht_cap.ht_supported &&
			    sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
			    !(iftd->he_cap.he_cap_elem.phy_cap_info[0] & he_40_mhz_cap))
				return -EINVAL;

			/* no support for per-band vendor elems with MLO */
			if (WARN_ON(iftd->vendor_elems.len &&
				    hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_MLO))
				return -EINVAL;
		}

		/* HT, VHT, HE require QoS, thus >= 4 queues */
		if (WARN_ON(local->hw.queues < IEEE80211_NUM_ACS &&
			    (supp_ht || supp_vht || supp_he)))
			return -EINVAL;

		/* EHT requires HE support */
		if (WARN_ON(supp_eht && !supp_he))
			return -EINVAL;

		if (!sband->ht_cap.ht_supported)
			continue;

		/* TODO: consider VHT for RX chains, hopefully it's the same */
		local->rx_chains =
			max(ieee80211_mcs_to_chains(&sband->ht_cap.mcs),
			    local->rx_chains);

		/* no need to mask, SM_PS_DISABLED has all bits set */
		sband->ht_cap.cap |= WLAN_HT_CAP_SM_PS_DISABLED <<
			             IEEE80211_HT_CAP_SM_PS_SHIFT;
	}

	/* if low-level driver supports AP, we also support VLAN.
	 * drivers advertising SW_CRYPTO_CONTROL should enable AP_VLAN
	 * based on their support to transmit SW encrypted packets.
	 */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_AP) &&
	    !ieee80211_hw_check(&local->hw, SW_CRYPTO_CONTROL)) {
		hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_AP_VLAN);
		hw->wiphy->software_iftypes |= BIT(NL80211_IFTYPE_AP_VLAN);
	}

	/* mac80211 always supports monitor */
	hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);
	hw->wiphy->software_iftypes |= BIT(NL80211_IFTYPE_MONITOR);


	local->int_scan_req = kzalloc(struct_size(local->int_scan_req,
						  channels, channels),
				      GFP_KERNEL);
	if (!local->int_scan_req)
		return -ENOMEM;

	eth_broadcast_addr(local->int_scan_req->bssid);

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		if (!local->hw.wiphy->bands[band])
			continue;
		local->int_scan_req->rates[band] = (u32) -1;
	}

#ifndef CONFIG_MAC80211_MESH
	/* mesh depends on Kconfig, but drivers should set it if they want */
	local->hw.wiphy->interface_modes &= ~BIT(NL80211_IFTYPE_MESH_POINT);
#endif

	/* if the underlying driver supports mesh, mac80211 will (at least)
	 * provide routing of mesh authentication frames to userspace */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_MESH_POINT))
		local->hw.wiphy->flags |= WIPHY_FLAG_MESH_AUTH;

	/* mac80211 supports control port protocol changing */
	local->hw.wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;

	if (ieee80211_hw_check(&local->hw, SIGNAL_DBM)) {
		local->hw.wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	} else if (ieee80211_hw_check(&local->hw, SIGNAL_UNSPEC)) {
		local->hw.wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;
		if (hw->max_signal <= 0) {
			result = -EINVAL;
			goto fail_workqueue;
		}
	}

	/* Mac80211 and therefore all drivers using SW crypto only
	 * are able to handle PTK rekeys and Extended Key ID.
	 */
	if (!local->ops->set_key) {
		wiphy_ext_feature_set(local->hw.wiphy,
				      NL80211_EXT_FEATURE_CAN_REPLACE_PTK0);
		wiphy_ext_feature_set(local->hw.wiphy,
				      NL80211_EXT_FEATURE_EXT_KEY_ID);
	}

	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_ADHOC))
		wiphy_ext_feature_set(local->hw.wiphy,
				      NL80211_EXT_FEATURE_DEL_IBSS_STA);

	/*
	 * Calculate scan IE length -- we need this to alloc
	 * memory and to subtract from the driver limit. It
	 * includes the DS Params, (extended) supported rates, and HT
	 * information -- SSID is the driver's responsibility.
	 */
	local->scan_ies_len = 4 + max_bitrates /* (ext) supp rates */ +
		3 /* DS Params */;
	if (supp_ht)
		local->scan_ies_len += 2 + sizeof(struct ieee80211_ht_cap);

	if (supp_vht)
		local->scan_ies_len +=
			2 + sizeof(struct ieee80211_vht_cap);

	/*
	 * HE cap element is variable in size - set len to allow max size */
	if (supp_he) {
		local->scan_ies_len +=
			3 + sizeof(struct ieee80211_he_cap_elem) +
			sizeof(struct ieee80211_he_mcs_nss_supp) +
			IEEE80211_HE_PPE_THRES_MAX_LEN;

		if (supp_eht)
			local->scan_ies_len +=
				3 + sizeof(struct ieee80211_eht_cap_elem) +
				sizeof(struct ieee80211_eht_mcs_nss_supp) +
				IEEE80211_EHT_PPE_THRES_MAX_LEN;
	}

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

	result = ieee80211_init_cipher_suites(local);
	if (result < 0)
		goto fail_workqueue;

	if (!local->ops->remain_on_channel)
		local->hw.wiphy->max_remain_on_channel_duration = 5000;

	/* mac80211 based drivers don't support internal TDLS setup */
	if (local->hw.wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS)
		local->hw.wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;

	/* mac80211 supports eCSA, if the driver supports STA CSA at all */
	if (ieee80211_hw_check(&local->hw, CHANCTX_STA_CSA))
		local->ext_capa[0] |= WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;

	/* mac80211 supports multi BSSID, if the driver supports it */
	if (ieee80211_hw_check(&local->hw, SUPPORTS_MULTI_BSSID)) {
		local->hw.wiphy->support_mbssid = true;
		if (ieee80211_hw_check(&local->hw,
				       SUPPORTS_ONLY_HE_MULTI_BSSID))
			local->hw.wiphy->support_only_he_mbssid = true;
		else
			local->ext_capa[2] |=
				WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT;
	}

	local->hw.wiphy->max_num_csa_counters = IEEE80211_MAX_CNTDWN_COUNTERS_NUM;

	/*
	 * We use the number of queues for feature tests (QoS, HT) internally
	 * so restrict them appropriately.
	 */
	if (hw->queues > IEEE80211_MAX_QUEUES)
		hw->queues = IEEE80211_MAX_QUEUES;

	local->workqueue =
		alloc_ordered_workqueue("%s", 0, wiphy_name(local->hw.wiphy));
	if (!local->workqueue) {
		result = -ENOMEM;
		goto fail_workqueue;
	}

	/*
	 * The hardware needs headroom for sending the frame,
	 * and we need some headroom for passing the frame to monitor
	 * interfaces, but never both at the same time.
	 */
	local->tx_headroom = max_t(unsigned int , local->hw.extra_tx_headroom,
				   IEEE80211_TX_STATUS_HEADROOM);

	/*
	 * if the driver doesn't specify a max listen interval we
	 * use 5 which should be a safe default
	 */
	if (local->hw.max_listen_interval == 0)
		local->hw.max_listen_interval = 5;

	local->hw.conf.listen_interval = local->hw.max_listen_interval;

	local->dynamic_ps_forced_timeout = -1;

	if (!local->hw.max_nan_de_entries)
		local->hw.max_nan_de_entries = IEEE80211_MAX_NAN_INSTANCE_ID;

	if (!local->hw.weight_multiplier)
		local->hw.weight_multiplier = 1;

	ieee80211_wep_init(local);

	local->hw.conf.flags = IEEE80211_CONF_IDLE;

	ieee80211_led_init(local);

	result = ieee80211_txq_setup_flows(local);
	if (result)
		goto fail_flows;

	rtnl_lock();
	result = ieee80211_init_rate_ctrl_alg(local,
					      hw->rate_control_algorithm);
	rtnl_unlock();
	if (result < 0) {
		wiphy_debug(local->hw.wiphy,
			    "Failed to initialize rate control algorithm\n");
		goto fail_rate;
	}

	if (local->rate_ctrl) {
		clear_bit(IEEE80211_HW_SUPPORTS_VHT_EXT_NSS_BW, hw->flags);
		if (local->rate_ctrl->ops->capa & RATE_CTRL_CAPA_VHT_EXT_NSS_BW)
			ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);
	}

	/*
	 * If the VHT capabilities don't have IEEE80211_VHT_EXT_NSS_BW_CAPABLE,
	 * or have it when we don't, copy the sband structure and set/clear it.
	 * This is necessary because rate scaling algorithms could be switched
	 * and have different support values.
	 * Print a message so that in the common case the reallocation can be
	 * avoided.
	 */
	BUILD_BUG_ON(NUM_NL80211_BANDS > 8 * sizeof(local->sband_allocated));
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sband;
		bool local_cap, ie_cap;

		local_cap = ieee80211_hw_check(hw, SUPPORTS_VHT_EXT_NSS_BW);

		sband = local->hw.wiphy->bands[band];
		if (!sband || !sband->vht_cap.vht_supported)
			continue;

		ie_cap = !!(sband->vht_cap.vht_mcs.tx_highest &
			    cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE));

		if (local_cap == ie_cap)
			continue;

		sband = kmemdup(sband, sizeof(*sband), GFP_KERNEL);
		if (!sband) {
			result = -ENOMEM;
			goto fail_rate;
		}

		wiphy_dbg(hw->wiphy, "copying sband (band %d) due to VHT EXT NSS BW flag\n",
			  band);

		sband->vht_cap.vht_mcs.tx_highest ^=
			cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE);

		local->hw.wiphy->bands[band] = sband;
		local->sband_allocated |= BIT(band);
	}

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		goto fail_wiphy_register;

	debugfs_hw_add(local);
	rate_control_add_debugfs(local);

	ieee80211_check_wbrf_support(local);

	rtnl_lock();
	wiphy_lock(hw->wiphy);

	/* add one default STA interface if supported */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_STATION) &&
	    !ieee80211_hw_check(hw, NO_AUTO_VIF)) {
		struct vif_params params = {0};

		result = ieee80211_if_add(local, "wlan%d", NET_NAME_ENUM, NULL,
					  NL80211_IFTYPE_STATION, &params);
		if (result)
			wiphy_warn(local->hw.wiphy,
				   "Failed to add default virtual iface\n");
	}

	wiphy_unlock(hw->wiphy);
	rtnl_unlock();

#ifdef CONFIG_INET
	local->ifa_notifier.notifier_call = ieee80211_ifa_changed;
	result = register_inetaddr_notifier(&local->ifa_notifier);
	if (result)
		goto fail_ifa;
#endif

#if IS_ENABLED(CONFIG_IPV6)
	local->ifa6_notifier.notifier_call = ieee80211_ifa6_changed;
	result = register_inet6addr_notifier(&local->ifa6_notifier);
	if (result)
		goto fail_ifa6;
#endif

	return 0;

#if IS_ENABLED(CONFIG_IPV6)
 fail_ifa6:
#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&local->ifa_notifier);
#endif
#endif
#if defined(CONFIG_INET) || defined(CONFIG_IPV6)
 fail_ifa:
#endif
	wiphy_unregister(local->hw.wiphy);
 fail_wiphy_register:
	rtnl_lock();
	rate_control_deinitialize(local);
	ieee80211_remove_interfaces(local);
	rtnl_unlock();
 fail_rate:
	ieee80211_txq_teardown_flows(local);
 fail_flows:
	ieee80211_led_exit(local);
	destroy_workqueue(local->workqueue);
 fail_workqueue:
	kfree(local->int_scan_req);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	tasklet_kill(&local->tx_pending_tasklet);
	tasklet_kill(&local->tasklet);

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&local->ifa_notifier);
#endif
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&local->ifa6_notifier);
#endif

	rtnl_lock();

	/*
	 * At this point, interface list manipulations are fine
	 * because the driver cannot be handing us frames any
	 * more and the tasklet is killed.
	 */
	ieee80211_remove_interfaces(local);

	ieee80211_txq_teardown_flows(local);

	wiphy_lock(local->hw.wiphy);
	wiphy_delayed_work_cancel(local->hw.wiphy, &local->roc_work);
	wiphy_work_cancel(local->hw.wiphy, &local->reconfig_filter);
	wiphy_work_cancel(local->hw.wiphy, &local->sched_scan_stopped_work);
	wiphy_work_cancel(local->hw.wiphy, &local->radar_detected_work);
	wiphy_unlock(local->hw.wiphy);
	rtnl_unlock();

	cancel_work_sync(&local->restart_work);

	ieee80211_clear_tx_pending(local);
	rate_control_deinitialize(local);

	if (skb_queue_len(&local->skb_queue) ||
	    skb_queue_len(&local->skb_queue_unreliable))
		wiphy_warn(local->hw.wiphy, "skb_queue not empty\n");
	skb_queue_purge(&local->skb_queue);
	skb_queue_purge(&local->skb_queue_unreliable);

	wiphy_unregister(local->hw.wiphy);
	destroy_workqueue(local->workqueue);
	ieee80211_led_exit(local);
	kfree(local->int_scan_req);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

static int ieee80211_free_ack_frame(int id, void *p, void *data)
{
	WARN_ONCE(1, "Have pending ack frames!\n");
	kfree_skb(p);
	return 0;
}

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	enum nl80211_band band;

	mutex_destroy(&local->iflist_mtx);

	idr_for_each(&local->ack_status_frames,
		     ieee80211_free_ack_frame, NULL);
	idr_destroy(&local->ack_status_frames);

	sta_info_stop(local);

	ieee80211_free_led_names(local);

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		if (!(local->sband_allocated & BIT(band)))
			continue;
		kfree(local->hw.wiphy->bands[band]);
	}

	wiphy_free(local->hw.wiphy);
}
EXPORT_SYMBOL(ieee80211_free_hw);
#define V(x)   #x,
static const char * const drop_reasons_unusable[] = {
	[0] = "RX_DROP_UNUSABLE",
	MAC80211_DROP_REASONS_UNUSABLE(V)
#undef V
};

static struct drop_reason_list drop_reason_list_unusable = {
	.reasons = drop_reasons_unusable,
	.n_reasons = ARRAY_SIZE(drop_reasons_unusable),
};

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

	ret = ieee80211_iface_init();
	if (ret)
		goto err_netdev;

	drop_reasons_register_subsys(SKB_DROP_REASON_SUBSYS_MAC80211_UNUSABLE,
				     &drop_reason_list_unusable);

	return 0;
 err_netdev:
	rc80211_minstrel_exit();

	return ret;
}

static void __exit ieee80211_exit(void)
{
	rc80211_minstrel_exit();

	ieee80211s_stop();

	ieee80211_iface_exit();

	drop_reasons_unregister_subsys(SKB_DROP_REASON_SUBSYS_MAC80211_UNUSABLE);

	rcu_barrier();
}


subsys_initcall(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
