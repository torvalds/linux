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
 * Hardware acceleration is done on a best-effort basis, for each key
 * that is eligible the hardware is asked to enable that key but if
 * it cannot do that they key is simply kept for software encryption.
 * There is currently no way of knowing this except by looking into
 * debugfs.
 *
 * All key operations are protected internally.
 *
 * Within mac80211, key references are, just as STA structure references,
 * protected by RCU. Note, however, that some things are unprotected,
 * namely the key->sta dereferences within the hardware acceleration
 * functions. This means that sta_info_destroy() must remove the key
 * which waits for an RCU grace period.
 */

static const u8 bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void assert_key_lock(struct ieee80211_local *local)
{
	WARN_ON(!mutex_is_locked(&local->key_mtx));
}

static struct ieee80211_sta *get_sta_for_key(struct ieee80211_key *key)
{
	if (key->sta)
		return &key->sta->sta;

	return NULL;
}

static void ieee80211_key_enable_hw_accel(struct ieee80211_key *key)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_sta *sta;
	int ret;

	might_sleep();

	if (!key->local->ops->set_key)
		return;

	assert_key_lock(key->local);

	sta = get_sta_for_key(key);

	sdata = key->sdata;
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		sdata = container_of(sdata->bss,
				     struct ieee80211_sub_if_data,
				     u.ap);

	ret = drv_set_key(key->local, SET_KEY, sdata, sta, &key->conf);

	if (!ret)
		key->flags |= KEY_FLAG_UPLOADED_TO_HARDWARE;

	if (ret && ret != -ENOSPC && ret != -EOPNOTSUPP)
		printk(KERN_ERR "mac80211-%s: failed to set key "
		       "(%d, %pM) to hardware (%d)\n",
		       wiphy_name(key->local->hw.wiphy),
		       key->conf.keyidx, sta ? sta->addr : bcast_addr, ret);
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
		printk(KERN_ERR "mac80211-%s: failed to remove key "
		       "(%d, %pM) from hardware (%d)\n",
		       wiphy_name(key->local->hw.wiphy),
		       key->conf.keyidx, sta ? sta->addr : bcast_addr, ret);

	key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
}

static void __ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata,
					int idx)
{
	struct ieee80211_key *key = NULL;

	assert_key_lock(sdata->local);

	if (idx >= 0 && idx < NUM_DEFAULT_KEYS)
		key = sdata->keys[idx];

	rcu_assign_pointer(sdata->default_key, key);

	if (key) {
		ieee80211_debugfs_key_remove_default(key->sdata);
		ieee80211_debugfs_key_add_default(key->sdata);
	}
}

void ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata, int idx)
{
	mutex_lock(&sdata->local->key_mtx);
	__ieee80211_set_default_key(sdata, idx);
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

	if (key) {
		ieee80211_debugfs_key_remove_mgmt_default(key->sdata);
		ieee80211_debugfs_key_add_mgmt_default(key->sdata);
	}
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
				    struct ieee80211_key *old,
				    struct ieee80211_key *new)
{
	int idx, defkey, defmgmtkey;

	if (new)
		list_add(&new->list, &sdata->key_list);

	if (sta) {
		rcu_assign_pointer(sta->key, new);
	} else {
		WARN_ON(new && old && new->conf.keyidx != old->conf.keyidx);

		if (old)
			idx = old->conf.keyidx;
		else
			idx = new->conf.keyidx;

		defkey = old && sdata->default_key == old;
		defmgmtkey = old && sdata->default_mgmt_key == old;

		if (defkey && !new)
			__ieee80211_set_default_key(sdata, -1);
		if (defmgmtkey && !new)
			__ieee80211_set_default_mgmt_key(sdata, -1);

		rcu_assign_pointer(sdata->keys[idx], new);
		if (defkey && new)
			__ieee80211_set_default_key(sdata, new->conf.keyidx);
		if (defmgmtkey && new)
			__ieee80211_set_default_mgmt_key(sdata,
							 new->conf.keyidx);
	}

	if (old) {
		/*
		 * We'll use an empty list to indicate that the key
		 * has already been removed.
		 */
		list_del_init(&old->list);
	}
}

struct ieee80211_key *ieee80211_key_alloc(enum ieee80211_key_alg alg,
					  int idx,
					  size_t key_len,
					  const u8 *key_data,
					  size_t seq_len, const u8 *seq)
{
	struct ieee80211_key *key;
	int i, j;

	BUG_ON(idx < 0 || idx >= NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS);

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
	switch (alg) {
	case ALG_WEP:
		key->conf.iv_len = WEP_IV_LEN;
		key->conf.icv_len = WEP_ICV_LEN;
		break;
	case ALG_TKIP:
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
	case ALG_CCMP:
		key->conf.iv_len = CCMP_HDR_LEN;
		key->conf.icv_len = CCMP_MIC_LEN;
		if (seq) {
			for (i = 0; i < NUM_RX_DATA_QUEUES + 1; i++)
				for (j = 0; j < CCMP_PN_LEN; j++)
					key->u.ccmp.rx_pn[i][j] =
						seq[CCMP_PN_LEN - j - 1];
		}
		break;
	case ALG_AES_CMAC:
		key->conf.iv_len = 0;
		key->conf.icv_len = sizeof(struct ieee80211_mmie);
		if (seq)
			for (j = 0; j < 6; j++)
				key->u.aes_cmac.rx_pn[j] = seq[6 - j - 1];
		break;
	}
	memcpy(key->conf.key, key_data, key_len);
	INIT_LIST_HEAD(&key->list);

	if (alg == ALG_CCMP) {
		/*
		 * Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.ccmp.tfm = ieee80211_aes_key_setup_encrypt(key_data);
		if (!key->u.ccmp.tfm) {
			kfree(key);
			return NULL;
		}
	}

	if (alg == ALG_AES_CMAC) {
		/*
		 * Initialize AES key state here as an optimization so that
		 * it does not need to be initialized for every packet.
		 */
		key->u.aes_cmac.tfm =
			ieee80211_aes_cmac_key_setup(key_data);
		if (!key->u.aes_cmac.tfm) {
			kfree(key);
			return NULL;
		}
	}

	return key;
}

static void __ieee80211_key_destroy(struct ieee80211_key *key)
{
	if (!key)
		return;

	if (key->local)
		ieee80211_key_disable_hw_accel(key);

	if (key->conf.alg == ALG_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	if (key->conf.alg == ALG_AES_CMAC)
		ieee80211_aes_cmac_key_free(key->u.aes_cmac.tfm);
	if (key->local)
		ieee80211_debugfs_key_remove(key);

	kfree(key);
}

void ieee80211_key_link(struct ieee80211_key *key,
			struct ieee80211_sub_if_data *sdata,
			struct sta_info *sta)
{
	struct ieee80211_key *old_key;
	int idx;

	BUG_ON(!sdata);
	BUG_ON(!key);

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

		/*
		 * This key is for a specific sta interface,
		 * inform the driver that it should try to store
		 * this key as pairwise key.
		 */
		key->conf.flags |= IEEE80211_KEY_FLAG_PAIRWISE;
	} else {
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			struct sta_info *ap;

			/*
			 * We're getting a sta pointer in,
			 * so must be under RCU read lock.
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

	if (sta)
		old_key = sta->key;
	else
		old_key = sdata->keys[idx];

	__ieee80211_key_replace(sdata, sta, old_key, key);
	__ieee80211_key_destroy(old_key);

	ieee80211_debugfs_key_add(key);

	ieee80211_key_enable_hw_accel(key);

	mutex_unlock(&sdata->local->key_mtx);
}

static void __ieee80211_key_free(struct ieee80211_key *key)
{
	/*
	 * Replace key with nothingness if it was ever used.
	 */
	if (key->sdata)
		__ieee80211_key_replace(key->sdata, key->sta,
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

	ieee80211_debugfs_key_remove_default(sdata);
	ieee80211_debugfs_key_remove_mgmt_default(sdata);

	list_for_each_entry_safe(key, tmp, &sdata->key_list, list)
		__ieee80211_key_free(key);

	mutex_unlock(&sdata->local->key_mtx);
}
