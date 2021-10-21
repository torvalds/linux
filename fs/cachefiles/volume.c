// SPDX-License-Identifier: GPL-2.0-or-later
/* Volume handling.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include "internal.h"
#include <trace/events/fscache.h>

/*
 * Allocate and set up a volume representation.  We make sure all the fanout
 * directories are created and pinned.
 */
void cachefiles_acquire_volume(struct fscache_volume *vcookie)
{
	struct cachefiles_volume *volume;
	struct cachefiles_cache *cache = vcookie->cache->cache_priv;
	const struct cred *saved_cred;
	struct dentry *vdentry, *fan;
	size_t len;
	char *name;
	int n_accesses, i;

	_enter("");

	volume = kzalloc(sizeof(struct cachefiles_volume), GFP_KERNEL);
	if (!volume)
		return;
	volume->vcookie = vcookie;
	volume->cache = cache;
	INIT_LIST_HEAD(&volume->cache_link);

	cachefiles_begin_secure(cache, &saved_cred);

	len = vcookie->key[0];
	name = kmalloc(len + 3, GFP_NOFS);
	if (!name)
		goto error_vol;
	name[0] = 'I';
	memcpy(name + 1, vcookie->key + 1, len);
	name[len + 1] = 0;

	vdentry = cachefiles_get_directory(cache, cache->store, name, NULL);
	if (IS_ERR(vdentry))
		goto error_name;
	volume->dentry = vdentry;

	for (i = 0; i < 256; i++) {
		sprintf(name, "@%02x", i);
		fan = cachefiles_get_directory(cache, vdentry, name, NULL);
		if (IS_ERR(fan))
			goto error_fan;
		volume->fanout[i] = fan;
	}

	cachefiles_end_secure(cache, saved_cred);

	vcookie->cache_priv = volume;
	n_accesses = atomic_inc_return(&vcookie->n_accesses); /* Stop wakeups on dec-to-0 */
	trace_fscache_access_volume(vcookie->debug_id, 0,
				    refcount_read(&vcookie->ref),
				    n_accesses, fscache_access_cache_pin);

	spin_lock(&cache->object_list_lock);
	list_add(&volume->cache_link, &volume->cache->volumes);
	spin_unlock(&cache->object_list_lock);

	kfree(name);
	return;

error_fan:
	for (i = 0; i < 256; i++)
		cachefiles_put_directory(volume->fanout[i]);
	cachefiles_put_directory(volume->dentry);
error_name:
	kfree(name);
error_vol:
	kfree(volume);
	cachefiles_end_secure(cache, saved_cred);
}

/*
 * Release a volume representation.
 */
static void __cachefiles_free_volume(struct cachefiles_volume *volume)
{
	int i;

	_enter("");

	volume->vcookie->cache_priv = NULL;

	for (i = 0; i < 256; i++)
		cachefiles_put_directory(volume->fanout[i]);
	cachefiles_put_directory(volume->dentry);
	kfree(volume);
}

void cachefiles_free_volume(struct fscache_volume *vcookie)
{
	struct cachefiles_volume *volume = vcookie->cache_priv;

	if (volume) {
		spin_lock(&volume->cache->object_list_lock);
		list_del_init(&volume->cache_link);
		spin_unlock(&volume->cache->object_list_lock);
		__cachefiles_free_volume(volume);
	}
}

void cachefiles_withdraw_volume(struct cachefiles_volume *volume)
{
	fscache_withdraw_volume(volume->vcookie);
	__cachefiles_free_volume(volume);
}
