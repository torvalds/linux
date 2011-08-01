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
#include <linux/timer.h>
#include <linux/rtnetlink.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "sta_info.h"
#include "debugfs_sta.h"
#include "mesh.h"

/**
 * DOC: STA information lifetime rules
 *
 * STA info structures (&struct sta_info) are managed in a hash table
 * for faster lookup and a list for iteration. They are managed using
 * RCU, i.e. access to the list and hash table is protected by RCU.
 *
 * Upon allocating a STA info structure with sta_info_alloc(), the caller
 * owns that structure. It must then insert it into the hash table using
 * either sta_info_insert() or sta_info_insert_rcu(); only in the latter
 * case (which acquires an rcu read section but must not be called from
 * within one) will the pointer still be valid after the call. Note that
 * the caller may not do much with the STA info before inserting it, in
 * particular, it may not start any mesh peer link management or add
 * encryption keys.
 *
 * When the insertion fails (sta_info_insert()) returns non-zero), the
 * structure will have been freed by sta_info_insert()!
 *
 * Station entries are added by mac80211 when you establish a link with a
 * peer. This means different things for the different type of interfaces
 * we support. For a regular station this mean we add the AP sta when we
 * receive an association response from the AP. For IBSS this occurs when
 * get to know about a peer on the same IBSS. For WDS we add the sta for
 * the peer immediately upon device open. When using AP mode we add stations
 * for each respective station upon request from userspace through nl80211.
 *
 * In order to remove a STA info structure, various sta_info_destroy_*()
 * calls are available.
 *
 * There is no concept of ownership on a STA entry, each structure is
 * owned by the global hash table/list until it is removed. All users of
 * the structure need to be RCU protected so that the structure won't be
 * freed before they are done using it.
 */

/* Caller must hold local->sta_lock */
static int sta_info_hash_del(struct ieee80211_local *local,
			     struct sta_info *sta)
{
	struct sta_info *s;

	s = rcu_dereference_protected(local->sta_hash[STA_HASH(sta->sta.addr)],
				      lockdep_is_held(&local->sta_lock));
	if (!s)
		return -ENOENT;
	if (s == sta) {
		RCU_INIT_POINTER(local->sta_hash[STA_HASH(sta->sta.addr)],
				   s->hnext);
		return 0;
	}

	while (rcu_access_pointer(s->hnext) &&
	       rcu_access_pointer(s->hnext) != sta)
		s = rcu_dereference_protected(s->hnext,
					lockdep_is_held(&local->sta_lock));
	if (rcu_access_pointer(s->hnext)) {
		RCU_INIT_POINTER(s->hnext, sta->hnext);
		return 0;
	}

	return -ENOENT;
}

/* protected by RCU */
struct sta_info *sta_info_get(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	sta = rcu_dereference_check(local->sta_hash[STA_HASH(addr)],
				    lockdep_is_held(&local->sta_lock) ||
				    lockdep_is_held(&local->sta_mtx));
	while (sta) {
		if (sta->sdata == sdata &&
		    memcmp(sta->sta.addr, addr, ETH_ALEN) == 0)
			break;
		sta = rcu_dereference_check(sta->hnext,
					    lockdep_is_held(&local->sta_lock) ||
					    lockdep_is_held(&local->sta_mtx));
	}
	return sta;
}

/*
 * Get sta info either from the specified interface
 * or from one of its vlans
 */
struct sta_info *sta_info_get_bss(struct ieee80211_sub_if_data *sdata,
				  const u8 *addr)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	sta = rcu_dereference_check(local->sta_hash[STA_HASH(addr)],
				    lockdep_is_held(&local->sta_lock) ||
				    lockdep_is_held(&local->sta_mtx));
	while (sta) {
		if ((sta->sdata == sdata ||
		     (sta->sdata->bss && sta->sdata->bss == sdata->bss)) &&
		    memcmp(sta->sta.addr, addr, ETH_ALEN) == 0)
			break;
		sta = rcu_dereference_check(sta->hnext,
					    lockdep_is_held(&local->sta_lock) ||
					    lockdep_is_held(&local->sta_mtx));
	}
	return sta;
}

struct sta_info *sta_info_get_by_idx(struct ieee80211_sub_if_data *sdata,
				     int idx)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	int i = 0;

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata)
			continue;
		if (i < idx) {
			++i;
			continue;
		}
		return sta;
	}

	return NULL;
}

/**
 * __sta_info_free - internal STA free helper
 *
 * @local: pointer to the global information
 * @sta: STA info to free
 *
 * This function must undo everything done by sta_info_alloc()
 * that may happen before sta_info_insert().
 */
static void __sta_info_free(struct ieee80211_local *local,
			    struct sta_info *sta)
{
	if (sta->rate_ctrl) {
		rate_control_free_sta(sta);
		rate_control_put(sta->rate_ctrl);
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	wiphy_debug(local->hw.wiphy, "Destroyed STA %pM\n", sta->sta.addr);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

	kfree(sta);
}

/* Caller must hold local->sta_lock */
static void sta_info_hash_add(struct ieee80211_local *local,
			      struct sta_info *sta)
{
	sta->hnext = local->sta_hash[STA_HASH(sta->sta.addr)];
	RCU_INIT_POINTER(local->sta_hash[STA_HASH(sta->sta.addr)], sta);
}

static void sta_unblock(struct work_struct *wk)
{
	struct sta_info *sta;

	sta = container_of(wk, struct sta_info, drv_unblock_wk);

	if (sta->dead)
		return;

	if (!test_sta_flags(sta, WLAN_STA_PS_STA))
		ieee80211_sta_ps_deliver_wakeup(sta);
	else if (test_and_clear_sta_flags(sta, WLAN_STA_PSPOLL)) {
		clear_sta_flags(sta, WLAN_STA_PS_DRIVER);
		ieee80211_sta_ps_deliver_poll_response(sta);
	} else
		clear_sta_flags(sta, WLAN_STA_PS_DRIVER);
}

static int sta_prepare_rate_control(struct ieee80211_local *local,
				    struct sta_info *sta, gfp_t gfp)
{
	if (local->hw.flags & IEEE80211_HW_HAS_RATE_CONTROL)
		return 0;

	sta->rate_ctrl = rate_control_get(local->rate_ctrl);
	sta->rate_ctrl_priv = rate_control_alloc_sta(sta->rate_ctrl,
						     &sta->sta, gfp);
	if (!sta->rate_ctrl_priv) {
		rate_control_put(sta->rate_ctrl);
		return -ENOMEM;
	}

	return 0;
}

struct sta_info *sta_info_alloc(struct ieee80211_sub_if_data *sdata,
				u8 *addr, gfp_t gfp)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct timespec uptime;
	int i;

	sta = kzalloc(sizeof(*sta) + local->hw.sta_data_size, gfp);
	if (!sta)
		return NULL;

	spin_lock_init(&sta->lock);
	spin_lock_init(&sta->flaglock);
	INIT_WORK(&sta->drv_unblock_wk, sta_unblock);
	INIT_WORK(&sta->ampdu_mlme.work, ieee80211_ba_session_work);
	mutex_init(&sta->ampdu_mlme.mtx);

	memcpy(sta->sta.addr, addr, ETH_ALEN);
	sta->local = local;
	sta->sdata = sdata;
	sta->last_rx = jiffies;

	do_posix_clock_monotonic_gettime(&uptime);
	sta->last_connected = uptime.tv_sec;
	ewma_init(&sta->avg_signal, 1024, 8);

	if (sta_prepare_rate_control(local, sta, gfp)) {
		kfree(sta);
		return NULL;
	}

	for (i = 0; i < STA_TID_NUM; i++) {
		/*
		 * timer_to_tid must be initialized with identity mapping
		 * to enable session_timer's data differentiation. See
		 * sta_rx_agg_session_timer_expired for usage.
		 */
		sta->timer_to_tid[i] = i;
	}
	skb_queue_head_init(&sta->ps_tx_buf);
	skb_queue_head_init(&sta->tx_filtered);

	for (i = 0; i < NUM_RX_DATA_QUEUES; i++)
		sta->last_seq_ctrl[i] = cpu_to_le16(USHRT_MAX);

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	wiphy_debug(local->hw.wiphy, "Allocated STA %pM\n", sta->sta.addr);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

#ifdef CONFIG_MAC80211_MESH
	sta->plink_state = NL80211_PLINK_LISTEN;
	init_timer(&sta->plink_timer);
#endif

	return sta;
}

static int sta_info_finish_insert(struct sta_info *sta, bool async)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct station_info sinfo;
	unsigned long flags;
	int err = 0;

	lockdep_assert_held(&local->sta_mtx);

	/* notify driver */
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		sdata = container_of(sdata->bss,
				     struct ieee80211_sub_if_data,
				     u.ap);
	err = drv_sta_add(local, sdata, &sta->sta);
	if (err) {
		if (!async)
			return err;
		printk(KERN_DEBUG "%s: failed to add IBSS STA %pM to driver (%d)"
				  " - keeping it anyway.\n",
		       sdata->name, sta->sta.addr, err);
	} else {
		sta->uploaded = true;
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (async)
			wiphy_debug(local->hw.wiphy,
				    "Finished adding IBSS STA %pM\n",
				    sta->sta.addr);
#endif
	}

	sdata = sta->sdata;

	if (!async) {
		local->num_sta++;
		local->sta_generation++;
		smp_mb();

		/* make the station visible */
		spin_lock_irqsave(&local->sta_lock, flags);
		sta_info_hash_add(local, sta);
		spin_unlock_irqrestore(&local->sta_lock, flags);
	}

	list_add(&sta->list, &local->sta_list);

	ieee80211_sta_debugfs_add(sta);
	rate_control_add_sta_debugfs(sta);

	sinfo.filled = 0;
	sinfo.generation = local->sta_generation;
	cfg80211_new_sta(sdata->dev, sta->sta.addr, &sinfo, GFP_KERNEL);


	return 0;
}

static void sta_info_finish_pending(struct ieee80211_local *local)
{
	struct sta_info *sta;
	unsigned long flags;

	spin_lock_irqsave(&local->sta_lock, flags);
	while (!list_empty(&local->sta_pending_list)) {
		sta = list_first_entry(&local->sta_pending_list,
				       struct sta_info, list);
		list_del(&sta->list);
		spin_unlock_irqrestore(&local->sta_lock, flags);

		sta_info_finish_insert(sta, true);

		spin_lock_irqsave(&local->sta_lock, flags);
	}
	spin_unlock_irqrestore(&local->sta_lock, flags);
}

static void sta_info_finish_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, sta_finish_work);

	mutex_lock(&local->sta_mtx);
	sta_info_finish_pending(local);
	mutex_unlock(&local->sta_mtx);
}

int sta_info_insert_rcu(struct sta_info *sta) __acquires(RCU)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	unsigned long flags;
	int err = 0;

	/*
	 * Can't be a WARN_ON because it can be triggered through a race:
	 * something inserts a STA (on one CPU) without holding the RTNL
	 * and another CPU turns off the net device.
	 */
	if (unlikely(!ieee80211_sdata_running(sdata))) {
		err = -ENETDOWN;
		rcu_read_lock();
		goto out_free;
	}

	if (WARN_ON(compare_ether_addr(sta->sta.addr, sdata->vif.addr) == 0 ||
		    is_multicast_ether_addr(sta->sta.addr))) {
		err = -EINVAL;
		rcu_read_lock();
		goto out_free;
	}

	/*
	 * In ad-hoc mode, we sometimes need to insert stations
	 * from tasklet context from the RX path. To avoid races,
	 * always do so in that case -- see the comment below.
	 */
	if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		spin_lock_irqsave(&local->sta_lock, flags);
		/* check if STA exists already */
		if (sta_info_get_bss(sdata, sta->sta.addr)) {
			spin_unlock_irqrestore(&local->sta_lock, flags);
			rcu_read_lock();
			err = -EEXIST;
			goto out_free;
		}

		local->num_sta++;
		local->sta_generation++;
		smp_mb();
		sta_info_hash_add(local, sta);

		list_add_tail(&sta->list, &local->sta_pending_list);

		rcu_read_lock();
		spin_unlock_irqrestore(&local->sta_lock, flags);

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		wiphy_debug(local->hw.wiphy, "Added IBSS STA %pM\n",
			    sta->sta.addr);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

		ieee80211_queue_work(&local->hw, &local->sta_finish_work);

		return 0;
	}

	/*
	 * On first glance, this will look racy, because the code
	 * below this point, which inserts a station with sleeping,
	 * unlocks the sta_lock between checking existence in the
	 * hash table and inserting into it.
	 *
	 * However, it is not racy against itself because it keeps
	 * the mutex locked. It still seems to race against the
	 * above code that atomically inserts the station... That,
	 * however, is not true because the above code can only
	 * be invoked for IBSS interfaces, and the below code will
	 * not be -- and the two do not race against each other as
	 * the hash table also keys off the interface.
	 */

	might_sleep();

	mutex_lock(&local->sta_mtx);

	spin_lock_irqsave(&local->sta_lock, flags);
	/* check if STA exists already */
	if (sta_info_get_bss(sdata, sta->sta.addr)) {
		spin_unlock_irqrestore(&local->sta_lock, flags);
		mutex_unlock(&local->sta_mtx);
		rcu_read_lock();
		err = -EEXIST;
		goto out_free;
	}

	spin_unlock_irqrestore(&local->sta_lock, flags);

	err = sta_info_finish_insert(sta, false);
	if (err) {
		mutex_unlock(&local->sta_mtx);
		rcu_read_lock();
		goto out_free;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	wiphy_debug(local->hw.wiphy, "Inserted STA %pM\n", sta->sta.addr);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

	/* move reference to rcu-protected */
	rcu_read_lock();
	mutex_unlock(&local->sta_mtx);

	if (ieee80211_vif_is_mesh(&sdata->vif))
		mesh_accept_plinks_update(sdata);

	return 0;
 out_free:
	BUG_ON(!err);
	__sta_info_free(local, sta);
	return err;
}

int sta_info_insert(struct sta_info *sta)
{
	int err = sta_info_insert_rcu(sta);

	rcu_read_unlock();

	return err;
}

static inline void __bss_tim_set(struct ieee80211_if_ap *bss, u16 aid)
{
	/*
	 * This format has been mandated by the IEEE specifications,
	 * so this line may not be changed to use the __set_bit() format.
	 */
	bss->tim[aid / 8] |= (1 << (aid % 8));
}

static inline void __bss_tim_clear(struct ieee80211_if_ap *bss, u16 aid)
{
	/*
	 * This format has been mandated by the IEEE specifications,
	 * so this line may not be changed to use the __clear_bit() format.
	 */
	bss->tim[aid / 8] &= ~(1 << (aid % 8));
}

static void __sta_info_set_tim_bit(struct ieee80211_if_ap *bss,
				   struct sta_info *sta)
{
	BUG_ON(!bss);

	__bss_tim_set(bss, sta->sta.aid);

	if (sta->local->ops->set_tim) {
		sta->local->tim_in_locked_section = true;
		drv_set_tim(sta->local, &sta->sta, true);
		sta->local->tim_in_locked_section = false;
	}
}

void sta_info_set_tim_bit(struct sta_info *sta)
{
	unsigned long flags;

	BUG_ON(!sta->sdata->bss);

	spin_lock_irqsave(&sta->local->sta_lock, flags);
	__sta_info_set_tim_bit(sta->sdata->bss, sta);
	spin_unlock_irqrestore(&sta->local->sta_lock, flags);
}

static void __sta_info_clear_tim_bit(struct ieee80211_if_ap *bss,
				     struct sta_info *sta)
{
	BUG_ON(!bss);

	__bss_tim_clear(bss, sta->sta.aid);

	if (sta->local->ops->set_tim) {
		sta->local->tim_in_locked_section = true;
		drv_set_tim(sta->local, &sta->sta, false);
		sta->local->tim_in_locked_section = false;
	}
}

void sta_info_clear_tim_bit(struct sta_info *sta)
{
	unsigned long flags;

	BUG_ON(!sta->sdata->bss);

	spin_lock_irqsave(&sta->local->sta_lock, flags);
	__sta_info_clear_tim_bit(sta->sdata->bss, sta);
	spin_unlock_irqrestore(&sta->local->sta_lock, flags);
}

static int sta_info_buffer_expired(struct sta_info *sta,
				   struct sk_buff *skb)
{
	struct ieee80211_tx_info *info;
	int timeout;

	if (!skb)
		return 0;

	info = IEEE80211_SKB_CB(skb);

	/* Timeout: (2 * listen_interval * beacon_int * 1024 / 1000000) sec */
	timeout = (sta->listen_interval *
		   sta->sdata->vif.bss_conf.beacon_int *
		   32 / 15625) * HZ;
	if (timeout < STA_TX_BUFFER_EXPIRE)
		timeout = STA_TX_BUFFER_EXPIRE;
	return time_after(jiffies, info->control.jiffies + timeout);
}


static bool sta_info_cleanup_expire_buffered(struct ieee80211_local *local,
					     struct sta_info *sta)
{
	unsigned long flags;
	struct sk_buff *skb;

	if (skb_queue_empty(&sta->ps_tx_buf))
		return false;

	for (;;) {
		spin_lock_irqsave(&sta->ps_tx_buf.lock, flags);
		skb = skb_peek(&sta->ps_tx_buf);
		if (sta_info_buffer_expired(sta, skb))
			skb = __skb_dequeue(&sta->ps_tx_buf);
		else
			skb = NULL;
		spin_unlock_irqrestore(&sta->ps_tx_buf.lock, flags);

		if (!skb)
			break;

		local->total_ps_buffered--;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "Buffered frame expired (STA %pM)\n",
		       sta->sta.addr);
#endif
		dev_kfree_skb(skb);

		if (skb_queue_empty(&sta->ps_tx_buf) &&
		    !test_sta_flags(sta, WLAN_STA_PS_DRIVER_BUF))
			sta_info_clear_tim_bit(sta);
	}

	return true;
}

static int __must_check __sta_info_destroy(struct sta_info *sta)
{
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sk_buff *skb;
	unsigned long flags;
	int ret, i;

	might_sleep();

	if (!sta)
		return -ENOENT;

	local = sta->local;
	sdata = sta->sdata;

	/*
	 * Before removing the station from the driver and
	 * rate control, it might still start new aggregation
	 * sessions -- block that to make sure the tear-down
	 * will be sufficient.
	 */
	set_sta_flags(sta, WLAN_STA_BLOCK_BA);
	ieee80211_sta_tear_down_BA_sessions(sta, true);

	spin_lock_irqsave(&local->sta_lock, flags);
	ret = sta_info_hash_del(local, sta);
	/* this might still be the pending list ... which is fine */
	if (!ret)
		list_del(&sta->list);
	spin_unlock_irqrestore(&local->sta_lock, flags);
	if (ret)
		return ret;

	mutex_lock(&local->key_mtx);
	for (i = 0; i < NUM_DEFAULT_KEYS; i++)
		__ieee80211_key_free(key_mtx_dereference(local, sta->gtk[i]));
	if (sta->ptk)
		__ieee80211_key_free(key_mtx_dereference(local, sta->ptk));
	mutex_unlock(&local->key_mtx);

	sta->dead = true;

	if (test_and_clear_sta_flags(sta,
				WLAN_STA_PS_STA | WLAN_STA_PS_DRIVER)) {
		BUG_ON(!sdata->bss);

		atomic_dec(&sdata->bss->num_sta_ps);
		__sta_info_clear_tim_bit(sdata->bss, sta);
	}

	local->num_sta--;
	local->sta_generation++;

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		RCU_INIT_POINTER(sdata->u.vlan.sta, NULL);

	if (sta->uploaded) {
		if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			sdata = container_of(sdata->bss,
					     struct ieee80211_sub_if_data,
					     u.ap);
		drv_sta_remove(local, sdata, &sta->sta);
		sdata = sta->sdata;
	}

	/*
	 * At this point, after we wait for an RCU grace period,
	 * neither mac80211 nor the driver can reference this
	 * sta struct any more except by still existing timers
	 * associated with this station that we clean up below.
	 */
	synchronize_rcu();

#ifdef CONFIG_MAC80211_MESH
	if (ieee80211_vif_is_mesh(&sdata->vif))
		mesh_accept_plinks_update(sdata);
#endif

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	wiphy_debug(local->hw.wiphy, "Removed STA %pM\n", sta->sta.addr);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	cancel_work_sync(&sta->drv_unblock_wk);

	cfg80211_del_sta(sdata->dev, sta->sta.addr, GFP_KERNEL);

	rate_control_remove_sta_debugfs(sta);
	ieee80211_sta_debugfs_remove(sta);

#ifdef CONFIG_MAC80211_MESH
	if (ieee80211_vif_is_mesh(&sta->sdata->vif)) {
		mesh_plink_deactivate(sta);
		del_timer_sync(&sta->plink_timer);
	}
#endif

	while ((skb = skb_dequeue(&sta->ps_tx_buf)) != NULL) {
		local->total_ps_buffered--;
		dev_kfree_skb_any(skb);
	}

	while ((skb = skb_dequeue(&sta->tx_filtered)) != NULL)
		dev_kfree_skb_any(skb);

	__sta_info_free(local, sta);

	return 0;
}

int sta_info_destroy_addr(struct ieee80211_sub_if_data *sdata, const u8 *addr)
{
	struct sta_info *sta;
	int ret;

	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get(sdata, addr);
	ret = __sta_info_destroy(sta);
	mutex_unlock(&sdata->local->sta_mtx);

	return ret;
}

int sta_info_destroy_addr_bss(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr)
{
	struct sta_info *sta;
	int ret;

	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get_bss(sdata, addr);
	ret = __sta_info_destroy(sta);
	mutex_unlock(&sdata->local->sta_mtx);

	return ret;
}

static void sta_info_cleanup(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sta_info *sta;
	bool timer_needed = false;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list, list)
		if (sta_info_cleanup_expire_buffered(local, sta))
			timer_needed = true;
	rcu_read_unlock();

	if (local->quiescing)
		return;

	if (!timer_needed)
		return;

	mod_timer(&local->sta_cleanup,
		  round_jiffies(jiffies + STA_INFO_CLEANUP_INTERVAL));
}

void sta_info_init(struct ieee80211_local *local)
{
	spin_lock_init(&local->sta_lock);
	mutex_init(&local->sta_mtx);
	INIT_LIST_HEAD(&local->sta_list);
	INIT_LIST_HEAD(&local->sta_pending_list);
	INIT_WORK(&local->sta_finish_work, sta_info_finish_work);

	setup_timer(&local->sta_cleanup, sta_info_cleanup,
		    (unsigned long)local);
}

void sta_info_stop(struct ieee80211_local *local)
{
	del_timer(&local->sta_cleanup);
	sta_info_flush(local, NULL);
}

/**
 * sta_info_flush - flush matching STA entries from the STA table
 *
 * Returns the number of removed STA entries.
 *
 * @local: local interface data
 * @sdata: matching rule for the net device (sta->dev) or %NULL to match all STAs
 */
int sta_info_flush(struct ieee80211_local *local,
		   struct ieee80211_sub_if_data *sdata)
{
	struct sta_info *sta, *tmp;
	int ret = 0;

	might_sleep();

	mutex_lock(&local->sta_mtx);

	sta_info_finish_pending(local);

	list_for_each_entry_safe(sta, tmp, &local->sta_list, list) {
		if (!sdata || sdata == sta->sdata)
			WARN_ON(__sta_info_destroy(sta));
	}
	mutex_unlock(&local->sta_mtx);

	return ret;
}

void ieee80211_sta_expire(struct ieee80211_sub_if_data *sdata,
			  unsigned long exp_time)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta, *tmp;

	mutex_lock(&local->sta_mtx);
	list_for_each_entry_safe(sta, tmp, &local->sta_list, list)
		if (time_after(jiffies, sta->last_rx + exp_time)) {
#ifdef CONFIG_MAC80211_IBSS_DEBUG
			printk(KERN_DEBUG "%s: expiring inactive STA %pM\n",
			       sdata->name, sta->sta.addr);
#endif
			WARN_ON(__sta_info_destroy(sta));
		}
	mutex_unlock(&local->sta_mtx);
}

struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
					       const u8 *addr,
					       const u8 *localaddr)
{
	struct sta_info *sta, *nxt;

	/*
	 * Just return a random station if localaddr is NULL
	 * ... first in list.
	 */
	for_each_sta_info(hw_to_local(hw), addr, sta, nxt) {
		if (localaddr &&
		    compare_ether_addr(sta->sdata->vif.addr, localaddr) != 0)
			continue;
		if (!sta->uploaded)
			return NULL;
		return &sta->sta;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ieee80211_find_sta_by_ifaddr);

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
					 const u8 *addr)
{
	struct sta_info *sta;

	if (!vif)
		return NULL;

	sta = sta_info_get_bss(vif_to_sdata(vif), addr);
	if (!sta)
		return NULL;

	if (!sta->uploaded)
		return NULL;

	return &sta->sta;
}
EXPORT_SYMBOL(ieee80211_find_sta);

static void clear_sta_ps_flags(void *_sta)
{
	struct sta_info *sta = _sta;

	clear_sta_flags(sta, WLAN_STA_PS_DRIVER | WLAN_STA_PS_STA);
}

/* powersave support code */
void ieee80211_sta_ps_deliver_wakeup(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	int sent, buffered;

	clear_sta_flags(sta, WLAN_STA_PS_DRIVER_BUF);
	if (!(local->hw.flags & IEEE80211_HW_AP_LINK_PS))
		drv_sta_notify(local, sdata, STA_NOTIFY_AWAKE, &sta->sta);

	if (!skb_queue_empty(&sta->ps_tx_buf))
		sta_info_clear_tim_bit(sta);

	/* Send all buffered frames to the station */
	sent = ieee80211_add_pending_skbs(local, &sta->tx_filtered);
	buffered = ieee80211_add_pending_skbs_fn(local, &sta->ps_tx_buf,
						 clear_sta_ps_flags, sta);
	sent += buffered;
	local->total_ps_buffered -= buffered;

#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	printk(KERN_DEBUG "%s: STA %pM aid %d sending %d filtered/%d PS frames "
	       "since STA not sleeping anymore\n", sdata->name,
	       sta->sta.addr, sta->sta.aid, sent - buffered, buffered);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
}

void ieee80211_sta_ps_deliver_poll_response(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	int no_pending_pkts;

	skb = skb_dequeue(&sta->tx_filtered);
	if (!skb) {
		skb = skb_dequeue(&sta->ps_tx_buf);
		if (skb)
			local->total_ps_buffered--;
	}
	no_pending_pkts = skb_queue_empty(&sta->tx_filtered) &&
		skb_queue_empty(&sta->ps_tx_buf);

	if (skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) skb->data;

		/*
		 * Tell TX path to send this frame even though the STA may
		 * still remain is PS mode after this frame exchange.
		 */
		info->flags |= IEEE80211_TX_CTL_PSPOLL_RESPONSE;

#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "STA %pM aid %d: PS Poll (entries after %d)\n",
		       sta->sta.addr, sta->sta.aid,
		       skb_queue_len(&sta->ps_tx_buf));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */

		/* Use MoreData flag to indicate whether there are more
		 * buffered frames for this STA */
		if (no_pending_pkts)
			hdr->frame_control &= cpu_to_le16(~IEEE80211_FCTL_MOREDATA);
		else
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREDATA);

		ieee80211_add_pending_skb(local, skb);

		if (no_pending_pkts)
			sta_info_clear_tim_bit(sta);
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	} else {
		/*
		 * FIXME: This can be the result of a race condition between
		 *	  us expiring a frame and the station polling for it.
		 *	  Should we send it a null-func frame indicating we
		 *	  have nothing buffered for it?
		 */
		printk(KERN_DEBUG "%s: STA %pM sent PS Poll even "
		       "though there are no buffered frames for it\n",
		       sdata->name, sta->sta.addr);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
	}
}

void ieee80211_sta_block_awake(struct ieee80211_hw *hw,
			       struct ieee80211_sta *pubsta, bool block)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	trace_api_sta_block_awake(sta->local, pubsta, block);

	if (block)
		set_sta_flags(sta, WLAN_STA_PS_DRIVER);
	else if (test_sta_flags(sta, WLAN_STA_PS_DRIVER))
		ieee80211_queue_work(hw, &sta->drv_unblock_wk);
}
EXPORT_SYMBOL(ieee80211_sta_block_awake);

void ieee80211_sta_set_tim(struct ieee80211_sta *pubsta)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	set_sta_flags(sta, WLAN_STA_PS_DRIVER_BUF);
	sta_info_set_tim_bit(sta);
}
EXPORT_SYMBOL(ieee80211_sta_set_tim);
