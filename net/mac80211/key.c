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
 * All key operations are protected internally so you can call them at
 * any time.
 *
 * Within mac80211, key references are, just as STA structure references,
 * protected by RCU. Note, however, that some things are unprotected,
 * namely the key->sta dereferences within the hardware acceleration
 * functions. This means that sta_info_destroy() must flush the key todo
 * list.
 *
 * All the direct key list manipulation functions must not sleep because
 * they can operate on STA info structs that are protected by RCU.
 */

static const u8 bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 zero_addr[ETH_ALEN];

/* key mutex: used to synchronise todo runners */
static DEFINE_MUTEX(key_mutex);
static DEFINE_SPINLOCK(todo_lock);
static LIST_HEAD(todo_list);

static void key_todo(struct work_struct *work)
{
	ieee80211_key_todo();
}

static DECLARE_WORK(todo_work, key_todo);

/**
 * add_todo - add todo item for a key
 *
 * @key: key to add to do item for
 * @flag: todo flag(s)
 */
static void add_todo(struct ieee80211_key *key, u32 flag)
{
	if (!key)
		return;

	spin_lock(&todo_lock);
	key->flags |= flag;
	/*
	 * Remove again if already on the list so that we move it to the end.
	 */
	if (!list_empty(&key->todo))
		list_del(&key->todo);
	list_add_tail(&key->todo, &todo_list);
	schedule_work(&todo_work);
	spin_unlock(&todo_lock);
}

/**
 * ieee80211_key_lock - lock the mac80211 key operation lock
 *
 * This locks the (global) mac80211 key operation lock, all
 * key operations must be done under this lock.
 */
static void ieee80211_key_lock(void)
{
	mutex_lock(&key_mutex);
}

/**
 * ieee80211_key_unlock - unlock the mac80211 key operation lock
 */
static void ieee80211_key_unlock(void)
{
	mutex_unlock(&key_mutex);
}

static void assert_key_lock(void)
{
	WARN_ON(!mutex_is_locked(&key_mutex));
}

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

	assert_key_lock();
	might_sleep();

	if (!key->local->ops->set_key)
		return;

	addr = get_mac_for_key(key);

	ret = key->local->ops->set_key(local_to_hw(key->local), SET_KEY,
				       key->sdata->dev->dev_addr, addr,
				       &key->conf);

	if (!ret) {
		spin_lock(&todo_lock);
		key->flags |= KEY_FLAG_UPLOADED_TO_HARDWARE;
		spin_unlock(&todo_lock);
	}

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

	assert_key_lock();
	might_sleep();

	if (!key || !key->local->ops->set_key)
		return;

	spin_lock(&todo_lock);
	if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE)) {
		spin_unlock(&todo_lock);
		return;
	}
	spin_unlock(&todo_lock);

	addr = get_mac_for_key(key);

	ret = key->local->ops->set_key(local_to_hw(key->local), DISABLE_KEY,
				       key->sdata->dev->dev_addr, addr,
				       &key->conf);

	if (ret)
		printk(KERN_ERR "mac80211-%s: failed to remove key "
		       "(%d, %s) from hardware (%d)\n",
		       wiphy_name(key->local->hw.wiphy),
		       key->conf.keyidx, print_mac(mac, addr), ret);

	spin_lock(&todo_lock);
	key->flags &= ~KEY_FLAG_UPLOADED_TO_HARDWARE;
	spin_unlock(&todo_lock);
}

static void __ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata,
					int idx)
{
	struct ieee80211_key *key = NULL;

	if (idx >= 0 && idx < NUM_DEFAULT_KEYS)
		key = sdata->keys[idx];

	rcu_assign_pointer(sdata->default_key, key);

	if (key)
		add_todo(key, KEY_FLAG_TODO_DEFKEY);
}

void ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata, int idx)
{
	unsigned long flags;

	spin_lock_irqsave(&sdata->local->key_lock, flags);
	__ieee80211_set_default_key(sdata, idx);
	spin_unlock_irqrestore(&sdata->local->key_lock, flags);
}


static void __ieee80211_key_replace(struct ieee80211_sub_if_data *sdata,
				    struct sta_info *sta,
				    struct ieee80211_key *old,
				    struct ieee80211_key *new)
{
	int idx, defkey;

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

		if (defkey && !new)
			__ieee80211_set_default_key(sdata, -1);

		rcu_assign_pointer(sdata->keys[idx], new);
		if (defkey && new)
			__ieee80211_set_default_key(sdata, new->conf.keyidx);
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
	INIT_LIST_HEAD(&key->todo);

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

	return key;
}

void ieee80211_key_link(struct ieee80211_key *key,
			struct ieee80211_sub_if_data *sdata,
			struct sta_info *sta)
{
	struct ieee80211_key *old_key;
	unsigned long flags;
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
		if (sdata->vif.type == IEEE80211_IF_TYPE_STA) {
			struct sta_info *ap;

			/*
			 * We're getting a sta pointer in,
			 * so must be under RCU read lock.
			 */

			/* same here, the AP could be using QoS */
			ap = sta_info_get(key->local, key->sdata->u.sta.bssid);
			if (ap) {
				if (test_sta_flags(ap, WLAN_STA_WME))
					key->conf.flags |=
						IEEE80211_KEY_FLAG_WMM_STA;
			}
		}
	}

	spin_lock_irqsave(&sdata->local->key_lock, flags);

	if (sta)
		old_key = sta->key;
	else
		old_key = sdata->keys[idx];

	__ieee80211_key_replace(sdata, sta, old_key, key);

	spin_unlock_irqrestore(&sdata->local->key_lock, flags);

	/* free old key later */
	add_todo(old_key, KEY_FLAG_TODO_DELETE);

	add_todo(key, KEY_FLAG_TODO_ADD_DEBUGFS);
	if (netif_running(sdata->dev))
		add_todo(key, KEY_FLAG_TODO_HWACCEL_ADD);
}

static void __ieee80211_key_free(struct ieee80211_key *key)
{
	/*
	 * Replace key with nothingness if it was ever used.
	 */
	if (key->sdata)
		__ieee80211_key_replace(key->sdata, key->sta,
					key, NULL);

	add_todo(key, KEY_FLAG_TODO_DELETE);
}

void ieee80211_key_free(struct ieee80211_key *key)
{
	unsigned long flags;

	if (!key)
		return;

	if (!key->sdata) {
		/* The key has not been linked yet, simply free it
		 * and don't Oops */
		if (key->conf.alg == ALG_CCMP)
			ieee80211_aes_key_free(key->u.ccmp.tfm);
		kfree(key);
		return;
	}

	spin_lock_irqsave(&key->sdata->local->key_lock, flags);
	__ieee80211_key_free(key);
	spin_unlock_irqrestore(&key->sdata->local->key_lock, flags);
}

/*
 * To be safe against concurrent manipulations of the list (which shouldn't
 * actually happen) we need to hold the spinlock. But under the spinlock we
 * can't actually do much, so we defer processing to the todo list. Then run
 * the todo list to be sure the operation and possibly previously pending
 * operations are completed.
 */
static void ieee80211_todo_for_each_key(struct ieee80211_sub_if_data *sdata,
					u32 todo_flags)
{
	struct ieee80211_key *key;
	unsigned long flags;

	might_sleep();

	spin_lock_irqsave(&sdata->local->key_lock, flags);
	list_for_each_entry(key, &sdata->key_list, list)
		add_todo(key, todo_flags);
	spin_unlock_irqrestore(&sdata->local->key_lock, flags);

	ieee80211_key_todo();
}

void ieee80211_enable_keys(struct ieee80211_sub_if_data *sdata)
{
	ASSERT_RTNL();

	if (WARN_ON(!netif_running(sdata->dev)))
		return;

	ieee80211_todo_for_each_key(sdata, KEY_FLAG_TODO_HWACCEL_ADD);
}

void ieee80211_disable_keys(struct ieee80211_sub_if_data *sdata)
{
	ASSERT_RTNL();

	ieee80211_todo_for_each_key(sdata, KEY_FLAG_TODO_HWACCEL_REMOVE);
}

static void __ieee80211_key_destroy(struct ieee80211_key *key)
{
	if (!key)
		return;

	ieee80211_key_disable_hw_accel(key);

	if (key->conf.alg == ALG_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	ieee80211_debugfs_key_remove(key);

	kfree(key);
}

static void __ieee80211_key_todo(void)
{
	struct ieee80211_key *key;
	bool work_done;
	u32 todoflags;

	/*
	 * NB: sta_info_destroy relies on this!
	 */
	synchronize_rcu();

	spin_lock(&todo_lock);
	while (!list_empty(&todo_list)) {
		key = list_first_entry(&todo_list, struct ieee80211_key, todo);
		list_del_init(&key->todo);
		todoflags = key->flags & (KEY_FLAG_TODO_ADD_DEBUGFS |
					  KEY_FLAG_TODO_DEFKEY |
					  KEY_FLAG_TODO_HWACCEL_ADD |
					  KEY_FLAG_TODO_HWACCEL_REMOVE |
					  KEY_FLAG_TODO_DELETE);
		key->flags &= ~todoflags;
		spin_unlock(&todo_lock);

		work_done = false;

		if (todoflags & KEY_FLAG_TODO_ADD_DEBUGFS) {
			ieee80211_debugfs_key_add(key);
			work_done = true;
		}
		if (todoflags & KEY_FLAG_TODO_DEFKEY) {
			ieee80211_debugfs_key_remove_default(key->sdata);
			ieee80211_debugfs_key_add_default(key->sdata);
			work_done = true;
		}
		if (todoflags & KEY_FLAG_TODO_HWACCEL_ADD) {
			ieee80211_key_enable_hw_accel(key);
			work_done = true;
		}
		if (todoflags & KEY_FLAG_TODO_HWACCEL_REMOVE) {
			ieee80211_key_disable_hw_accel(key);
			work_done = true;
		}
		if (todoflags & KEY_FLAG_TODO_DELETE) {
			__ieee80211_key_destroy(key);
			work_done = true;
		}

		WARN_ON(!work_done);

		spin_lock(&todo_lock);
	}
	spin_unlock(&todo_lock);
}

void ieee80211_key_todo(void)
{
	ieee80211_key_lock();
	__ieee80211_key_todo();
	ieee80211_key_unlock();
}

void ieee80211_free_keys(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_key *key, *tmp;
	unsigned long flags;

	ieee80211_key_lock();

	ieee80211_debugfs_key_remove_default(sdata);

	spin_lock_irqsave(&sdata->local->key_lock, flags);
	list_for_each_entry_safe(key, tmp, &sdata->key_list, list)
		__ieee80211_key_free(key);
	spin_unlock_irqrestore(&sdata->local->key_lock, flags);

	__ieee80211_key_todo();

	ieee80211_key_unlock();
}
