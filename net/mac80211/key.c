/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "debugfs_key.h"
#include "aes_ccm.h"

struct ieee80211_key *ieee80211_key_alloc(struct ieee80211_sub_if_data *sdata,
					  int idx, size_t key_len, gfp_t flags)
{
	struct ieee80211_key *key;

	key = kzalloc(sizeof(struct ieee80211_key) + key_len, flags);
	if (!key)
		return NULL;
	kref_init(&key->kref);
	return key;
}

static void ieee80211_key_release(struct kref *kref)
{
	struct ieee80211_key *key;

	key = container_of(kref, struct ieee80211_key, kref);
	if (key->conf.alg == ALG_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	ieee80211_debugfs_key_remove(key);
	kfree(key);
}

void ieee80211_key_free(struct ieee80211_key *key)
{
	if (key)
		kref_put(&key->kref, ieee80211_key_release);
}
