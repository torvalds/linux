/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IEEE80211_RATE_H
#define IEEE80211_RATE_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"

#define RATE_CONTROL_NUM_DOWN 20
#define RATE_CONTROL_NUM_UP   15


struct rate_control_extra {
	/* values from rate_control_get_rate() to the caller: */
	struct ieee80211_rate *probe; /* probe with this rate, or NULL for no
				       * probing */
	struct ieee80211_rate *nonerp;

	/* parameters from the caller to rate_control_get_rate(): */
	struct ieee80211_hw_mode *mode;
	int mgmt_data; /* this is data frame that is used for management
			* (e.g., IEEE 802.1X EAPOL) */
	u16 ethertype;
};


struct rate_control_ops {
	struct module *module;
	const char *name;
	void (*tx_status)(void *priv, struct net_device *dev,
			  struct sk_buff *skb,
			  struct ieee80211_tx_status *status);
	struct ieee80211_rate *(*get_rate)(void *priv, struct net_device *dev,
					   struct sk_buff *skb,
					   struct rate_control_extra *extra);
	void (*rate_init)(void *priv, void *priv_sta,
			  struct ieee80211_local *local, struct sta_info *sta);
	void (*clear)(void *priv);

	void *(*alloc)(struct ieee80211_local *local);
	void (*free)(void *priv);
	void *(*alloc_sta)(void *priv, gfp_t gfp);
	void (*free_sta)(void *priv, void *priv_sta);

	int (*add_attrs)(void *priv, struct kobject *kobj);
	void (*remove_attrs)(void *priv, struct kobject *kobj);
	void (*add_sta_debugfs)(void *priv, void *priv_sta,
				struct dentry *dir);
	void (*remove_sta_debugfs)(void *priv, void *priv_sta);
};

struct rate_control_ref {
	struct rate_control_ops *ops;
	void *priv;
	struct kref kref;
};

int ieee80211_rate_control_register(struct rate_control_ops *ops);
void ieee80211_rate_control_unregister(struct rate_control_ops *ops);

/* Get a reference to the rate control algorithm. If `name' is NULL, get the
 * first available algorithm. */
struct rate_control_ref *rate_control_alloc(const char *name,
					    struct ieee80211_local *local);
struct rate_control_ref *rate_control_get(struct rate_control_ref *ref);
void rate_control_put(struct rate_control_ref *ref);

static inline void rate_control_tx_status(struct ieee80211_local *local,
					  struct net_device *dev,
					  struct sk_buff *skb,
					  struct ieee80211_tx_status *status)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	ref->ops->tx_status(ref->priv, dev, skb, status);
}


static inline struct ieee80211_rate *
rate_control_get_rate(struct ieee80211_local *local, struct net_device *dev,
		      struct sk_buff *skb, struct rate_control_extra *extra)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	return ref->ops->get_rate(ref->priv, dev, skb, extra);
}


static inline void rate_control_rate_init(struct sta_info *sta,
					  struct ieee80211_local *local)
{
	struct rate_control_ref *ref = sta->rate_ctrl;
	ref->ops->rate_init(ref->priv, sta->rate_ctrl_priv, local, sta);
}


static inline void rate_control_clear(struct ieee80211_local *local)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	ref->ops->clear(ref->priv);
}

static inline void *rate_control_alloc_sta(struct rate_control_ref *ref,
					   gfp_t gfp)
{
	return ref->ops->alloc_sta(ref->priv, gfp);
}

static inline void rate_control_free_sta(struct rate_control_ref *ref,
					 void *priv)
{
	ref->ops->free_sta(ref->priv, priv);
}

static inline void rate_control_add_sta_debugfs(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_DEBUGFS
	struct rate_control_ref *ref = sta->rate_ctrl;
	if (sta->debugfs.dir && ref->ops->add_sta_debugfs)
		ref->ops->add_sta_debugfs(ref->priv, sta->rate_ctrl_priv,
					  sta->debugfs.dir);
#endif
}

static inline void rate_control_remove_sta_debugfs(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_DEBUGFS
	struct rate_control_ref *ref = sta->rate_ctrl;
	if (ref->ops->remove_sta_debugfs)
		ref->ops->remove_sta_debugfs(ref->priv, sta->rate_ctrl_priv);
#endif
}

#endif /* IEEE80211_RATE_H */
