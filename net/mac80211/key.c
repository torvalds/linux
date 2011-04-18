/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2008	Johannes Berg <johannes@sipsolutions.net>
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
#include <linux/slab.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "debugfs_key.h"
#include "aes_ccm.h"
#include "aes_cmac.h"


/**
 * DOC: Key handling basics
 *
 * Key handling in mac80211 is done based on per-interface (sub_if_data)
 * keys and per-station keys. Since each station belongs to an interface,
 * each station key also belongs to that interface.
 *
 * Hardware acceleration is done on a best-effort basis for algorithms
 * that are implemented in software,  for each key the hardware is asked
 * to enable that key for offloading but if it cannot do that the key is
 * simply kept for software encryption (unless it is for an algorithm
 * that isn't implemented in software).
 * There is currently no way of knowing whether a key is handled in SW
 * or HW except by looking into debugfs.
 *
 * All key management is internally protected by a mutex. Within all
 * other parts of mac80211, key references are, just as STA structure
 * references, protected by RCU. Note, however, that some things are
 * unprotected, namely the key->sta dereferences within the hardware
 * acceleration functions. This means that sta_info_destroy() must
 * remove the key which waits for an RCU grace period.
 */

static const u8 bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void assert_key_lock(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->key_mtx);
}

static struct ieee80211_sta *get_sta_for_key(struct ieee80211_key *key)
{
	if (key->sta)
		return &key->sta->sta;

	return NULL;
}

static int ieee80211_key_enable_hw_accel(struct ieee80211_key *key)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_sta *sta;
	int ret;

	might_sleep();

	if (!key->local->ops->set_key)
		goto out_unsupported;

	assert_key_lock(key->local);

	sta = get_sta_for_key(key);

	/*
	 * If this is a per-STA GTK, check if it
	 * is supported; if not, return.
	 */
	if (sta && !(key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE) &&
	    !(key->local->hw.flags & IEEE80211_HW_SUPPORTS_PER_STA_GTK))
		goto out_unsupported;

	sdata = key->sdata;
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		/*
		 * The driver doesn't know anything about VLAN interfaces.
		 * Hence, don't send GTKs for VLAN interfaces to the driver.
		 */
		if (!(key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE))
			goto out_unsupported;
		sdata = container_of(sdata->bss,
				     struct ieee80211_sub_if_data,
				     u.ap);
	}

	ret = drv_set_key(key->local, SET_KEY, sdata, sta, &key->conf);

	if (!ret) {
		key->flags |= KEY_FLAG_UPLOADED_TO_HARDWARE;
		return 0;
	}

	if (ret != -ENOSPC && ret != -EOPNOTSUPP)
		wiphy_err(key->local->hw.wiphy,
			  "failed to set key (%d, %pM) to hardware (%d)\n",
			  key->conf.keyidx, sta ? sta->addr : bcast_addr, ret);

 out_unsupported:
	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_AES_CMAC:
		/* all of these we can do in software */
		return 0;
	default:
		return -EINVAL;
	}
}

static void ieee80211_key_disable_hw_accel(struct ieee80211_key *key)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_sta *sta;
	int ret;

	might_sleep();

	if (!key || !key->local->ops->set_key)
		return;

	assert_key_lock(key->local);

	if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		return;

	sta = get_sta_for_key(key);
	sdata = key->sdata;

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		sdata = container_of(sdata->bss,
				     struct ieee80211_sub_if_data,
				     u.ap);

	ret = drv_set_key(key->local, DISABLE_KEY, sdata,
			  sta, &key->conf);

	if (ret)
		wiphy_err(key->local->hw.wiphy,
			  "failed to remove key (%d, %pM) from hardware (%d)\n",
			  key->conf.keyidx, sta ? sta->addr : bcast_addr, ret);

	key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
}

void ieee80211_key_removed(struct ieee80211_key_conf *key_conf)
{
	struct ieee80211_key *key;

	key = container_of(key_conf, struct ieee80211_key, conf);

	might_sleep();
	assert_key_lock(key->local);

	key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;

	/*
	 * Flush TX path to avoid attempts to use this key
	 * after this function returns. Until then, drivers
	 * must be prepared to handle the key.
	 */
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(ieee80211_key_removed);

static void __ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata,
					int idx, bool uni, bool multi)
{
	struct ieee80211_key *key = NULL;

	assert_key_lock(sdata->local);

	if (idx >= 0 && idx < NUM_DEFAULT_KEYS)
		key = sdata->keys[idx];

	if (uni)
		rcu_assign_pointer(sdata->default_unicast_key, key);
	if (multi)
		rcu_assign_pointer(sdata->default_multicast_key, key);

	ieee80211_debugfs_key_update_default(sdata);
}

void ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata, int idx,
			       bool uni, bool multi)
{
	mutex_lock(&sdata->local->key_mtx);
	__ieee80211_set_default_key(sdata, idx, uni, multi);
	mutex_unlock(&sdata->local->key_mtx);
}

static void
__ieee80211_set_default_mgmt_key(struct ieee80211_sub_if_data *sdata, int idx)
{
	struct ieee80211_key *key = NULL;

	assert_key_lock(sdata->local);

	if (idx >= NUM_DEFAULT_KEYS &&
	    idx < NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS)
		key = sdata->keys[idx];

	rcu_assign_pointer(sdata->default_mgmt_key, key);

	ieee80211_debugfs_key_update_default(sdata);
}

void ieee80211_set_default_mgmt_key(struct ieee80211_sub_if_data *sdata,
				    int idx)
{
	mutex_lock(&sdata->local->key_mtx);
	__ieee80211_set_default_mgmt_key(sdata, idx);
	mutex_unlock(&sdata->local->key_mtx);
}


static void __ieee80211_key_replace(struct ieee80211_sub_if_data *sdata,
				    struct sta_info *sta,
				    bool pairwise,
				    struct ieee80211_key *old,
				    struct ieee80211_key *new)
{
	int idx;
	bool defunikey, defmultikey, defmgmtkey;

	if (new)
		list_add(&new->list, &sdata->key_list);

	if (sta && pairwise) {
		rcu_assign_pointer(sta->ptk, new);
	} else if (sta) {
		if (old)
			idx = old->conf.keyidx;
		else
			idx = new->conf.keyidx;
		rcu_assign_pointer(sta->gtk[idx], new);
	} else {
		WARN_ON(new && old && new->conf.keyidx != old->conf.keyidx);

		if (old)
			idx = old->conf.keyidx;
		else
			idx = new->conf.keyidx;

		defunikey = old && sdata->default_unicast_key == old;
		defmultikey = old && sdata->default_multicast_key == old;
		defmgmtkey = old && sdata->default_mgmt_key == old;

		if (defunikey && !new)
			__ieee80211_set_default_key(sdata, -1, true, false);
		if (defmultikey && !new)
			__ieee80211_set_default_key(sdata, -1, false, true);
		if (defmgmtkey && !new)
			__ieee80211_set_default_mgmt_key(sdata, -1);

		rcu_assign_pointer(sdata->keys[idx], new);
		if (defunikey && new)
			__ieee80211_set_default_key(sdata, new->conf.keyidx,
						    true, false);
		if (defmultikey && new)
			__ieee80211_set_default_key(sdata, new->conf.keyidx,
						    false, true);
		if (defmgmtkey && new)
			__ieee80211_set_default_mgmt_key(sdata,
							 new->conf.keyidx);
	}

	if (old)
		list_del(&old->list);
}

struct ieee80211_key *ieee80211_key_alloc(u32 cipher, int idx, size_t key_len,
					  const u8 *key_data,
					  size_t seq_len, const u8 *seq)
{
	struct ieee80211_key *key;
	int i, j, err;

	BUG_ON(idx < 0 || idx >= NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS);

	key = kzalloc(sizeof(struct ieee80211_key) + key_len, GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);

	/*
	 * Default to software encryption; we'll later upload the
	 * key to the hardware if possible.
	 */
	key->conf.flags = 0;
	key->flags = 0;

	key->conf.cipher = cipher;
	key->conf.keyidx = idx;
	key->conf.keylen = key_len;
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		key->conf.iv_len = WEP_IV_LEN;
		key->conf.icv_len = WEP_ICV_LEN;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key->conf.iv_len = TKIP_IV_LEN;
		key->conf.icv_len = TKIP_ICV_LEN;
		if (seq) {
			for (i = 0; i < NUM_RX_DATA_QUEUES; i++) {
				key->u.tkip.rx[i].iv32 =
					get_unaligned_le32(&seq[2]);
				key->u.tkip.rx[i].iv16 =
					get_unaligned_le16(seq);
			}
		}
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key->conf.iv_len = CCMP_HDR_LEN;
		key->conf.icv_len = CCMP_MIC_LEN;
		if (seq) {
			for (i = 0; i < NUM_RX_DATA_QUEUES + 1; i++)
				for (j = 0; j < CCMP_PN_LEN; j++)
					key->u.ccmp.rx_pn[i][j] =
						seq[CCMP_PN_LEN - j - 1];
		}
		/*
		 * Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.ccmp.tfm = ieee80211_aes_key_setup_encrypt(key_data);
		if (IS_ERR(key->u.ccmp.tfm)) {
			err = PTR_ERR(key->u.ccmp.tfm);
			kfree(key);
			return ERR_PTR(err);
		}
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key->conf.iv_len = 0;
		key->conf.icv_len = sizeof(struct ieee80211_mmie);
		if (seq)
			for (j = 0; j < 6; j++)
				key->u.aes_cmac.rx_pn[j] = seq[6 - j - 1];
		/*
		 * Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.aes_cmac.tfm =
			ieee80211_aes_cmac_key_setup(key_data);
		if (IS_ERR(key->u.aes_cmac.tfm)) {
			err = PTR_ERR(key->u.aes_cmac.tfm);
			kfree(key);
			return ERR_PTR(err);
		}
		break;
	}
	memcpy(key->conf.key, key_data, key_len);
	INIT_LIST_HEAD(&key->list);

	return key;
}

static void __ieee80211_key_destroy(struct ieee80211_key *key)
{
	if (!key)
		return;

	/*
	 * Synchronize so the TX path can no longer be using
	 * this key before we free/remove it.
	 */
	synchronize_rcu();

	if (key->local)
		ieee80211_key_disable_hw_accel(key);

	if (key->conf.cipher == WLAN_CIPHER_SUITE_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	if (key->conf.cipher == WLAN_CIPHER_SUITE_AES_CMAC)
		ieee80211_aes_cmac_key_free(key->u.aes_cmac.tfm);
	if (key->local)
		ieee80211_debugfs_key_remove(key);

	kfree(key);
}

int ieee80211_key_link(struct ieee80211_key *key,
		       struct ieee80211_sub_if_data *sdata,
		       struct sta_info *sta)
{
	struct ieee80211_key *old_key;
	int idx, ret;
	bool pairwise;

	BUG_ON(!sdata);
	BUG_ON(!key);

	pairwise = key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE;
	idx = key->conf.keyidx;
	key->local = sdata->local;
	key->sdata = sdata;
	key->sta = sta;

	if (sta) {
		/*
		 * some hardware cannot handle TKIP with QoS, so
		 * we indicate whether QoS could be in use.
		 */
		if (test_sta_flags(sta, WLAN_STA_WME))
			key->conf.flags |= IEEE80211_KEY_FLAG_WMM_STA;
	} else {
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			struct sta_info *ap;

			/*
			 * We're getting a sta pointer in, so must be under
			 * appropriate locking for sta_info_get().
			 */

			/* same here, the AP could be using QoS */
			ap = sta_info_get(key->sdata, key->sdata->u.mgd.bssid);
			if (ap) {
				if (test_sta_flags(ap, WLAN_STA_WME))
					key->conf.flags |=
						IEEE80211_KEY_FLAG_WMM_STA;
			}
		}
	}

	mutex_lock(&sdata->local->key_mtx);

	if (sta && pairwise)
		old_key = sta->ptk;
	else if (sta)
		old_key = sta->gtk[idx];
	else
		old_key = sdata->keys[idx];

	__ieee80211_key_replace(sdata, sta, pairwise, old_key, key);
	__ieee80211_key_destroy(old_key);

	ieee80211_debugfs_key_add(key);

	ret = ieee80211_key_enable_hw_accel(key);

	mutex_unlock(&sdata->local->key_mtx);

	return ret;
}

static void __ieee80211_key_free(struct ieee80211_key *key)
{
	/*
	 * Replace key with nothingness if it was ever used.
	 */
	if (key->sdata)
		__ieee80211_key_replace(key->sdata, key->sta,
				key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE,
				key, NULL);
	__ieee80211_key_destroy(key);
}

void ieee80211_key_free(struct ieee80211_local *local,
			struct ieee80211_key *key)
{
	if (!key)
		return;

	mutex_lock(&local->key_mtx);
	__ieee80211_key_free(key);
	mutex_unlock(&local->key_mtx);
}

void ieee80211_enable_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key;

	ASSERT_RTNL();

	if (WARN_ON(!ieee80211_sdata_running(sdata)))
		return;

	mutex_lock(&sdata->local->key_mtx);

	list_for_each_entry(key, &sdata->key_list, list)
		ieee80211_key_enable_hw_accel(key);

	mutex_unlock(&sdata->local->key_mtx);
}

void ieee80211_disable_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key;

	ASSERT_RTNL();

	mutex_lock(&sdata->local->key_mtx);

	list_for_each_entry(key, &sdata->key_list, list)
		ieee80211_key_disable_hw_accel(key);

	mutex_unlock(&sdata->local->key_mtx);
}

void ieee80211_free_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key, *tmp;

	mutex_lock(&sdata->local->key_mtx);

	ieee80211_debugfs_key_remove_mgmt_default(sdata);

	list_for_each_entry_safe(key, tmp, &sdata->key_list, list)
		__ieee80211_key_free(key);

	ieee80211_debugfs_key_update_default(sdata);

	mutex_unlock(&sdata->local->key_mtx);
}
