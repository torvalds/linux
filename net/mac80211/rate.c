/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/rtnetlink.h>
#include "rate.h"
#include "ieee80211_i.h"
#include "debugfs.h"

struct rate_control_alg {
	struct list_head list;
	struct rate_control_ops *ops;
};

static LIST_HEAD(rate_ctrl_algs);
static DEFINE_MUTEX(rate_ctrl_mutex);

static char *ieee80211_default_rc_algo = CONFIG_MAC80211_RC_DEFAULT;
module_param(ieee80211_default_rc_algo, charp, 0644);
MODULE_PARM_DESC(ieee80211_default_rc_algo,
		 "Default rate control algorithm for mac80211 to use");

int ieee80211_rate_control_register(struct rate_control_ops *ops)
{
	struct rate_control_alg *alg;

	if (!ops->name)
		return -EINVAL;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (!strcmp(alg->ops->name, ops->name)) {
			/* don't register an algorithm twice */
			WARN_ON(1);
			mutex_unlock(&rate_ctrl_mutex);
			return -EALREADY;
		}
	}

	alg = kzalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL) {
		mutex_unlock(&rate_ctrl_mutex);
		return -ENOMEM;
	}
	alg->ops = ops;

	list_add_tail(&alg->list, &rate_ctrl_algs);
	mutex_unlock(&rate_ctrl_mutex);

	return 0;
}
EXPORT_SYMBOL(ieee80211_rate_control_register);

void ieee80211_rate_control_unregister(struct rate_control_ops *ops)
{
	struct rate_control_alg *alg;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (alg->ops == ops) {
			list_del(&alg->list);
			kfree(alg);
			break;
		}
	}
	mutex_unlock(&rate_ctrl_mutex);
}
EXPORT_SYMBOL(ieee80211_rate_control_unregister);

static struct rate_control_ops *
ieee80211_try_rate_control_ops_get(const char *name)
{
	struct rate_control_alg *alg;
	struct rate_control_ops *ops = NULL;

	if (!name)
		return NULL;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (!strcmp(alg->ops->name, name))
			if (try_module_get(alg->ops->module)) {
				ops = alg->ops;
				break;
			}
	}
	mutex_unlock(&rate_ctrl_mutex);
	return ops;
}

/* Get the rate control algorithm. */
static struct rate_control_ops *
ieee80211_rate_control_ops_get(const char *name)
{
	struct rate_control_ops *ops;
	const char *alg_name;

	if (!name)
		alg_name = ieee80211_default_rc_algo;
	else
		alg_name = name;

	ops = ieee80211_try_rate_control_ops_get(alg_name);
	if (!ops) {
		request_module("rc80211_%s", alg_name);
		ops = ieee80211_try_rate_control_ops_get(alg_name);
	}
	if (!ops && name)
		/* try default if specific alg requested but not found */
		ops = ieee80211_try_rate_control_ops_get(ieee80211_default_rc_algo);

	/* try built-in one if specific alg requested but not found */
	if (!ops && strlen(CONFIG_MAC80211_RC_DEFAULT))
		ops = ieee80211_try_rate_control_ops_get(CONFIG_MAC80211_RC_DEFAULT);

	return ops;
}

static void ieee80211_rate_control_ops_put(struct rate_control_ops *ops)
{
	module_put(ops->module);
}

#ifdef CONFIG_MAC80211_DEBUGFS
static ssize_t rcname_read(struct file *file, char __user *userbuf,
			   size_t count, loff_t *ppos)
{
	struct rate_control_ref *ref = file->private_data;
	int len = strlen(ref->ops->name);

	return simple_read_from_buffer(userbuf, count, ppos,
				       ref->ops->name, len);
}

static const struct file_operations rcname_ops = {
	.read = rcname_read,
	.open = mac80211_open_file_generic,
};
#endif

struct rate_control_ref *rate_control_alloc(const char *name,
					    struct ieee80211_local *local)
{
	struct dentry *debugfsdir = NULL;
	struct rate_control_ref *ref;

	ref = kmalloc(sizeof(struct rate_control_ref), GFP_KERNEL);
	if (!ref)
		goto fail_ref;
	kref_init(&ref->kref);
	ref->local = local;
	ref->ops = ieee80211_rate_control_ops_get(name);
	if (!ref->ops)
		goto fail_ops;

#ifdef CONFIG_MAC80211_DEBUGFS
	debugfsdir = debugfs_create_dir("rc", local->hw.wiphy->debugfsdir);
	local->debugfs.rcdir = debugfsdir;
	debugfs_create_file("name", 0400, debugfsdir, ref, &rcname_ops);
#endif

	ref->priv = ref->ops->alloc(&local->hw, debugfsdir);
	if (!ref->priv)
		goto fail_priv;
	return ref;

fail_priv:
	ieee80211_rate_control_ops_put(ref->ops);
fail_ops:
	kfree(ref);
fail_ref:
	return NULL;
}

static void rate_control_release(struct kref *kref)
{
	struct rate_control_ref *ctrl_ref;

	ctrl_ref = container_of(kref, struct rate_control_ref, kref);
	ctrl_ref->ops->free(ctrl_ref->priv);

#ifdef CONFIG_MAC80211_DEBUGFS
	debugfs_remove_recursive(ctrl_ref->local->debugfs.rcdir);
	ctrl_ref->local->debugfs.rcdir = NULL;
#endif

	ieee80211_rate_control_ops_put(ctrl_ref->ops);
	kfree(ctrl_ref);
}

static bool rc_no_data_or_no_ack(struct ieee80211_tx_rate_control *txrc)
{
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	__le16 fc;

	fc = hdr->frame_control;

	return ((info->flags & IEEE80211_TX_CTL_NO_ACK) || !ieee80211_is_data(fc));
}

bool rate_control_send_low(struct ieee80211_sta *sta,
			   void *priv_sta,
			   struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);

	if (!sta || !priv_sta || rc_no_data_or_no_ack(txrc)) {
		info->control.rates[0].idx = rate_lowest_index(txrc->sband, sta);
		info->control.rates[0].count =
			(info->flags & IEEE80211_TX_CTL_NO_ACK) ?
			1 : txrc->hw->max_rate_tries;
		return true;
	}
	return false;
}
EXPORT_SYMBOL(rate_control_send_low);

void rate_control_get_rate(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta,
			   struct ieee80211_tx_rate_control *txrc)
{
	struct rate_control_ref *ref = sdata->local->rate_ctrl;
	void *priv_sta = NULL;
	struct ieee80211_sta *ista = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
	int i;

	if (sta) {
		ista = &sta->sta;
		priv_sta = sta->rate_ctrl_priv;
	}

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		info->control.rates[i].idx = -1;
		info->control.rates[i].flags = 0;
		info->control.rates[i].count = 1;
	}

	if (sdata->local->hw.flags & IEEE80211_HW_HAS_RATE_CONTROL)
		return;

	if (sta && sdata->force_unicast_rateidx > -1) {
		info->control.rates[0].idx = sdata->force_unicast_rateidx;
	} else {
		ref->ops->get_rate(ref->priv, ista, priv_sta, txrc);
		info->flags |= IEEE80211_TX_INTFL_RCALGO;
	}

	/*
	 * try to enforce the maximum rate the user wanted
	 */
	if (sdata->max_ratectrl_rateidx > -1)
		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
			if (info->control.rates[i].flags & IEEE80211_TX_RC_MCS)
				continue;
			info->control.rates[i].idx =
				min_t(s8, info->control.rates[i].idx,
				      sdata->max_ratectrl_rateidx);
	}

	BUG_ON(info->control.rates[0].idx < 0);
}

struct rate_control_ref *rate_control_get(struct rate_control_ref *ref)
{
	kref_get(&ref->kref);
	return ref;
}

void rate_control_put(struct rate_control_ref *ref)
{
	kref_put(&ref->kref, rate_control_release);
}

int ieee80211_init_rate_ctrl_alg(struct ieee80211_local *local,
				 const char *name)
{
	struct rate_control_ref *ref, *old;

	ASSERT_RTNL();

	if (local->open_count)
		return -EBUSY;

	if (local->hw.flags & IEEE80211_HW_HAS_RATE_CONTROL) {
		if (WARN_ON(!local->ops->set_rts_threshold))
			return -EINVAL;
		return 0;
	}

	ref = rate_control_alloc(name, local);
	if (!ref) {
		printk(KERN_WARNING "%s: Failed to select rate control "
		       "algorithm\n", wiphy_name(local->hw.wiphy));
		return -ENOENT;
	}

	old = local->rate_ctrl;
	local->rate_ctrl = ref;
	if (old) {
		rate_control_put(old);
		sta_info_flush(local, NULL);
	}

	printk(KERN_DEBUG "%s: Selected rate control "
	       "algorithm '%s'\n", wiphy_name(local->hw.wiphy),
	       ref->ops->name);

	return 0;
}

void rate_control_deinitialize(struct ieee80211_local *local)
{
	struct rate_control_ref *ref;

	ref = local->rate_ctrl;

	if (!ref)
		return;

	local->rate_ctrl = NULL;
	rate_control_put(ref);
}

