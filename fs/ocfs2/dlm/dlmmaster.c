/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmmod.c
 *
 * standalone DLM module
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/random.h>
#include <linux/blkdev.h>
#include <linux/socket.h>
#include <linux/inet.h>
#include <linux/spinlock.h>
#include <linux/delay.h>


#include "cluster/heartbeat.h"
#include "cluster/nodemanager.h"
#include "cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"
#include "dlmdomain.h"
#include "dlmdebug.h"

#define MLOG_MASK_PREFIX (ML_DLM|ML_DLM_MASTER)
#include "cluster/masklog.h"

static void dlm_mle_node_down(struct dlm_ctxt *dlm,
			      struct dlm_master_list_entry *mle,
			      struct o2nm_node *node,
			      int idx);
static void dlm_mle_node_up(struct dlm_ctxt *dlm,
			    struct dlm_master_list_entry *mle,
			    struct o2nm_node *node,
			    int idx);

static void dlm_assert_master_worker(struct dlm_work_item *item, void *data);
static int dlm_do_assert_master(struct dlm_ctxt *dlm,
				struct dlm_lock_resource *res,
				void *nodemap, u32 flags);
static void dlm_deref_lockres_worker(struct dlm_work_item *item, void *data);

static inline int dlm_mle_equal(struct dlm_ctxt *dlm,
				struct dlm_master_list_entry *mle,
				const char *name,
				unsigned int namelen)
{
	if (dlm != mle->dlm)
		return 0;

	if (namelen != mle->mnamelen ||
	    memcmp(name, mle->mname, namelen) != 0)
		return 0;

	return 1;
}

static struct kmem_cache *dlm_lockres_cache = NULL;
static struct kmem_cache *dlm_lockname_cache = NULL;
static struct kmem_cache *dlm_mle_cache = NULL;

static void dlm_mle_release(struct kref *kref);
static void dlm_init_mle(struct dlm_master_list_entry *mle,
			enum dlm_mle_type type,
			struct dlm_ctxt *dlm,
			struct dlm_lock_resource *res,
			const char *name,
			unsigned int namelen);
static void dlm_put_mle(struct dlm_master_list_entry *mle);
static void __dlm_put_mle(struct dlm_master_list_entry *mle);
static int dlm_find_mle(struct dlm_ctxt *dlm,
			struct dlm_master_list_entry **mle,
			char *name, unsigned int namelen);

static int dlm_do_master_request(struct dlm_lock_resource *res,
				 struct dlm_master_list_entry *mle, int to);


static int dlm_wait_for_lock_mastery(struct dlm_ctxt *dlm,
				     struct dlm_lock_resource *res,
				     struct dlm_master_list_entry *mle,
				     int *blocked);
static int dlm_restart_lock_mastery(struct dlm_ctxt *dlm,
				    struct dlm_lock_resource *res,
				    struct dlm_master_list_entry *mle,
				    int blocked);
static int dlm_add_migration_mle(struct dlm_ctxt *dlm,
				 struct dlm_lock_resource *res,
				 struct dlm_master_list_entry *mle,
				 struct dlm_master_list_entry **oldmle,
				 const char *name, unsigned int namelen,
				 u8 new_master, u8 master);

static u8 dlm_pick_migration_target(struct dlm_ctxt *dlm,
				    struct dlm_lock_resource *res);
static void dlm_remove_nonlocal_locks(struct dlm_ctxt *dlm,
				      struct dlm_lock_resource *res);
static int dlm_mark_lockres_migrating(struct dlm_ctxt *dlm,
				       struct dlm_lock_resource *res,
				       u8 target);
static int dlm_pre_master_reco_lockres(struct dlm_ctxt *dlm,
				       struct dlm_lock_resource *res);


int dlm_is_host_down(int errno)
{
	switch (errno) {
		case -EBADF:
		case -ECONNREFUSED:
		case -ENOTCONN:
		case -ECONNRESET:
		case -EPIPE:
		case -EHOSTDOWN:
		case -EHOSTUNREACH:
		case -ETIMEDOUT:
		case -ECONNABORTED:
		case -ENETDOWN:
		case -ENETUNREACH:
		case -ENETRESET:
		case -ESHUTDOWN:
		case -ENOPROTOOPT:
		case -EINVAL:   /* if returned from our tcp code,
				   this means there is no socket */
			return 1;
	}
	return 0;
}


/*
 * MASTER LIST FUNCTIONS
 */


/*
 * regarding master list entries and heartbeat callbacks:
 *
 * in order to avoid sleeping and allocation that occurs in
 * heartbeat, master list entries are simply attached to the
 * dlm's established heartbeat callbacks.  the mle is attached
 * when it is created, and since the dlm->spinlock is held at
 * that time, any heartbeat event will be properly discovered
 * by the mle.  the mle needs to be detached from the
 * dlm->mle_hb_events list as soon as heartbeat events are no
 * longer useful to the mle, and before the mle is freed.
 *
 * as a general rule, heartbeat events are no longer needed by
 * the mle once an "answer" regarding the lock master has been
 * received.
 */
static inline void __dlm_mle_attach_hb_events(struct dlm_ctxt *dlm,
					      struct dlm_master_list_entry *mle)
{
	assert_spin_locked(&dlm->spinlock);

	list_add_tail(&mle->hb_events, &dlm->mle_hb_events);
}


static inline void __dlm_mle_detach_hb_events(struct dlm_ctxt *dlm,
					      struct dlm_master_list_entry *mle)
{
	if (!list_empty(&mle->hb_events))
		list_del_init(&mle->hb_events);
}


static inline void dlm_mle_detach_hb_events(struct dlm_ctxt *dlm,
					    struct dlm_master_list_entry *mle)
{
	spin_lock(&dlm->spinlock);
	__dlm_mle_detach_hb_events(dlm, mle);
	spin_unlock(&dlm->spinlock);
}

static void dlm_get_mle_inuse(struct dlm_master_list_entry *mle)
{
	struct dlm_ctxt *dlm;
	dlm = mle->dlm;

	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&dlm->master_lock);
	mle->inuse++;
	kref_get(&mle->mle_refs);
}

static void dlm_put_mle_inuse(struct dlm_master_list_entry *mle)
{
	struct dlm_ctxt *dlm;
	dlm = mle->dlm;

	spin_lock(&dlm->spinlock);
	spin_lock(&dlm->master_lock);
	mle->inuse--;
	__dlm_put_mle(mle);
	spin_unlock(&dlm->master_lock);
	spin_unlock(&dlm->spinlock);

}

/* remove from list and free */
static void __dlm_put_mle(struct dlm_master_list_entry *mle)
{
	struct dlm_ctxt *dlm;
	dlm = mle->dlm;

	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&dlm->master_lock);
	if (!atomic_read(&mle->mle_refs.refcount)) {
		/* this may or may not crash, but who cares.
		 * it's a BUG. */
		mlog(ML_ERROR, "bad mle: %p\n", mle);
		dlm_print_one_mle(mle);
		BUG();
	} else
		kref_put(&mle->mle_refs, dlm_mle_release);
}


/* must not have any spinlocks coming in */
static void dlm_put_mle(struct dlm_master_list_entry *mle)
{
	struct dlm_ctxt *dlm;
	dlm = mle->dlm;

	spin_lock(&dlm->spinlock);
	spin_lock(&dlm->master_lock);
	__dlm_put_mle(mle);
	spin_unlock(&dlm->master_lock);
	spin_unlock(&dlm->spinlock);
}

static inline void dlm_get_mle(struct dlm_master_list_entry *mle)
{
	kref_get(&mle->mle_refs);
}

static void dlm_init_mle(struct dlm_master_list_entry *mle,
			enum dlm_mle_type type,
			struct dlm_ctxt *dlm,
			struct dlm_lock_resource *res,
			const char *name,
			unsigned int namelen)
{
	assert_spin_locked(&dlm->spinlock);

	mle->dlm = dlm;
	mle->type = type;
	INIT_HLIST_NODE(&mle->master_hash_node);
	INIT_LIST_HEAD(&mle->hb_events);
	memset(mle->maybe_map, 0, sizeof(mle->maybe_map));
	spin_lock_init(&mle->spinlock);
	init_waitqueue_head(&mle->wq);
	atomic_set(&mle->woken, 0);
	kref_init(&mle->mle_refs);
	memset(mle->response_map, 0, sizeof(mle->response_map));
	mle->master = O2NM_MAX_NODES;
	mle->new_master = O2NM_MAX_NODES;
	mle->inuse = 0;

	BUG_ON(mle->type != DLM_MLE_BLOCK &&
	       mle->type != DLM_MLE_MASTER &&
	       mle->type != DLM_MLE_MIGRATION);

	if (mle->type == DLM_MLE_MASTER) {
		BUG_ON(!res);
		mle->mleres = res;
		memcpy(mle->mname, res->lockname.name, res->lockname.len);
		mle->mnamelen = res->lockname.len;
		mle->mnamehash = res->lockname.hash;
	} else {
		BUG_ON(!name);
		mle->mleres = NULL;
		memcpy(mle->mname, name, namelen);
		mle->mnamelen = namelen;
		mle->mnamehash = dlm_lockid_hash(name, namelen);
	}

	atomic_inc(&dlm->mle_tot_count[mle->type]);
	atomic_inc(&dlm->mle_cur_count[mle->type]);

	/* copy off the node_map and register hb callbacks on our copy */
	memcpy(mle->node_map, dlm->domain_map, sizeof(mle->node_map));
	memcpy(mle->vote_map, dlm->domain_map, sizeof(mle->vote_map));
	clear_bit(dlm->node_num, mle->vote_map);
	clear_bit(dlm->node_num, mle->node_map);

	/* attach the mle to the domain node up/down events */
	__dlm_mle_attach_hb_events(dlm, mle);
}

void __dlm_unlink_mle(struct dlm_ctxt *dlm, struct dlm_master_list_entry *mle)
{
	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&dlm->master_lock);

	if (!hlist_unhashed(&mle->master_hash_node))
		hlist_del_init(&mle->master_hash_node);
}

void __dlm_insert_mle(struct dlm_ctxt *dlm, struct dlm_master_list_entry *mle)
{
	struct hlist_head *bucket;

	assert_spin_locked(&dlm->master_lock);

	bucket = dlm_master_hash(dlm, mle->mnamehash);
	hlist_add_head(&mle->master_hash_node, bucket);
}

/* returns 1 if found, 0 if not */
static int dlm_find_mle(struct dlm_ctxt *dlm,
			struct dlm_master_list_entry **mle,
			char *name, unsigned int namelen)
{
	struct dlm_master_list_entry *tmpmle;
	struct hlist_head *bucket;
	unsigned int hash;

	assert_spin_locked(&dlm->master_lock);

	hash = dlm_lockid_hash(name, namelen);
	bucket = dlm_master_hash(dlm, hash);
	hlist_for_each_entry(tmpmle, bucket, master_hash_node) {
		if (!dlm_mle_equal(dlm, tmpmle, name, namelen))
			continue;
		dlm_get_mle(tmpmle);
		*mle = tmpmle;
		return 1;
	}
	return 0;
}

void dlm_hb_event_notify_attached(struct dlm_ctxt *dlm, int idx, int node_up)
{
	struct dlm_master_list_entry *mle;

	assert_spin_locked(&dlm->spinlock);

	list_for_each_entry(mle, &dlm->mle_hb_events, hb_events) {
		if (node_up)
			dlm_mle_node_up(dlm, mle, NULL, idx);
		else
			dlm_mle_node_down(dlm, mle, NULL, idx);
	}
}

static void dlm_mle_node_down(struct dlm_ctxt *dlm,
			      struct dlm_master_list_entry *mle,
			      struct o2nm_node *node, int idx)
{
	spin_lock(&mle->spinlock);

	if (!test_bit(idx, mle->node_map))
		mlog(0, "node %u already removed from nodemap!\n", idx);
	else
		clear_bit(idx, mle->node_map);

	spin_unlock(&mle->spinlock);
}

static void dlm_mle_node_up(struct dlm_ctxt *dlm,
			    struct dlm_master_list_entry *mle,
			    struct o2nm_node *node, int idx)
{
	spin_lock(&mle->spinlock);

	if (test_bit(idx, mle->node_map))
		mlog(0, "node %u already in node map!\n", idx);
	else
		set_bit(idx, mle->node_map);

	spin_unlock(&mle->spinlock);
}


int dlm_init_mle_cache(void)
{
	dlm_mle_cache = kmem_cache_create("o2dlm_mle",
					  sizeof(struct dlm_master_list_entry),
					  0, SLAB_HWCACHE_ALIGN,
					  NULL);
	if (dlm_mle_cache == NULL)
		return -ENOMEM;
	return 0;
}

void dlm_destroy_mle_cache(void)
{
	if (dlm_mle_cache)
		kmem_cache_destroy(dlm_mle_cache);
}

static void dlm_mle_release(struct kref *kref)
{
	struct dlm_master_list_entry *mle;
	struct dlm_ctxt *dlm;

	mle = container_of(kref, struct dlm_master_list_entry, mle_refs);
	dlm = mle->dlm;

	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&dlm->master_lock);

	mlog(0, "Releasing mle for %.*s, type %d\n", mle->mnamelen, mle->mname,
	     mle->type);

	/* remove from list if not already */
	__dlm_unlink_mle(dlm, mle);

	/* detach the mle from the domain node up/down events */
	__dlm_mle_detach_hb_events(dlm, mle);

	atomic_dec(&dlm->mle_cur_count[mle->type]);

	/* NOTE: kfree under spinlock here.
	 * if this is bad, we can move this to a freelist. */
	kmem_cache_free(dlm_mle_cache, mle);
}


/*
 * LOCK RESOURCE FUNCTIONS
 */

int dlm_init_master_caches(void)
{
	dlm_lockres_cache = kmem_cache_create("o2dlm_lockres",
					      sizeof(struct dlm_lock_resource),
					      0, SLAB_HWCACHE_ALIGN, NULL);
	if (!dlm_lockres_cache)
		goto bail;

	dlm_lockname_cache = kmem_cache_create("o2dlm_lockname",
					       DLM_LOCKID_NAME_MAX, 0,
					       SLAB_HWCACHE_ALIGN, NULL);
	if (!dlm_lockname_cache)
		goto bail;

	return 0;
bail:
	dlm_destroy_master_caches();
	return -ENOMEM;
}

void dlm_destroy_master_caches(void)
{
	if (dlm_lockname_cache) {
		kmem_cache_destroy(dlm_lockname_cache);
		dlm_lockname_cache = NULL;
	}

	if (dlm_lockres_cache) {
		kmem_cache_destroy(dlm_lockres_cache);
		dlm_lockres_cache = NULL;
	}
}

static void dlm_lockres_release(struct kref *kref)
{
	struct dlm_lock_resource *res;
	struct dlm_ctxt *dlm;

	res = container_of(kref, struct dlm_lock_resource, refs);
	dlm = res->dlm;

	/* This should not happen -- all lockres' have a name
	 * associated with them at init time. */
	BUG_ON(!res->lockname.name);

	mlog(0, "destroying lockres %.*s\n", res->lockname.len,
	     res->lockname.name);

	spin_lock(&dlm->track_lock);
	if (!list_empty(&res->tracking))
		list_del_init(&res->tracking);
	else {
		mlog(ML_ERROR, "Resource %.*s not on the Tracking list\n",
		     res->lockname.len, res->lockname.name);
		dlm_print_one_lock_resource(res);
	}
	spin_unlock(&dlm->track_lock);

	atomic_dec(&dlm->res_cur_count);

	if (!hlist_unhashed(&res->hash_node) ||
	    !list_empty(&res->granted) ||
	    !list_empty(&res->converting) ||
	    !list_empty(&res->blocked) ||
	    !list_empty(&res->dirty) ||
	    !list_empty(&res->recovering) ||
	    !list_empty(&res->purge)) {
		mlog(ML_ERROR,
		     "Going to BUG for resource %.*s."
		     "  We're on a list! [%c%c%c%c%c%c%c]\n",
		     res->lockname.len, res->lockname.name,
		     !hlist_unhashed(&res->hash_node) ? 'H' : ' ',
		     !list_empty(&res->granted) ? 'G' : ' ',
		     !list_empty(&res->converting) ? 'C' : ' ',
		     !list_empty(&res->blocked) ? 'B' : ' ',
		     !list_empty(&res->dirty) ? 'D' : ' ',
		     !list_empty(&res->recovering) ? 'R' : ' ',
		     !list_empty(&res->purge) ? 'P' : ' ');

		dlm_print_one_lock_resource(res);
	}

	/* By the time we're ready to blow this guy away, we shouldn't
	 * be on any lists. */
	BUG_ON(!hlist_unhashed(&res->hash_node));
	BUG_ON(!list_empty(&res->granted));
	BUG_ON(!list_empty(&res->converting));
	BUG_ON(!list_empty(&res->blocked));
	BUG_ON(!list_empty(&res->dirty));
	BUG_ON(!list_empty(&res->recovering));
	BUG_ON(!list_empty(&res->purge));

	kmem_cache_free(dlm_lockname_cache, (void *)res->lockname.name);

	kmem_cache_free(dlm_lockres_cache, res);
}

void dlm_lockres_put(struct dlm_lock_resource *res)
{
	kref_put(&res->refs, dlm_lockres_release);
}

static void dlm_init_lockres(struct dlm_ctxt *dlm,
			     struct dlm_lock_resource *res,
			     const char *name, unsigned int namelen)
{
	char *qname;

	/* If we memset here, we lose our reference to the kmalloc'd
	 * res->lockname.name, so be sure to init every field
	 * correctly! */

	qname = (char *) res->lockname.name;
	memcpy(qname, name, namelen);

	res->lockname.len = namelen;
	res->lockname.hash = dlm_lockid_hash(name, namelen);

	init_waitqueue_head(&res->wq);
	spin_lock_init(&res->spinlock);
	INIT_HLIST_NODE(&res->hash_node);
	INIT_LIST_HEAD(&res->granted);
	INIT_LIST_HEAD(&res->converting);
	INIT_LIST_HEAD(&res->blocked);
	INIT_LIST_HEAD(&res->dirty);
	INIT_LIST_HEAD(&res->recovering);
	INIT_LIST_HEAD(&res->purge);
	INIT_LIST_HEAD(&res->tracking);
	atomic_set(&res->asts_reserved, 0);
	res->migration_pending = 0;
	res->inflight_locks = 0;

	res->dlm = dlm;

	kref_init(&res->refs);

	atomic_inc(&dlm->res_tot_count);
	atomic_inc(&dlm->res_cur_count);

	/* just for consistency */
	spin_lock(&res->spinlock);
	dlm_set_lockres_owner(dlm, res, DLM_LOCK_RES_OWNER_UNKNOWN);
	spin_unlock(&res->spinlock);

	res->state = DLM_LOCK_RES_IN_PROGRESS;

	res->last_used = 0;

	spin_lock(&dlm->spinlock);
	list_add_tail(&res->tracking, &dlm->tracking_list);
	spin_unlock(&dlm->spinlock);

	memset(res->lvb, 0, DLM_LVB_LEN);
	memset(res->refmap, 0, sizeof(res->refmap));
}

struct dlm_lock_resource *dlm_new_lockres(struct dlm_ctxt *dlm,
				   const char *name,
				   unsigned int namelen)
{
	struct dlm_lock_resource *res = NULL;

	res = kmem_cache_zalloc(dlm_lockres_cache, GFP_NOFS);
	if (!res)
		goto error;

	res->lockname.name = kmem_cache_zalloc(dlm_lockname_cache, GFP_NOFS);
	if (!res->lockname.name)
		goto error;

	dlm_init_lockres(dlm, res, name, namelen);
	return res;

error:
	if (res && res->lockname.name)
		kmem_cache_free(dlm_lockname_cache, (void *)res->lockname.name);

	if (res)
		kmem_cache_free(dlm_lockres_cache, res);
	return NULL;
}

void dlm_lockres_set_refmap_bit(struct dlm_ctxt *dlm,
				struct dlm_lock_resource *res, int bit)
{
	assert_spin_locked(&res->spinlock);

	mlog(0, "res %.*s, set node %u, %ps()\n", res->lockname.len,
	     res->lockname.name, bit, __builtin_return_address(0));

	set_bit(bit, res->refmap);
}

void dlm_lockres_clear_refmap_bit(struct dlm_ctxt *dlm,
				  struct dlm_lock_resource *res, int bit)
{
	assert_spin_locked(&res->spinlock);

	mlog(0, "res %.*s, clr node %u, %ps()\n", res->lockname.len,
	     res->lockname.name, bit, __builtin_return_address(0));

	clear_bit(bit, res->refmap);
}


void dlm_lockres_grab_inflight_ref(struct dlm_ctxt *dlm,
				   struct dlm_lock_resource *res)
{
	assert_spin_locked(&res->spinlock);

	res->inflight_locks++;

	mlog(0, "%s: res %.*s, inflight++: now %u, %ps()\n", dlm->name,
	     res->lockname.len, res->lockname.name, res->inflight_locks,
	     __builtin_return_address(0));
}

void dlm_lockres_drop_inflight_ref(struct dlm_ctxt *dlm,
				   struct dlm_lock_resource *res)
{
	assert_spin_locked(&res->spinlock);

	BUG_ON(res->inflight_locks == 0);

	res->inflight_locks--;

	mlog(0, "%s: res %.*s, inflight--: now %u, %ps()\n", dlm->name,
	     res->lockname.len, res->lockname.name, res->inflight_locks,
	     __builtin_return_address(0));

	wake_up(&res->wq);
}

/*
 * lookup a lock resource by name.
 * may already exist in the hashtable.
 * lockid is null terminated
 *
 * if not, allocate enough for the lockres and for
 * the temporary structure used in doing the mastering.
 *
 * also, do a lookup in the dlm->master_list to see
 * if another node has begun mastering the same lock.
 * if so, there should be a block entry in there
 * for this name, and we should *not* attempt to master
 * the lock here.   need to wait around for that node
 * to assert_master (or die).
 *
 */
struct dlm_lock_resource * dlm_get_lock_resource(struct dlm_ctxt *dlm,
					  const char *lockid,
					  int namelen,
					  int flags)
{
	struct dlm_lock_resource *tmpres=NULL, *res=NULL;
	struct dlm_master_list_entry *mle = NULL;
	struct dlm_master_list_entry *alloc_mle = NULL;
	int blocked = 0;
	int ret, nodenum;
	struct dlm_node_iter iter;
	unsigned int hash;
	int tries = 0;
	int bit, wait_on_recovery = 0;

	BUG_ON(!lockid);

	hash = dlm_lockid_hash(lockid, namelen);

	mlog(0, "get lockres %s (len %d)\n", lockid, namelen);

lookup:
	spin_lock(&dlm->spinlock);
	tmpres = __dlm_lookup_lockres_full(dlm, lockid, namelen, hash);
	if (tmpres) {
		spin_unlock(&dlm->spinlock);
		spin_lock(&tmpres->spinlock);
		/* Wait on the thread that is mastering the resource */
		if (tmpres->owner == DLM_LOCK_RES_OWNER_UNKNOWN) {
			__dlm_wait_on_lockres(tmpres);
			BUG_ON(tmpres->owner == DLM_LOCK_RES_OWNER_UNKNOWN);
			spin_unlock(&tmpres->spinlock);
			dlm_lockres_put(tmpres);
			tmpres = NULL;
			goto lookup;
		}

		/* Wait on the resource purge to complete before continuing */
		if (tmpres->state & DLM_LOCK_RES_DROPPING_REF) {
			BUG_ON(tmpres->owner == dlm->node_num);
			__dlm_wait_on_lockres_flags(tmpres,
						    DLM_LOCK_RES_DROPPING_REF);
			spin_unlock(&tmpres->spinlock);
			dlm_lockres_put(tmpres);
			tmpres = NULL;
			goto lookup;
		}

		/* Grab inflight ref to pin the resource */
		dlm_lockres_grab_inflight_ref(dlm, tmpres);

		spin_unlock(&tmpres->spinlock);
		if (res)
			dlm_lockres_put(res);
		res = tmpres;
		goto leave;
	}

	if (!res) {
		spin_unlock(&dlm->spinlock);
		mlog(0, "allocating a new resource\n");
		/* nothing found and we need to allocate one. */
		alloc_mle = kmem_cache_alloc(dlm_mle_cache, GFP_NOFS);
		if (!alloc_mle)
			goto leave;
		res = dlm_new_lockres(dlm, lockid, namelen);
		if (!res)
			goto leave;
		goto lookup;
	}

	mlog(0, "no lockres found, allocated our own: %p\n", res);

	if (flags & LKM_LOCAL) {
		/* caller knows it's safe to assume it's not mastered elsewhere
		 * DONE!  return right away */
		spin_lock(&res->spinlock);
		dlm_change_lockres_owner(dlm, res, dlm->node_num);
		__dlm_insert_lockres(dlm, res);
		dlm_lockres_grab_inflight_ref(dlm, res);
		spin_unlock(&res->spinlock);
		spin_unlock(&dlm->spinlock);
		/* lockres still marked IN_PROGRESS */
		goto wake_waiters;
	}

	/* check master list to see if another node has started mastering it */
	spin_lock(&dlm->master_lock);

	/* if we found a block, wait for lock to be mastered by another node */
	blocked = dlm_find_mle(dlm, &mle, (char *)lockid, namelen);
	if (blocked) {
		int mig;
		if (mle->type == DLM_MLE_MASTER) {
			mlog(ML_ERROR, "master entry for nonexistent lock!\n");
			BUG();
		}
		mig = (mle->type == DLM_MLE_MIGRATION);
		/* if there is a migration in progress, let the migration
		 * finish before continuing.  we can wait for the absence
		 * of the MIGRATION mle: either the migrate finished or
		 * one of the nodes died and the mle was cleaned up.
		 * if there is a BLOCK here, but it already has a master
		 * set, we are too late.  the master does not have a ref
		 * for us in the refmap.  detach the mle and drop it.
		 * either way, go back to the top and start over. */
		if (mig || mle->master != O2NM_MAX_NODES) {
			BUG_ON(mig && mle->master == dlm->node_num);
			/* we arrived too late.  the master does not
			 * have a ref for us. retry. */
			mlog(0, "%s:%.*s: late on %s\n",
			     dlm->name, namelen, lockid,
			     mig ?  "MIGRATION" : "BLOCK");
			spin_unlock(&dlm->master_lock);
			spin_unlock(&dlm->spinlock);

			/* master is known, detach */
			if (!mig)
				dlm_mle_detach_hb_events(dlm, mle);
			dlm_put_mle(mle);
			mle = NULL;
			/* this is lame, but we can't wait on either
			 * the mle or lockres waitqueue here */
			if (mig)
				msleep(100);
			goto lookup;
		}
	} else {
		/* go ahead and try to master lock on this node */
		mle = alloc_mle;
		/* make sure this does not get freed below */
		alloc_mle = NULL;
		dlm_init_mle(mle, DLM_MLE_MASTER, dlm, res, NULL, 0);
		set_bit(dlm->node_num, mle->maybe_map);
		__dlm_insert_mle(dlm, mle);

		/* still holding the dlm spinlock, check the recovery map
		 * to see if there are any nodes that still need to be
		 * considered.  these will not appear in the mle nodemap
		 * but they might own this lockres.  wait on them. */
		bit = find_next_bit(dlm->recovery_map, O2NM_MAX_NODES, 0);
		if (bit < O2NM_MAX_NODES) {
			mlog(0, "%s: res %.*s, At least one node (%d) "
			     "to recover before lock mastery can begin\n",
			     dlm->name, namelen, (char *)lockid, bit);
			wait_on_recovery = 1;
		}
	}

	/* at this point there is either a DLM_MLE_BLOCK or a
	 * DLM_MLE_MASTER on the master list, so it's safe to add the
	 * lockres to the hashtable.  anyone who finds the lock will
	 * still have to wait on the IN_PROGRESS. */

	/* finally add the lockres to its hash bucket */
	__dlm_insert_lockres(dlm, res);

	/* Grab inflight ref to pin the resource */
	spin_lock(&res->spinlock);
	dlm_lockres_grab_inflight_ref(dlm, res);
	spin_unlock(&res->spinlock);

	/* get an extra ref on the mle in case this is a BLOCK
	 * if so, the creator of the BLOCK may try to put the last
	 * ref at this time in the assert master handler, so we
	 * need an extra one to keep from a bad ptr deref. */
	dlm_get_mle_inuse(mle);
	spin_unlock(&dlm->master_lock);
	spin_unlock(&dlm->spinlock);

redo_request:
	while (wait_on_recovery) {
		/* any cluster changes that occurred after dropping the
		 * dlm spinlock would be detectable be a change on the mle,
		 * so we only need to clear out the recovery map once. */
		if (dlm_is_recovery_lock(lockid, namelen)) {
			mlog(0, "%s: Recovery map is not empty, but must "
			     "master $RECOVERY lock now\n", dlm->name);
			if (!dlm_pre_master_reco_lockres(dlm, res))
				wait_on_recovery = 0;
			else {
				mlog(0, "%s: waiting 500ms for heartbeat state "
				    "change\n", dlm->name);
				msleep(500);
			}
			continue;
		}

		dlm_kick_recovery_thread(dlm);
		msleep(1000);
		dlm_wait_for_recovery(dlm);

		spin_lock(&dlm->spinlock);
		bit = find_next_bit(dlm->recovery_map, O2NM_MAX_NODES, 0);
		if (bit < O2NM_MAX_NODES) {
			mlog(0, "%s: res %.*s, At least one node (%d) "
			     "to recover before lock mastery can begin\n",
			     dlm->name, namelen, (char *)lockid, bit);
			wait_on_recovery = 1;
		} else
			wait_on_recovery = 0;
		spin_unlock(&dlm->spinlock);

		if (wait_on_recovery)
			dlm_wait_for_node_recovery(dlm, bit, 10000);
	}

	/* must wait for lock to be mastered elsewhere */
	if (blocked)
		goto wait;

	ret = -EINVAL;
	dlm_node_iter_init(mle->vote_map, &iter);
	while ((nodenum = dlm_node_iter_next(&iter)) >= 0) {
		ret = dlm_do_master_request(res, mle, nodenum);
		if (ret < 0)
			mlog_errno(ret);
		if (mle->master != O2NM_MAX_NODES) {
			/* found a master ! */
			if (mle->master <= nodenum)
				break;
			/* if our master request has not reached the master
			 * yet, keep going until it does.  this is how the
			 * master will know that asserts are needed back to
			 * the lower nodes. */
			mlog(0, "%s: res %.*s, Requests only up to %u but "
			     "master is %u, keep going\n", dlm->name, namelen,
			     lockid, nodenum, mle->master);
		}
	}

wait:
	/* keep going until the response map includes all nodes */
	ret = dlm_wait_for_lock_mastery(dlm, res, mle, &blocked);
	if (ret < 0) {
		wait_on_recovery = 1;
		mlog(0, "%s: res %.*s, Node map changed, redo the master "
		     "request now, blocked=%d\n", dlm->name, res->lockname.len,
		     res->lockname.name, blocked);
		if (++tries > 20) {
			mlog(ML_ERROR, "%s: res %.*s, Spinning on "
			     "dlm_wait_for_lock_mastery, blocked = %d\n",
			     dlm->name, res->lockname.len,
			     res->lockname.name, blocked);
			dlm_print_one_lock_resource(res);
			dlm_print_one_mle(mle);
			tries = 0;
		}
		goto redo_request;
	}

	mlog(0, "%s: res %.*s, Mastered by %u\n", dlm->name, res->lockname.len,
	     res->lockname.name, res->owner);
	/* make sure we never continue without this */
	BUG_ON(res->owner == O2NM_MAX_NODES);

	/* master is known, detach if not already detached */
	dlm_mle_detach_hb_events(dlm, mle);
	dlm_put_mle(mle);
	/* put the extra ref */
	dlm_put_mle_inuse(mle);

wake_waiters:
	spin_lock(&res->spinlock);
	res->state &= ~DLM_LOCK_RES_IN_PROGRESS;
	spin_unlock(&res->spinlock);
	wake_up(&res->wq);

leave:
	/* need to free the unused mle */
	if (alloc_mle)
		kmem_cache_free(dlm_mle_cache, alloc_mle);

	return res;
}


#define DLM_MASTERY_TIMEOUT_MS   5000

static int dlm_wait_for_lock_mastery(struct dlm_ctxt *dlm,
				     struct dlm_lock_resource *res,
				     struct dlm_master_list_entry *mle,
				     int *blocked)
{
	u8 m;
	int ret, bit;
	int map_changed, voting_done;
	int assert, sleep;

recheck:
	ret = 0;
	assert = 0;

	/* check if another node has already become the owner */
	spin_lock(&res->spinlock);
	if (res->owner != DLM_LOCK_RES_OWNER_UNKNOWN) {
		mlog(0, "%s:%.*s: owner is suddenly %u\n", dlm->name,
		     res->lockname.len, res->lockname.name, res->owner);
		spin_unlock(&res->spinlock);
		/* this will cause the master to re-assert across
		 * the whole cluster, freeing up mles */
		if (res->owner != dlm->node_num) {
			ret = dlm_do_master_request(res, mle, res->owner);
			if (ret < 0) {
				/* give recovery a chance to run */
				mlog(ML_ERROR, "link to %u went down?: %d\n", res->owner, ret);
				msleep(500);
				goto recheck;
			}
		}
		ret = 0;
		goto leave;
	}
	spin_unlock(&res->spinlock);

	spin_lock(&mle->spinlock);
	m = mle->master;
	map_changed = (memcmp(mle->vote_map, mle->node_map,
			      sizeof(mle->vote_map)) != 0);
	voting_done = (memcmp(mle->vote_map, mle->response_map,
			     sizeof(mle->vote_map)) == 0);

	/* restart if we hit any errors */
	if (map_changed) {
		int b;
		mlog(0, "%s: %.*s: node map changed, restarting\n",
		     dlm->name, res->lockname.len, res->lockname.name);
		ret = dlm_restart_lock_mastery(dlm, res, mle, *blocked);
		b = (mle->type == DLM_MLE_BLOCK);
		if ((*blocked && !b) || (!*blocked && b)) {
			mlog(0, "%s:%.*s: status change: old=%d new=%d\n",
			     dlm->name, res->lockname.len, res->lockname.name,
			     *blocked, b);
			*blocked = b;
		}
		spin_unlock(&mle->spinlock);
		if (ret < 0) {
			mlog_errno(ret);
			goto leave;
		}
		mlog(0, "%s:%.*s: restart lock mastery succeeded, "
		     "rechecking now\n", dlm->name, res->lockname.len,
		     res->lockname.name);
		goto recheck;
	} else {
		if (!voting_done) {
			mlog(0, "map not changed and voting not done "
			     "for %s:%.*s\n", dlm->name, res->lockname.len,
			     res->lockname.name);
		}
	}

	if (m != O2NM_MAX_NODES) {
		/* another node has done an assert!
		 * all done! */
		sleep = 0;
	} else {
		sleep = 1;
		/* have all nodes responded? */
		if (voting_done && !*blocked) {
			bit = find_next_bit(mle->maybe_map, O2NM_MAX_NODES, 0);
			if (dlm->node_num <= bit) {
				/* my node number is lowest.
			 	 * now tell other nodes that I am
				 * mastering this. */
				mle->master = dlm->node_num;
				/* ref was grabbed in get_lock_resource
				 * will be dropped in dlmlock_master */
				assert = 1;
				sleep = 0;
			}
			/* if voting is done, but we have not received
			 * an assert master yet, we must sleep */
		}
	}

	spin_unlock(&mle->spinlock);

	/* sleep if we haven't finished voting yet */
	if (sleep) {
		unsigned long timeo = msecs_to_jiffies(DLM_MASTERY_TIMEOUT_MS);

		/*
		if (atomic_read(&mle->mle_refs.refcount) < 2)
			mlog(ML_ERROR, "mle (%p) refs=%d, name=%.*s\n", mle,
			atomic_read(&mle->mle_refs.refcount),
			res->lockname.len, res->lockname.name);
		*/
		atomic_set(&mle->woken, 0);
		(void)wait_event_timeout(mle->wq,
					 (atomic_read(&mle->woken) == 1),
					 timeo);
		if (res->owner == O2NM_MAX_NODES) {
			mlog(0, "%s:%.*s: waiting again\n", dlm->name,
			     res->lockname.len, res->lockname.name);
			goto recheck;
		}
		mlog(0, "done waiting, master is %u\n", res->owner);
		ret = 0;
		goto leave;
	}

	ret = 0;   /* done */
	if (assert) {
		m = dlm->node_num;
		mlog(0, "about to master %.*s here, this=%u\n",
		     res->lockname.len, res->lockname.name, m);
		ret = dlm_do_assert_master(dlm, res, mle->vote_map, 0);
		if (ret) {
			/* This is a failure in the network path,
			 * not in the response to the assert_master
			 * (any nonzero response is a BUG on this node).
			 * Most likely a socket just got disconnected
			 * due to node death. */
			mlog_errno(ret);
		}
		/* no longer need to restart lock mastery.
		 * all living nodes have been contacted. */
		ret = 0;
	}

	/* set the lockres owner */
	spin_lock(&res->spinlock);
	/* mastery reference obtained either during
	 * assert_master_handler or in get_lock_resource */
	dlm_change_lockres_owner(dlm, res, m);
	spin_unlock(&res->spinlock);

leave:
	return ret;
}

struct dlm_bitmap_diff_iter
{
	int curnode;
	unsigned long *orig_bm;
	unsigned long *cur_bm;
	unsigned long diff_bm[BITS_TO_LONGS(O2NM_MAX_NODES)];
};

enum dlm_node_state_change
{
	NODE_DOWN = -1,
	NODE_NO_CHANGE = 0,
	NODE_UP
};

static void dlm_bitmap_diff_iter_init(struct dlm_bitmap_diff_iter *iter,
				      unsigned long *orig_bm,
				      unsigned long *cur_bm)
{
	unsigned long p1, p2;
	int i;

	iter->curnode = -1;
	iter->orig_bm = orig_bm;
	iter->cur_bm = cur_bm;

	for (i = 0; i < BITS_TO_LONGS(O2NM_MAX_NODES); i++) {
       		p1 = *(iter->orig_bm + i);
	       	p2 = *(iter->cur_bm + i);
		iter->diff_bm[i] = (p1 & ~p2) | (p2 & ~p1);
	}
}

static int dlm_bitmap_diff_iter_next(struct dlm_bitmap_diff_iter *iter,
				     enum dlm_node_state_change *state)
{
	int bit;

	if (iter->curnode >= O2NM_MAX_NODES)
		return -ENOENT;

	bit = find_next_bit(iter->diff_bm, O2NM_MAX_NODES,
			    iter->curnode+1);
	if (bit >= O2NM_MAX_NODES) {
		iter->curnode = O2NM_MAX_NODES;
		return -ENOENT;
	}

	/* if it was there in the original then this node died */
	if (test_bit(bit, iter->orig_bm))
		*state = NODE_DOWN;
	else
		*state = NODE_UP;

	iter->curnode = bit;
	return bit;
}


static int dlm_restart_lock_mastery(struct dlm_ctxt *dlm,
				    struct dlm_lock_resource *res,
				    struct dlm_master_list_entry *mle,
				    int blocked)
{
	struct dlm_bitmap_diff_iter bdi;
	enum dlm_node_state_change sc;
	int node;
	int ret = 0;

	mlog(0, "something happened such that the "
	     "master process may need to be restarted!\n");

	assert_spin_locked(&mle->spinlock);

	dlm_bitmap_diff_iter_init(&bdi, mle->vote_map, mle->node_map);
	node = dlm_bitmap_diff_iter_next(&bdi, &sc);
	while (node >= 0) {
		if (sc == NODE_UP) {
			/* a node came up.  clear any old vote from
			 * the response map and set it in the vote map
			 * then restart the mastery. */
			mlog(ML_NOTICE, "node %d up while restarting\n", node);

			/* redo the master request, but only for the new node */
			mlog(0, "sending request to new node\n");
			clear_bit(node, mle->response_map);
			set_bit(node, mle->vote_map);
		} else {
			mlog(ML_ERROR, "node down! %d\n", node);
			if (blocked) {
				int lowest = find_next_bit(mle->maybe_map,
						       O2NM_MAX_NODES, 0);

				/* act like it was never there */
				clear_bit(node, mle->maybe_map);

			       	if (node == lowest) {
					mlog(0, "expected master %u died"
					    " while this node was blocked "
					    "waiting on it!\n", node);
					lowest = find_next_bit(mle->maybe_map,
						       	O2NM_MAX_NODES,
						       	lowest+1);
					if (lowest < O2NM_MAX_NODES) {
						mlog(0, "%s:%.*s:still "
						     "blocked. waiting on %u "
						     "now\n", dlm->name,
						     res->lockname.len,
						     res->lockname.name,
						     lowest);
					} else {
						/* mle is an MLE_BLOCK, but
						 * there is now nothing left to
						 * block on.  we need to return
						 * all the way back out and try
						 * again with an MLE_MASTER.
						 * dlm_do_local_recovery_cleanup
						 * has already run, so the mle
						 * refcount is ok */
						mlog(0, "%s:%.*s: no "
						     "longer blocking. try to "
						     "master this here\n",
						     dlm->name,
						     res->lockname.len,
						     res->lockname.name);
						mle->type = DLM_MLE_MASTER;
						mle->mleres = res;
					}
				}
			}

			/* now blank out everything, as if we had never
			 * contacted anyone */
			memset(mle->maybe_map, 0, sizeof(mle->maybe_map));
			memset(mle->response_map, 0, sizeof(mle->response_map));
			/* reset the vote_map to the current node_map */
			memcpy(mle->vote_map, mle->node_map,
			       sizeof(mle->node_map));
			/* put myself into the maybe map */
			if (mle->type != DLM_MLE_BLOCK)
				set_bit(dlm->node_num, mle->maybe_map);
		}
		ret = -EAGAIN;
		node = dlm_bitmap_diff_iter_next(&bdi, &sc);
	}
	return ret;
}


/*
 * DLM_MASTER_REQUEST_MSG
 *
 * returns: 0 on success,
 *          -errno on a network error
 *
 * on error, the caller should assume the target node is "dead"
 *
 */

static int dlm_do_master_request(struct dlm_lock_resource *res,
				 struct dlm_master_list_entry *mle, int to)
{
	struct dlm_ctxt *dlm = mle->dlm;
	struct dlm_master_request request;
	int ret, response=0, resend;

	memset(&request, 0, sizeof(request));
	request.node_idx = dlm->node_num;

	BUG_ON(mle->type == DLM_MLE_MIGRATION);

	request.namelen = (u8)mle->mnamelen;
	memcpy(request.name, mle->mname, request.namelen);

again:
	ret = o2net_send_message(DLM_MASTER_REQUEST_MSG, dlm->key, &request,
				 sizeof(request), to, &response);
	if (ret < 0)  {
		if (ret == -ESRCH) {
			/* should never happen */
			mlog(ML_ERROR, "TCP stack not ready!\n");
			BUG();
		} else if (ret == -EINVAL) {
			mlog(ML_ERROR, "bad args passed to o2net!\n");
			BUG();
		} else if (ret == -ENOMEM) {
			mlog(ML_ERROR, "out of memory while trying to send "
			     "network message!  retrying\n");
			/* this is totally crude */
			msleep(50);
			goto again;
		} else if (!dlm_is_host_down(ret)) {
			/* not a network error. bad. */
			mlog_errno(ret);
			mlog(ML_ERROR, "unhandled error!");
			BUG();
		}
		/* all other errors should be network errors,
		 * and likely indicate node death */
		mlog(ML_ERROR, "link to %d went down!\n", to);
		goto out;
	}

	ret = 0;
	resend = 0;
	spin_lock(&mle->spinlock);
	switch (response) {
		case DLM_MASTER_RESP_YES:
			set_bit(to, mle->response_map);
			mlog(0, "node %u is the master, response=YES\n", to);
			mlog(0, "%s:%.*s: master node %u now knows I have a "
			     "reference\n", dlm->name, res->lockname.len,
			     res->lockname.name, to);
			mle->master = to;
			break;
		case DLM_MASTER_RESP_NO:
			mlog(0, "node %u not master, response=NO\n", to);
			set_bit(to, mle->response_map);
			break;
		case DLM_MASTER_RESP_MAYBE:
			mlog(0, "node %u not master, response=MAYBE\n", to);
			set_bit(to, mle->response_map);
			set_bit(to, mle->maybe_map);
			break;
		case DLM_MASTER_RESP_ERROR:
			mlog(0, "node %u hit an error, resending\n", to);
			resend = 1;
			response = 0;
			break;
		default:
			mlog(ML_ERROR, "bad response! %u\n", response);
			BUG();
	}
	spin_unlock(&mle->spinlock);
	if (resend) {
		/* this is also totally crude */
		msleep(50);
		goto again;
	}

out:
	return ret;
}

/*
 * locks that can be taken here:
 * dlm->spinlock
 * res->spinlock
 * mle->spinlock
 * dlm->master_list
 *
 * if possible, TRIM THIS DOWN!!!
 */
int dlm_master_request_handler(struct o2net_msg *msg, u32 len, void *data,
			       void **ret_data)
{
	u8 response = DLM_MASTER_RESP_MAYBE;
	struct dlm_ctxt *dlm = data;
	struct dlm_lock_resource *res = NULL;
	struct dlm_master_request *request = (struct dlm_master_request *) msg->buf;
	struct dlm_master_list_entry *mle = NULL, *tmpmle = NULL;
	char *name;
	unsigned int namelen, hash;
	int found, ret;
	int set_maybe;
	int dispatch_assert = 0;

	if (!dlm_grab(dlm))
		return DLM_MASTER_RESP_NO;

	if (!dlm_domain_fully_joined(dlm)) {
		response = DLM_MASTER_RESP_NO;
		goto send_response;
	}

	name = request->name;
	namelen = request->namelen;
	hash = dlm_lockid_hash(name, namelen);

	if (namelen > DLM_LOCKID_NAME_MAX) {
		response = DLM_IVBUFLEN;
		goto send_response;
	}

way_up_top:
	spin_lock(&dlm->spinlock);
	res = __dlm_lookup_lockres(dlm, name, namelen, hash);
	if (res) {
		spin_unlock(&dlm->spinlock);

		/* take care of the easy cases up front */
		spin_lock(&res->spinlock);
		if (res->state & (DLM_LOCK_RES_RECOVERING|
				  DLM_LOCK_RES_MIGRATING)) {
			spin_unlock(&res->spinlock);
			mlog(0, "returning DLM_MASTER_RESP_ERROR since res is "
			     "being recovered/migrated\n");
			response = DLM_MASTER_RESP_ERROR;
			if (mle)
				kmem_cache_free(dlm_mle_cache, mle);
			goto send_response;
		}

		if (res->owner == dlm->node_num) {
			dlm_lockres_set_refmap_bit(dlm, res, request->node_idx);
			spin_unlock(&res->spinlock);
			response = DLM_MASTER_RESP_YES;
			if (mle)
				kmem_cache_free(dlm_mle_cache, mle);

			/* this node is the owner.
			 * there is some extra work that needs to
			 * happen now.  the requesting node has
			 * caused all nodes up to this one to
			 * create mles.  this node now needs to
			 * go back and clean those up. */
			dispatch_assert = 1;
			goto send_response;
		} else if (res->owner != DLM_LOCK_RES_OWNER_UNKNOWN) {
			spin_unlock(&res->spinlock);
			// mlog(0, "node %u is the master\n", res->owner);
			response = DLM_MASTER_RESP_NO;
			if (mle)
				kmem_cache_free(dlm_mle_cache, mle);
			goto send_response;
		}

		/* ok, there is no owner.  either this node is
		 * being blocked, or it is actively trying to
		 * master this lock. */
		if (!(res->state & DLM_LOCK_RES_IN_PROGRESS)) {
			mlog(ML_ERROR, "lock with no owner should be "
			     "in-progress!\n");
			BUG();
		}

		// mlog(0, "lockres is in progress...\n");
		spin_lock(&dlm->master_lock);
		found = dlm_find_mle(dlm, &tmpmle, name, namelen);
		if (!found) {
			mlog(ML_ERROR, "no mle found for this lock!\n");
			BUG();
		}
		set_maybe = 1;
		spin_lock(&tmpmle->spinlock);
		if (tmpmle->type == DLM_MLE_BLOCK) {
			// mlog(0, "this node is waiting for "
			// "lockres to be mastered\n");
			response = DLM_MASTER_RESP_NO;
		} else if (tmpmle->type == DLM_MLE_MIGRATION) {
			mlog(0, "node %u is master, but trying to migrate to "
			     "node %u.\n", tmpmle->master, tmpmle->new_master);
			if (tmpmle->master == dlm->node_num) {
				mlog(ML_ERROR, "no owner on lockres, but this "
				     "node is trying to migrate it to %u?!\n",
				     tmpmle->new_master);
				BUG();
			} else {
				/* the real master can respond on its own */
				response = DLM_MASTER_RESP_NO;
			}
		} else if (tmpmle->master != DLM_LOCK_RES_OWNER_UNKNOWN) {
			set_maybe = 0;
			if (tmpmle->master == dlm->node_num) {
				response = DLM_MASTER_RESP_YES;
				/* this node will be the owner.
				 * go back and clean the mles on any
				 * other nodes */
				dispatch_assert = 1;
				dlm_lockres_set_refmap_bit(dlm, res,
							   request->node_idx);
			} else
				response = DLM_MASTER_RESP_NO;
		} else {
			// mlog(0, "this node is attempting to "
			// "master lockres\n");
			response = DLM_MASTER_RESP_MAYBE;
		}
		if (set_maybe)
			set_bit(request->node_idx, tmpmle->maybe_map);
		spin_unlock(&tmpmle->spinlock);

		spin_unlock(&dlm->master_lock);
		spin_unlock(&res->spinlock);

		/* keep the mle attached to heartbeat events */
		dlm_put_mle(tmpmle);
		if (mle)
			kmem_cache_free(dlm_mle_cache, mle);
		goto send_response;
	}

	/*
	 * lockres doesn't exist on this node
	 * if there is an MLE_BLOCK, return NO
	 * if there is an MLE_MASTER, return MAYBE
	 * otherwise, add an MLE_BLOCK, return NO
	 */
	spin_lock(&dlm->master_lock);
	found = dlm_find_mle(dlm, &tmpmle, name, namelen);
	if (!found) {
		/* this lockid has never been seen on this node yet */
		// mlog(0, "no mle found\n");
		if (!mle) {
			spin_unlock(&dlm->master_lock);
			spin_unlock(&dlm->spinlock);

			mle = kmem_cache_alloc(dlm_mle_cache, GFP_NOFS);
			if (!mle) {
				response = DLM_MASTER_RESP_ERROR;
				mlog_errno(-ENOMEM);
				goto send_response;
			}
			goto way_up_top;
		}

		// mlog(0, "this is second time thru, already allocated, "
		// "add the block.\n");
		dlm_init_mle(mle, DLM_MLE_BLOCK, dlm, NULL, name, namelen);
		set_bit(request->node_idx, mle->maybe_map);
		__dlm_insert_mle(dlm, mle);
		response = DLM_MASTER_RESP_NO;
	} else {
		// mlog(0, "mle was found\n");
		set_maybe = 1;
		spin_lock(&tmpmle->spinlock);
		if (tmpmle->master == dlm->node_num) {
			mlog(ML_ERROR, "no lockres, but an mle with this node as master!\n");
			BUG();
		}
		if (tmpmle->type == DLM_MLE_BLOCK)
			response = DLM_MASTER_RESP_NO;
		else if (tmpmle->type == DLM_MLE_MIGRATION) {
			mlog(0, "migration mle was found (%u->%u)\n",
			     tmpmle->master, tmpmle->new_master);
			/* real master can respond on its own */
			response = DLM_MASTER_RESP_NO;
		} else
			response = DLM_MASTER_RESP_MAYBE;
		if (set_maybe)
			set_bit(request->node_idx, tmpmle->maybe_map);
		spin_unlock(&tmpmle->spinlock);
	}
	spin_unlock(&dlm->master_lock);
	spin_unlock(&dlm->spinlock);

	if (found) {
		/* keep the mle attached to heartbeat events */
		dlm_put_mle(tmpmle);
	}
send_response:
	/*
	 * __dlm_lookup_lockres() grabbed a reference to this lockres.
	 * The reference is released by dlm_assert_master_worker() under
	 * the call to dlm_dispatch_assert_master().  If
	 * dlm_assert_master_worker() isn't called, we drop it here.
	 */
	if (dispatch_assert) {
		if (response != DLM_MASTER_RESP_YES)
			mlog(ML_ERROR, "invalid response %d\n", response);
		if (!res) {
			mlog(ML_ERROR, "bad lockres while trying to assert!\n");
			BUG();
		}
		mlog(0, "%u is the owner of %.*s, cleaning everyone else\n",
			     dlm->node_num, res->lockname.len, res->lockname.name);
		ret = dlm_dispatch_assert_master(dlm, res, 0, request->node_idx,
						 DLM_ASSERT_MASTER_MLE_CLEANUP);
		if (ret < 0) {
			mlog(ML_ERROR, "failed to dispatch assert master work\n");
			response = DLM_MASTER_RESP_ERROR;
			dlm_lockres_put(res);
		}
	} else {
		if (res)
			dlm_lockres_put(res);
	}

	dlm_put(dlm);
	return response;
}

/*
 * DLM_ASSERT_MASTER_MSG
 */


/*
 * NOTE: this can be used for debugging
 * can periodically run all locks owned by this node
 * and re-assert across the cluster...
 */
static int dlm_do_assert_master(struct dlm_ctxt *dlm,
				struct dlm_lock_resource *res,
				void *nodemap, u32 flags)
{
	struct dlm_assert_master assert;
	int to, tmpret;
	struct dlm_node_iter iter;
	int ret = 0;
	int reassert;
	const char *lockname = res->lockname.name;
	unsigned int namelen = res->lockname.len;

	BUG_ON(namelen > O2NM_MAX_NAME_LEN);

	spin_lock(&res->spinlock);
	res->state |= DLM_LOCK_RES_SETREF_INPROG;
	spin_unlock(&res->spinlock);

again:
	reassert = 0;

	/* note that if this nodemap is empty, it returns 0 */
	dlm_node_iter_init(nodemap, &iter);
	while ((to = dlm_node_iter_next(&iter)) >= 0) {
		int r = 0;
		struct dlm_master_list_entry *mle = NULL;

		mlog(0, "sending assert master to %d (%.*s)\n", to,
		     namelen, lockname);
		memset(&assert, 0, sizeof(assert));
		assert.node_idx = dlm->node_num;
		assert.namelen = namelen;
		memcpy(assert.name, lockname, namelen);
		assert.flags = cpu_to_be32(flags);

		tmpret = o2net_send_message(DLM_ASSERT_MASTER_MSG, dlm->key,
					    &assert, sizeof(assert), to, &r);
		if (tmpret < 0) {
			mlog(ML_ERROR, "Error %d when sending message %u (key "
			     "0x%x) to node %u\n", tmpret,
			     DLM_ASSERT_MASTER_MSG, dlm->key, to);
			if (!dlm_is_host_down(tmpret)) {
				mlog(ML_ERROR, "unhandled error=%d!\n", tmpret);
				BUG();
			}
			/* a node died.  finish out the rest of the nodes. */
			mlog(0, "link to %d went down!\n", to);
			/* any nonzero status return will do */
			ret = tmpret;
			r = 0;
		} else if (r < 0) {
			/* ok, something horribly messed.  kill thyself. */
			mlog(ML_ERROR,"during assert master of %.*s to %u, "
			     "got %d.\n", namelen, lockname, to, r);
			spin_lock(&dlm->spinlock);
			spin_lock(&dlm->master_lock);
			if (dlm_find_mle(dlm, &mle, (char *)lockname,
					 namelen)) {
				dlm_print_one_mle(mle);
				__dlm_put_mle(mle);
			}
			spin_unlock(&dlm->master_lock);
			spin_unlock(&dlm->spinlock);
			BUG();
		}

		if (r & DLM_ASSERT_RESPONSE_REASSERT &&
		    !(r & DLM_ASSERT_RESPONSE_MASTERY_REF)) {
				mlog(ML_ERROR, "%.*s: very strange, "
				     "master MLE but no lockres on %u\n",
				     namelen, lockname, to);
		}

		if (r & DLM_ASSERT_RESPONSE_REASSERT) {
			mlog(0, "%.*s: node %u create mles on other "
			     "nodes and requests a re-assert\n",
			     namelen, lockname, to);
			reassert = 1;
		}
		if (r & DLM_ASSERT_RESPONSE_MASTERY_REF) {
			mlog(0, "%.*s: node %u has a reference to this "
			     "lockres, set the bit in the refmap\n",
			     namelen, lockname, to);
			spin_lock(&res->spinlock);
			dlm_lockres_set_refmap_bit(dlm, res, to);
			spin_unlock(&res->spinlock);
		}
	}

	if (reassert)
		goto again;

	spin_lock(&res->spinlock);
	res->state &= ~DLM_LOCK_RES_SETREF_INPROG;
	spin_unlock(&res->spinlock);
	wake_up(&res->wq);

	return ret;
}

/*
 * locks that can be taken here:
 * dlm->spinlock
 * res->spinlock
 * mle->spinlock
 * dlm->master_list
 *
 * if possible, TRIM THIS DOWN!!!
 */
int dlm_assert_master_handler(struct o2net_msg *msg, u32 len, void *data,
			      void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_master_list_entry *mle = NULL;
	struct dlm_assert_master *assert = (struct dlm_assert_master *)msg->buf;
	struct dlm_lock_resource *res = NULL;
	char *name;
	unsigned int namelen, hash;
	u32 flags;
	int master_request = 0, have_lockres_ref = 0;
	int ret = 0;

	if (!dlm_grab(dlm))
		return 0;

	name = assert->name;
	namelen = assert->namelen;
	hash = dlm_lockid_hash(name, namelen);
	flags = be32_to_cpu(assert->flags);

	if (namelen > DLM_LOCKID_NAME_MAX) {
		mlog(ML_ERROR, "Invalid name length!");
		goto done;
	}

	spin_lock(&dlm->spinlock);

	if (flags)
		mlog(0, "assert_master with flags: %u\n", flags);

	/* find the MLE */
	spin_lock(&dlm->master_lock);
	if (!dlm_find_mle(dlm, &mle, name, namelen)) {
		/* not an error, could be master just re-asserting */
		mlog(0, "just got an assert_master from %u, but no "
		     "MLE for it! (%.*s)\n", assert->node_idx,
		     namelen, name);
	} else {
		int bit = find_next_bit (mle->maybe_map, O2NM_MAX_NODES, 0);
		if (bit >= O2NM_MAX_NODES) {
			/* not necessarily an error, though less likely.
			 * could be master just re-asserting. */
			mlog(0, "no bits set in the maybe_map, but %u "
			     "is asserting! (%.*s)\n", assert->node_idx,
			     namelen, name);
		} else if (bit != assert->node_idx) {
			if (flags & DLM_ASSERT_MASTER_MLE_CLEANUP) {
				mlog(0, "master %u was found, %u should "
				     "back off\n", assert->node_idx, bit);
			} else {
				/* with the fix for bug 569, a higher node
				 * number winning the mastery will respond
				 * YES to mastery requests, but this node
				 * had no way of knowing.  let it pass. */
				mlog(0, "%u is the lowest node, "
				     "%u is asserting. (%.*s)  %u must "
				     "have begun after %u won.\n", bit,
				     assert->node_idx, namelen, name, bit,
				     assert->node_idx);
			}
		}
		if (mle->type == DLM_MLE_MIGRATION) {
			if (flags & DLM_ASSERT_MASTER_MLE_CLEANUP) {
				mlog(0, "%s:%.*s: got cleanup assert"
				     " from %u for migration\n",
				     dlm->name, namelen, name,
				     assert->node_idx);
			} else if (!(flags & DLM_ASSERT_MASTER_FINISH_MIGRATION)) {
				mlog(0, "%s:%.*s: got unrelated assert"
				     " from %u for migration, ignoring\n",
				     dlm->name, namelen, name,
				     assert->node_idx);
				__dlm_put_mle(mle);
				spin_unlock(&dlm->master_lock);
				spin_unlock(&dlm->spinlock);
				goto done;
			}
		}
	}
	spin_unlock(&dlm->master_lock);

	/* ok everything checks out with the MLE
	 * now check to see if there is a lockres */
	res = __dlm_lookup_lockres(dlm, name, namelen, hash);
	if (res) {
		spin_lock(&res->spinlock);
		if (res->state & DLM_LOCK_RES_RECOVERING)  {
			mlog(ML_ERROR, "%u asserting but %.*s is "
			     "RECOVERING!\n", assert->node_idx, namelen, name);
			goto kill;
		}
		if (!mle) {
			if (res->owner != DLM_LOCK_RES_OWNER_UNKNOWN &&
			    res->owner != assert->node_idx) {
				mlog(ML_ERROR, "DIE! Mastery assert from %u, "
				     "but current owner is %u! (%.*s)\n",
				     assert->node_idx, res->owner, namelen,
				     name);
				__dlm_print_one_lock_resource(res);
				BUG();
			}
		} else if (mle->type != DLM_MLE_MIGRATION) {
			if (res->owner != DLM_LOCK_RES_OWNER_UNKNOWN) {
				/* owner is just re-asserting */
				if (res->owner == assert->node_idx) {
					mlog(0, "owner %u re-asserting on "
					     "lock %.*s\n", assert->node_idx,
					     namelen, name);
					goto ok;
				}
				mlog(ML_ERROR, "got assert_master from "
				     "node %u, but %u is the owner! "
				     "(%.*s)\n", assert->node_idx,
				     res->owner, namelen, name);
				goto kill;
			}
			if (!(res->state & DLM_LOCK_RES_IN_PROGRESS)) {
				mlog(ML_ERROR, "got assert from %u, but lock "
				     "with no owner should be "
				     "in-progress! (%.*s)\n",
				     assert->node_idx,
				     namelen, name);
				goto kill;
			}
		} else /* mle->type == DLM_MLE_MIGRATION */ {
			/* should only be getting an assert from new master */
			if (assert->node_idx != mle->new_master) {
				mlog(ML_ERROR, "got assert from %u, but "
				     "new master is %u, and old master "
				     "was %u (%.*s)\n",
				     assert->node_idx, mle->new_master,
				     mle->master, namelen, name);
				goto kill;
			}

		}
ok:
		spin_unlock(&res->spinlock);
	}

	// mlog(0, "woo!  got an assert_master from node %u!\n",
	// 	     assert->node_idx);
	if (mle) {
		int extra_ref = 0;
		int nn = -1;
		int rr, err = 0;

		spin_lock(&mle->spinlock);
		if (mle->type == DLM_MLE_BLOCK || mle->type == DLM_MLE_MIGRATION)
			extra_ref = 1;
		else {
			/* MASTER mle: if any bits set in the response map
			 * then the calling node needs to re-assert to clear
			 * up nodes that this node contacted */
			while ((nn = find_next_bit (mle->response_map, O2NM_MAX_NODES,
						    nn+1)) < O2NM_MAX_NODES) {
				if (nn != dlm->node_num && nn != assert->node_idx) {
					master_request = 1;
					break;
				}
			}
		}
		mle->master = assert->node_idx;
		atomic_set(&mle->woken, 1);
		wake_up(&mle->wq);
		spin_unlock(&mle->spinlock);

		if (res) {
			int wake = 0;
			spin_lock(&res->spinlock);
			if (mle->type == DLM_MLE_MIGRATION) {
				mlog(0, "finishing off migration of lockres %.*s, "
			     		"from %u to %u\n",
			       		res->lockname.len, res->lockname.name,
			       		dlm->node_num, mle->new_master);
				res->state &= ~DLM_LOCK_RES_MIGRATING;
				wake = 1;
				dlm_change_lockres_owner(dlm, res, mle->new_master);
				BUG_ON(res->state & DLM_LOCK_RES_DIRTY);
			} else {
				dlm_change_lockres_owner(dlm, res, mle->master);
			}
			spin_unlock(&res->spinlock);
			have_lockres_ref = 1;
			if (wake)
				wake_up(&res->wq);
		}

		/* master is known, detach if not already detached.
		 * ensures that only one assert_master call will happen
		 * on this mle. */
		spin_lock(&dlm->master_lock);

		rr = atomic_read(&mle->mle_refs.refcount);
		if (mle->inuse > 0) {
			if (extra_ref && rr < 3)
				err = 1;
			else if (!extra_ref && rr < 2)
				err = 1;
		} else {
			if (extra_ref && rr < 2)
				err = 1;
			else if (!extra_ref && rr < 1)
				err = 1;
		}
		if (err) {
			mlog(ML_ERROR, "%s:%.*s: got assert master from %u "
			     "that will mess up this node, refs=%d, extra=%d, "
			     "inuse=%d\n", dlm->name, namelen, name,
			     assert->node_idx, rr, extra_ref, mle->inuse);
			dlm_print_one_mle(mle);
		}
		__dlm_unlink_mle(dlm, mle);
		__dlm_mle_detach_hb_events(dlm, mle);
		__dlm_put_mle(mle);
		if (extra_ref) {
			/* the assert master message now balances the extra
		 	 * ref given by the master / migration request message.
		 	 * if this is the last put, it will be removed
		 	 * from the list. */
			__dlm_put_mle(mle);
		}
		spin_unlock(&dlm->master_lock);
	} else if (res) {
		if (res->owner != assert->node_idx) {
			mlog(0, "assert_master from %u, but current "
			     "owner is %u (%.*s), no mle\n", assert->node_idx,
			     res->owner, namelen, name);
		}
	}
	spin_unlock(&dlm->spinlock);

done:
	ret = 0;
	if (res) {
		spin_lock(&res->spinlock);
		res->state |= DLM_LOCK_RES_SETREF_INPROG;
		spin_unlock(&res->spinlock);
		*ret_data = (void *)res;
	}
	dlm_put(dlm);
	if (master_request) {
		mlog(0, "need to tell master to reassert\n");
		/* positive. negative would shoot down the node. */
		ret |= DLM_ASSERT_RESPONSE_REASSERT;
		if (!have_lockres_ref) {
			mlog(ML_ERROR, "strange, got assert from %u, MASTER "
			     "mle present here for %s:%.*s, but no lockres!\n",
			     assert->node_idx, dlm->name, namelen, name);
		}
	}
	if (have_lockres_ref) {
		/* let the master know we have a reference to the lockres */
		ret |= DLM_ASSERT_RESPONSE_MASTERY_REF;
		mlog(0, "%s:%.*s: got assert from %u, need a ref\n",
		     dlm->name, namelen, name, assert->node_idx);
	}
	return ret;

kill:
	/* kill the caller! */
	mlog(ML_ERROR, "Bad message received from another node.  Dumping state "
	     "and killing the other node now!  This node is OK and can continue.\n");
	__dlm_print_one_lock_resource(res);
	spin_unlock(&res->spinlock);
	spin_unlock(&dlm->spinlock);
	*ret_data = (void *)res;
	dlm_put(dlm);
	return -EINVAL;
}

void dlm_assert_master_post_handler(int status, void *data, void *ret_data)
{
	struct dlm_lock_resource *res = (struct dlm_lock_resource *)ret_data;

	if (ret_data) {
		spin_lock(&res->spinlock);
		res->state &= ~DLM_LOCK_RES_SETREF_INPROG;
		spin_unlock(&res->spinlock);
		wake_up(&res->wq);
		dlm_lockres_put(res);
	}
	return;
}

int dlm_dispatch_assert_master(struct dlm_ctxt *dlm,
			       struct dlm_lock_resource *res,
			       int ignore_higher, u8 request_from, u32 flags)
{
	struct dlm_work_item *item;
	item = kzalloc(sizeof(*item), GFP_ATOMIC);
	if (!item)
		return -ENOMEM;


	/* queue up work for dlm_assert_master_worker */
	dlm_grab(dlm);  /* get an extra ref for the work item */
	dlm_init_work_item(dlm, item, dlm_assert_master_worker, NULL);
	item->u.am.lockres = res; /* already have a ref */
	/* can optionally ignore node numbers higher than this node */
	item->u.am.ignore_higher = ignore_higher;
	item->u.am.request_from = request_from;
	item->u.am.flags = flags;

	if (ignore_higher)
		mlog(0, "IGNORE HIGHER: %.*s\n", res->lockname.len,
		     res->lockname.name);

	spin_lock(&dlm->work_lock);
	list_add_tail(&item->list, &dlm->work_list);
	spin_unlock(&dlm->work_lock);

	queue_work(dlm->dlm_worker, &dlm->dispatched_work);
	return 0;
}

static void dlm_assert_master_worker(struct dlm_work_item *item, void *data)
{
	struct dlm_ctxt *dlm = data;
	int ret = 0;
	struct dlm_lock_resource *res;
	unsigned long nodemap[BITS_TO_LONGS(O2NM_MAX_NODES)];
	int ignore_higher;
	int bit;
	u8 request_from;
	u32 flags;

	dlm = item->dlm;
	res = item->u.am.lockres;
	ignore_higher = item->u.am.ignore_higher;
	request_from = item->u.am.request_from;
	flags = item->u.am.flags;

	spin_lock(&dlm->spinlock);
	memcpy(nodemap, dlm->domain_map, sizeof(nodemap));
	spin_unlock(&dlm->spinlock);

	clear_bit(dlm->node_num, nodemap);
	if (ignore_higher) {
		/* if is this just to clear up mles for nodes below
		 * this node, do not send the message to the original
		 * caller or any node number higher than this */
		clear_bit(request_from, nodemap);
		bit = dlm->node_num;
		while (1) {
			bit = find_next_bit(nodemap, O2NM_MAX_NODES,
					    bit+1);
		       	if (bit >= O2NM_MAX_NODES)
				break;
			clear_bit(bit, nodemap);
		}
	}

	/*
	 * If we're migrating this lock to someone else, we are no
	 * longer allowed to assert out own mastery.  OTOH, we need to
	 * prevent migration from starting while we're still asserting
	 * our dominance.  The reserved ast delays migration.
	 */
	spin_lock(&res->spinlock);
	if (res->state & DLM_LOCK_RES_MIGRATING) {
		mlog(0, "Someone asked us to assert mastery, but we're "
		     "in the middle of migration.  Skipping assert, "
		     "the new master will handle that.\n");
		spin_unlock(&res->spinlock);
		goto put;
	} else
		__dlm_lockres_reserve_ast(res);
	spin_unlock(&res->spinlock);

	/* this call now finishes out the nodemap
	 * even if one or more nodes die */
	mlog(0, "worker about to master %.*s here, this=%u\n",
		     res->lockname.len, res->lockname.name, dlm->node_num);
	ret = dlm_do_assert_master(dlm, res, nodemap, flags);
	if (ret < 0) {
		/* no need to restart, we are done */
		if (!dlm_is_host_down(ret))
			mlog_errno(ret);
	}

	/* Ok, we've asserted ourselves.  Let's let migration start. */
	dlm_lockres_release_ast(dlm, res);

put:
	dlm_lockres_put(res);

	mlog(0, "finished with dlm_assert_master_worker\n");
}

/* SPECIAL CASE for the $RECOVERY lock used by the recovery thread.
 * We cannot wait for node recovery to complete to begin mastering this
 * lockres because this lockres is used to kick off recovery! ;-)
 * So, do a pre-check on all living nodes to see if any of those nodes
 * think that $RECOVERY is currently mastered by a dead node.  If so,
 * we wait a short time to allow that node to get notified by its own
 * heartbeat stack, then check again.  All $RECOVERY lock resources
 * mastered by dead nodes are purged when the hearbeat callback is
 * fired, so we can know for sure that it is safe to continue once
 * the node returns a live node or no node.  */
static int dlm_pre_master_reco_lockres(struct dlm_ctxt *dlm,
				       struct dlm_lock_resource *res)
{
	struct dlm_node_iter iter;
	int nodenum;
	int ret = 0;
	u8 master = DLM_LOCK_RES_OWNER_UNKNOWN;

	spin_lock(&dlm->spinlock);
	dlm_node_iter_init(dlm->domain_map, &iter);
	spin_unlock(&dlm->spinlock);

	while ((nodenum = dlm_node_iter_next(&iter)) >= 0) {
		/* do not send to self */
		if (nodenum == dlm->node_num)
			continue;
		ret = dlm_do_master_requery(dlm, res, nodenum, &master);
		if (ret < 0) {
			mlog_errno(ret);
			if (!dlm_is_host_down(ret))
				BUG();
			/* host is down, so answer for that node would be
			 * DLM_LOCK_RES_OWNER_UNKNOWN.  continue. */
			ret = 0;
		}

		if (master != DLM_LOCK_RES_OWNER_UNKNOWN) {
			/* check to see if this master is in the recovery map */
			spin_lock(&dlm->spinlock);
			if (test_bit(master, dlm->recovery_map)) {
				mlog(ML_NOTICE, "%s: node %u has not seen "
				     "node %u go down yet, and thinks the "
				     "dead node is mastering the recovery "
				     "lock.  must wait.\n", dlm->name,
				     nodenum, master);
				ret = -EAGAIN;
			}
			spin_unlock(&dlm->spinlock);
			mlog(0, "%s: reco lock master is %u\n", dlm->name,
			     master);
			break;
		}
	}
	return ret;
}

/*
 * DLM_DEREF_LOCKRES_MSG
 */

int dlm_drop_lockres_ref(struct dlm_ctxt *dlm, struct dlm_lock_resource *res)
{
	struct dlm_deref_lockres deref;
	int ret = 0, r;
	const char *lockname;
	unsigned int namelen;

	lockname = res->lockname.name;
	namelen = res->lockname.len;
	BUG_ON(namelen > O2NM_MAX_NAME_LEN);

	memset(&deref, 0, sizeof(deref));
	deref.node_idx = dlm->node_num;
	deref.namelen = namelen;
	memcpy(deref.name, lockname, namelen);

	ret = o2net_send_message(DLM_DEREF_LOCKRES_MSG, dlm->key,
				 &deref, sizeof(deref), res->owner, &r);
	if (ret < 0)
		mlog(ML_ERROR, "%s: res %.*s, error %d send DEREF to node %u\n",
		     dlm->name, namelen, lockname, ret, res->owner);
	else if (r < 0) {
		/* BAD.  other node says I did not have a ref. */
		mlog(ML_ERROR, "%s: res %.*s, DEREF to node %u got %d\n",
		     dlm->name, namelen, lockname, res->owner, r);
		dlm_print_one_lock_resource(res);
		BUG();
	}
	return ret;
}

int dlm_deref_lockres_handler(struct o2net_msg *msg, u32 len, void *data,
			      void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_deref_lockres *deref = (struct dlm_deref_lockres *)msg->buf;
	struct dlm_lock_resource *res = NULL;
	char *name;
	unsigned int namelen;
	int ret = -EINVAL;
	u8 node;
	unsigned int hash;
	struct dlm_work_item *item;
	int cleared = 0;
	int dispatch = 0;

	if (!dlm_grab(dlm))
		return 0;

	name = deref->name;
	namelen = deref->namelen;
	node = deref->node_idx;

	if (namelen > DLM_LOCKID_NAME_MAX) {
		mlog(ML_ERROR, "Invalid name length!");
		goto done;
	}
	if (deref->node_idx >= O2NM_MAX_NODES) {
		mlog(ML_ERROR, "Invalid node number: %u\n", node);
		goto done;
	}

	hash = dlm_lockid_hash(name, namelen);

	spin_lock(&dlm->spinlock);
	res = __dlm_lookup_lockres_full(dlm, name, namelen, hash);
	if (!res) {
		spin_unlock(&dlm->spinlock);
		mlog(ML_ERROR, "%s:%.*s: bad lockres name\n",
		     dlm->name, namelen, name);
		goto done;
	}
	spin_unlock(&dlm->spinlock);

	spin_lock(&res->spinlock);
	if (res->state & DLM_LOCK_RES_SETREF_INPROG)
		dispatch = 1;
	else {
		BUG_ON(res->state & DLM_LOCK_RES_DROPPING_REF);
		if (test_bit(node, res->refmap)) {
			dlm_lockres_clear_refmap_bit(dlm, res, node);
			cleared = 1;
		}
	}
	spin_unlock(&res->spinlock);

	if (!dispatch) {
		if (cleared)
			dlm_lockres_calc_usage(dlm, res);
		else {
			mlog(ML_ERROR, "%s:%.*s: node %u trying to drop ref "
		     	"but it is already dropped!\n", dlm->name,
		     	res->lockname.len, res->lockname.name, node);
			dlm_print_one_lock_resource(res);
		}
		ret = 0;
		goto done;
	}

	item = kzalloc(sizeof(*item), GFP_NOFS);
	if (!item) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto done;
	}

	dlm_init_work_item(dlm, item, dlm_deref_lockres_worker, NULL);
	item->u.dl.deref_res = res;
	item->u.dl.deref_node = node;

	spin_lock(&dlm->work_lock);
	list_add_tail(&item->list, &dlm->work_list);
	spin_unlock(&dlm->work_lock);

	queue_work(dlm->dlm_worker, &dlm->dispatched_work);
	return 0;

done:
	if (res)
		dlm_lockres_put(res);
	dlm_put(dlm);

	return ret;
}

static void dlm_deref_lockres_worker(struct dlm_work_item *item, void *data)
{
	struct dlm_ctxt *dlm;
	struct dlm_lock_resource *res;
	u8 node;
	u8 cleared = 0;

	dlm = item->dlm;
	res = item->u.dl.deref_res;
	node = item->u.dl.deref_node;

	spin_lock(&res->spinlock);
	BUG_ON(res->state & DLM_LOCK_RES_DROPPING_REF);
	if (test_bit(node, res->refmap)) {
		__dlm_wait_on_lockres_flags(res, DLM_LOCK_RES_SETREF_INPROG);
		dlm_lockres_clear_refmap_bit(dlm, res, node);
		cleared = 1;
	}
	spin_unlock(&res->spinlock);

	if (cleared) {
		mlog(0, "%s:%.*s node %u ref dropped in dispatch\n",
		     dlm->name, res->lockname.len, res->lockname.name, node);
		dlm_lockres_calc_usage(dlm, res);
	} else {
		mlog(ML_ERROR, "%s:%.*s: node %u trying to drop ref "
		     "but it is already dropped!\n", dlm->name,
		     res->lockname.len, res->lockname.name, node);
		dlm_print_one_lock_resource(res);
	}

	dlm_lockres_put(res);
}

/*
 * A migrateable resource is one that is :
 * 1. locally mastered, and,
 * 2. zero local locks, and,
 * 3. one or more non-local locks, or, one or more references
 * Returns 1 if yes, 0 if not.
 */
static int dlm_is_lockres_migrateable(struct dlm_ctxt *dlm,
				      struct dlm_lock_resource *res)
{
	enum dlm_lockres_list idx;
	int nonlocal = 0, node_ref;
	struct list_head *queue;
	struct dlm_lock *lock;
	u64 cookie;

	assert_spin_locked(&res->spinlock);

	/* delay migration when the lockres is in MIGRATING state */
	if (res->state & DLM_LOCK_RES_MIGRATING)
		return 0;

	if (res->owner != dlm->node_num)
		return 0;

        for (idx = DLM_GRANTED_LIST; idx <= DLM_BLOCKED_LIST; idx++) {
		queue = dlm_list_idx_to_ptr(res, idx);
		list_for_each_entry(lock, queue, list) {
			if (lock->ml.node != dlm->node_num) {
				nonlocal++;
				continue;
			}
			cookie = be64_to_cpu(lock->ml.cookie);
			mlog(0, "%s: Not migrateable res %.*s, lock %u:%llu on "
			     "%s list\n", dlm->name, res->lockname.len,
			     res->lockname.name,
			     dlm_get_lock_cookie_node(cookie),
			     dlm_get_lock_cookie_seq(cookie),
			     dlm_list_in_text(idx));
			return 0;
		}
	}

	if (!nonlocal) {
		node_ref = find_next_bit(res->refmap, O2NM_MAX_NODES, 0);
		if (node_ref >= O2NM_MAX_NODES)
			return 0;
	}

	mlog(0, "%s: res %.*s, Migrateable\n", dlm->name, res->lockname.len,
	     res->lockname.name);

	return 1;
}

/*
 * DLM_MIGRATE_LOCKRES
 */


static int dlm_migrate_lockres(struct dlm_ctxt *dlm,
			       struct dlm_lock_resource *res, u8 target)
{
	struct dlm_master_list_entry *mle = NULL;
	struct dlm_master_list_entry *oldmle = NULL;
 	struct dlm_migratable_lockres *mres = NULL;
	int ret = 0;
	const char *name;
	unsigned int namelen;
	int mle_added = 0;
	int wake = 0;

	if (!dlm_grab(dlm))
		return -EINVAL;

	BUG_ON(target == O2NM_MAX_NODES);

	name = res->lockname.name;
	namelen = res->lockname.len;

	mlog(0, "%s: Migrating %.*s to node %u\n", dlm->name, namelen, name,
	     target);

	/* preallocate up front. if this fails, abort */
	ret = -ENOMEM;
	mres = (struct dlm_migratable_lockres *) __get_free_page(GFP_NOFS);
	if (!mres) {
		mlog_errno(ret);
		goto leave;
	}

	mle = kmem_cache_alloc(dlm_mle_cache, GFP_NOFS);
	if (!mle) {
		mlog_errno(ret);
		goto leave;
	}
	ret = 0;

	/*
	 * clear any existing master requests and
	 * add the migration mle to the list
	 */
	spin_lock(&dlm->spinlock);
	spin_lock(&dlm->master_lock);
	ret = dlm_add_migration_mle(dlm, res, mle, &oldmle, name,
				    namelen, target, dlm->node_num);
	spin_unlock(&dlm->master_lock);
	spin_unlock(&dlm->spinlock);

	if (ret == -EEXIST) {
		mlog(0, "another process is already migrating it\n");
		goto fail;
	}
	mle_added = 1;

	/*
	 * set the MIGRATING flag and flush asts
	 * if we fail after this we need to re-dirty the lockres
	 */
	if (dlm_mark_lockres_migrating(dlm, res, target) < 0) {
		mlog(ML_ERROR, "tried to migrate %.*s to %u, but "
		     "the target went down.\n", res->lockname.len,
		     res->lockname.name, target);
		spin_lock(&res->spinlock);
		res->state &= ~DLM_LOCK_RES_MIGRATING;
		wake = 1;
		spin_unlock(&res->spinlock);
		ret = -EINVAL;
	}

fail:
	if (oldmle) {
		/* master is known, detach if not already detached */
		dlm_mle_detach_hb_events(dlm, oldmle);
		dlm_put_mle(oldmle);
	}

	if (ret < 0) {
		if (mle_added) {
			dlm_mle_detach_hb_events(dlm, mle);
			dlm_put_mle(mle);
		} else if (mle) {
			kmem_cache_free(dlm_mle_cache, mle);
			mle = NULL;
		}
		goto leave;
	}

	/*
	 * at this point, we have a migration target, an mle
	 * in the master list, and the MIGRATING flag set on
	 * the lockres
	 */

	/* now that remote nodes are spinning on the MIGRATING flag,
	 * ensure that all assert_master work is flushed. */
	flush_workqueue(dlm->dlm_worker);

	/* get an extra reference on the mle.
	 * otherwise the assert_master from the new
	 * master will destroy this.
	 * also, make sure that all callers of dlm_get_mle
	 * take both dlm->spinlock and dlm->master_lock */
	spin_lock(&dlm->spinlock);
	spin_lock(&dlm->master_lock);
	dlm_get_mle_inuse(mle);
	spin_unlock(&dlm->master_lock);
	spin_unlock(&dlm->spinlock);

	/* notify new node and send all lock state */
	/* call send_one_lockres with migration flag.
	 * this serves as notice to the target node that a
	 * migration is starting. */
	ret = dlm_send_one_lockres(dlm, res, mres, target,
				   DLM_MRES_MIGRATION);

	if (ret < 0) {
		mlog(0, "migration to node %u failed with %d\n",
		     target, ret);
		/* migration failed, detach and clean up mle */
		dlm_mle_detach_hb_events(dlm, mle);
		dlm_put_mle(mle);
		dlm_put_mle_inuse(mle);
		spin_lock(&res->spinlock);
		res->state &= ~DLM_LOCK_RES_MIGRATING;
		wake = 1;
		spin_unlock(&res->spinlock);
		if (dlm_is_host_down(ret))
			dlm_wait_for_node_death(dlm, target,
						DLM_NODE_DEATH_WAIT_MAX);
		goto leave;
	}

	/* at this point, the target sends a message to all nodes,
	 * (using dlm_do_migrate_request).  this node is skipped since
	 * we had to put an mle in the list to begin the process.  this
	 * node now waits for target to do an assert master.  this node
	 * will be the last one notified, ensuring that the migration
	 * is complete everywhere.  if the target dies while this is
	 * going on, some nodes could potentially see the target as the
	 * master, so it is important that my recovery finds the migration
	 * mle and sets the master to UNKNOWN. */


	/* wait for new node to assert master */
	while (1) {
		ret = wait_event_interruptible_timeout(mle->wq,
					(atomic_read(&mle->woken) == 1),
					msecs_to_jiffies(5000));

		if (ret >= 0) {
		       	if (atomic_read(&mle->woken) == 1 ||
			    res->owner == target)
				break;

			mlog(0, "%s:%.*s: timed out during migration\n",
			     dlm->name, res->lockname.len, res->lockname.name);
			/* avoid hang during shutdown when migrating lockres
			 * to a node which also goes down */
			if (dlm_is_node_dead(dlm, target)) {
				mlog(0, "%s:%.*s: expected migration "
				     "target %u is no longer up, restarting\n",
				     dlm->name, res->lockname.len,
				     res->lockname.name, target);
				ret = -EINVAL;
				/* migration failed, detach and clean up mle */
				dlm_mle_detach_hb_events(dlm, mle);
				dlm_put_mle(mle);
				dlm_put_mle_inuse(mle);
				spin_lock(&res->spinlock);
				res->state &= ~DLM_LOCK_RES_MIGRATING;
				wake = 1;
				spin_unlock(&res->spinlock);
				goto leave;
			}
		} else
			mlog(0, "%s:%.*s: caught signal during migration\n",
			     dlm->name, res->lockname.len, res->lockname.name);
	}

	/* all done, set the owner, clear the flag */
	spin_lock(&res->spinlock);
	dlm_set_lockres_owner(dlm, res, target);
	res->state &= ~DLM_LOCK_RES_MIGRATING;
	dlm_remove_nonlocal_locks(dlm, res);
	spin_unlock(&res->spinlock);
	wake_up(&res->wq);

	/* master is known, detach if not already detached */
	dlm_mle_detach_hb_events(dlm, mle);
	dlm_put_mle_inuse(mle);
	ret = 0;

	dlm_lockres_calc_usage(dlm, res);

leave:
	/* re-dirty the lockres if we failed */
	if (ret < 0)
		dlm_kick_thread(dlm, res);

	/* wake up waiters if the MIGRATING flag got set
	 * but migration failed */
	if (wake)
		wake_up(&res->wq);

	if (mres)
		free_page((unsigned long)mres);

	dlm_put(dlm);

	mlog(0, "%s: Migrating %.*s to %u, returns %d\n", dlm->name, namelen,
	     name, target, ret);
	return ret;
}

#define DLM_MIGRATION_RETRY_MS  100

/*
 * Should be called only after beginning the domain leave process.
 * There should not be any remaining locks on nonlocal lock resources,
 * and there should be no local locks left on locally mastered resources.
 *
 * Called with the dlm spinlock held, may drop it to do migration, but
 * will re-acquire before exit.
 *
 * Returns: 1 if dlm->spinlock was dropped/retaken, 0 if never dropped
 */
int dlm_empty_lockres(struct dlm_ctxt *dlm, struct dlm_lock_resource *res)
{
	int ret;
	int lock_dropped = 0;
	u8 target = O2NM_MAX_NODES;

	assert_spin_locked(&dlm->spinlock);

	spin_lock(&res->spinlock);
	if (dlm_is_lockres_migrateable(dlm, res))
		target = dlm_pick_migration_target(dlm, res);
	spin_unlock(&res->spinlock);

	if (target == O2NM_MAX_NODES)
		goto leave;

	/* Wheee! Migrate lockres here! Will sleep so drop spinlock. */
	spin_unlock(&dlm->spinlock);
	lock_dropped = 1;
	ret = dlm_migrate_lockres(dlm, res, target);
	if (ret)
		mlog(0, "%s: res %.*s, Migrate to node %u failed with %d\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     target, ret);
	spin_lock(&dlm->spinlock);
leave:
	return lock_dropped;
}

int dlm_lock_basts_flushed(struct dlm_ctxt *dlm, struct dlm_lock *lock)
{
	int ret;
	spin_lock(&dlm->ast_lock);
	spin_lock(&lock->spinlock);
	ret = (list_empty(&lock->bast_list) && !lock->bast_pending);
	spin_unlock(&lock->spinlock);
	spin_unlock(&dlm->ast_lock);
	return ret;
}

static int dlm_migration_can_proceed(struct dlm_ctxt *dlm,
				     struct dlm_lock_resource *res,
				     u8 mig_target)
{
	int can_proceed;
	spin_lock(&res->spinlock);
	can_proceed = !!(res->state & DLM_LOCK_RES_MIGRATING);
	spin_unlock(&res->spinlock);

	/* target has died, so make the caller break out of the
	 * wait_event, but caller must recheck the domain_map */
	spin_lock(&dlm->spinlock);
	if (!test_bit(mig_target, dlm->domain_map))
		can_proceed = 1;
	spin_unlock(&dlm->spinlock);
	return can_proceed;
}

static int dlm_lockres_is_dirty(struct dlm_ctxt *dlm,
				struct dlm_lock_resource *res)
{
	int ret;
	spin_lock(&res->spinlock);
	ret = !!(res->state & DLM_LOCK_RES_DIRTY);
	spin_unlock(&res->spinlock);
	return ret;
}


static int dlm_mark_lockres_migrating(struct dlm_ctxt *dlm,
				       struct dlm_lock_resource *res,
				       u8 target)
{
	int ret = 0;

	mlog(0, "dlm_mark_lockres_migrating: %.*s, from %u to %u\n",
	       res->lockname.len, res->lockname.name, dlm->node_num,
	       target);
	/* need to set MIGRATING flag on lockres.  this is done by
	 * ensuring that all asts have been flushed for this lockres. */
	spin_lock(&res->spinlock);
	BUG_ON(res->migration_pending);
	res->migration_pending = 1;
	/* strategy is to reserve an extra ast then release
	 * it below, letting the release do all of the work */
	__dlm_lockres_reserve_ast(res);
	spin_unlock(&res->spinlock);

	/* now flush all the pending asts */
	dlm_kick_thread(dlm, res);
	/* before waiting on DIRTY, block processes which may
	 * try to dirty the lockres before MIGRATING is set */
	spin_lock(&res->spinlock);
	BUG_ON(res->state & DLM_LOCK_RES_BLOCK_DIRTY);
	res->state |= DLM_LOCK_RES_BLOCK_DIRTY;
	spin_unlock(&res->spinlock);
	/* now wait on any pending asts and the DIRTY state */
	wait_event(dlm->ast_wq, !dlm_lockres_is_dirty(dlm, res));
	dlm_lockres_release_ast(dlm, res);

	mlog(0, "about to wait on migration_wq, dirty=%s\n",
	       res->state & DLM_LOCK_RES_DIRTY ? "yes" : "no");
	/* if the extra ref we just put was the final one, this
	 * will pass thru immediately.  otherwise, we need to wait
	 * for the last ast to finish. */
again:
	ret = wait_event_interruptible_timeout(dlm->migration_wq,
		   dlm_migration_can_proceed(dlm, res, target),
		   msecs_to_jiffies(1000));
	if (ret < 0) {
		mlog(0, "woken again: migrating? %s, dead? %s\n",
		       res->state & DLM_LOCK_RES_MIGRATING ? "yes":"no",
		       test_bit(target, dlm->domain_map) ? "no":"yes");
	} else {
		mlog(0, "all is well: migrating? %s, dead? %s\n",
		       res->state & DLM_LOCK_RES_MIGRATING ? "yes":"no",
		       test_bit(target, dlm->domain_map) ? "no":"yes");
	}
	if (!dlm_migration_can_proceed(dlm, res, target)) {
		mlog(0, "trying again...\n");
		goto again;
	}

	ret = 0;
	/* did the target go down or die? */
	spin_lock(&dlm->spinlock);
	if (!test_bit(target, dlm->domain_map)) {
		mlog(ML_ERROR, "aha. migration target %u just went down\n",
		     target);
		ret = -EHOSTDOWN;
	}
	spin_unlock(&dlm->spinlock);

	/*
	 * if target is down, we need to clear DLM_LOCK_RES_BLOCK_DIRTY for
	 * another try; otherwise, we are sure the MIGRATING state is there,
	 * drop the unneded state which blocked threads trying to DIRTY
	 */
	spin_lock(&res->spinlock);
	BUG_ON(!(res->state & DLM_LOCK_RES_BLOCK_DIRTY));
	res->state &= ~DLM_LOCK_RES_BLOCK_DIRTY;
	if (!ret)
		BUG_ON(!(res->state & DLM_LOCK_RES_MIGRATING));
	spin_unlock(&res->spinlock);

	/*
	 * at this point:
	 *
	 *   o the DLM_LOCK_RES_MIGRATING flag is set if target not down
	 *   o there are no pending asts on this lockres
	 *   o all processes trying to reserve an ast on this
	 *     lockres must wait for the MIGRATING flag to clear
	 */
	return ret;
}

/* last step in the migration process.
 * original master calls this to free all of the dlm_lock
 * structures that used to be for other nodes. */
static void dlm_remove_nonlocal_locks(struct dlm_ctxt *dlm,
				      struct dlm_lock_resource *res)
{
	struct list_head *queue = &res->granted;
	int i, bit;
	struct dlm_lock *lock, *next;

	assert_spin_locked(&res->spinlock);

	BUG_ON(res->owner == dlm->node_num);

	for (i=0; i<3; i++) {
		list_for_each_entry_safe(lock, next, queue, list) {
			if (lock->ml.node != dlm->node_num) {
				mlog(0, "putting lock for node %u\n",
				     lock->ml.node);
				/* be extra careful */
				BUG_ON(!list_empty(&lock->ast_list));
				BUG_ON(!list_empty(&lock->bast_list));
				BUG_ON(lock->ast_pending);
				BUG_ON(lock->bast_pending);
				dlm_lockres_clear_refmap_bit(dlm, res,
							     lock->ml.node);
				list_del_init(&lock->list);
				dlm_lock_put(lock);
				/* In a normal unlock, we would have added a
				 * DLM_UNLOCK_FREE_LOCK action. Force it. */
				dlm_lock_put(lock);
			}
		}
		queue++;
	}
	bit = 0;
	while (1) {
		bit = find_next_bit(res->refmap, O2NM_MAX_NODES, bit);
		if (bit >= O2NM_MAX_NODES)
			break;
		/* do not clear the local node reference, if there is a
		 * process holding this, let it drop the ref itself */
		if (bit != dlm->node_num) {
			mlog(0, "%s:%.*s: node %u had a ref to this "
			     "migrating lockres, clearing\n", dlm->name,
			     res->lockname.len, res->lockname.name, bit);
			dlm_lockres_clear_refmap_bit(dlm, res, bit);
		}
		bit++;
	}
}

/*
 * Pick a node to migrate the lock resource to. This function selects a
 * potential target based first on the locks and then on refmap. It skips
 * nodes that are in the process of exiting the domain.
 */
static u8 dlm_pick_migration_target(struct dlm_ctxt *dlm,
				    struct dlm_lock_resource *res)
{
	enum dlm_lockres_list idx;
	struct list_head *queue = &res->granted;
	struct dlm_lock *lock;
	int noderef;
	u8 nodenum = O2NM_MAX_NODES;

	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&res->spinlock);

	/* Go through all the locks */
	for (idx = DLM_GRANTED_LIST; idx <= DLM_BLOCKED_LIST; idx++) {
		queue = dlm_list_idx_to_ptr(res, idx);
		list_for_each_entry(lock, queue, list) {
			if (lock->ml.node == dlm->node_num)
				continue;
			if (test_bit(lock->ml.node, dlm->exit_domain_map))
				continue;
			nodenum = lock->ml.node;
			goto bail;
		}
	}

	/* Go thru the refmap */
	noderef = -1;
	while (1) {
		noderef = find_next_bit(res->refmap, O2NM_MAX_NODES,
					noderef + 1);
		if (noderef >= O2NM_MAX_NODES)
			break;
		if (noderef == dlm->node_num)
			continue;
		if (test_bit(noderef, dlm->exit_domain_map))
			continue;
		nodenum = noderef;
		goto bail;
	}

bail:
	return nodenum;
}

/* this is called by the new master once all lockres
 * data has been received */
static int dlm_do_migrate_request(struct dlm_ctxt *dlm,
				  struct dlm_lock_resource *res,
				  u8 master, u8 new_master,
				  struct dlm_node_iter *iter)
{
	struct dlm_migrate_request migrate;
	int ret, skip, status = 0;
	int nodenum;

	memset(&migrate, 0, sizeof(migrate));
	migrate.namelen = res->lockname.len;
	memcpy(migrate.name, res->lockname.name, migrate.namelen);
	migrate.new_master = new_master;
	migrate.master = master;

	ret = 0;

	/* send message to all nodes, except the master and myself */
	while ((nodenum = dlm_node_iter_next(iter)) >= 0) {
		if (nodenum == master ||
		    nodenum == new_master)
			continue;

		/* We could race exit domain. If exited, skip. */
		spin_lock(&dlm->spinlock);
		skip = (!test_bit(nodenum, dlm->domain_map));
		spin_unlock(&dlm->spinlock);
		if (skip) {
			clear_bit(nodenum, iter->node_map);
			continue;
		}

		ret = o2net_send_message(DLM_MIGRATE_REQUEST_MSG, dlm->key,
					 &migrate, sizeof(migrate), nodenum,
					 &status);
		if (ret < 0) {
			mlog(ML_ERROR, "%s: res %.*s, Error %d send "
			     "MIGRATE_REQUEST to node %u\n", dlm->name,
			     migrate.namelen, migrate.name, ret, nodenum);
			if (!dlm_is_host_down(ret)) {
				mlog(ML_ERROR, "unhandled error=%d!\n", ret);
				BUG();
			}
			clear_bit(nodenum, iter->node_map);
			ret = 0;
		} else if (status < 0) {
			mlog(0, "migrate request (node %u) returned %d!\n",
			     nodenum, status);
			ret = status;
		} else if (status == DLM_MIGRATE_RESPONSE_MASTERY_REF) {
			/* during the migration request we short-circuited
			 * the mastery of the lockres.  make sure we have
			 * a mastery ref for nodenum */
			mlog(0, "%s:%.*s: need ref for node %u\n",
			     dlm->name, res->lockname.len, res->lockname.name,
			     nodenum);
			spin_lock(&res->spinlock);
			dlm_lockres_set_refmap_bit(dlm, res, nodenum);
			spin_unlock(&res->spinlock);
		}
	}

	if (ret < 0)
		mlog_errno(ret);

	mlog(0, "returning ret=%d\n", ret);
	return ret;
}


/* if there is an existing mle for this lockres, we now know who the master is.
 * (the one who sent us *this* message) we can clear it up right away.
 * since the process that put the mle on the list still has a reference to it,
 * we can unhash it now, set the master and wake the process.  as a result,
 * we will have no mle in the list to start with.  now we can add an mle for
 * the migration and this should be the only one found for those scanning the
 * list.  */
int dlm_migrate_request_handler(struct o2net_msg *msg, u32 len, void *data,
				void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_lock_resource *res = NULL;
	struct dlm_migrate_request *migrate = (struct dlm_migrate_request *) msg->buf;
	struct dlm_master_list_entry *mle = NULL, *oldmle = NULL;
	const char *name;
	unsigned int namelen, hash;
	int ret = 0;

	if (!dlm_grab(dlm))
		return -EINVAL;

	name = migrate->name;
	namelen = migrate->namelen;
	hash = dlm_lockid_hash(name, namelen);

	/* preallocate.. if this fails, abort */
	mle = kmem_cache_alloc(dlm_mle_cache, GFP_NOFS);

	if (!mle) {
		ret = -ENOMEM;
		goto leave;
	}

	/* check for pre-existing lock */
	spin_lock(&dlm->spinlock);
	res = __dlm_lookup_lockres(dlm, name, namelen, hash);
	if (res) {
		spin_lock(&res->spinlock);
		if (res->state & DLM_LOCK_RES_RECOVERING) {
			/* if all is working ok, this can only mean that we got
		 	* a migrate request from a node that we now see as
		 	* dead.  what can we do here?  drop it to the floor? */
			spin_unlock(&res->spinlock);
			mlog(ML_ERROR, "Got a migrate request, but the "
			     "lockres is marked as recovering!");
			kmem_cache_free(dlm_mle_cache, mle);
			ret = -EINVAL; /* need a better solution */
			goto unlock;
		}
		res->state |= DLM_LOCK_RES_MIGRATING;
		spin_unlock(&res->spinlock);
	}

	spin_lock(&dlm->master_lock);
	/* ignore status.  only nonzero status would BUG. */
	ret = dlm_add_migration_mle(dlm, res, mle, &oldmle,
				    name, namelen,
				    migrate->new_master,
				    migrate->master);

	spin_unlock(&dlm->master_lock);
unlock:
	spin_unlock(&dlm->spinlock);

	if (oldmle) {
		/* master is known, detach if not already detached */
		dlm_mle_detach_hb_events(dlm, oldmle);
		dlm_put_mle(oldmle);
	}

	if (res)
		dlm_lockres_put(res);
leave:
	dlm_put(dlm);
	return ret;
}

/* must be holding dlm->spinlock and dlm->master_lock
 * when adding a migration mle, we can clear any other mles
 * in the master list because we know with certainty that
 * the master is "master".  so we remove any old mle from
 * the list after setting it's master field, and then add
 * the new migration mle.  this way we can hold with the rule
 * of having only one mle for a given lock name at all times. */
static int dlm_add_migration_mle(struct dlm_ctxt *dlm,
				 struct dlm_lock_resource *res,
				 struct dlm_master_list_entry *mle,
				 struct dlm_master_list_entry **oldmle,
				 const char *name, unsigned int namelen,
				 u8 new_master, u8 master)
{
	int found;
	int ret = 0;

	*oldmle = NULL;

	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&dlm->master_lock);

	/* caller is responsible for any ref taken here on oldmle */
	found = dlm_find_mle(dlm, oldmle, (char *)name, namelen);
	if (found) {
		struct dlm_master_list_entry *tmp = *oldmle;
		spin_lock(&tmp->spinlock);
		if (tmp->type == DLM_MLE_MIGRATION) {
			if (master == dlm->node_num) {
				/* ah another process raced me to it */
				mlog(0, "tried to migrate %.*s, but some "
				     "process beat me to it\n",
				     namelen, name);
				ret = -EEXIST;
			} else {
				/* bad.  2 NODES are trying to migrate! */
				mlog(ML_ERROR, "migration error  mle: "
				     "master=%u new_master=%u // request: "
				     "master=%u new_master=%u // "
				     "lockres=%.*s\n",
				     tmp->master, tmp->new_master,
				     master, new_master,
				     namelen, name);
				BUG();
			}
		} else {
			/* this is essentially what assert_master does */
			tmp->master = master;
			atomic_set(&tmp->woken, 1);
			wake_up(&tmp->wq);
			/* remove it so that only one mle will be found */
			__dlm_unlink_mle(dlm, tmp);
			__dlm_mle_detach_hb_events(dlm, tmp);
			ret = DLM_MIGRATE_RESPONSE_MASTERY_REF;
			mlog(0, "%s:%.*s: master=%u, newmaster=%u, "
			    "telling master to get ref for cleared out mle "
			    "during migration\n", dlm->name, namelen, name,
			    master, new_master);
		}
		spin_unlock(&tmp->spinlock);
	}

	/* now add a migration mle to the tail of the list */
	dlm_init_mle(mle, DLM_MLE_MIGRATION, dlm, res, name, namelen);
	mle->new_master = new_master;
	/* the new master will be sending an assert master for this.
	 * at that point we will get the refmap reference */
	mle->master = master;
	/* do this for consistency with other mle types */
	set_bit(new_master, mle->maybe_map);
	__dlm_insert_mle(dlm, mle);

	return ret;
}

/*
 * Sets the owner of the lockres, associated to the mle, to UNKNOWN
 */
static struct dlm_lock_resource *dlm_reset_mleres_owner(struct dlm_ctxt *dlm,
					struct dlm_master_list_entry *mle)
{
	struct dlm_lock_resource *res;

	/* Find the lockres associated to the mle and set its owner to UNK */
	res = __dlm_lookup_lockres(dlm, mle->mname, mle->mnamelen,
				   mle->mnamehash);
	if (res) {
		spin_unlock(&dlm->master_lock);

		/* move lockres onto recovery list */
		spin_lock(&res->spinlock);
		dlm_set_lockres_owner(dlm, res, DLM_LOCK_RES_OWNER_UNKNOWN);
		dlm_move_lockres_to_recovery_list(dlm, res);
		spin_unlock(&res->spinlock);
		dlm_lockres_put(res);

		/* about to get rid of mle, detach from heartbeat */
		__dlm_mle_detach_hb_events(dlm, mle);

		/* dump the mle */
		spin_lock(&dlm->master_lock);
		__dlm_put_mle(mle);
		spin_unlock(&dlm->master_lock);
	}

	return res;
}

static void dlm_clean_migration_mle(struct dlm_ctxt *dlm,
				    struct dlm_master_list_entry *mle)
{
	__dlm_mle_detach_hb_events(dlm, mle);

	spin_lock(&mle->spinlock);
	__dlm_unlink_mle(dlm, mle);
	atomic_set(&mle->woken, 1);
	spin_unlock(&mle->spinlock);

	wake_up(&mle->wq);
}

static void dlm_clean_block_mle(struct dlm_ctxt *dlm,
				struct dlm_master_list_entry *mle, u8 dead_node)
{
	int bit;

	BUG_ON(mle->type != DLM_MLE_BLOCK);

	spin_lock(&mle->spinlock);
	bit = find_next_bit(mle->maybe_map, O2NM_MAX_NODES, 0);
	if (bit != dead_node) {
		mlog(0, "mle found, but dead node %u would not have been "
		     "master\n", dead_node);
		spin_unlock(&mle->spinlock);
	} else {
		/* Must drop the refcount by one since the assert_master will
		 * never arrive. This may result in the mle being unlinked and
		 * freed, but there may still be a process waiting in the
		 * dlmlock path which is fine. */
		mlog(0, "node %u was expected master\n", dead_node);
		atomic_set(&mle->woken, 1);
		spin_unlock(&mle->spinlock);
		wake_up(&mle->wq);

		/* Do not need events any longer, so detach from heartbeat */
		__dlm_mle_detach_hb_events(dlm, mle);
		__dlm_put_mle(mle);
	}
}

void dlm_clean_master_list(struct dlm_ctxt *dlm, u8 dead_node)
{
	struct dlm_master_list_entry *mle;
	struct dlm_lock_resource *res;
	struct hlist_head *bucket;
	struct hlist_node *tmp;
	unsigned int i;

	mlog(0, "dlm=%s, dead node=%u\n", dlm->name, dead_node);
top:
	assert_spin_locked(&dlm->spinlock);

	/* clean the master list */
	spin_lock(&dlm->master_lock);
	for (i = 0; i < DLM_HASH_BUCKETS; i++) {
		bucket = dlm_master_hash(dlm, i);
		hlist_for_each_entry_safe(mle, tmp, bucket, master_hash_node) {
			BUG_ON(mle->type != DLM_MLE_BLOCK &&
			       mle->type != DLM_MLE_MASTER &&
			       mle->type != DLM_MLE_MIGRATION);

			/* MASTER mles are initiated locally. The waiting
			 * process will notice the node map change shortly.
			 * Let that happen as normal. */
			if (mle->type == DLM_MLE_MASTER)
				continue;

			/* BLOCK mles are initiated by other nodes. Need to
			 * clean up if the dead node would have been the
			 * master. */
			if (mle->type == DLM_MLE_BLOCK) {
				dlm_clean_block_mle(dlm, mle, dead_node);
				continue;
			}

			/* Everything else is a MIGRATION mle */

			/* The rule for MIGRATION mles is that the master
			 * becomes UNKNOWN if *either* the original or the new
			 * master dies. All UNKNOWN lockres' are sent to
			 * whichever node becomes the recovery master. The new
			 * master is responsible for determining if there is
			 * still a master for this lockres, or if he needs to
			 * take over mastery. Either way, this node should
			 * expect another message to resolve this. */

			if (mle->master != dead_node &&
			    mle->new_master != dead_node)
				continue;

			/* If we have reached this point, this mle needs to be
			 * removed from the list and freed. */
			dlm_clean_migration_mle(dlm, mle);

			mlog(0, "%s: node %u died during migration from "
			     "%u to %u!\n", dlm->name, dead_node, mle->master,
			     mle->new_master);

			/* If we find a lockres associated with the mle, we've
			 * hit this rare case that messes up our lock ordering.
			 * If so, we need to drop the master lock so that we can
			 * take the lockres lock, meaning that we will have to
			 * restart from the head of list. */
			res = dlm_reset_mleres_owner(dlm, mle);
			if (res)
				/* restart */
				goto top;

			/* This may be the last reference */
			__dlm_put_mle(mle);
		}
	}
	spin_unlock(&dlm->master_lock);
}

int dlm_finish_migration(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			 u8 old_master)
{
	struct dlm_node_iter iter;
	int ret = 0;

	spin_lock(&dlm->spinlock);
	dlm_node_iter_init(dlm->domain_map, &iter);
	clear_bit(old_master, iter.node_map);
	clear_bit(dlm->node_num, iter.node_map);
	spin_unlock(&dlm->spinlock);

	/* ownership of the lockres is changing.  account for the
	 * mastery reference here since old_master will briefly have
	 * a reference after the migration completes */
	spin_lock(&res->spinlock);
	dlm_lockres_set_refmap_bit(dlm, res, old_master);
	spin_unlock(&res->spinlock);

	mlog(0, "now time to do a migrate request to other nodes\n");
	ret = dlm_do_migrate_request(dlm, res, old_master,
				     dlm->node_num, &iter);
	if (ret < 0) {
		mlog_errno(ret);
		goto leave;
	}

	mlog(0, "doing assert master of %.*s to all except the original node\n",
	     res->lockname.len, res->lockname.name);
	/* this call now finishes out the nodemap
	 * even if one or more nodes die */
	ret = dlm_do_assert_master(dlm, res, iter.node_map,
				   DLM_ASSERT_MASTER_FINISH_MIGRATION);
	if (ret < 0) {
		/* no longer need to retry.  all living nodes contacted. */
		mlog_errno(ret);
		ret = 0;
	}

	memset(iter.node_map, 0, sizeof(iter.node_map));
	set_bit(old_master, iter.node_map);
	mlog(0, "doing assert master of %.*s back to %u\n",
	     res->lockname.len, res->lockname.name, old_master);
	ret = dlm_do_assert_master(dlm, res, iter.node_map,
				   DLM_ASSERT_MASTER_FINISH_MIGRATION);
	if (ret < 0) {
		mlog(0, "assert master to original master failed "
		     "with %d.\n", ret);
		/* the only nonzero status here would be because of
		 * a dead original node.  we're done. */
		ret = 0;
	}

	/* all done, set the owner, clear the flag */
	spin_lock(&res->spinlock);
	dlm_set_lockres_owner(dlm, res, dlm->node_num);
	res->state &= ~DLM_LOCK_RES_MIGRATING;
	spin_unlock(&res->spinlock);
	/* re-dirty it on the new master */
	dlm_kick_thread(dlm, res);
	wake_up(&res->wq);
leave:
	return ret;
}

/*
 * LOCKRES AST REFCOUNT
 * this is integral to migration
 */

/* for future intent to call an ast, reserve one ahead of time.
 * this should be called only after waiting on the lockres
 * with dlm_wait_on_lockres, and while still holding the
 * spinlock after the call. */
void __dlm_lockres_reserve_ast(struct dlm_lock_resource *res)
{
	assert_spin_locked(&res->spinlock);
	if (res->state & DLM_LOCK_RES_MIGRATING) {
		__dlm_print_one_lock_resource(res);
	}
	BUG_ON(res->state & DLM_LOCK_RES_MIGRATING);

	atomic_inc(&res->asts_reserved);
}

/*
 * used to drop the reserved ast, either because it went unused,
 * or because the ast/bast was actually called.
 *
 * also, if there is a pending migration on this lockres,
 * and this was the last pending ast on the lockres,
 * atomically set the MIGRATING flag before we drop the lock.
 * this is how we ensure that migration can proceed with no
 * asts in progress.  note that it is ok if the state of the
 * queues is such that a lock should be granted in the future
 * or that a bast should be fired, because the new master will
 * shuffle the lists on this lockres as soon as it is migrated.
 */
void dlm_lockres_release_ast(struct dlm_ctxt *dlm,
			     struct dlm_lock_resource *res)
{
	if (!atomic_dec_and_lock(&res->asts_reserved, &res->spinlock))
		return;

	if (!res->migration_pending) {
		spin_unlock(&res->spinlock);
		return;
	}

	BUG_ON(res->state & DLM_LOCK_RES_MIGRATING);
	res->migration_pending = 0;
	res->state |= DLM_LOCK_RES_MIGRATING;
	spin_unlock(&res->spinlock);
	wake_up(&res->wq);
	wake_up(&dlm->migration_wq);
}

void dlm_force_free_mles(struct dlm_ctxt *dlm)
{
	int i;
	struct hlist_head *bucket;
	struct dlm_master_list_entry *mle;
	struct hlist_node *tmp;

	/*
	 * We notified all other nodes that we are exiting the domain and
	 * marked the dlm state to DLM_CTXT_LEAVING. If any mles are still
	 * around we force free them and wake any processes that are waiting
	 * on the mles
	 */
	spin_lock(&dlm->spinlock);
	spin_lock(&dlm->master_lock);

	BUG_ON(dlm->dlm_state != DLM_CTXT_LEAVING);
	BUG_ON((find_next_bit(dlm->domain_map, O2NM_MAX_NODES, 0) < O2NM_MAX_NODES));

	for (i = 0; i < DLM_HASH_BUCKETS; i++) {
		bucket = dlm_master_hash(dlm, i);
		hlist_for_each_entry_safe(mle, tmp, bucket, master_hash_node) {
			if (mle->type != DLM_MLE_BLOCK) {
				mlog(ML_ERROR, "bad mle: %p\n", mle);
				dlm_print_one_mle(mle);
			}
			atomic_set(&mle->woken, 1);
			wake_up(&mle->wq);

			__dlm_unlink_mle(dlm, mle);
			__dlm_mle_detach_hb_events(dlm, mle);
			__dlm_put_mle(mle);
		}
	}
	spin_unlock(&dlm->master_lock);
	spin_unlock(&dlm->spinlock);
}
