/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "debugfs_key.h"
#include "aes_ccm.h"


/**
 * DOC: Key handling basics
 *
 * Key handling in mac80211 is done based on per-interface (sub_if_data)
 * keys and per-station keys. Since each station belongs to an interface,
 * each station key also belongs to that interface.
 *
 * Hardware acceleration is done on a best-effort basis, for each key
 * that is eligible the hardware is asked to enable that key but if
 * it cannot do that they key is simply kept for software encryption.
 * There is currently no way of knowing this except by looking into
 * debugfs.
 *
 * All operations here are called under RTNL so no extra locking is
 * required.
 *
 * NOTE: This code requires that sta info *destruction* is done under
 *	 RTNL, otherwise it can try to access already freed STA structs
 *	 when a STA key is being freed.
 */

static const u8 bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 zero_addr[ETH_ALEN];

static const u8 *get_mac_for_key(struct ieee80211_key *key)
{
	const u8 *addr = bcast_addr;

	/*
	 * If we're an AP we won't ever receive frames with a non-WEP
	 * group key so we tell the driver that by using the zero MAC
	 * address to indicate a transmit-only key.
	 */
	if (key->conf.alg != ALG_WEP &&
	    (key->sdata->vif.type == IEEE80211_IF_TYPE_AP ||
	     key->sdata->vif.type == IEEE80211_IF_TYPE_VLAN))
		addr = zero_addr;

	if (key->sta)
		addr = key->sta->addr;

	return addr;
}

static void ieee80211_key_enable_hw_accel(struct ieee80211_key *key)
{
	const u8 *addr;
	int ret;
	DECLARE_MAC_BUF(mac);

	if (!key->local->ops->set_key)
		return;

	/*
	 * This makes sure that all pending flushes have
	 * actually completed prior to uploading new key
	 * material to the hardware. That is necessary to
	 * avoid races between flushing STAs and adding
	 * new keys for them.
	 */
	__ieee80211_run_pending_flush(key->local);

	addr = get_mac_for_key(key);

	ret = key->local->ops->set_key(local_to_hw(key->local), SET_KEY,
				       key->sdata->dev->dev_addr, addr,
				       &key->conf);

	if (!ret)
		key->flags |= KEY_FLAG_UPLOADED_TO_HARDWARE;

	if (ret && ret != -ENOSPC && ret != -EOPNOTSUPP)
		printk(KERN_ERR "mac80211-%s: failed to set key "
		       "(%d, %s) to hardware (%d)\n",
		       wiphy_name(key->local->hw.wiphy),
		       key->conf.keyidx, print_mac(mac, addr), ret);
}

static void ieee80211_key_mark_hw_accel_off(struct ieee80211_key *key)
{
	if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) {
		key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
		key->flags |= KEY_FLAG_REMOVE_FROM_HARDWARE;
	}
}

static void ieee80211_key_disable_hw_accel(struct ieee80211_key *key)
{
	const u8 *addr;
	int ret;
	DECLARE_MAC_BUF(mac);

	if (!key || !key->local->ops->set_key)
		return;

	if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) &&
	    !(key->flags & KEY_FLAG_REMOVE_FROM_HARDWARE))
		return;

	addr = get_mac_for_key(key);

	ret = key->local->ops->set_key(local_to_hw(key->local), DISABLE_KEY,
				       key->sdata->dev->dev_addr, addr,
				       &key->conf);

	if (ret)
		printk(KERN_ERR "mac80211-%s: failed to remove key "
		       "(%d, %s) from hardware (%d)\n",
		       wiphy_name(key->local->hw.wiphy),
		       key->conf.keyidx, print_mac(mac, addr), ret);

	key->flags &= ~(KEY_FLAG_UPLOADED_TO_HARDWARE |
			KEY_FLAG_REMOVE_FROM_HARDWARE);
}

struct ieee80211_key *ieee80211_key_alloc(enum ieee80211_key_alg alg,
					  int idx,
					  size_t key_len,
					  const u8 *key_data)
{
	struct ieee80211_key *key;

	BUG_ON(idx < 0 || idx >= NUM_DEFAULT_KEYS);

	key = kzalloc(sizeof(struct ieee80211_key) + key_len, GFP_KERNEL);
	if (!key)
		return NULL;

	/*
	 * Default to software encryption; we'll later upload the
	 * key to the hardware if possible.
	 */
	key->conf.flags = 0;
	key->flags = 0;

	key->conf.alg = alg;
	key->conf.keyidx = idx;
	key->conf.keylen = key_len;
	memcpy(key->conf.key, key_data, key_len);
	INIT_LIST_HEAD(&key->list);

	if (alg == ALG_CCMP) {
		/*
		 * Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.ccmp.tfm = ieee80211_aes_key_setup_encrypt(key_data);
		if (!key->u.ccmp.tfm) {
			ieee80211_key_free(key);
			return NULL;
		}
	}

	return key;
}

static void __ieee80211_key_replace(struct ieee80211_sub_if_data *sdata,
				    struct sta_info *sta,
				    struct ieee80211_key *key,
				    struct ieee80211_key *new)
{
	int idx, defkey;

	if (new)
		list_add(&new->list, &sdata->key_list);

	if (sta) {
		rcu_assign_pointer(sta->key, new);
	} else {
		WARN_ON(new && key && new->conf.keyidx != key->conf.keyidx);

		if (key)
			idx = key->conf.keyidx;
		else
			idx = new->conf.keyidx;

		defkey = key && sdata->default_key == key;

		if (defkey && !new)
			ieee80211_set_default_key(sdata, -1);

		rcu_assign_pointer(sdata->keys[idx], new);
		if (defkey && new)
			ieee80211_set_default_key(sdata, new->conf.keyidx);
	}

	if (key) {
		ieee80211_key_mark_hw_accel_off(key);
		/*
		 * We'll use an empty list to indicate that the key
		 * has already been removed.
		 */
		list_del_init(&key->list);
	}
}

void ieee80211_key_link(struct ieee80211_key *key,
			struct ieee80211_sub_if_data *sdata,
			struct sta_info *sta)
{
	struct ieee80211_key *old_key;
	int idx;

	ASSERT_RTNL();
	might_sleep();

	BUG_ON(!sdata);
	BUG_ON(!key);

	idx = key->conf.keyidx;
	key->local = sdata->local;
	key->sdata = sdata;
	key->sta = sta;

	ieee80211_debugfs_key_add(key->local, key);

	if (sta) {
		ieee80211_debugfs_key_sta_link(key, sta);

		/*
		 * some hardware cannot handle TKIP with QoS, so
		 * we indicate whether QoS could be in use.
		 */
		if (sta->flags & WLAN_STA_WME)
			key->conf.flags |= IEEE80211_KEY_FLAG_WMM_STA;
	} else {
		if (sdata->vif.type == IEEE80211_IF_TYPE_STA) {
			struct sta_info *ap;

			rcu_read_lock();

			/* same here, the AP could be using QoS */
			ap = sta_info_get(key->local, key->sdata->u.sta.bssid);
			if (ap) {
				if (ap->flags & WLAN_STA_WME)
					key->conf.flags |=
						IEEE80211_KEY_FLAG_WMM_STA;
			}

			rcu_read_unlock();
		}
	}

	if (sta)
		old_key = sta->key;
	else
		old_key = sdata->keys[idx];

	__ieee80211_key_replace(sdata, sta, old_key, key);

	if (old_key) {
		synchronize_rcu();
		ieee80211_key_free(old_key);
	}

	if (netif_running(sdata->dev))
		ieee80211_key_enable_hw_accel(key);
}

void ieee80211_key_free(struct ieee80211_key *key)
{
	ASSERT_RTNL();
	might_sleep();

	if (!key)
		return;

	if (key->sdata) {
		/*
		 * Replace key with nothingness.
		 *
		 * Because other code may have key reference (RCU protected)
		 * right now, we then wait for a grace period before freeing
		 * it.
		 * An empty list indicates it was never added to the key list
		 * or has been removed already. It may, however, still be in
		 * hardware for acceleration.
		 */
		if (!list_empty(&key->list))
			__ieee80211_key_replace(key->sdata, key->sta,
						key, NULL);

		/*
		 * Do NOT remove this without looking at sta_info_destroy()
		 */
		synchronize_rcu();

		/*
		 * Remove from hwaccel if appropriate, this will
		 * only happen when the key is actually unlinked,
		 * it will already be done when the key was replaced.
		 */
		ieee80211_key_disable_hw_accel(key);
	}

	if (key->conf.alg == ALG_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	ieee80211_debugfs_key_remove(key);

	kfree(key);
}

void ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata, int idx)
{
	struct ieee80211_key *key = NULL;

	if (idx >= 0 && idx < NUM_DEFAULT_KEYS)
		key = sdata->keys[idx];

	if (sdata->default_key != key) {
		ieee80211_debugfs_key_remove_default(sdata);

		rcu_assign_pointer(sdata->default_key, key);

		if (sdata->default_key)
			ieee80211_debugfs_key_add_default(sdata);
	}
}

void ieee80211_free_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key, *tmp;
	LIST_HEAD(tmp_list);

	ASSERT_RTNL();
	might_sleep();

	list_for_each_entry_safe(key, tmp, &sdata->key_list, list)
		ieee80211_key_free(key);
}

void ieee80211_enable_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key;

	ASSERT_RTNL();
	might_sleep();

	if (WARN_ON(!netif_running(sdata->dev)))
		return;

	list_for_each_entry(key, &sdata->key_list, list)
		ieee80211_key_enable_hw_accel(key);
}

void ieee80211_disable_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key;

	ASSERT_RTNL();
	might_sleep();

	list_for_each_entry(key, &sdata->key_list, list)
		ieee80211_key_disable_hw_accel(key);
}
