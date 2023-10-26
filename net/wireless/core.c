// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is the linux wireless configuration interface.
 *
 * Copyright 2006-2010		Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright 2015-2017	Intel Deutschland GmbH
 * Copyright (C) 2018-2022 Intel Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/if.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/nl80211.h>
#include <linux/debugfs.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"
#include "sysfs.h"
#include "debugfs.h"
#include "wext-compat.h"
#include "rdev-ops.h"

/* name for sysfs, %d is appended */
#define PHY_NAME "phy"

MODULE_AUTHOR("Johannes Berg");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("wireless configuration support");
MODULE_ALIAS_GENL_FAMILY(NL80211_GENL_NAME);

/* RCU-protected (and RTNL for writers) */
LIST_HEAD(cfg80211_rdev_list);
int cfg80211_rdev_list_generation;

/* for debugfs */
static struct dentry *ieee80211_debugfs_dir;

/* for the cleanup, scan and event works */
struct workqueue_struct *cfg80211_wq;

static bool cfg80211_disable_40mhz_24ghz;
module_param(cfg80211_disable_40mhz_24ghz, bool, 0644);
MODULE_PARM_DESC(cfg80211_disable_40mhz_24ghz,
		 "Disable 40MHz support in the 2.4GHz band");

struct cfg80211_registered_device *cfg80211_rdev_by_wiphy_idx(int wiphy_idx)
{
	struct cfg80211_registered_device *result = NULL, *rdev;

	ASSERT_RTNL();

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (rdev->wiphy_idx == wiphy_idx) {
			result = rdev;
			break;
		}
	}

	return result;
}

int get_wiphy_idx(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	return rdev->wiphy_idx;
}

struct wiphy *wiphy_idx_to_wiphy(int wiphy_idx)
{
	struct cfg80211_registered_device *rdev;

	ASSERT_RTNL();

	rdev = cfg80211_rdev_by_wiphy_idx(wiphy_idx);
	if (!rdev)
		return NULL;
	return &rdev->wiphy;
}

static int cfg80211_dev_check_name(struct cfg80211_registered_device *rdev,
				   const char *newname)
{
	struct cfg80211_registered_device *rdev2;
	int wiphy_idx, taken = -1, digits;

	ASSERT_RTNL();

	if (strlen(newname) > NL80211_WIPHY_NAME_MAXLEN)
		return -EINVAL;

	/* prohibit calling the thing phy%d when %d is not its number */
	sscanf(newname, PHY_NAME "%d%n", &wiphy_idx, &taken);
	if (taken == strlen(newname) && wiphy_idx != rdev->wiphy_idx) {
		/* count number of places needed to print wiphy_idx */
		digits = 1;
		while (wiphy_idx /= 10)
			digits++;
		/*
		 * deny the name if it is phy<idx> where <idx> is printed
		 * without leading zeroes. taken == strlen(newname) here
		 */
		if (taken == strlen(PHY_NAME) + digits)
			return -EINVAL;
	}

	/* Ensure another device does not already have this name. */
	list_for_each_entry(rdev2, &cfg80211_rdev_list, list)
		if (strcmp(newname, wiphy_name(&rdev2->wiphy)) == 0)
			return -EINVAL;

	return 0;
}

int cfg80211_dev_rename(struct cfg80211_registered_device *rdev,
			char *newname)
{
	int result;

	ASSERT_RTNL();

	/* Ignore nop renames */
	if (strcmp(newname, wiphy_name(&rdev->wiphy)) == 0)
		return 0;

	result = cfg80211_dev_check_name(rdev, newname);
	if (result < 0)
		return result;

	result = device_rename(&rdev->wiphy.dev, newname);
	if (result)
		return result;

	if (!IS_ERR_OR_NULL(rdev->wiphy.debugfsdir))
		debugfs_rename(rdev->wiphy.debugfsdir->d_parent,
			       rdev->wiphy.debugfsdir,
			       rdev->wiphy.debugfsdir->d_parent, newname);

	nl80211_notify_wiphy(rdev, NL80211_CMD_NEW_WIPHY);

	return 0;
}

int cfg80211_switch_netns(struct cfg80211_registered_device *rdev,
			  struct net *net)
{
	struct wireless_dev *wdev;
	int err = 0;

	if (!(rdev->wiphy.flags & WIPHY_FLAG_NETNS_OK))
		return -EOPNOTSUPP;

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
		if (!wdev->netdev)
			continue;
		wdev->netdev->features &= ~NETIF_F_NETNS_LOCAL;
		err = dev_change_net_namespace(wdev->netdev, net, "wlan%d");
		if (err)
			break;
		wdev->netdev->features |= NETIF_F_NETNS_LOCAL;
	}

	if (err) {
		/* failed -- clean up to old netns */
		net = wiphy_net(&rdev->wiphy);

		list_for_each_entry_continue_reverse(wdev,
						     &rdev->wiphy.wdev_list,
						     list) {
			if (!wdev->netdev)
				continue;
			wdev->netdev->features &= ~NETIF_F_NETNS_LOCAL;
			err = dev_change_net_namespace(wdev->netdev, net,
							"wlan%d");
			WARN_ON(err);
			wdev->netdev->features |= NETIF_F_NETNS_LOCAL;
		}

		return err;
	}

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
		if (!wdev->netdev)
			continue;
		nl80211_notify_iface(rdev, wdev, NL80211_CMD_DEL_INTERFACE);
	}
	nl80211_notify_wiphy(rdev, NL80211_CMD_DEL_WIPHY);

	wiphy_net_set(&rdev->wiphy, net);

	err = device_rename(&rdev->wiphy.dev, dev_name(&rdev->wiphy.dev));
	WARN_ON(err);

	nl80211_notify_wiphy(rdev, NL80211_CMD_NEW_WIPHY);
	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
		if (!wdev->netdev)
			continue;
		nl80211_notify_iface(rdev, wdev, NL80211_CMD_NEW_INTERFACE);
	}

	return 0;
}

static void cfg80211_rfkill_poll(struct rfkill *rfkill, void *data)
{
	struct cfg80211_registered_device *rdev = data;

	rdev_rfkill_poll(rdev);
}

void cfg80211_stop_p2p_device(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev)
{
	lockdep_assert_held(&rdev->wiphy.mtx);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_P2P_DEVICE))
		return;

	if (!wdev_running(wdev))
		return;

	rdev_stop_p2p_device(rdev, wdev);
	wdev->is_running = false;

	rdev->opencount--;

	if (rdev->scan_req && rdev->scan_req->wdev == wdev) {
		if (WARN_ON(!rdev->scan_req->notified &&
			    (!rdev->int_scan_req ||
			     !rdev->int_scan_req->notified)))
			rdev->scan_req->info.aborted = true;
		___cfg80211_scan_done(rdev, false);
	}
}

void cfg80211_stop_nan(struct cfg80211_registered_device *rdev,
		       struct wireless_dev *wdev)
{
	lockdep_assert_held(&rdev->wiphy.mtx);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_NAN))
		return;

	if (!wdev_running(wdev))
		return;

	rdev_stop_nan(rdev, wdev);
	wdev->is_running = false;

	rdev->opencount--;
}

void cfg80211_shutdown_all_interfaces(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct wireless_dev *wdev;

	ASSERT_RTNL();

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
		if (wdev->netdev) {
			dev_close(wdev->netdev);
			continue;
		}

		/* otherwise, check iftype */

		wiphy_lock(wiphy);

		switch (wdev->iftype) {
		case NL80211_IFTYPE_P2P_DEVICE:
			cfg80211_stop_p2p_device(rdev, wdev);
			break;
		case NL80211_IFTYPE_NAN:
			cfg80211_stop_nan(rdev, wdev);
			break;
		default:
			break;
		}

		wiphy_unlock(wiphy);
	}
}
EXPORT_SYMBOL_GPL(cfg80211_shutdown_all_interfaces);

static int cfg80211_rfkill_set_block(void *data, bool blocked)
{
	struct cfg80211_registered_device *rdev = data;

	if (!blocked)
		return 0;

	rtnl_lock();
	cfg80211_shutdown_all_interfaces(&rdev->wiphy);
	rtnl_unlock();

	return 0;
}

static void cfg80211_rfkill_block_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(work, struct cfg80211_registered_device,
			    rfkill_block);
	cfg80211_rfkill_set_block(rdev, true);
}

static void cfg80211_event_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(work, struct cfg80211_registered_device,
			    event_work);

	wiphy_lock(&rdev->wiphy);
	cfg80211_process_rdev_events(rdev);
	wiphy_unlock(&rdev->wiphy);
}

void cfg80211_destroy_ifaces(struct cfg80211_registered_device *rdev)
{
	struct wireless_dev *wdev, *tmp;

	ASSERT_RTNL();

	list_for_each_entry_safe(wdev, tmp, &rdev->wiphy.wdev_list, list) {
		if (wdev->nl_owner_dead) {
			if (wdev->netdev)
				dev_close(wdev->netdev);

			wiphy_lock(&rdev->wiphy);
			cfg80211_leave(rdev, wdev);
			cfg80211_remove_virtual_intf(rdev, wdev);
			wiphy_unlock(&rdev->wiphy);
		}
	}
}

static void cfg80211_destroy_iface_wk(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(work, struct cfg80211_registered_device,
			    destroy_work);

	rtnl_lock();
	cfg80211_destroy_ifaces(rdev);
	rtnl_unlock();
}

static void cfg80211_sched_scan_stop_wk(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;
	struct cfg80211_sched_scan_request *req, *tmp;

	rdev = container_of(work, struct cfg80211_registered_device,
			   sched_scan_stop_wk);

	wiphy_lock(&rdev->wiphy);
	list_for_each_entry_safe(req, tmp, &rdev->sched_scan_req_list, list) {
		if (req->nl_owner_dead)
			cfg80211_stop_sched_scan_req(rdev, req, false);
	}
	wiphy_unlock(&rdev->wiphy);
}

static void cfg80211_propagate_radar_detect_wk(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(work, struct cfg80211_registered_device,
			    propagate_radar_detect_wk);

	rtnl_lock();

	regulatory_propagate_dfs_state(&rdev->wiphy, &rdev->radar_chandef,
				       NL80211_DFS_UNAVAILABLE,
				       NL80211_RADAR_DETECTED);

	rtnl_unlock();
}

static void cfg80211_propagate_cac_done_wk(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(work, struct cfg80211_registered_device,
			    propagate_cac_done_wk);

	rtnl_lock();

	regulatory_propagate_dfs_state(&rdev->wiphy, &rdev->cac_done_chandef,
				       NL80211_DFS_AVAILABLE,
				       NL80211_RADAR_CAC_FINISHED);

	rtnl_unlock();
}

static void cfg80211_wiphy_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;
	struct wiphy_work *wk;

	rdev = container_of(work, struct cfg80211_registered_device, wiphy_work);

	wiphy_lock(&rdev->wiphy);
	if (rdev->suspended)
		goto out;

	spin_lock_irq(&rdev->wiphy_work_lock);
	wk = list_first_entry_or_null(&rdev->wiphy_work_list,
				      struct wiphy_work, entry);
	if (wk) {
		list_del_init(&wk->entry);
		if (!list_empty(&rdev->wiphy_work_list))
			schedule_work(work);
		spin_unlock_irq(&rdev->wiphy_work_lock);

		wk->func(&rdev->wiphy, wk);
	} else {
		spin_unlock_irq(&rdev->wiphy_work_lock);
	}
out:
	wiphy_unlock(&rdev->wiphy);
}

/* exported functions */

struct wiphy *wiphy_new_nm(const struct cfg80211_ops *ops, int sizeof_priv,
			   const char *requested_name)
{
	static atomic_t wiphy_counter = ATOMIC_INIT(0);

	struct cfg80211_registered_device *rdev;
	int alloc_size;

	WARN_ON(ops->add_key && (!ops->del_key || !ops->set_default_key));
	WARN_ON(ops->auth && (!ops->assoc || !ops->deauth || !ops->disassoc));
	WARN_ON(ops->connect && !ops->disconnect);
	WARN_ON(ops->join_ibss && !ops->leave_ibss);
	WARN_ON(ops->add_virtual_intf && !ops->del_virtual_intf);
	WARN_ON(ops->add_station && !ops->del_station);
	WARN_ON(ops->add_mpath && !ops->del_mpath);
	WARN_ON(ops->join_mesh && !ops->leave_mesh);
	WARN_ON(ops->start_p2p_device && !ops->stop_p2p_device);
	WARN_ON(ops->start_ap && !ops->stop_ap);
	WARN_ON(ops->join_ocb && !ops->leave_ocb);
	WARN_ON(ops->suspend && !ops->resume);
	WARN_ON(ops->sched_scan_start && !ops->sched_scan_stop);
	WARN_ON(ops->remain_on_channel && !ops->cancel_remain_on_channel);
	WARN_ON(ops->tdls_channel_switch && !ops->tdls_cancel_channel_switch);
	WARN_ON(ops->add_tx_ts && !ops->del_tx_ts);

	alloc_size = sizeof(*rdev) + sizeof_priv;

	rdev = kzalloc(alloc_size, GFP_KERNEL);
	if (!rdev)
		return NULL;

	rdev->ops = ops;

	rdev->wiphy_idx = atomic_inc_return(&wiphy_counter);

	if (unlikely(rdev->wiphy_idx < 0)) {
		/* ugh, wrapped! */
		atomic_dec(&wiphy_counter);
		kfree(rdev);
		return NULL;
	}

	/* atomic_inc_return makes it start at 1, make it start at 0 */
	rdev->wiphy_idx--;

	/* give it a proper name */
	if (requested_name && requested_name[0]) {
		int rv;

		rtnl_lock();
		rv = cfg80211_dev_check_name(rdev, requested_name);

		if (rv < 0) {
			rtnl_unlock();
			goto use_default_name;
		}

		rv = dev_set_name(&rdev->wiphy.dev, "%s", requested_name);
		rtnl_unlock();
		if (rv)
			goto use_default_name;
	} else {
		int rv;

use_default_name:
		/* NOTE:  This is *probably* safe w/out holding rtnl because of
		 * the restrictions on phy names.  Probably this call could
		 * fail if some other part of the kernel (re)named a device
		 * phyX.  But, might should add some locking and check return
		 * value, and use a different name if this one exists?
		 */
		rv = dev_set_name(&rdev->wiphy.dev, PHY_NAME "%d", rdev->wiphy_idx);
		if (rv < 0) {
			kfree(rdev);
			return NULL;
		}
	}

	mutex_init(&rdev->wiphy.mtx);
	INIT_LIST_HEAD(&rdev->wiphy.wdev_list);
	INIT_LIST_HEAD(&rdev->beacon_registrations);
	spin_lock_init(&rdev->beacon_registrations_lock);
	spin_lock_init(&rdev->bss_lock);
	INIT_LIST_HEAD(&rdev->bss_list);
	INIT_LIST_HEAD(&rdev->sched_scan_req_list);
	INIT_WORK(&rdev->scan_done_wk, __cfg80211_scan_done);
	INIT_DELAYED_WORK(&rdev->dfs_update_channels_wk,
			  cfg80211_dfs_channels_update_work);
#ifdef CONFIG_CFG80211_WEXT
	rdev->wiphy.wext = &cfg80211_wext_handler;
#endif

	device_initialize(&rdev->wiphy.dev);
	rdev->wiphy.dev.class = &ieee80211_class;
	rdev->wiphy.dev.platform_data = rdev;
	device_enable_async_suspend(&rdev->wiphy.dev);

	INIT_WORK(&rdev->destroy_work, cfg80211_destroy_iface_wk);
	INIT_WORK(&rdev->sched_scan_stop_wk, cfg80211_sched_scan_stop_wk);
	INIT_WORK(&rdev->sched_scan_res_wk, cfg80211_sched_scan_results_wk);
	INIT_WORK(&rdev->propagate_radar_detect_wk,
		  cfg80211_propagate_radar_detect_wk);
	INIT_WORK(&rdev->propagate_cac_done_wk, cfg80211_propagate_cac_done_wk);
	INIT_WORK(&rdev->mgmt_registrations_update_wk,
		  cfg80211_mgmt_registrations_update_wk);
	spin_lock_init(&rdev->mgmt_registrations_lock);

#ifdef CONFIG_CFG80211_DEFAULT_PS
	rdev->wiphy.flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif

	wiphy_net_set(&rdev->wiphy, &init_net);

	rdev->rfkill_ops.set_block = cfg80211_rfkill_set_block;
	rdev->wiphy.rfkill = rfkill_alloc(dev_name(&rdev->wiphy.dev),
					  &rdev->wiphy.dev, RFKILL_TYPE_WLAN,
					  &rdev->rfkill_ops, rdev);

	if (!rdev->wiphy.rfkill) {
		wiphy_free(&rdev->wiphy);
		return NULL;
	}

	INIT_WORK(&rdev->wiphy_work, cfg80211_wiphy_work);
	INIT_LIST_HEAD(&rdev->wiphy_work_list);
	spin_lock_init(&rdev->wiphy_work_lock);
	INIT_WORK(&rdev->rfkill_block, cfg80211_rfkill_block_work);
	INIT_WORK(&rdev->conn_work, cfg80211_conn_work);
	INIT_WORK(&rdev->event_work, cfg80211_event_work);
	INIT_WORK(&rdev->background_cac_abort_wk,
		  cfg80211_background_cac_abort_wk);
	INIT_DELAYED_WORK(&rdev->background_cac_done_wk,
			  cfg80211_background_cac_done_wk);

	init_waitqueue_head(&rdev->dev_wait);

	/*
	 * Initialize wiphy parameters to IEEE 802.11 MIB default values.
	 * Fragmentation and RTS threshold are disabled by default with the
	 * special -1 value.
	 */
	rdev->wiphy.retry_short = 7;
	rdev->wiphy.retry_long = 4;
	rdev->wiphy.frag_threshold = (u32) -1;
	rdev->wiphy.rts_threshold = (u32) -1;
	rdev->wiphy.coverage_class = 0;

	rdev->wiphy.max_num_csa_counters = 1;

	rdev->wiphy.max_sched_scan_plans = 1;
	rdev->wiphy.max_sched_scan_plan_interval = U32_MAX;

	return &rdev->wiphy;
}
EXPORT_SYMBOL(wiphy_new_nm);

static int wiphy_verify_combinations(struct wiphy *wiphy)
{
	const struct ieee80211_iface_combination *c;
	int i, j;

	for (i = 0; i < wiphy->n_iface_combinations; i++) {
		u32 cnt = 0;
		u16 all_iftypes = 0;

		c = &wiphy->iface_combinations[i];

		/*
		 * Combinations with just one interface aren't real,
		 * however we make an exception for DFS.
		 */
		if (WARN_ON((c->max_interfaces < 2) && !c->radar_detect_widths))
			return -EINVAL;

		/* Need at least one channel */
		if (WARN_ON(!c->num_different_channels))
			return -EINVAL;

		/* DFS only works on one channel. */
		if (WARN_ON(c->radar_detect_widths &&
			    (c->num_different_channels > 1)))
			return -EINVAL;

		if (WARN_ON(!c->n_limits))
			return -EINVAL;

		for (j = 0; j < c->n_limits; j++) {
			u16 types = c->limits[j].types;

			/* interface types shouldn't overlap */
			if (WARN_ON(types & all_iftypes))
				return -EINVAL;
			all_iftypes |= types;

			if (WARN_ON(!c->limits[j].max))
				return -EINVAL;

			/* Shouldn't list software iftypes in combinations! */
			if (WARN_ON(wiphy->software_iftypes & types))
				return -EINVAL;

			/* Only a single P2P_DEVICE can be allowed */
			if (WARN_ON(types & BIT(NL80211_IFTYPE_P2P_DEVICE) &&
				    c->limits[j].max > 1))
				return -EINVAL;

			/* Only a single NAN can be allowed */
			if (WARN_ON(types & BIT(NL80211_IFTYPE_NAN) &&
				    c->limits[j].max > 1))
				return -EINVAL;

			/*
			 * This isn't well-defined right now. If you have an
			 * IBSS interface, then its beacon interval may change
			 * by joining other networks, and nothing prevents it
			 * from doing that.
			 * So technically we probably shouldn't even allow AP
			 * and IBSS in the same interface, but it seems that
			 * some drivers support that, possibly only with fixed
			 * beacon intervals for IBSS.
			 */
			if (WARN_ON(types & BIT(NL80211_IFTYPE_ADHOC) &&
				    c->beacon_int_min_gcd)) {
				return -EINVAL;
			}

			cnt += c->limits[j].max;
			/*
			 * Don't advertise an unsupported type
			 * in a combination.
			 */
			if (WARN_ON((wiphy->interface_modes & types) != types))
				return -EINVAL;
		}

		if (WARN_ON(all_iftypes & BIT(NL80211_IFTYPE_WDS)))
			return -EINVAL;

		/* You can't even choose that many! */
		if (WARN_ON(cnt < c->max_interfaces))
			return -EINVAL;
	}

	return 0;
}

int wiphy_register(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	int res;
	enum nl80211_band band;
	struct ieee80211_supported_band *sband;
	bool have_band = false;
	int i;
	u16 ifmodes = wiphy->interface_modes;

#ifdef CONFIG_PM
	if (WARN_ON(wiphy->wowlan &&
		    (wiphy->wowlan->flags & WIPHY_WOWLAN_GTK_REKEY_FAILURE) &&
		    !(wiphy->wowlan->flags & WIPHY_WOWLAN_SUPPORTS_GTK_REKEY)))
		return -EINVAL;
	if (WARN_ON(wiphy->wowlan &&
		    !wiphy->wowlan->flags && !wiphy->wowlan->n_patterns &&
		    !wiphy->wowlan->tcp))
		return -EINVAL;
#endif
	if (WARN_ON((wiphy->features & NL80211_FEATURE_TDLS_CHANNEL_SWITCH) &&
		    (!rdev->ops->tdls_channel_switch ||
		     !rdev->ops->tdls_cancel_channel_switch)))
		return -EINVAL;

	if (WARN_ON((wiphy->interface_modes & BIT(NL80211_IFTYPE_NAN)) &&
		    (!rdev->ops->start_nan || !rdev->ops->stop_nan ||
		     !rdev->ops->add_nan_func || !rdev->ops->del_nan_func ||
		     !(wiphy->nan_supported_bands & BIT(NL80211_BAND_2GHZ)))))
		return -EINVAL;

	if (WARN_ON(wiphy->interface_modes & BIT(NL80211_IFTYPE_WDS)))
		return -EINVAL;

	if (WARN_ON(wiphy->pmsr_capa && !wiphy->pmsr_capa->ftm.supported))
		return -EINVAL;

	if (wiphy->pmsr_capa && wiphy->pmsr_capa->ftm.supported) {
		if (WARN_ON(!wiphy->pmsr_capa->ftm.asap &&
			    !wiphy->pmsr_capa->ftm.non_asap))
			return -EINVAL;
		if (WARN_ON(!wiphy->pmsr_capa->ftm.preambles ||
			    !wiphy->pmsr_capa->ftm.bandwidths))
			return -EINVAL;
		if (WARN_ON(wiphy->pmsr_capa->ftm.preambles &
				~(BIT(NL80211_PREAMBLE_LEGACY) |
				  BIT(NL80211_PREAMBLE_HT) |
				  BIT(NL80211_PREAMBLE_VHT) |
				  BIT(NL80211_PREAMBLE_HE) |
				  BIT(NL80211_PREAMBLE_DMG))))
			return -EINVAL;
		if (WARN_ON((wiphy->pmsr_capa->ftm.trigger_based ||
			     wiphy->pmsr_capa->ftm.non_trigger_based) &&
			    !(wiphy->pmsr_capa->ftm.preambles &
			      BIT(NL80211_PREAMBLE_HE))))
			return -EINVAL;
		if (WARN_ON(wiphy->pmsr_capa->ftm.bandwidths &
				~(BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				  BIT(NL80211_CHAN_WIDTH_20) |
				  BIT(NL80211_CHAN_WIDTH_40) |
				  BIT(NL80211_CHAN_WIDTH_80) |
				  BIT(NL80211_CHAN_WIDTH_80P80) |
				  BIT(NL80211_CHAN_WIDTH_160) |
				  BIT(NL80211_CHAN_WIDTH_5) |
				  BIT(NL80211_CHAN_WIDTH_10))))
			return -EINVAL;
	}

	if (WARN_ON((wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED) &&
		    (wiphy->regulatory_flags &
					(REGULATORY_CUSTOM_REG |
					 REGULATORY_STRICT_REG |
					 REGULATORY_COUNTRY_IE_FOLLOW_POWER |
					 REGULATORY_COUNTRY_IE_IGNORE))))
		return -EINVAL;

	if (WARN_ON(wiphy->coalesce &&
		    (!wiphy->coalesce->n_rules ||
		     !wiphy->coalesce->n_patterns) &&
		    (!wiphy->coalesce->pattern_min_len ||
		     wiphy->coalesce->pattern_min_len >
			wiphy->coalesce->pattern_max_len)))
		return -EINVAL;

	if (WARN_ON(wiphy->ap_sme_capa &&
		    !(wiphy->flags & WIPHY_FLAG_HAVE_AP_SME)))
		return -EINVAL;

	if (WARN_ON(wiphy->addresses && !wiphy->n_addresses))
		return -EINVAL;

	if (WARN_ON(wiphy->addresses &&
		    !is_zero_ether_addr(wiphy->perm_addr) &&
		    memcmp(wiphy->perm_addr, wiphy->addresses[0].addr,
			   ETH_ALEN)))
		return -EINVAL;

	if (WARN_ON(wiphy->max_acl_mac_addrs &&
		    (!(wiphy->flags & WIPHY_FLAG_HAVE_AP_SME) ||
		     !rdev->ops->set_mac_acl)))
		return -EINVAL;

	/* assure only valid behaviours are flagged by driver
	 * hence subtract 2 as bit 0 is invalid.
	 */
	if (WARN_ON(wiphy->bss_select_support &&
		    (wiphy->bss_select_support & ~(BIT(__NL80211_BSS_SELECT_ATTR_AFTER_LAST) - 2))))
		return -EINVAL;

	if (WARN_ON(wiphy_ext_feature_isset(&rdev->wiphy,
					    NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X) &&
		    (!rdev->ops->set_pmk || !rdev->ops->del_pmk)))
		return -EINVAL;

	if (WARN_ON(!(rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_FW_ROAM) &&
		    rdev->ops->update_connect_params))
		return -EINVAL;

	if (wiphy->addresses)
		memcpy(wiphy->perm_addr, wiphy->addresses[0].addr, ETH_ALEN);

	/* sanity check ifmodes */
	WARN_ON(!ifmodes);
	ifmodes &= ((1 << NUM_NL80211_IFTYPES) - 1) & ~1;
	if (WARN_ON(ifmodes != wiphy->interface_modes))
		wiphy->interface_modes = ifmodes;

	res = wiphy_verify_combinations(wiphy);
	if (res)
		return res;

	/* sanity check supported bands/channels */
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		u16 types = 0;
		bool have_he = false;

		sband = wiphy->bands[band];
		if (!sband)
			continue;

		sband->band = band;
		if (WARN_ON(!sband->n_channels))
			return -EINVAL;
		/*
		 * on 60GHz or sub-1Ghz band, there are no legacy rates, so
		 * n_bitrates is 0
		 */
		if (WARN_ON((band != NL80211_BAND_60GHZ &&
			     band != NL80211_BAND_S1GHZ) &&
			    !sband->n_bitrates))
			return -EINVAL;

		if (WARN_ON(band == NL80211_BAND_6GHZ &&
			    (sband->ht_cap.ht_supported ||
			     sband->vht_cap.vht_supported)))
			return -EINVAL;

		/*
		 * Since cfg80211_disable_40mhz_24ghz is global, we can
		 * modify the sband's ht data even if the driver uses a
		 * global structure for that.
		 */
		if (cfg80211_disable_40mhz_24ghz &&
		    band == NL80211_BAND_2GHZ &&
		    sband->ht_cap.ht_supported) {
			sband->ht_cap.cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			sband->ht_cap.cap &= ~IEEE80211_HT_CAP_SGI_40;
		}

		/*
		 * Since we use a u32 for rate bitmaps in
		 * ieee80211_get_response_rate, we cannot
		 * have more than 32 legacy rates.
		 */
		if (WARN_ON(sband->n_bitrates > 32))
			return -EINVAL;

		for (i = 0; i < sband->n_channels; i++) {
			sband->channels[i].orig_flags =
				sband->channels[i].flags;
			sband->channels[i].orig_mag = INT_MAX;
			sband->channels[i].orig_mpwr =
				sband->channels[i].max_power;
			sband->channels[i].band = band;

			if (WARN_ON(sband->channels[i].freq_offset >= 1000))
				return -EINVAL;
		}

		for (i = 0; i < sband->n_iftype_data; i++) {
			const struct ieee80211_sband_iftype_data *iftd;
			bool has_ap, has_non_ap;
			u32 ap_bits = BIT(NL80211_IFTYPE_AP) |
				      BIT(NL80211_IFTYPE_P2P_GO);

			iftd = &sband->iftype_data[i];

			if (WARN_ON(!iftd->types_mask))
				return -EINVAL;
			if (WARN_ON(types & iftd->types_mask))
				return -EINVAL;

			/* at least one piece of information must be present */
			if (WARN_ON(!iftd->he_cap.has_he))
				return -EINVAL;

			types |= iftd->types_mask;

			if (i == 0)
				have_he = iftd->he_cap.has_he;
			else
				have_he = have_he &&
					  iftd->he_cap.has_he;

			has_ap = iftd->types_mask & ap_bits;
			has_non_ap = iftd->types_mask & ~ap_bits;

			/*
			 * For EHT 20 MHz STA, the capabilities format differs
			 * but to simplify, don't check 20 MHz but rather check
			 * only if AP and non-AP were mentioned at the same time,
			 * reject if so.
			 */
			if (WARN_ON(iftd->eht_cap.has_eht &&
				    has_ap && has_non_ap))
				return -EINVAL;
		}

		if (WARN_ON(!have_he && band == NL80211_BAND_6GHZ))
			return -EINVAL;

		have_band = true;
	}

	if (!have_band) {
		WARN_ON(1);
		return -EINVAL;
	}

	for (i = 0; i < rdev->wiphy.n_vendor_commands; i++) {
		/*
		 * Validate we have a policy (can be explicitly set to
		 * VENDOR_CMD_RAW_DATA which is non-NULL) and also that
		 * we have at least one of doit/dumpit.
		 */
		if (WARN_ON(!rdev->wiphy.vendor_commands[i].policy))
			return -EINVAL;
		if (WARN_ON(!rdev->wiphy.vendor_commands[i].doit &&
			    !rdev->wiphy.vendor_commands[i].dumpit))
			return -EINVAL;
	}

#ifdef CONFIG_PM
	if (WARN_ON(rdev->wiphy.wowlan && rdev->wiphy.wowlan->n_patterns &&
		    (!rdev->wiphy.wowlan->pattern_min_len ||
		     rdev->wiphy.wowlan->pattern_min_len >
				rdev->wiphy.wowlan->pattern_max_len)))
		return -EINVAL;
#endif

	if (!wiphy->max_num_akm_suites)
		wiphy->max_num_akm_suites = NL80211_MAX_NR_AKM_SUITES;
	else if (wiphy->max_num_akm_suites < NL80211_MAX_NR_AKM_SUITES ||
		 wiphy->max_num_akm_suites > CFG80211_MAX_NUM_AKM_SUITES)
		return -EINVAL;

	/* check and set up bitrates */
	ieee80211_set_bitrate_flags(wiphy);

	rdev->wiphy.features |= NL80211_FEATURE_SCAN_FLUSH;

	rtnl_lock();
	res = device_add(&rdev->wiphy.dev);
	if (res) {
		rtnl_unlock();
		return res;
	}

	list_add_rcu(&rdev->list, &cfg80211_rdev_list);
	cfg80211_rdev_list_generation++;

	/* add to debugfs */
	rdev->wiphy.debugfsdir = debugfs_create_dir(wiphy_name(&rdev->wiphy),
						    ieee80211_debugfs_dir);

	cfg80211_debugfs_rdev_add(rdev);
	nl80211_notify_wiphy(rdev, NL80211_CMD_NEW_WIPHY);

	/* set up regulatory info */
	wiphy_regulatory_register(wiphy);

	if (wiphy->regulatory_flags & REGULATORY_CUSTOM_REG) {
		struct regulatory_request request;

		request.wiphy_idx = get_wiphy_idx(wiphy);
		request.initiator = NL80211_REGDOM_SET_BY_DRIVER;
		request.alpha2[0] = '9';
		request.alpha2[1] = '9';

		nl80211_send_reg_change_event(&request);
	}

	/* Check that nobody globally advertises any capabilities they do not
	 * advertise on all possible interface types.
	 */
	if (wiphy->extended_capabilities_len &&
	    wiphy->num_iftype_ext_capab &&
	    wiphy->iftype_ext_capab) {
		u8 supported_on_all, j;
		const struct wiphy_iftype_ext_capab *capab;

		capab = wiphy->iftype_ext_capab;
		for (j = 0; j < wiphy->extended_capabilities_len; j++) {
			if (capab[0].extended_capabilities_len > j)
				supported_on_all =
					capab[0].extended_capabilities[j];
			else
				supported_on_all = 0x00;
			for (i = 1; i < wiphy->num_iftype_ext_capab; i++) {
				if (j >= capab[i].extended_capabilities_len) {
					supported_on_all = 0x00;
					break;
				}
				supported_on_all &=
					capab[i].extended_capabilities[j];
			}
			if (WARN_ON(wiphy->extended_capabilities[j] &
				    ~supported_on_all))
				break;
		}
	}

	rdev->wiphy.registered = true;
	rtnl_unlock();

	res = rfkill_register(rdev->wiphy.rfkill);
	if (res) {
		rfkill_destroy(rdev->wiphy.rfkill);
		rdev->wiphy.rfkill = NULL;
		wiphy_unregister(&rdev->wiphy);
		return res;
	}

	return 0;
}
EXPORT_SYMBOL(wiphy_register);

void wiphy_rfkill_start_polling(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	if (!rdev->ops->rfkill_poll)
		return;
	rdev->rfkill_ops.poll = cfg80211_rfkill_poll;
	rfkill_resume_polling(wiphy->rfkill);
}
EXPORT_SYMBOL(wiphy_rfkill_start_polling);

void cfg80211_process_wiphy_works(struct cfg80211_registered_device *rdev)
{
	unsigned int runaway_limit = 100;
	unsigned long flags;

	lockdep_assert_held(&rdev->wiphy.mtx);

	spin_lock_irqsave(&rdev->wiphy_work_lock, flags);
	while (!list_empty(&rdev->wiphy_work_list)) {
		struct wiphy_work *wk;

		wk = list_first_entry(&rdev->wiphy_work_list,
				      struct wiphy_work, entry);
		list_del_init(&wk->entry);
		spin_unlock_irqrestore(&rdev->wiphy_work_lock, flags);

		wk->func(&rdev->wiphy, wk);

		spin_lock_irqsave(&rdev->wiphy_work_lock, flags);
		if (WARN_ON(--runaway_limit == 0))
			INIT_LIST_HEAD(&rdev->wiphy_work_list);
	}
	spin_unlock_irqrestore(&rdev->wiphy_work_lock, flags);
}

void wiphy_unregister(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	wait_event(rdev->dev_wait, ({
		int __count;
		wiphy_lock(&rdev->wiphy);
		__count = rdev->opencount;
		wiphy_unlock(&rdev->wiphy);
		__count == 0; }));

	if (rdev->wiphy.rfkill)
		rfkill_unregister(rdev->wiphy.rfkill);

	rtnl_lock();
	wiphy_lock(&rdev->wiphy);
	nl80211_notify_wiphy(rdev, NL80211_CMD_DEL_WIPHY);
	rdev->wiphy.registered = false;

	WARN_ON(!list_empty(&rdev->wiphy.wdev_list));

	/*
	 * First remove the hardware from everywhere, this makes
	 * it impossible to find from userspace.
	 */
	debugfs_remove_recursive(rdev->wiphy.debugfsdir);
	list_del_rcu(&rdev->list);
	synchronize_rcu();

	/*
	 * If this device got a regulatory hint tell core its
	 * free to listen now to a new shiny device regulatory hint
	 */
	wiphy_regulatory_deregister(wiphy);

	cfg80211_rdev_list_generation++;
	device_del(&rdev->wiphy.dev);

#ifdef CONFIG_PM
	if (rdev->wiphy.wowlan_config && rdev->ops->set_wakeup)
		rdev_set_wakeup(rdev, false);
#endif

	/* surely nothing is reachable now, clean up work */
	cfg80211_process_wiphy_works(rdev);
	wiphy_unlock(&rdev->wiphy);
	rtnl_unlock();

	/* this has nothing to do now but make sure it's gone */
	cancel_work_sync(&rdev->wiphy_work);

	flush_work(&rdev->scan_done_wk);
	cancel_work_sync(&rdev->conn_work);
	flush_work(&rdev->event_work);
	cancel_delayed_work_sync(&rdev->dfs_update_channels_wk);
	cancel_delayed_work_sync(&rdev->background_cac_done_wk);
	flush_work(&rdev->destroy_work);
	flush_work(&rdev->sched_scan_stop_wk);
	flush_work(&rdev->propagate_radar_detect_wk);
	flush_work(&rdev->propagate_cac_done_wk);
	flush_work(&rdev->mgmt_registrations_update_wk);
	flush_work(&rdev->background_cac_abort_wk);

	cfg80211_rdev_free_wowlan(rdev);
	cfg80211_rdev_free_coalesce(rdev);
}
EXPORT_SYMBOL(wiphy_unregister);

void cfg80211_dev_free(struct cfg80211_registered_device *rdev)
{
	struct cfg80211_internal_bss *scan, *tmp;
	struct cfg80211_beacon_registration *reg, *treg;
	rfkill_destroy(rdev->wiphy.rfkill);
	list_for_each_entry_safe(reg, treg, &rdev->beacon_registrations, list) {
		list_del(&reg->list);
		kfree(reg);
	}
	list_for_each_entry_safe(scan, tmp, &rdev->bss_list, list)
		cfg80211_put_bss(&rdev->wiphy, &scan->pub);
	mutex_destroy(&rdev->wiphy.mtx);

	/*
	 * The 'regd' can only be non-NULL if we never finished
	 * initializing the wiphy and thus never went through the
	 * unregister path - e.g. in failure scenarios. Thus, it
	 * cannot have been visible to anyone if non-NULL, so we
	 * can just free it here.
	 */
	kfree(rcu_dereference_raw(rdev->wiphy.regd));

	kfree(rdev);
}

void wiphy_free(struct wiphy *wiphy)
{
	put_device(&wiphy->dev);
}
EXPORT_SYMBOL(wiphy_free);

void wiphy_rfkill_set_hw_state_reason(struct wiphy *wiphy, bool blocked,
				      enum rfkill_hard_block_reasons reason)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	if (rfkill_set_hw_state_reason(wiphy->rfkill, blocked, reason))
		schedule_work(&rdev->rfkill_block);
}
EXPORT_SYMBOL(wiphy_rfkill_set_hw_state_reason);

void cfg80211_cqm_config_free(struct wireless_dev *wdev)
{
	kfree(wdev->cqm_config);
	wdev->cqm_config = NULL;
}

static void _cfg80211_unregister_wdev(struct wireless_dev *wdev,
				      bool unregister_netdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	unsigned int link_id;

	ASSERT_RTNL();
	lockdep_assert_held(&rdev->wiphy.mtx);

	flush_work(&wdev->pmsr_free_wk);

	nl80211_notify_iface(rdev, wdev, NL80211_CMD_DEL_INTERFACE);

	wdev->registered = false;

	if (wdev->netdev) {
		sysfs_remove_link(&wdev->netdev->dev.kobj, "phy80211");
		if (unregister_netdev)
			unregister_netdevice(wdev->netdev);
	}

	list_del_rcu(&wdev->list);
	synchronize_net();
	rdev->devlist_generation++;

	cfg80211_mlme_purge_registrations(wdev);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_P2P_DEVICE:
		cfg80211_stop_p2p_device(rdev, wdev);
		break;
	case NL80211_IFTYPE_NAN:
		cfg80211_stop_nan(rdev, wdev);
		break;
	default:
		break;
	}

#ifdef CONFIG_CFG80211_WEXT
	kfree_sensitive(wdev->wext.keys);
	wdev->wext.keys = NULL;
#endif
	cfg80211_cqm_config_free(wdev);

	/*
	 * Ensure that all events have been processed and
	 * freed.
	 */
	cfg80211_process_wdev_events(wdev);

	if (wdev->iftype == NL80211_IFTYPE_STATION ||
	    wdev->iftype == NL80211_IFTYPE_P2P_CLIENT) {
		for (link_id = 0; link_id < ARRAY_SIZE(wdev->links); link_id++) {
			struct cfg80211_internal_bss *curbss;

			curbss = wdev->links[link_id].client.current_bss;

			if (WARN_ON(curbss)) {
				cfg80211_unhold_bss(curbss);
				cfg80211_put_bss(wdev->wiphy, &curbss->pub);
				wdev->links[link_id].client.current_bss = NULL;
			}
		}
	}

	wdev->connected = false;
}

void cfg80211_unregister_wdev(struct wireless_dev *wdev)
{
	_cfg80211_unregister_wdev(wdev, true);
}
EXPORT_SYMBOL(cfg80211_unregister_wdev);

static const struct device_type wiphy_type = {
	.name	= "wlan",
};

void cfg80211_update_iface_num(struct cfg80211_registered_device *rdev,
			       enum nl80211_iftype iftype, int num)
{
	lockdep_assert_held(&rdev->wiphy.mtx);

	rdev->num_running_ifaces += num;
	if (iftype == NL80211_IFTYPE_MONITOR)
		rdev->num_running_monitor_ifaces += num;
}

void __cfg80211_leave(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev)
{
	struct net_device *dev = wdev->netdev;
	struct cfg80211_sched_scan_request *pos, *tmp;

	lockdep_assert_held(&rdev->wiphy.mtx);
	ASSERT_WDEV_LOCK(wdev);

	cfg80211_pmsr_wdev_down(wdev);

	cfg80211_stop_background_radar_detection(wdev);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_ADHOC:
		__cfg80211_leave_ibss(rdev, dev, true);
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
		list_for_each_entry_safe(pos, tmp, &rdev->sched_scan_req_list,
					 list) {
			if (dev == pos->dev)
				cfg80211_stop_sched_scan_req(rdev, pos, false);
		}

#ifdef CONFIG_CFG80211_WEXT
		kfree(wdev->wext.ie);
		wdev->wext.ie = NULL;
		wdev->wext.ie_len = 0;
		wdev->wext.connect.auth_type = NL80211_AUTHTYPE_AUTOMATIC;
#endif
		cfg80211_disconnect(rdev, dev,
				    WLAN_REASON_DEAUTH_LEAVING, true);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		__cfg80211_leave_mesh(rdev, dev);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		__cfg80211_stop_ap(rdev, dev, -1, true);
		break;
	case NL80211_IFTYPE_OCB:
		__cfg80211_leave_ocb(rdev, dev);
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
	case NL80211_IFTYPE_NAN:
		/* cannot happen, has no netdev */
		break;
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_MONITOR:
		/* nothing to do */
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_WDS:
	case NUM_NL80211_IFTYPES:
		/* invalid */
		break;
	}
}

void cfg80211_leave(struct cfg80211_registered_device *rdev,
		    struct wireless_dev *wdev)
{
	wdev_lock(wdev);
	__cfg80211_leave(rdev, wdev);
	wdev_unlock(wdev);
}

void cfg80211_stop_iface(struct wiphy *wiphy, struct wireless_dev *wdev,
			 gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	trace_cfg80211_stop_iface(wiphy, wdev);

	ev = kzalloc(sizeof(*ev), gfp);
	if (!ev)
		return;

	ev->type = EVENT_STOPPED;

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	queue_work(cfg80211_wq, &rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_stop_iface);

void cfg80211_init_wdev(struct wireless_dev *wdev)
{
	mutex_init(&wdev->mtx);
	INIT_LIST_HEAD(&wdev->event_list);
	spin_lock_init(&wdev->event_lock);
	INIT_LIST_HEAD(&wdev->mgmt_registrations);
	INIT_LIST_HEAD(&wdev->pmsr_list);
	spin_lock_init(&wdev->pmsr_lock);
	INIT_WORK(&wdev->pmsr_free_wk, cfg80211_pmsr_free_wk);

#ifdef CONFIG_CFG80211_WEXT
	wdev->wext.default_key = -1;
	wdev->wext.default_mgmt_key = -1;
	wdev->wext.connect.auth_type = NL80211_AUTHTYPE_AUTOMATIC;
#endif

	if (wdev->wiphy->flags & WIPHY_FLAG_PS_ON_BY_DEFAULT)
		wdev->ps = true;
	else
		wdev->ps = false;
	/* allow mac80211 to determine the timeout */
	wdev->ps_timeout = -1;

	if ((wdev->iftype == NL80211_IFTYPE_STATION ||
	     wdev->iftype == NL80211_IFTYPE_P2P_CLIENT ||
	     wdev->iftype == NL80211_IFTYPE_ADHOC) && !wdev->use_4addr)
		wdev->netdev->priv_flags |= IFF_DONT_BRIDGE;

	INIT_WORK(&wdev->disconnect_wk, cfg80211_autodisconnect_wk);
}

void cfg80211_register_wdev(struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev)
{
	ASSERT_RTNL();
	lockdep_assert_held(&rdev->wiphy.mtx);

	/*
	 * We get here also when the interface changes network namespaces,
	 * as it's registered into the new one, but we don't want it to
	 * change ID in that case. Checking if the ID is already assigned
	 * works, because 0 isn't considered a valid ID and the memory is
	 * 0-initialized.
	 */
	if (!wdev->identifier)
		wdev->identifier = ++rdev->wdev_id;
	list_add_rcu(&wdev->list, &rdev->wiphy.wdev_list);
	rdev->devlist_generation++;
	wdev->registered = true;

	if (wdev->netdev &&
	    sysfs_create_link(&wdev->netdev->dev.kobj, &rdev->wiphy.dev.kobj,
			      "phy80211"))
		pr_err("failed to add phy80211 symlink to netdev!\n");

	nl80211_notify_iface(rdev, wdev, NL80211_CMD_NEW_INTERFACE);
}

int cfg80211_register_netdevice(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev;
	int ret;

	ASSERT_RTNL();

	if (WARN_ON(!wdev))
		return -EINVAL;

	rdev = wiphy_to_rdev(wdev->wiphy);

	lockdep_assert_held(&rdev->wiphy.mtx);

	/* we'll take care of this */
	wdev->registered = true;
	wdev->registering = true;
	ret = register_netdevice(dev);
	if (ret)
		goto out;

	cfg80211_register_wdev(rdev, wdev);
	ret = 0;
out:
	wdev->registering = false;
	if (ret)
		wdev->registered = false;
	return ret;
}
EXPORT_SYMBOL(cfg80211_register_netdevice);

static int cfg80211_netdev_notifier_call(struct notifier_block *nb,
					 unsigned long state, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev;
	struct cfg80211_sched_scan_request *pos, *tmp;

	if (!wdev)
		return NOTIFY_DONE;

	rdev = wiphy_to_rdev(wdev->wiphy);

	WARN_ON(wdev->iftype == NL80211_IFTYPE_UNSPECIFIED);

	switch (state) {
	case NETDEV_POST_INIT:
		SET_NETDEV_DEVTYPE(dev, &wiphy_type);
		wdev->netdev = dev;
		/* can only change netns with wiphy */
		dev->features |= NETIF_F_NETNS_LOCAL;

		cfg80211_init_wdev(wdev);
		break;
	case NETDEV_REGISTER:
		if (!wdev->registered) {
			wiphy_lock(&rdev->wiphy);
			cfg80211_register_wdev(rdev, wdev);
			wiphy_unlock(&rdev->wiphy);
		}
		break;
	case NETDEV_UNREGISTER:
		/*
		 * It is possible to get NETDEV_UNREGISTER multiple times,
		 * so check wdev->registered.
		 */
		if (wdev->registered && !wdev->registering) {
			wiphy_lock(&rdev->wiphy);
			_cfg80211_unregister_wdev(wdev, false);
			wiphy_unlock(&rdev->wiphy);
		}
		break;
	case NETDEV_GOING_DOWN:
		wiphy_lock(&rdev->wiphy);
		cfg80211_leave(rdev, wdev);
		cfg80211_remove_links(wdev);
		wiphy_unlock(&rdev->wiphy);
		/* since we just did cfg80211_leave() nothing to do there */
		cancel_work_sync(&wdev->disconnect_wk);
		break;
	case NETDEV_DOWN:
		wiphy_lock(&rdev->wiphy);
		cfg80211_update_iface_num(rdev, wdev->iftype, -1);
		if (rdev->scan_req && rdev->scan_req->wdev == wdev) {
			if (WARN_ON(!rdev->scan_req->notified &&
				    (!rdev->int_scan_req ||
				     !rdev->int_scan_req->notified)))
				rdev->scan_req->info.aborted = true;
			___cfg80211_scan_done(rdev, false);
		}

		list_for_each_entry_safe(pos, tmp,
					 &rdev->sched_scan_req_list, list) {
			if (WARN_ON(pos->dev == wdev->netdev))
				cfg80211_stop_sched_scan_req(rdev, pos, false);
		}

		rdev->opencount--;
		wiphy_unlock(&rdev->wiphy);
		wake_up(&rdev->dev_wait);
		break;
	case NETDEV_UP:
		wiphy_lock(&rdev->wiphy);
		cfg80211_update_iface_num(rdev, wdev->iftype, 1);
		wdev_lock(wdev);
		switch (wdev->iftype) {
#ifdef CONFIG_CFG80211_WEXT
		case NL80211_IFTYPE_ADHOC:
			cfg80211_ibss_wext_join(rdev, wdev);
			break;
		case NL80211_IFTYPE_STATION:
			cfg80211_mgd_wext_connect(rdev, wdev);
			break;
#endif
#ifdef CONFIG_MAC80211_MESH
		case NL80211_IFTYPE_MESH_POINT:
			{
				/* backward compat code... */
				struct mesh_setup setup;
				memcpy(&setup, &default_mesh_setup,
						sizeof(setup));
				 /* back compat only needed for mesh_id */
				setup.mesh_id = wdev->u.mesh.id;
				setup.mesh_id_len = wdev->u.mesh.id_up_len;
				if (wdev->u.mesh.id_up_len)
					__cfg80211_join_mesh(rdev, dev,
							&setup,
							&default_mesh_config);
				break;
			}
#endif
		default:
			break;
		}
		wdev_unlock(wdev);
		rdev->opencount++;

		/*
		 * Configure power management to the driver here so that its
		 * correctly set also after interface type changes etc.
		 */
		if ((wdev->iftype == NL80211_IFTYPE_STATION ||
		     wdev->iftype == NL80211_IFTYPE_P2P_CLIENT) &&
		    rdev->ops->set_power_mgmt &&
		    rdev_set_power_mgmt(rdev, dev, wdev->ps,
					wdev->ps_timeout)) {
			/* assume this means it's off */
			wdev->ps = false;
		}
		wiphy_unlock(&rdev->wiphy);
		break;
	case NETDEV_PRE_UP:
		if (!cfg80211_iftype_allowed(wdev->wiphy, wdev->iftype,
					     wdev->use_4addr, 0))
			return notifier_from_errno(-EOPNOTSUPP);

		if (rfkill_blocked(rdev->wiphy.rfkill))
			return notifier_from_errno(-ERFKILL);
		break;
	default:
		return NOTIFY_DONE;
	}

	wireless_nlevent_flush();

	return NOTIFY_OK;
}

static struct notifier_block cfg80211_netdev_notifier = {
	.notifier_call = cfg80211_netdev_notifier_call,
};

static void __net_exit cfg80211_pernet_exit(struct net *net)
{
	struct cfg80211_registered_device *rdev;

	rtnl_lock();
	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (net_eq(wiphy_net(&rdev->wiphy), net))
			WARN_ON(cfg80211_switch_netns(rdev, &init_net));
	}
	rtnl_unlock();
}

static struct pernet_operations cfg80211_pernet_ops = {
	.exit = cfg80211_pernet_exit,
};

void wiphy_work_queue(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	unsigned long flags;

	spin_lock_irqsave(&rdev->wiphy_work_lock, flags);
	if (list_empty(&work->entry))
		list_add_tail(&work->entry, &rdev->wiphy_work_list);
	spin_unlock_irqrestore(&rdev->wiphy_work_lock, flags);

	queue_work(system_unbound_wq, &rdev->wiphy_work);
}
EXPORT_SYMBOL_GPL(wiphy_work_queue);

void wiphy_work_cancel(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	unsigned long flags;

	lockdep_assert_held(&wiphy->mtx);

	spin_lock_irqsave(&rdev->wiphy_work_lock, flags);
	if (!list_empty(&work->entry))
		list_del_init(&work->entry);
	spin_unlock_irqrestore(&rdev->wiphy_work_lock, flags);
}
EXPORT_SYMBOL_GPL(wiphy_work_cancel);

void wiphy_delayed_work_timer(struct timer_list *t)
{
	struct wiphy_delayed_work *dwork = from_timer(dwork, t, timer);

	wiphy_work_queue(dwork->wiphy, &dwork->work);
}
EXPORT_SYMBOL(wiphy_delayed_work_timer);

void wiphy_delayed_work_queue(struct wiphy *wiphy,
			      struct wiphy_delayed_work *dwork,
			      unsigned long delay)
{
	if (!delay) {
		wiphy_work_queue(wiphy, &dwork->work);
		return;
	}

	dwork->wiphy = wiphy;
	mod_timer(&dwork->timer, jiffies + delay);
}
EXPORT_SYMBOL_GPL(wiphy_delayed_work_queue);

void wiphy_delayed_work_cancel(struct wiphy *wiphy,
			       struct wiphy_delayed_work *dwork)
{
	lockdep_assert_held(&wiphy->mtx);

	del_timer_sync(&dwork->timer);
	wiphy_work_cancel(wiphy, &dwork->work);
}
EXPORT_SYMBOL_GPL(wiphy_delayed_work_cancel);

static int __init cfg80211_init(void)
{
	int err;

	err = register_pernet_device(&cfg80211_pernet_ops);
	if (err)
		goto out_fail_pernet;

	err = wiphy_sysfs_init();
	if (err)
		goto out_fail_sysfs;

	err = register_netdevice_notifier(&cfg80211_netdev_notifier);
	if (err)
		goto out_fail_notifier;

	err = nl80211_init();
	if (err)
		goto out_fail_nl80211;

	ieee80211_debugfs_dir = debugfs_create_dir("ieee80211", NULL);

	err = regulatory_init();
	if (err)
		goto out_fail_reg;

	cfg80211_wq = alloc_ordered_workqueue("cfg80211", WQ_MEM_RECLAIM);
	if (!cfg80211_wq) {
		err = -ENOMEM;
		goto out_fail_wq;
	}

	return 0;

out_fail_wq:
	regulatory_exit();
out_fail_reg:
	debugfs_remove(ieee80211_debugfs_dir);
	nl80211_exit();
out_fail_nl80211:
	unregister_netdevice_notifier(&cfg80211_netdev_notifier);
out_fail_notifier:
	wiphy_sysfs_exit();
out_fail_sysfs:
	unregister_pernet_device(&cfg80211_pernet_ops);
out_fail_pernet:
	return err;
}
fs_initcall(cfg80211_init);

static void __exit cfg80211_exit(void)
{
	debugfs_remove(ieee80211_debugfs_dir);
	nl80211_exit();
	unregister_netdevice_notifier(&cfg80211_netdev_notifier);
	wiphy_sysfs_exit();
	regulatory_exit();
	unregister_pernet_device(&cfg80211_pernet_ops);
	destroy_workqueue(cfg80211_wq);
}
module_exit(cfg80211_exit);
