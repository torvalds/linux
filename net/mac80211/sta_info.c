/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "ieee80211_rate.h"
#include "sta_info.h"
#include "debugfs_key.h"
#include "debugfs_sta.h"

/* Caller must hold local->sta_lock */
static void sta_info_hash_add(struct ieee80211_local *local,
			      struct sta_info *sta)
{
	sta->hnext = local->sta_hash[STA_HASH(sta->addr)];
	local->sta_hash[STA_HASH(sta->addr)] = sta;
}


/* Caller must hold local->sta_lock */
static void sta_info_hash_del(struct ieee80211_local *local,
			      struct sta_info *sta)
{
	struct sta_info *s;

	s = local->sta_hash[STA_HASH(sta->addr)];
	if (!s)
		return;
	if (memcmp(s->addr, sta->addr, ETH_ALEN) == 0) {
		local->sta_hash[STA_HASH(sta->addr)] = s->hnext;
		return;
	}

	while (s->hnext && memcmp(s->hnext->addr, sta->addr, ETH_ALEN) != 0)
		s = s->hnext;
	if (s->hnext)
		s->hnext = s->hnext->hnext;
	else
		printk(KERN_ERR "%s: could not remove STA " MAC_FMT " from "
		       "hash table\n", local->mdev->name, MAC_ARG(sta->addr));
}

static inline void __sta_info_get(struct sta_info *sta)
{
	kref_get(&sta->kref);
}

struct sta_info *sta_info_get(struct ieee80211_local *local, u8 *addr)
{
	struct sta_info *sta;

	spin_lock_bh(&local->sta_lock);
	sta = local->sta_hash[STA_HASH(addr)];
	while (sta) {
		if (memcmp(sta->addr, addr, ETH_ALEN) == 0) {
			__sta_info_get(sta);
			break;
		}
		sta = sta->hnext;
	}
	spin_unlock_bh(&local->sta_lock);

	return sta;
}
EXPORT_SYMBOL(sta_info_get);

int sta_info_min_txrate_get(struct ieee80211_local *local)
{
	struct sta_info *sta;
	struct ieee80211_hw_mode *mode;
	int min_txrate = 9999999;
	int i;

	spin_lock_bh(&local->sta_lock);
	mode = local->oper_hw_mode;
	for (i = 0; i < STA_HASH_SIZE; i++) {
		sta = local->sta_hash[i];
		while (sta) {
			if (sta->txrate < min_txrate)
				min_txrate = sta->txrate;
			sta = sta->hnext;
		}
	}
	spin_unlock_bh(&local->sta_lock);
	if (min_txrate == 9999999)
		min_txrate = 0;

	return mode->rates[min_txrate].rate;
}


static void sta_info_release(struct kref *kref)
{
	struct sta_info *sta = container_of(kref, struct sta_info, kref);
	struct ieee80211_local *local = sta->local;
	struct sk_buff *skb;

	/* free sta structure; it has already been removed from
	 * hash table etc. external structures. Make sure that all
	 * buffered frames are release (one might have been added
	 * after sta_info_free() was called). */
	while ((skb = skb_dequeue(&sta->ps_tx_buf)) != NULL) {
		local->total_ps_buffered--;
		dev_kfree_skb_any(skb);
	}
	while ((skb = skb_dequeue(&sta->tx_filtered)) != NULL) {
		dev_kfree_skb_any(skb);
	}
	rate_control_free_sta(sta->rate_ctrl, sta->rate_ctrl_priv);
	rate_control_put(sta->rate_ctrl);
	if (sta->key)
		ieee80211_debugfs_key_sta_del(sta->key, sta);
	kfree(sta);
}


void sta_info_put(struct sta_info *sta)
{
	kref_put(&sta->kref, sta_info_release);
}
EXPORT_SYMBOL(sta_info_put);


struct sta_info * sta_info_add(struct ieee80211_local *local,
			       struct net_device *dev, u8 *addr, gfp_t gfp)
{
	struct sta_info *sta;

	sta = kzalloc(sizeof(*sta), gfp);
	if (!sta)
		return NULL;

	kref_init(&sta->kref);

	sta->rate_ctrl = rate_control_get(local->rate_ctrl);
	sta->rate_ctrl_priv = rate_control_alloc_sta(sta->rate_ctrl, gfp);
	if (!sta->rate_ctrl_priv) {
		rate_control_put(sta->rate_ctrl);
		kref_put(&sta->kref, sta_info_release);
		kfree(sta);
		return NULL;
	}

	memcpy(sta->addr, addr, ETH_ALEN);
	sta->local = local;
	sta->dev = dev;
	skb_queue_head_init(&sta->ps_tx_buf);
	skb_queue_head_init(&sta->tx_filtered);
	__sta_info_get(sta);	/* sta used by caller, decremented by
				 * sta_info_put() */
	spin_lock_bh(&local->sta_lock);
	list_add(&sta->list, &local->sta_list);
	local->num_sta++;
	sta_info_hash_add(local, sta);
	spin_unlock_bh(&local->sta_lock);
	if (local->ops->sta_table_notification)
		local->ops->sta_table_notification(local_to_hw(local),
						  local->num_sta);
	sta->key_idx_compression = HW_KEY_IDX_INVALID;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: Added STA " MAC_FMT "\n",
	       local->mdev->name, MAC_ARG(addr));
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

#ifdef CONFIG_MAC80211_DEBUGFS
	if (!in_interrupt()) {
		sta->debugfs_registered = 1;
		ieee80211_sta_debugfs_add(sta);
		rate_control_add_sta_debugfs(sta);
	} else {
		/* debugfs entry adding might sleep, so schedule process
		 * context task for adding entry for STAs that do not yet
		 * have one. */
		queue_work(local->hw.workqueue, &local->sta_debugfs_add);
	}
#endif

	return sta;
}

static void finish_sta_info_free(struct ieee80211_local *local,
				 struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: Removed STA " MAC_FMT "\n",
	       local->mdev->name, MAC_ARG(sta->addr));
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

	if (sta->key) {
		ieee80211_debugfs_key_remove(sta->key);
		ieee80211_key_free(sta->key);
		sta->key = NULL;
	}

	rate_control_remove_sta_debugfs(sta);
	ieee80211_sta_debugfs_remove(sta);

	sta_info_put(sta);
}

static void sta_info_remove(struct sta_info *sta)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata;

	sta_info_hash_del(local, sta);
	list_del(&sta->list);
	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);
	if (sta->flags & WLAN_STA_PS) {
		sta->flags &= ~WLAN_STA_PS;
		if (sdata->bss)
			atomic_dec(&sdata->bss->num_sta_ps);
	}
	local->num_sta--;
	sta_info_remove_aid_ptr(sta);
}

void sta_info_free(struct sta_info *sta, int locked)
{
	struct sk_buff *skb;
	struct ieee80211_local *local = sta->local;

	if (!locked) {
		spin_lock_bh(&local->sta_lock);
		sta_info_remove(sta);
		spin_unlock_bh(&local->sta_lock);
	} else {
		sta_info_remove(sta);
	}
	if (local->ops->sta_table_notification)
		local->ops->sta_table_notification(local_to_hw(local),
						  local->num_sta);

	while ((skb = skb_dequeue(&sta->ps_tx_buf)) != NULL) {
		local->total_ps_buffered--;
		dev_kfree_skb_any(skb);
	}
	while ((skb = skb_dequeue(&sta->tx_filtered)) != NULL) {
		dev_kfree_skb_any(skb);
	}

	if (sta->key) {
		if (local->ops->set_key) {
			struct ieee80211_key_conf *key;
			key = ieee80211_key_data2conf(local, sta->key);
			if (key) {
				local->ops->set_key(local_to_hw(local),
						   DISABLE_KEY,
						   sta->addr, key, sta->aid);
				kfree(key);
			}
		}
	} else if (sta->key_idx_compression != HW_KEY_IDX_INVALID) {
		struct ieee80211_key_conf conf;
		memset(&conf, 0, sizeof(conf));
		conf.hw_key_idx = sta->key_idx_compression;
		conf.alg = ALG_NULL;
		conf.flags |= IEEE80211_KEY_FORCE_SW_ENCRYPT;
		local->ops->set_key(local_to_hw(local), DISABLE_KEY,
				   sta->addr, &conf, sta->aid);
		sta->key_idx_compression = HW_KEY_IDX_INVALID;
	}

#ifdef CONFIG_MAC80211_DEBUGFS
	if (in_atomic()) {
		list_add(&sta->list, &local->deleted_sta_list);
		queue_work(local->hw.workqueue, &local->sta_debugfs_add);
	} else
#endif
		finish_sta_info_free(local, sta);
}


static inline int sta_info_buffer_expired(struct ieee80211_local *local,
					  struct sta_info *sta,
					  struct sk_buff *skb)
{
	struct ieee80211_tx_packet_data *pkt_data;
	int timeout;

	if (!skb)
		return 0;

	pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;

	/* Timeout: (2 * listen_interval * beacon_int * 1024 / 1000000) sec */
	timeout = (sta->listen_interval * local->hw.conf.beacon_int * 32 /
		   15625) * HZ;
	if (timeout < STA_TX_BUFFER_EXPIRE)
		timeout = STA_TX_BUFFER_EXPIRE;
	return time_after(jiffies, pkt_data->jiffies + timeout);
}


static void sta_info_cleanup_expire_buffered(struct ieee80211_local *local,
					     struct sta_info *sta)
{
	unsigned long flags;
	struct sk_buff *skb;

	if (skb_queue_empty(&sta->ps_tx_buf))
		return;

	for (;;) {
		spin_lock_irqsave(&sta->ps_tx_buf.lock, flags);
		skb = skb_peek(&sta->ps_tx_buf);
		if (sta_info_buffer_expired(local, sta, skb)) {
			skb = __skb_dequeue(&sta->ps_tx_buf);
			if (skb_queue_empty(&sta->ps_tx_buf))
				sta->flags &= ~WLAN_STA_TIM;
		} else
			skb = NULL;
		spin_unlock_irqrestore(&sta->ps_tx_buf.lock, flags);

		if (skb) {
			local->total_ps_buffered--;
			printk(KERN_DEBUG "Buffered frame expired (STA "
			       MAC_FMT ")\n", MAC_ARG(sta->addr));
			dev_kfree_skb(skb);
		} else
			break;
	}
}


static void sta_info_cleanup(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sta_info *sta;

	spin_lock_bh(&local->sta_lock);
	list_for_each_entry(sta, &local->sta_list, list) {
		__sta_info_get(sta);
		sta_info_cleanup_expire_buffered(local, sta);
		sta_info_put(sta);
	}
	spin_unlock_bh(&local->sta_lock);

	local->sta_cleanup.expires = jiffies + STA_INFO_CLEANUP_INTERVAL;
	add_timer(&local->sta_cleanup);
}

#ifdef CONFIG_MAC80211_DEBUGFS
static void sta_info_debugfs_add_task(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, sta_debugfs_add);
	struct sta_info *sta, *tmp;

	while (1) {
		spin_lock_bh(&local->sta_lock);
		if (!list_empty(&local->deleted_sta_list)) {
			sta = list_entry(local->deleted_sta_list.next,
					 struct sta_info, list);
			list_del(local->deleted_sta_list.next);
		} else
			sta = NULL;
		spin_unlock_bh(&local->sta_lock);
		if (!sta)
			break;
		finish_sta_info_free(local, sta);
	}

	while (1) {
		sta = NULL;
		spin_lock_bh(&local->sta_lock);
		list_for_each_entry(tmp, &local->sta_list, list) {
			if (!tmp->debugfs_registered) {
				sta = tmp;
				__sta_info_get(sta);
				break;
			}
		}
		spin_unlock_bh(&local->sta_lock);

		if (!sta)
			break;

		sta->debugfs_registered = 1;
		ieee80211_sta_debugfs_add(sta);
		rate_control_add_sta_debugfs(sta);
		sta_info_put(sta);
	}
}
#endif

void sta_info_init(struct ieee80211_local *local)
{
	spin_lock_init(&local->sta_lock);
	INIT_LIST_HEAD(&local->sta_list);
	INIT_LIST_HEAD(&local->deleted_sta_list);

	init_timer(&local->sta_cleanup);
	local->sta_cleanup.expires = jiffies + STA_INFO_CLEANUP_INTERVAL;
	local->sta_cleanup.data = (unsigned long) local;
	local->sta_cleanup.function = sta_info_cleanup;

#ifdef CONFIG_MAC80211_DEBUGFS
	INIT_WORK(&local->sta_debugfs_add, sta_info_debugfs_add_task);
#endif
}

int sta_info_start(struct ieee80211_local *local)
{
	add_timer(&local->sta_cleanup);
	return 0;
}

void sta_info_stop(struct ieee80211_local *local)
{
	struct sta_info *sta, *tmp;

	del_timer(&local->sta_cleanup);

	list_for_each_entry_safe(sta, tmp, &local->sta_list, list) {
		/* sta_info_free must be called with 0 as the last
		 * parameter to ensure all debugfs sta entries are
		 * unregistered. We don't need locking at this
		 * point. */
		sta_info_free(sta, 0);
	}
}

void sta_info_remove_aid_ptr(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata;

	if (sta->aid <= 0)
		return;

	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);

	if (sdata->local->ops->set_tim)
		sdata->local->ops->set_tim(local_to_hw(sdata->local),
					  sta->aid, 0);
	if (sdata->bss)
		__bss_tim_clear(sdata->bss, sta->aid);
}


/**
 * sta_info_flush - flush matching STA entries from the STA table
 * @local: local interface data
 * @dev: matching rule for the net device (sta->dev) or %NULL to match all STAs
 */
void sta_info_flush(struct ieee80211_local *local, struct net_device *dev)
{
	struct sta_info *sta, *tmp;

	spin_lock_bh(&local->sta_lock);
	list_for_each_entry_safe(sta, tmp, &local->sta_list, list)
		if (!dev || dev == sta->dev)
			sta_info_free(sta, 1);
	spin_unlock_bh(&local->sta_lock);
}
