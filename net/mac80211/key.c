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
#include <linux/export.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>
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

static void increment_tailroom_need_count(struct ieee80211_sub_if_data *sdata)
{
	/*
	 * When this count is zero, SKB resizing for allocating tailroom
	 * for IV or MMIC is skipped. But, this check has created two race
	 * cases in xmit path while transiting from zero count to one:
	 *
	 * 1. SKB resize was skipped because no key was added but just before
	 * the xmit key is added and SW encryption kicks off.
	 *
	 * 2. SKB resize was skipped because all the keys were hw planted but
	 * just before xmit one of the key is deleted and SW encryption kicks
	 * off.
	 *
	 * In both the above case SW encryption will find not enough space for
	 * tailroom and exits with WARN_ON. (See WARN_ONs at wpa.c)
	 *
	 * Solution has been explained at
	 * http://mid.gmane.org/1308590980.4322.19.camel@jlt3.sipsolutions.net
	 */

	if (!sdata->crypto_tx_tailroom_needed_cnt++) {
		/*
		 * Flush all XMIT packets currently using HW encryption or no
		 * encryption at all if the count transition is from 0 -> 1.
		 */
		synchronize_net();
	}
}

static int ieee80211_key_enable_hw_accel(struct ieee80211_key *key)
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	int ret;

	might_sleep();

	if (!key->local->ops->set_key)
		goto out_unsupported;

	assert_key_lock(key->local);

	sta = key->sta;

	/*
	 * If this is a per-STA GTK, check if it
	 * is supported; if not, return.
	 */
	if (sta && !(key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE) &&
	    !(key->local->hw.flags & IEEE80211_HW_SUPPORTS_PER_STA_GTK))
		goto out_unsupported;

	if (sta && !sta->uploaded)
		goto out_unsupported;

	sdata = key->sdata;
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		/*
		 * The driver doesn't know anything about VLAN interfaces.
		 * Hence, don't send GTKs for VLAN interfaces to the driver.
		 */
		if (!(key->conf.flags & IEEE80211_KEY_FLAG_PAIRWISE))
			goto out_unsupported;
	}

	ret = drv_set_key(key->local, SET_KEY, sdata,
			  sta ? &sta->sta : NULL, &key->conf);

	if (!ret) {
		key->flags |= KEY_FLAG_UPLOADED_TO_HARDWARE;

		if (!((key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_MMIC) ||
		      (key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV) ||
		      (key->conf.flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE)))
			sdata->crypto_tx_tailroom_needed_cnt--;

		WARN_ON((key->conf.flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE) &&
			(key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV));

		return 0;
	}

	if (ret != -ENOSPC && ret != -EOPNOTSUPP)
		sdata_err(sdata,
			  "failed to set key (%d, %pM) to hardware (%d)\n",
			  key->conf.keyidx,
			  sta ? sta->sta.addr : bcast_addr, ret);

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
	struct sta_info *sta;
	int ret;

	might_sleep();

	if (!key || !key->local->ops->set_key)
		return;

	assert_key_lock(key->local);

	if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		return;

	sta = key->sta;
	sdata = key->sdata;

	if (!((key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_MMIC) ||
	      (key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV) ||
	      (key->conf.flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE)))
		increment_tailroom_need_count(sdata);

	ret = drv_set_key(key->local, DISABLE_KEY, sdata,
			  sta ? &sta->sta : NULL, &key->conf);

	if (ret)
		sdata_err(sdata,
			  "failed to remove key (%d, %pM) from hardware (%d)\n",
			  key->conf.keyidx,
			  sta ? sta->sta.addr : bcast_addr, ret);

	key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
}

static void __ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata,
					int idx, bool uni, bool multi)
{
	struct ieee80211_key *key = NULL;

	assert_key_lock(sdata->local);

	if (idx >= 0 && idx < NUM_DEFAULT_KEYS)
		key = key_mtx_dereference(sdata->local, sdata->keys[idx]);

	if (uni) {
		rcu_assign_pointer(sdata->default_unicast_key, key);
		drv_set_default_unicast_key(sdata->local, sdata, idx);
	}

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
		key = key_mtx_dereference(sdata->local, sdata->keys[idx]);

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
		list_add_tail(&new->list, &sdata->key_list);

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

		defunikey = old &&
			old == key_mtx_dereference(sdata->local,
						sdata->default_unicast_key);
		defmultikey = old &&
			old == key_mtx_dereference(sdata->local,
						sdata->default_multicast_key);
		defmgmtkey = old &&
			old == key_mtx_dereference(sdata->local,
						sdata->default_mgmt_key);

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
			for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
				key->u.tkip.rx[i].iv32 =
					get_unaligned_le32(&seq[2]);
				key->u.tkip.rx[i].iv16 =
					get_unaligned_le16(seq);
			}
		}
		spin_lock_init(&key->u.tkip.txlock);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key->conf.iv_len = CCMP_HDR_LEN;
		key->conf.icv_len = CCMP_MIC_LEN;
		if (seq) {
			for (i = 0; i < IEEE80211_NUM_TIDS + 1; i++)
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
			for (j = 0; j < CMAC_PN_LEN; j++)
				key->u.aes_cmac.rx_pn[j] =
					seq[CMAC_PN_LEN - j - 1];
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
	synchronize_net();

	if (key->local)
		ieee80211_key_disable_hw_accel(key);

	if (key->conf.cipher == WLAN_CIPHER_SUITE_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	if (key->conf.cipher == WLAN_CIPHER_SUITE_AES_CMAC)
		ieee80211_aes_cmac_key_free(key->u.aes_cmac.tfm);
	if (key->local) {
		ieee80211_debugfs_key_remove(key);
		key->sdata->crypto_tx_tailroom_needed_cnt--;
	}

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

	mutex_lock(&sdata->local->key_mtx);

	if (sta && pairwise)
		old_key = key_mtx_dereference(sdata->local, sta->ptk);
	else if (sta)
		old_key = key_mtx_dereference(sdata->local, sta->gtk[idx]);
	else
		old_key = key_mtx_dereference(sdata->local, sdata->keys[idx]);

	increment_tailroom_need_count(sdata);

	__ieee80211_key_replace(sdata, sta, pairwise, old_key, key);
	__ieee80211_key_destroy(old_key);

	ieee80211_debugfs_key_add(key);

	ret = ieee80211_key_enable_hw_accel(key);

	mutex_unlock(&sdata->local->key_mtx);

	return ret;
}

void __ieee80211_key_free(struct ieee80211_key *key)
{
	if (!key)
		return;

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

	sdata->crypto_tx_tailroom_needed_cnt = 0;

	list_for_each_entry(key, &sdata->key_list, list) {
		increment_tailroom_need_count(sdata);
		ieee80211_key_enable_hw_accel(key);
	}

	mutex_unlock(&sdata->local->key_mtx);
}

void ieee80211_iter_keys(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 void (*iter)(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct ieee80211_key_conf *key,
				      void *data),
			 void *iter_data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_key *key;
	struct ieee80211_sub_if_data *sdata;

	ASSERT_RTNL();

	mutex_lock(&local->key_mtx);
	if (vif) {
		sdata = vif_to_sdata(vif);
		list_for_each_entry(key, &sdata->key_list, list)
			iter(hw, &sdata->vif,
			     key->sta ? &key->sta->sta : NULL,
			     &key->conf, iter_data);
	} else {
		list_for_each_entry(sdata, &local->interfaces, list)
			list_for_each_entry(key, &sdata->key_list, list)
				iter(hw, &sdata->vif,
				     key->sta ? &key->sta->sta : NULL,
				     &key->conf, iter_data);
	}
	mutex_unlock(&local->key_mtx);
}
EXPORT_SYMBOL(ieee80211_iter_keys);

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


void ieee80211_gtk_rekey_notify(struct ieee80211_vif *vif, const u8 *bssid,
				const u8 *replay_ctr, gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	trace_api_gtk_rekey_notify(sdata, bssid, replay_ctr);

	cfg80211_gtk_rekey_notify(sdata->dev, bssid, replay_ctr, gfp);
}
EXPORT_SYMBOL_GPL(ieee80211_gtk_rekey_notify);

void ieee80211_get_key_tx_seq(struct ieee80211_key_conf *keyconf,
			      struct ieee80211_key_seq *seq)
{
	struct ieee80211_key *key;
	u64 pn64;

	if (WARN_ON(!(keyconf->flags & IEEE80211_KEY_FLAG_GENERATE_IV)))
		return;

	key = container_of(keyconf, struct ieee80211_key, conf);

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		seq->tkip.iv32 = key->u.tkip.tx.iv32;
		seq->tkip.iv16 = key->u.tkip.tx.iv16;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		pn64 = atomic64_read(&key->u.ccmp.tx_pn);
		seq->ccmp.pn[5] = pn64;
		seq->ccmp.pn[4] = pn64 >> 8;
		seq->ccmp.pn[3] = pn64 >> 16;
		seq->ccmp.pn[2] = pn64 >> 24;
		seq->ccmp.pn[1] = pn64 >> 32;
		seq->ccmp.pn[0] = pn64 >> 40;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		pn64 = atomic64_read(&key->u.aes_cmac.tx_pn);
		seq->ccmp.pn[5] = pn64;
		seq->ccmp.pn[4] = pn64 >> 8;
		seq->ccmp.pn[3] = pn64 >> 16;
		seq->ccmp.pn[2] = pn64 >> 24;
		seq->ccmp.pn[1] = pn64 >> 32;
		seq->ccmp.pn[0] = pn64 >> 40;
		break;
	default:
		WARN_ON(1);
	}
}
EXPORT_SYMBOL(ieee80211_get_key_tx_seq);

void ieee80211_get_key_rx_seq(struct ieee80211_key_conf *keyconf,
			      int tid, struct ieee80211_key_seq *seq)
{
	struct ieee80211_key *key;
	const u8 *pn;

	key = container_of(keyconf, struct ieee80211_key, conf);

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (WARN_ON(tid < 0 || tid >= IEEE80211_NUM_TIDS))
			return;
		seq->tkip.iv32 = key->u.tkip.rx[tid].iv32;
		seq->tkip.iv16 = key->u.tkip.rx[tid].iv16;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (WARN_ON(tid < -1 || tid >= IEEE80211_NUM_TIDS))
			return;
		if (tid < 0)
			pn = key->u.ccmp.rx_pn[IEEE80211_NUM_TIDS];
		else
			pn = key->u.ccmp.rx_pn[tid];
		memcpy(seq->ccmp.pn, pn, CCMP_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		if (WARN_ON(tid != 0))
			return;
		pn = key->u.aes_cmac.rx_pn;
		memcpy(seq->aes_cmac.pn, pn, CMAC_PN_LEN);
		break;
	}
}
EXPORT_SYMBOL(ieee80211_get_key_rx_seq);
