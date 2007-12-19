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
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "debugfs_key.h"
#include "aes_ccm.h"


/*
 * Key handling basics
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

static void ieee80211_key_disable_hw_accel(struct ieee80211_key *key)
{
	const u8 *addr;
	int ret;
	DECLARE_MAC_BUF(mac);

	if (!key->local->ops->set_key)
		return;

	if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
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

	key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
}

struct ieee80211_key *ieee80211_key_alloc(struct ieee80211_sub_if_data *sdata,
					  struct sta_info *sta,
					  enum ieee80211_key_alg alg,
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

	key->local = sdata->local;
	key->sdata = sdata;
	key->sta = sta;

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

	ieee80211_debugfs_key_add(key->local, key);

	/* remove key first */
	if (sta)
		ieee80211_key_free(sta->key);
	else
		ieee80211_key_free(sdata->keys[idx]);

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

			/* same here, the AP could be using QoS */
			ap = sta_info_get(key->local, key->sdata->u.sta.bssid);
			if (ap) {
				if (ap->flags & WLAN_STA_WME)
					key->conf.flags |=
						IEEE80211_KEY_FLAG_WMM_STA;
				sta_info_put(ap);
			}
		}
	}

	/* enable hwaccel if appropriate */
	if (netif_running(key->sdata->dev))
		ieee80211_key_enable_hw_accel(key);

	if (sta)
		rcu_assign_pointer(sta->key, key);
	else
		rcu_assign_pointer(sdata->keys[idx], key);

	list_add(&key->list, &sdata->key_list);

	return key;
}

void ieee80211_key_free(struct ieee80211_key *key)
{
	if (!key)
		return;

	if (key->sta) {
		rcu_assign_pointer(key->sta->key, NULL);
	} else {
		if (key->sdata->default_key == key)
			ieee80211_set_default_key(key->sdata, -1);
		if (key->conf.keyidx >= 0 &&
		    key->conf.keyidx < NUM_DEFAULT_KEYS)
			rcu_assign_pointer(key->sdata->keys[key->conf.keyidx],
					   NULL);
		else
			WARN_ON(1);
	}

	/* wait for all key users to complete */
	synchronize_rcu();

	/* remove from hwaccel if appropriate */
	ieee80211_key_disable_hw_accel(key);

	if (key->conf.alg == ALG_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	ieee80211_debugfs_key_remove(key);

	list_del(&key->list);

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

	list_for_each_entry_safe(key, tmp, &sdata->key_list, list)
		ieee80211_key_free(key);
}

void ieee80211_enable_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key;

	WARN_ON(!netif_running(sdata->dev));
	if (!netif_running(sdata->dev))
		return;

	list_for_each_entry(key, &sdata->key_list, list)
		ieee80211_key_enable_hw_accel(key);
}

void ieee80211_disable_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key;

	list_for_each_entry(key, &sdata->key_list, list)
		ieee80211_key_disable_hw_accel(key);
}
