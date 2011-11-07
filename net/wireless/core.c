/*
 * This is the linux wireless configuration interface.
 *
 * Copyright 2006-2010		Johannes Berg <johannes@sipsolutions.net>
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
#include "ethtool.h"

/* name for sysfs, %d is appended */
#define PHY_NAME "phy"

MODULE_AUTHOR("Johannes Berg");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("wireless configuration support");

/* RCU-protected (and cfg80211_mutex for writers) */
LIST_HEAD(cfg80211_rdev_list);
int cfg80211_rdev_list_generation;

DEFINE_MUTEX(cfg80211_mutex);

/* for debugfs */
static struct dentry *ieee80211_debugfs_dir;

/* for the cleanup, scan and event works */
struct workqueue_struct *cfg80211_wq;

static bool cfg80211_disable_40mhz_24ghz;
module_param(cfg80211_disable_40mhz_24ghz, bool, 0644);
MODULE_PARM_DESC(cfg80211_disable_40mhz_24ghz,
		 "Disable 40MHz support in the 2.4GHz band");

/* requires cfg80211_mutex to be held! */
struct cfg80211_registered_device *cfg80211_rdev_by_wiphy_idx(int wiphy_idx)
{
	struct cfg80211_registered_device *result = NULL, *rdev;

	if (!wiphy_idx_valid(wiphy_idx))
		return NULL;

	assert_cfg80211_lock();

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
	struct cfg80211_registered_device *rdev;
	if (!wiphy)
		return WIPHY_IDX_STALE;
	rdev = wiphy_to_dev(wiphy);
	return rdev->wiphy_idx;
}

/* requires cfg80211_rdev_mutex to be held! */
struct wiphy *wiphy_idx_to_wiphy(int wiphy_idx)
{
	struct cfg80211_registered_device *rdev;

	if (!wiphy_idx_valid(wiphy_idx))
		return NULL;

	assert_cfg80211_lock();

	rdev = cfg80211_rdev_by_wiphy_idx(wiphy_idx);
	if (!rdev)
		return NULL;
	return &rdev->wiphy;
}

/* requires cfg80211_mutex to be held! */
struct cfg80211_registered_device *
__cfg80211_rdev_from_info(struct genl_info *info)
{
	int ifindex;
	struct cfg80211_registered_device *bywiphyidx = NULL, *byifidx = NULL;
	struct net_device *dev;
	int err = -EINVAL;

	assert_cfg80211_lock();

	if (info->attrs[NL80211_ATTR_WIPHY]) {
		bywiphyidx = cfg80211_rdev_by_wiphy_idx(
				nla_get_u32(info->attrs[NL80211_ATTR_WIPHY]));
		err = -ENODEV;
	}

	if (info->attrs[NL80211_ATTR_IFINDEX]) {
		ifindex = nla_get_u32(info->attrs[NL80211_ATTR_IFINDEX]);
		dev = dev_get_by_index(genl_info_net(info), ifindex);
		if (dev) {
			if (dev->ieee80211_ptr)
				byifidx =
					wiphy_to_dev(dev->ieee80211_ptr->wiphy);
			dev_put(dev);
		}
		err = -ENODEV;
	}

	if (bywiphyidx && byifidx) {
		if (bywiphyidx != byifidx)
			return ERR_PTR(-EINVAL);
		else
			return bywiphyidx; /* == byifidx */
	}
	if (bywiphyidx)
		return bywiphyidx;

	if (byifidx)
		return byifidx;

	return ERR_PTR(err);
}

struct cfg80211_registered_device *
cfg80211_get_dev_from_info(struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;

	mutex_lock(&cfg80211_mutex);
	rdev = __cfg80211_rdev_from_info(info);

	/* if it is not an error we grab the lock on
	 * it to assure it won't be going away while
	 * we operate on it */
	if (!IS_ERR(rdev))
		mutex_lock(&rdev->mtx);

	mutex_unlock(&cfg80211_mutex);

	return rdev;
}

struct cfg80211_registered_device *
cfg80211_get_dev_from_ifindex(struct net *net, int ifindex)
{
	struct cfg80211_registered_device *rdev = ERR_PTR(-ENODEV);
	struct net_device *dev;

	mutex_lock(&cfg80211_mutex);
	dev = dev_get_by_index(net, ifindex);
	if (!dev)
		goto out;
	if (dev->ieee80211_ptr) {
		rdev = wiphy_to_dev(dev->ieee80211_ptr->wiphy);
		mutex_lock(&rdev->mtx);
	} else
		rdev = ERR_PTR(-ENODEV);
	dev_put(dev);
 out:
	mutex_unlock(&cfg80211_mutex);
	return rdev;
}

/* requires cfg80211_mutex to be held */
int cfg80211_dev_rename(struct cfg80211_registered_device *rdev,
			char *newname)
{
	struct cfg80211_registered_device *rdev2;
	int wiphy_idx, taken = -1, result, digits;

	assert_cfg80211_lock();

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


	/* Ignore nop renames */
	if (strcmp(newname, dev_name(&rdev->wiphy.dev)) == 0)
		return 0;

	/* Ensure another device does not already have this name. */
	list_for_each_entry(rdev2, &cfg80211_rdev_list, list)
		if (strcmp(newname, dev_name(&rdev2->wiphy.dev)) == 0)
			return -EINVAL;

	result = device_rename(&rdev->wiphy.dev, newname);
	if (result)
		return result;

	if (rdev->wiphy.debugfsdir &&
	    !debugfs_rename(rdev->wiphy.debugfsdir->d_parent,
			    rdev->wiphy.debugfsdir,
			    rdev->wiphy.debugfsdir->d_parent,
			    newname))
		pr_err("failed to rename debugfs dir to %s!\n", newname);

	nl80211_notify_dev_rename(rdev);

	return 0;
}

int cfg80211_switch_netns(struct cfg80211_registered_device *rdev,
			  struct net *net)
{
	struct wireless_dev *wdev;
	int err = 0;

	if (!(rdev->wiphy.flags & WIPHY_FLAG_NETNS_OK))
		return -EOPNOTSUPP;

	list_for_each_entry(wdev, &rdev->netdev_list, list) {
		wdev->netdev->features &= ~NETIF_F_NETNS_LOCAL;
		err = dev_change_net_namespace(wdev->netdev, net, "wlan%d");
		if (err)
			break;
		wdev->netdev->features |= NETIF_F_NETNS_LOCAL;
	}

	if (err) {
		/* failed -- clean up to old netns */
		net = wiphy_net(&rdev->wiphy);

		list_for_each_entry_continue_reverse(wdev, &rdev->netdev_list,
						     list) {
			wdev->netdev->features &= ~NETIF_F_NETNS_LOCAL;
			err = dev_change_net_namespace(wdev->netdev, net,
							"wlan%d");
			WARN_ON(err);
			wdev->netdev->features |= NETIF_F_NETNS_LOCAL;
		}

		return err;
	}

	wiphy_net_set(&rdev->wiphy, net);

	err = device_rename(&rdev->wiphy.dev, dev_name(&rdev->wiphy.dev));
	WARN_ON(err);

	return 0;
}

static void cfg80211_rfkill_poll(struct rfkill *rfkill, void *data)
{
	struct cfg80211_registered_device *rdev = data;

	rdev->ops->rfkill_poll(&rdev->wiphy);
}

static int cfg80211_rfkill_set_block(void *data, bool blocked)
{
	struct cfg80211_registered_device *rdev = data;
	struct wireless_dev *wdev;

	if (!blocked)
		return 0;

	rtnl_lock();
	mutex_lock(&rdev->devlist_mtx);

	list_for_each_entry(wdev, &rdev->netdev_list, list)
		dev_close(wdev->netdev);

	mutex_unlock(&rdev->devlist_mtx);
	rtnl_unlock();

	return 0;
}

static void cfg80211_rfkill_sync_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(work, struct cfg80211_registered_device, rfkill_sync);
	cfg80211_rfkill_set_block(rdev, rfkill_blocked(rdev->rfkill));
}

static void cfg80211_event_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(work, struct cfg80211_registered_device,
			    event_work);

	rtnl_lock();
	cfg80211_lock_rdev(rdev);

	cfg80211_process_rdev_events(rdev);
	cfg80211_unlock_rdev(rdev);
	rtnl_unlock();
}

/* exported functions */

struct wiphy *wiphy_new(const struct cfg80211_ops *ops, int sizeof_priv)
{
	static int wiphy_counter;

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

	alloc_size = sizeof(*rdev) + sizeof_priv;

	rdev = kzalloc(alloc_size, GFP_KERNEL);
	if (!rdev)
		return NULL;

	rdev->ops = ops;

	mutex_lock(&cfg80211_mutex);

	rdev->wiphy_idx = wiphy_counter++;

	if (unlikely(!wiphy_idx_valid(rdev->wiphy_idx))) {
		wiphy_counter--;
		mutex_unlock(&cfg80211_mutex);
		/* ugh, wrapped! */
		kfree(rdev);
		return NULL;
	}

	mutex_unlock(&cfg80211_mutex);

	/* give it a proper name */
	dev_set_name(&rdev->wiphy.dev, PHY_NAME "%d", rdev->wiphy_idx);

	mutex_init(&rdev->mtx);
	mutex_init(&rdev->devlist_mtx);
	mutex_init(&rdev->sched_scan_mtx);
	INIT_LIST_HEAD(&rdev->netdev_list);
	spin_lock_init(&rdev->bss_lock);
	INIT_LIST_HEAD(&rdev->bss_list);
	INIT_WORK(&rdev->scan_done_wk, __cfg80211_scan_done);
	INIT_WORK(&rdev->sched_scan_results_wk, __cfg80211_sched_scan_results);
#ifdef CONFIG_CFG80211_WEXT
	rdev->wiphy.wext = &cfg80211_wext_handler;
#endif

	device_initialize(&rdev->wiphy.dev);
	rdev->wiphy.dev.class = &ieee80211_class;
	rdev->wiphy.dev.platform_data = rdev;

#ifdef CONFIG_CFG80211_DEFAULT_PS
	rdev->wiphy.flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif

	wiphy_net_set(&rdev->wiphy, &init_net);

	rdev->rfkill_ops.set_block = cfg80211_rfkill_set_block;
	rdev->rfkill = rfkill_alloc(dev_name(&rdev->wiphy.dev),
				   &rdev->wiphy.dev, RFKILL_TYPE_WLAN,
				   &rdev->rfkill_ops, rdev);

	if (!rdev->rfkill) {
		kfree(rdev);
		return NULL;
	}

	INIT_WORK(&rdev->rfkill_sync, cfg80211_rfkill_sync_work);
	INIT_WORK(&rdev->conn_work, cfg80211_conn_work);
	INIT_WORK(&rdev->event_work, cfg80211_event_work);

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

	return &rdev->wiphy;
}
EXPORT_SYMBOL(wiphy_new);

static int wiphy_verify_combinations(struct wiphy *wiphy)
{
	const struct ieee80211_iface_combination *c;
	int i, j;

	/* If we have combinations enforce them */
	if (wiphy->n_iface_combinations)
		wiphy->flags |= WIPHY_FLAG_ENFORCE_COMBINATIONS;

	for (i = 0; i < wiphy->n_iface_combinations; i++) {
		u32 cnt = 0;
		u16 all_iftypes = 0;

		c = &wiphy->iface_combinations[i];

		/* Combinations with just one interface aren't real */
		if (WARN_ON(c->max_interfaces < 2))
			return -EINVAL;

		/* Need at least one channel */
		if (WARN_ON(!c->num_different_channels))
			return -EINVAL;

		if (WARN_ON(!c->n_limits))
			return -EINVAL;

		for (j = 0; j < c->n_limits; j++) {
			u16 types = c->limits[j].types;

			/*
			 * interface types shouldn't overlap, this is
			 * used in cfg80211_can_change_interface()
			 */
			if (WARN_ON(types & all_iftypes))
				return -EINVAL;
			all_iftypes |= types;

			if (WARN_ON(!c->limits[j].max))
				return -EINVAL;

			/* Shouldn't list software iftypes in combinations! */
			if (WARN_ON(wiphy->software_iftypes & types))
				return -EINVAL;

			cnt += c->limits[j].max;
			/*
			 * Don't advertise an unsupported type
			 * in a combination.
			 */
			if (WARN_ON((wiphy->interface_modes & types) != types))
				return -EINVAL;
		}

		/* You can't even choose that many! */
		if (WARN_ON(cnt < c->max_interfaces))
			return -EINVAL;
	}

	return 0;
}

int wiphy_register(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	int res;
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;
	bool have_band = false;
	int i;
	u16 ifmodes = wiphy->interface_modes;

	if (WARN_ON((wiphy->wowlan.flags & WIPHY_WOWLAN_GTK_REKEY_FAILURE) &&
		    !(wiphy->wowlan.flags & WIPHY_WOWLAN_SUPPORTS_GTK_REKEY)))
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
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;

		sband->band = band;

		if (WARN_ON(!sband->n_channels || !sband->n_bitrates))
			return -EINVAL;

		/*
		 * Since cfg80211_disable_40mhz_24ghz is global, we can
		 * modify the sband's ht data even if the driver uses a
		 * global structure for that.
		 */
		if (cfg80211_disable_40mhz_24ghz &&
		    band == IEEE80211_BAND_2GHZ &&
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
			sband->channels[i].orig_mag =
				sband->channels[i].max_antenna_gain;
			sband->channels[i].orig_mpwr =
				sband->channels[i].max_power;
			sband->channels[i].band = band;
		}

		have_band = true;
	}

	if (!have_band) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (rdev->wiphy.wowlan.n_patterns) {
		if (WARN_ON(!rdev->wiphy.wowlan.pattern_min_len ||
			    rdev->wiphy.wowlan.pattern_min_len >
			    rdev->wiphy.wowlan.pattern_max_len))
			return -EINVAL;
	}

	/* check and set up bitrates */
	ieee80211_set_bitrate_flags(wiphy);

	mutex_lock(&cfg80211_mutex);

	res = device_add(&rdev->wiphy.dev);
	if (res) {
		mutex_unlock(&cfg80211_mutex);
		return res;
	}

	/* set up regulatory info */
	wiphy_update_regulatory(wiphy, NL80211_REGDOM_SET_BY_CORE);

	list_add_rcu(&rdev->list, &cfg80211_rdev_list);
	cfg80211_rdev_list_generation++;

	/* add to debugfs */
	rdev->wiphy.debugfsdir =
		debugfs_create_dir(wiphy_name(&rdev->wiphy),
				   ieee80211_debugfs_dir);
	if (IS_ERR(rdev->wiphy.debugfsdir))
		rdev->wiphy.debugfsdir = NULL;

	if (wiphy->flags & WIPHY_FLAG_CUSTOM_REGULATORY) {
		struct regulatory_request request;

		request.wiphy_idx = get_wiphy_idx(wiphy);
		request.initiator = NL80211_REGDOM_SET_BY_DRIVER;
		request.alpha2[0] = '9';
		request.alpha2[1] = '9';

		nl80211_send_reg_change_event(&request);
	}

	cfg80211_debugfs_rdev_add(rdev);
	mutex_unlock(&cfg80211_mutex);

	/*
	 * due to a locking dependency this has to be outside of the
	 * cfg80211_mutex lock
	 */
	res = rfkill_register(rdev->rfkill);
	if (res)
		goto out_rm_dev;

	return 0;

out_rm_dev:
	device_del(&rdev->wiphy.dev);
	return res;
}
EXPORT_SYMBOL(wiphy_register);

void wiphy_rfkill_start_polling(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	if (!rdev->ops->rfkill_poll)
		return;
	rdev->rfkill_ops.poll = cfg80211_rfkill_poll;
	rfkill_resume_polling(rdev->rfkill);
}
EXPORT_SYMBOL(wiphy_rfkill_start_polling);

void wiphy_rfkill_stop_polling(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	rfkill_pause_polling(rdev->rfkill);
}
EXPORT_SYMBOL(wiphy_rfkill_stop_polling);

void wiphy_unregister(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	rfkill_unregister(rdev->rfkill);

	/* protect the device list */
	mutex_lock(&cfg80211_mutex);

	wait_event(rdev->dev_wait, ({
		int __count;
		mutex_lock(&rdev->devlist_mtx);
		__count = rdev->opencount;
		mutex_unlock(&rdev->devlist_mtx);
		__count == 0;}));

	mutex_lock(&rdev->devlist_mtx);
	BUG_ON(!list_empty(&rdev->netdev_list));
	mutex_unlock(&rdev->devlist_mtx);

	/*
	 * First remove the hardware from everywhere, this makes
	 * it impossible to find from userspace.
	 */
	debugfs_remove_recursive(rdev->wiphy.debugfsdir);
	list_del_rcu(&rdev->list);
	synchronize_rcu();

	/*
	 * Try to grab rdev->mtx. If a command is still in progress,
	 * hopefully the driver will refuse it since it's tearing
	 * down the device already. We wait for this command to complete
	 * before unlinking the item from the list.
	 * Note: as codified by the BUG_ON above we cannot get here if
	 * a virtual interface is still present. Hence, we can only get
	 * to lock contention here if userspace issues a command that
	 * identified the hardware by wiphy index.
	 */
	cfg80211_lock_rdev(rdev);
	/* nothing */
	cfg80211_unlock_rdev(rdev);

	/* If this device got a regulatory hint tell core its
	 * free to listen now to a new shiny device regulatory hint */
	reg_device_remove(wiphy);

	cfg80211_rdev_list_generation++;
	device_del(&rdev->wiphy.dev);

	mutex_unlock(&cfg80211_mutex);

	flush_work(&rdev->scan_done_wk);
	cancel_work_sync(&rdev->conn_work);
	flush_work(&rdev->event_work);
}
EXPORT_SYMBOL(wiphy_unregister);

void cfg80211_dev_free(struct cfg80211_registered_device *rdev)
{
	struct cfg80211_internal_bss *scan, *tmp;
	rfkill_destroy(rdev->rfkill);
	mutex_destroy(&rdev->mtx);
	mutex_destroy(&rdev->devlist_mtx);
	mutex_destroy(&rdev->sched_scan_mtx);
	list_for_each_entry_safe(scan, tmp, &rdev->bss_list, list)
		cfg80211_put_bss(&scan->pub);
	cfg80211_rdev_free_wowlan(rdev);
	kfree(rdev);
}

void wiphy_free(struct wiphy *wiphy)
{
	put_device(&wiphy->dev);
}
EXPORT_SYMBOL(wiphy_free);

void wiphy_rfkill_set_hw_state(struct wiphy *wiphy, bool blocked)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	if (rfkill_set_hw_state(rdev->rfkill, blocked))
		schedule_work(&rdev->rfkill_sync);
}
EXPORT_SYMBOL(wiphy_rfkill_set_hw_state);

static void wdev_cleanup_work(struct work_struct *work)
{
	struct wireless_dev *wdev;
	struct cfg80211_registered_device *rdev;

	wdev = container_of(work, struct wireless_dev, cleanup_work);
	rdev = wiphy_to_dev(wdev->wiphy);

	cfg80211_lock_rdev(rdev);

	if (WARN_ON(rdev->scan_req && rdev->scan_req->dev == wdev->netdev)) {
		rdev->scan_req->aborted = true;
		___cfg80211_scan_done(rdev, true);
	}

	cfg80211_unlock_rdev(rdev);

	mutex_lock(&rdev->sched_scan_mtx);

	if (WARN_ON(rdev->sched_scan_req &&
		    rdev->sched_scan_req->dev == wdev->netdev)) {
		__cfg80211_stop_sched_scan(rdev, false);
	}

	mutex_unlock(&rdev->sched_scan_mtx);

	mutex_lock(&rdev->devlist_mtx);
	rdev->opencount--;
	mutex_unlock(&rdev->devlist_mtx);
	wake_up(&rdev->dev_wait);

	dev_put(wdev->netdev);
}

static struct device_type wiphy_type = {
	.name	= "wlan",
};

static int cfg80211_netdev_notifier_call(struct notifier_block * nb,
					 unsigned long state,
					 void *ndev)
{
	struct net_device *dev = ndev;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev;
	int ret;

	if (!wdev)
		return NOTIFY_DONE;

	rdev = wiphy_to_dev(wdev->wiphy);

	WARN_ON(wdev->iftype == NL80211_IFTYPE_UNSPECIFIED);

	switch (state) {
	case NETDEV_POST_INIT:
		SET_NETDEV_DEVTYPE(dev, &wiphy_type);
		break;
	case NETDEV_REGISTER:
		/*
		 * NB: cannot take rdev->mtx here because this may be
		 * called within code protected by it when interfaces
		 * are added with nl80211.
		 */
		mutex_init(&wdev->mtx);
		INIT_WORK(&wdev->cleanup_work, wdev_cleanup_work);
		INIT_LIST_HEAD(&wdev->event_list);
		spin_lock_init(&wdev->event_lock);
		INIT_LIST_HEAD(&wdev->mgmt_registrations);
		spin_lock_init(&wdev->mgmt_registrations_lock);

		mutex_lock(&rdev->devlist_mtx);
		list_add_rcu(&wdev->list, &rdev->netdev_list);
		rdev->devlist_generation++;
		/* can only change netns with wiphy */
		dev->features |= NETIF_F_NETNS_LOCAL;

		if (sysfs_create_link(&dev->dev.kobj, &rdev->wiphy.dev.kobj,
				      "phy80211")) {
			pr_err("failed to add phy80211 symlink to netdev!\n");
		}
		wdev->netdev = dev;
		wdev->sme_state = CFG80211_SME_IDLE;
		mutex_unlock(&rdev->devlist_mtx);
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

		if (!dev->ethtool_ops)
			dev->ethtool_ops = &cfg80211_ethtool_ops;

		if ((wdev->iftype == NL80211_IFTYPE_STATION ||
		     wdev->iftype == NL80211_IFTYPE_P2P_CLIENT ||
		     wdev->iftype == NL80211_IFTYPE_ADHOC) && !wdev->use_4addr)
			dev->priv_flags |= IFF_DONT_BRIDGE;
		break;
	case NETDEV_GOING_DOWN:
		switch (wdev->iftype) {
		case NL80211_IFTYPE_ADHOC:
			cfg80211_leave_ibss(rdev, dev, true);
			break;
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_STATION:
			mutex_lock(&rdev->sched_scan_mtx);
			__cfg80211_stop_sched_scan(rdev, false);
			mutex_unlock(&rdev->sched_scan_mtx);

			wdev_lock(wdev);
#ifdef CONFIG_CFG80211_WEXT
			kfree(wdev->wext.ie);
			wdev->wext.ie = NULL;
			wdev->wext.ie_len = 0;
			wdev->wext.connect.auth_type = NL80211_AUTHTYPE_AUTOMATIC;
#endif
			__cfg80211_disconnect(rdev, dev,
					      WLAN_REASON_DEAUTH_LEAVING, true);
			cfg80211_mlme_down(rdev, dev);
			wdev_unlock(wdev);
			break;
		case NL80211_IFTYPE_MESH_POINT:
			cfg80211_leave_mesh(rdev, dev);
			break;
		default:
			break;
		}
		wdev->beacon_interval = 0;
		break;
	case NETDEV_DOWN:
		dev_hold(dev);
		queue_work(cfg80211_wq, &wdev->cleanup_work);
		break;
	case NETDEV_UP:
		/*
		 * If we have a really quick DOWN/UP succession we may
		 * have this work still pending ... cancel it and see
		 * if it was pending, in which case we need to account
		 * for some of the work it would have done.
		 */
		if (cancel_work_sync(&wdev->cleanup_work)) {
			mutex_lock(&rdev->devlist_mtx);
			rdev->opencount--;
			mutex_unlock(&rdev->devlist_mtx);
			dev_put(dev);
		}
		cfg80211_lock_rdev(rdev);
		mutex_lock(&rdev->devlist_mtx);
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
				setup.mesh_id = wdev->ssid;
				setup.mesh_id_len = wdev->mesh_id_up_len;
				if (wdev->mesh_id_up_len)
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
		mutex_unlock(&rdev->devlist_mtx);
		cfg80211_unlock_rdev(rdev);

		/*
		 * Configure power management to the driver here so that its
		 * correctly set also after interface type changes etc.
		 */
		if (wdev->iftype == NL80211_IFTYPE_STATION &&
		    rdev->ops->set_power_mgmt)
			if (rdev->ops->set_power_mgmt(wdev->wiphy, dev,
						      wdev->ps,
						      wdev->ps_timeout)) {
				/* assume this means it's off */
				wdev->ps = false;
			}
		break;
	case NETDEV_UNREGISTER:
		/*
		 * NB: cannot take rdev->mtx here because this may be
		 * called within code protected by it when interfaces
		 * are removed with nl80211.
		 */
		mutex_lock(&rdev->devlist_mtx);
		/*
		 * It is possible to get NETDEV_UNREGISTER
		 * multiple times. To detect that, check
		 * that the interface is still on the list
		 * of registered interfaces, and only then
		 * remove and clean it up.
		 */
		if (!list_empty(&wdev->list)) {
			sysfs_remove_link(&dev->dev.kobj, "phy80211");
			list_del_rcu(&wdev->list);
			rdev->devlist_generation++;
			cfg80211_mlme_purge_registrations(wdev);
#ifdef CONFIG_CFG80211_WEXT
			kfree(wdev->wext.keys);
#endif
		}
		mutex_unlock(&rdev->devlist_mtx);
		/*
		 * synchronise (so that we won't find this netdev
		 * from other code any more) and then clear the list
		 * head so that the above code can safely check for
		 * !list_empty() to avoid double-cleanup.
		 */
		synchronize_rcu();
		INIT_LIST_HEAD(&wdev->list);
		break;
	case NETDEV_PRE_UP:
		if (!(wdev->wiphy->interface_modes & BIT(wdev->iftype)))
			return notifier_from_errno(-EOPNOTSUPP);
		if (rfkill_blocked(rdev->rfkill))
			return notifier_from_errno(-ERFKILL);
		ret = cfg80211_can_add_interface(rdev, wdev->iftype);
		if (ret)
			return notifier_from_errno(ret);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block cfg80211_netdev_notifier = {
	.notifier_call = cfg80211_netdev_notifier_call,
};

static void __net_exit cfg80211_pernet_exit(struct net *net)
{
	struct cfg80211_registered_device *rdev;

	rtnl_lock();
	mutex_lock(&cfg80211_mutex);
	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (net_eq(wiphy_net(&rdev->wiphy), net))
			WARN_ON(cfg80211_switch_netns(rdev, &init_net));
	}
	mutex_unlock(&cfg80211_mutex);
	rtnl_unlock();
}

static struct pernet_operations cfg80211_pernet_ops = {
	.exit = cfg80211_pernet_exit,
};

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

	cfg80211_wq = create_singlethread_workqueue("cfg80211");
	if (!cfg80211_wq)
		goto out_fail_wq;

	return 0;

out_fail_wq:
	regulatory_exit();
out_fail_reg:
	debugfs_remove(ieee80211_debugfs_dir);
out_fail_nl80211:
	unregister_netdevice_notifier(&cfg80211_netdev_notifier);
out_fail_notifier:
	wiphy_sysfs_exit();
out_fail_sysfs:
	unregister_pernet_device(&cfg80211_pernet_ops);
out_fail_pernet:
	return err;
}
subsys_initcall(cfg80211_init);

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
