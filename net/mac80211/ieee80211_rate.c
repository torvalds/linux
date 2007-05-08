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
#include "ieee80211_rate.h"
#include "ieee80211_i.h"

struct rate_control_alg {
	struct list_head list;
	struct rate_control_ops *ops;
};

static LIST_HEAD(rate_ctrl_algs);
static DEFINE_MUTEX(rate_ctrl_mutex);

int ieee80211_rate_control_register(struct rate_control_ops *ops)
{
	struct rate_control_alg *alg;

	alg = kmalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL) {
		return -ENOMEM;
	}
	memset(alg, 0, sizeof(*alg));
	alg->ops = ops;

	mutex_lock(&rate_ctrl_mutex);
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
			break;
		}
	}
	mutex_unlock(&rate_ctrl_mutex);
	kfree(alg);
}
EXPORT_SYMBOL(ieee80211_rate_control_unregister);

static struct rate_control_ops *
ieee80211_try_rate_control_ops_get(const char *name)
{
	struct rate_control_alg *alg;
	struct rate_control_ops *ops = NULL;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (!name || !strcmp(alg->ops->name, name))
			if (try_module_get(alg->ops->module)) {
				ops = alg->ops;
				break;
			}
	}
	mutex_unlock(&rate_ctrl_mutex);
	return ops;
}

/* Get the rate control algorithm. If `name' is NULL, get the first
 * available algorithm. */
static struct rate_control_ops *
ieee80211_rate_control_ops_get(const char *name)
{
	struct rate_control_ops *ops;

	ops = ieee80211_try_rate_control_ops_get(name);
	if (!ops) {
		request_module("rc80211_%s", name ? name : "default");
		ops = ieee80211_try_rate_control_ops_get(name);
	}
	return ops;
}

static void ieee80211_rate_control_ops_put(struct rate_control_ops *ops)
{
	module_put(ops->module);
}

struct rate_control_ref *rate_control_alloc(const char *name,
					    struct ieee80211_local *local)
{
	struct rate_control_ref *ref;

	ref = kmalloc(sizeof(struct rate_control_ref), GFP_KERNEL);
	if (!ref)
		goto fail_ref;
	kref_init(&ref->kref);
	ref->ops = ieee80211_rate_control_ops_get(name);
	if (!ref->ops)
		goto fail_ops;
	ref->priv = ref->ops->alloc(local);
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
	ieee80211_rate_control_ops_put(ctrl_ref->ops);
	kfree(ctrl_ref);
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
