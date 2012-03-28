/*
 * mac80211 work implementation
 *
 * Copyright 2003-2008, Jouni Malinen <j@w1.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2009, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "rate.h"
#include "driver-ops.h"

enum work_action {
	WORK_ACT_NONE,
	WORK_ACT_TIMEOUT,
};


/* utils */
static inline void ASSERT_WORK_MTX(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->mtx);
}

/*
 * We can have multiple work items (and connection probing)
 * scheduling this timer, but we need to take care to only
 * reschedule it when it should fire _earlier_ than it was
 * asked for before, or if it's not pending right now. This
 * function ensures that. Note that it then is required to
 * run this function for all timeouts after the first one
 * has happened -- the work that runs from this timer will
 * do that.
 */
static void run_again(struct ieee80211_local *local,
		      unsigned long timeout)
{
	ASSERT_WORK_MTX(local);

	if (!timer_pending(&local->work_timer) ||
	    time_before(timeout, local->work_timer.expires))
		mod_timer(&local->work_timer, timeout);
}

void free_work(struct ieee80211_work *wk)
{
	kfree_rcu(wk, rcu_head);
}

static enum work_action __must_check
ieee80211_remain_on_channel_timeout(struct ieee80211_work *wk)
{
	/*
	 * First time we run, do nothing -- the generic code will
	 * have switched to the right channel etc.
	 */
	if (!wk->started) {
		wk->timeout = jiffies + msecs_to_jiffies(wk->remain.duration);

		cfg80211_ready_on_channel(wk->sdata->dev, (unsigned long) wk,
					  wk->chan, wk->chan_type,
					  wk->remain.duration, GFP_KERNEL);

		return WORK_ACT_NONE;
	}

	return WORK_ACT_TIMEOUT;
}

static enum work_action __must_check
ieee80211_offchannel_tx(struct ieee80211_work *wk)
{
	if (!wk->started) {
		wk->timeout = jiffies + msecs_to_jiffies(wk->offchan_tx.wait);

		/*
		 * After this, offchan_tx.frame remains but now is no
		 * longer a valid pointer -- we still need it as the
		 * cookie for canceling this work/status matching.
		 */
		ieee80211_tx_skb(wk->sdata, wk->offchan_tx.frame);

		return WORK_ACT_NONE;
	}

	return WORK_ACT_TIMEOUT;
}

static void ieee80211_work_timer(unsigned long data)
{
	struct ieee80211_local *local = (void *) data;

	if (local->quiescing)
		return;

	ieee80211_queue_work(&local->hw, &local->work_work);
}

static void ieee80211_work_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, work_work);
	struct ieee80211_work *wk, *tmp;
	LIST_HEAD(free_work);
	enum work_action rma;
	bool remain_off_channel = false;

	if (local->scanning)
		return;

	/*
	 * ieee80211_queue_work() should have picked up most cases,
	 * here we'll pick the rest.
	 */
	if (WARN(local->suspended, "work scheduled while going to suspend\n"))
		return;

	mutex_lock(&local->mtx);

	ieee80211_recalc_idle(local);

	list_for_each_entry_safe(wk, tmp, &local->work_list, list) {
		bool started = wk->started;

		/* mark work as started if it's on the current off-channel */
		if (!started && local->tmp_channel &&
		    wk->chan == local->tmp_channel &&
		    wk->chan_type == local->tmp_channel_type) {
			started = true;
			wk->timeout = jiffies;
		}

		if (!started && !local->tmp_channel) {
			ieee80211_offchannel_stop_vifs(local, true);

			local->tmp_channel = wk->chan;
			local->tmp_channel_type = wk->chan_type;

			ieee80211_hw_config(local, 0);

			started = true;
			wk->timeout = jiffies;
		}

		/* don't try to work with items that aren't started */
		if (!started)
			continue;

		if (time_is_after_jiffies(wk->timeout)) {
			/*
			 * This work item isn't supposed to be worked on
			 * right now, but take care to adjust the timer
			 * properly.
			 */
			run_again(local, wk->timeout);
			continue;
		}

		switch (wk->type) {
		default:
			WARN_ON(1);
			/* nothing */
			rma = WORK_ACT_NONE;
			break;
		case IEEE80211_WORK_ABORT:
			rma = WORK_ACT_TIMEOUT;
			break;
		case IEEE80211_WORK_REMAIN_ON_CHANNEL:
			rma = ieee80211_remain_on_channel_timeout(wk);
			break;
		case IEEE80211_WORK_OFFCHANNEL_TX:
			rma = ieee80211_offchannel_tx(wk);
			break;
		}

		wk->started = started;

		switch (rma) {
		case WORK_ACT_NONE:
			/* might have changed the timeout */
			run_again(local, wk->timeout);
			break;
		case WORK_ACT_TIMEOUT:
			list_del_rcu(&wk->list);
			synchronize_rcu();
			list_add(&wk->list, &free_work);
			break;
		default:
			WARN(1, "unexpected: %d", rma);
		}
	}

	list_for_each_entry(wk, &local->work_list, list) {
		if (!wk->started)
			continue;
		if (wk->chan != local->tmp_channel ||
		    wk->chan_type != local->tmp_channel_type)
			continue;
		remain_off_channel = true;
	}

	if (!remain_off_channel && local->tmp_channel) {
		local->tmp_channel = NULL;
		ieee80211_hw_config(local, 0);

		ieee80211_offchannel_return(local, true);

		/* give connection some time to breathe */
		run_again(local, jiffies + HZ/2);
	}

	ieee80211_recalc_idle(local);
	ieee80211_run_deferred_scan(local);

	mutex_unlock(&local->mtx);

	list_for_each_entry_safe(wk, tmp, &free_work, list) {
		wk->done(wk, NULL);
		list_del(&wk->list);
		kfree(wk);
	}
}

void ieee80211_add_work(struct ieee80211_work *wk)
{
	struct ieee80211_local *local;

	if (WARN_ON(!wk->chan))
		return;

	if (WARN_ON(!wk->sdata))
		return;

	if (WARN_ON(!wk->done))
		return;

	if (WARN_ON(!ieee80211_sdata_running(wk->sdata)))
		return;

	wk->started = false;

	local = wk->sdata->local;
	mutex_lock(&local->mtx);
	list_add_tail(&wk->list, &local->work_list);
	mutex_unlock(&local->mtx);

	ieee80211_queue_work(&local->hw, &local->work_work);
}

void ieee80211_work_init(struct ieee80211_local *local)
{
	INIT_LIST_HEAD(&local->work_list);
	setup_timer(&local->work_timer, ieee80211_work_timer,
		    (unsigned long)local);
	INIT_WORK(&local->work_work, ieee80211_work_work);
}

void ieee80211_work_purge(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_work *wk;
	bool cleanup = false;

	mutex_lock(&local->mtx);
	list_for_each_entry(wk, &local->work_list, list) {
		if (wk->sdata != sdata)
			continue;
		cleanup = true;
		wk->type = IEEE80211_WORK_ABORT;
		wk->started = true;
		wk->timeout = jiffies;
	}
	mutex_unlock(&local->mtx);

	/* run cleanups etc. */
	if (cleanup)
		ieee80211_work_work(&local->work_work);

	mutex_lock(&local->mtx);
	list_for_each_entry(wk, &local->work_list, list) {
		if (wk->sdata != sdata)
			continue;
		WARN_ON(1);
		break;
	}
	mutex_unlock(&local->mtx);
}

static enum work_done_result ieee80211_remain_done(struct ieee80211_work *wk,
						   struct sk_buff *skb)
{
	/*
	 * We are done serving the remain-on-channel command.
	 */
	cfg80211_remain_on_channel_expired(wk->sdata->dev, (unsigned long) wk,
					   wk->chan, wk->chan_type,
					   GFP_KERNEL);

	return WORK_DONE_DESTROY;
}

int ieee80211_wk_remain_on_channel(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_channel *chan,
				   enum nl80211_channel_type channel_type,
				   unsigned int duration, u64 *cookie)
{
	struct ieee80211_work *wk;

	wk = kzalloc(sizeof(*wk), GFP_KERNEL);
	if (!wk)
		return -ENOMEM;

	wk->type = IEEE80211_WORK_REMAIN_ON_CHANNEL;
	wk->chan = chan;
	wk->chan_type = channel_type;
	wk->sdata = sdata;
	wk->done = ieee80211_remain_done;

	wk->remain.duration = duration;

	*cookie = (unsigned long) wk;

	ieee80211_add_work(wk);

	return 0;
}

int ieee80211_wk_cancel_remain_on_channel(struct ieee80211_sub_if_data *sdata,
					  u64 cookie)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_work *wk, *tmp;
	bool found = false;

	mutex_lock(&local->mtx);
	list_for_each_entry_safe(wk, tmp, &local->work_list, list) {
		if ((unsigned long) wk == cookie) {
			wk->timeout = jiffies;
			found = true;
			break;
		}
	}
	mutex_unlock(&local->mtx);

	if (!found)
		return -ENOENT;

	ieee80211_queue_work(&local->hw, &local->work_work);

	return 0;
}
