/**************************************************************************
 *
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */
/** @file ttm_ref_object.c
 *
 * Base- and reference object implementation for the various
 * ttm objects. Implements reference counting, minimal security checks
 * and release on file close.
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * struct ttm_object_file
 *
 * @tdev: Pointer to the ttm_object_device.
 *
 * @lock: Lock that protects the ref_list list and the
 * ref_hash hash tables.
 *
 * @ref_list: List of ttm_ref_objects to be destroyed at
 * file release.
 *
 * @ref_hash: Hash tables of ref objects, one per ttm_ref_type,
 * for fast lookup of ref objects given a base object.
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm.h>
#include <sys/rwlock.h>
#include <dev/drm2/ttm/ttm_object.h>
#include <dev/drm2/ttm/ttm_module.h>

struct ttm_object_file {
	struct ttm_object_device *tdev;
	struct rwlock lock;
	struct list_head ref_list;
	struct drm_open_hash ref_hash[TTM_REF_NUM];
	u_int refcount;
};

/**
 * struct ttm_object_device
 *
 * @object_lock: lock that protects the object_hash hash table.
 *
 * @object_hash: hash table for fast lookup of object global names.
 *
 * @object_count: Per device object count.
 *
 * This is the per-device data structure needed for ttm object management.
 */

struct ttm_object_device {
	struct rwlock object_lock;
	struct drm_open_hash object_hash;
	atomic_t object_count;
	struct ttm_mem_global *mem_glob;
};

/**
 * struct ttm_ref_object
 *
 * @hash: Hash entry for the per-file object reference hash.
 *
 * @head: List entry for the per-file list of ref-objects.
 *
 * @kref: Ref count.
 *
 * @obj: Base object this ref object is referencing.
 *
 * @ref_type: Type of ref object.
 *
 * This is similar to an idr object, but it also has a hash table entry
 * that allows lookup with a pointer to the referenced object as a key. In
 * that way, one can easily detect whether a base object is referenced by
 * a particular ttm_object_file. It also carries a ref count to avoid creating
 * multiple ref objects if a ttm_object_file references the same base
 * object more than once.
 */

struct ttm_ref_object {
	struct drm_hash_item hash;
	struct list_head head;
	u_int kref;
	enum ttm_ref_type ref_type;
	struct ttm_base_object *obj;
	struct ttm_object_file *tfile;
};

MALLOC_DEFINE(M_TTM_OBJ_FILE, "ttm_obj_file", "TTM File Objects");

static inline struct ttm_object_file *
ttm_object_file_ref(struct ttm_object_file *tfile)
{
	refcount_acquire(&tfile->refcount);
	return tfile;
}

static void ttm_object_file_destroy(struct ttm_object_file *tfile)
{

	free(tfile, M_TTM_OBJ_FILE);
}


static inline void ttm_object_file_unref(struct ttm_object_file **p_tfile)
{
	struct ttm_object_file *tfile = *p_tfile;

	*p_tfile = NULL;
	if (refcount_release(&tfile->refcount))
		ttm_object_file_destroy(tfile);
}


int ttm_base_object_init(struct ttm_object_file *tfile,
			 struct ttm_base_object *base,
			 bool shareable,
			 enum ttm_object_type object_type,
			 void (*rcount_release) (struct ttm_base_object **),
			 void (*ref_obj_release) (struct ttm_base_object *,
						  enum ttm_ref_type ref_type))
{
	struct ttm_object_device *tdev = tfile->tdev;
	int ret;

	base->shareable = shareable;
	base->tfile = ttm_object_file_ref(tfile);
	base->refcount_release = rcount_release;
	base->ref_obj_release = ref_obj_release;
	base->object_type = object_type;
	refcount_init(&base->refcount, 1);
	rw_init(&tdev->object_lock, "ttmbao");
	rw_wlock(&tdev->object_lock);
	ret = drm_ht_just_insert_please(&tdev->object_hash,
					    &base->hash,
					    (unsigned long)base, 31, 0, 0);
	rw_wunlock(&tdev->object_lock);
	if (unlikely(ret != 0))
		goto out_err0;

	ret = ttm_ref_object_add(tfile, base, TTM_REF_USAGE, NULL);
	if (unlikely(ret != 0))
		goto out_err1;

	ttm_base_object_unref(&base);

	return 0;
out_err1:
	rw_wlock(&tdev->object_lock);
	(void)drm_ht_remove_item(&tdev->object_hash, &base->hash);
	rw_wunlock(&tdev->object_lock);
out_err0:
	return ret;
}

static void ttm_release_base(struct ttm_base_object *base)
{
	struct ttm_object_device *tdev = base->tfile->tdev;

	(void)drm_ht_remove_item(&tdev->object_hash, &base->hash);
	rw_wunlock(&tdev->object_lock);
	/*
	 * Note: We don't use synchronize_rcu() here because it's far
	 * too slow. It's up to the user to free the object using
	 * call_rcu() or ttm_base_object_kfree().
	 */

	if (base->refcount_release) {
		ttm_object_file_unref(&base->tfile);
		base->refcount_release(&base);
	}
	rw_wlock(&tdev->object_lock);
}

void ttm_base_object_unref(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct ttm_object_device *tdev = base->tfile->tdev;

	*p_base = NULL;

	/*
	 * Need to take the lock here to avoid racing with
	 * users trying to look up the object.
	 */

	rw_wlock(&tdev->object_lock);
	if (refcount_release(&base->refcount))
		ttm_release_base(base);
	rw_wunlock(&tdev->object_lock);
}

struct ttm_base_object *ttm_base_object_lookup(struct ttm_object_file *tfile,
					       uint32_t key)
{
	struct ttm_object_device *tdev = tfile->tdev;
	struct ttm_base_object *base;
	struct drm_hash_item *hash;
	int ret;

	rw_rlock(&tdev->object_lock);
	ret = drm_ht_find_item(&tdev->object_hash, key, &hash);

	if (ret == 0) {
		base = drm_hash_entry(hash, struct ttm_base_object, hash);
		refcount_acquire(&base->refcount);
	}
	rw_runlock(&tdev->object_lock);

	if (unlikely(ret != 0))
		return NULL;

	if (tfile != base->tfile && !base->shareable) {
		printf("[TTM] Attempted access of non-shareable object %p\n",
		    base);
		ttm_base_object_unref(&base);
		return NULL;
	}

	return base;
}

MALLOC_DEFINE(M_TTM_OBJ_REF, "ttm_obj_ref", "TTM Ref Objects");

int ttm_ref_object_add(struct ttm_object_file *tfile,
		       struct ttm_base_object *base,
		       enum ttm_ref_type ref_type, bool *existed)
{
	struct drm_open_hash *ht = &tfile->ref_hash[ref_type];
	struct ttm_ref_object *ref;
	struct drm_hash_item *hash;
	struct ttm_mem_global *mem_glob = tfile->tdev->mem_glob;
	int ret = -EINVAL;

	if (existed != NULL)
		*existed = true;

	while (ret == -EINVAL) {
		rw_rlock(&tfile->lock);
		ret = drm_ht_find_item(ht, base->hash.key, &hash);

		if (ret == 0) {
			ref = drm_hash_entry(hash, struct ttm_ref_object, hash);
			refcount_acquire(&ref->kref);
			rw_runlock(&tfile->lock);
			break;
		}

		rw_runlock(&tfile->lock);
		ret = ttm_mem_global_alloc(mem_glob, sizeof(*ref),
					   false, false);
		if (unlikely(ret != 0))
			return ret;
		ref = malloc(sizeof(*ref), M_TTM_OBJ_REF, M_WAITOK);
		if (unlikely(ref == NULL)) {
			ttm_mem_global_free(mem_glob, sizeof(*ref));
			return -ENOMEM;
		}

		ref->hash.key = base->hash.key;
		ref->obj = base;
		ref->tfile = tfile;
		ref->ref_type = ref_type;
		refcount_init(&ref->kref, 1);

		rw_wlock(&tfile->lock);
		ret = drm_ht_insert_item(ht, &ref->hash);

		if (ret == 0) {
			list_add_tail(&ref->head, &tfile->ref_list);
			refcount_acquire(&base->refcount);
			rw_wunlock(&tfile->lock);
			if (existed != NULL)
				*existed = false;
			break;
		}

		rw_wunlock(&tfile->lock);
		MPASS(ret == -EINVAL);

		ttm_mem_global_free(mem_glob, sizeof(*ref));
		free(ref, M_TTM_OBJ_REF);
	}

	return ret;
}

static void ttm_ref_object_release(struct ttm_ref_object *ref)
{
	struct ttm_base_object *base = ref->obj;
	struct ttm_object_file *tfile = ref->tfile;
	struct drm_open_hash *ht;
	struct ttm_mem_global *mem_glob = tfile->tdev->mem_glob;

	ht = &tfile->ref_hash[ref->ref_type];
	(void)drm_ht_remove_item(ht, &ref->hash);
	list_del(&ref->head);
	rw_wunlock(&tfile->lock);

	if (ref->ref_type != TTM_REF_USAGE && base->ref_obj_release)
		base->ref_obj_release(base, ref->ref_type);

	ttm_base_object_unref(&ref->obj);
	ttm_mem_global_free(mem_glob, sizeof(*ref));
	free(ref, M_TTM_OBJ_REF);
	rw_wlock(&tfile->lock);
}

int ttm_ref_object_base_unref(struct ttm_object_file *tfile,
			      unsigned long key, enum ttm_ref_type ref_type)
{
	struct drm_open_hash *ht = &tfile->ref_hash[ref_type];
	struct ttm_ref_object *ref;
	struct drm_hash_item *hash;
	int ret;

	rw_wlock(&tfile->lock);
	ret = drm_ht_find_item(ht, key, &hash);
	if (unlikely(ret != 0)) {
		rw_wunlock(&tfile->lock);
		return -EINVAL;
	}
	ref = drm_hash_entry(hash, struct ttm_ref_object, hash);
	if (refcount_release(&ref->kref))
		ttm_ref_object_release(ref);
	rw_wunlock(&tfile->lock);
	return 0;
}

void ttm_object_file_release(struct ttm_object_file **p_tfile)
{
	struct ttm_ref_object *ref;
	struct list_head *list;
	unsigned int i;
	struct ttm_object_file *tfile = *p_tfile;

	*p_tfile = NULL;
	rw_wlock(&tfile->lock);

	/*
	 * Since we release the lock within the loop, we have to
	 * restart it from the beginning each time.
	 */

	while (!list_empty(&tfile->ref_list)) {
		list = tfile->ref_list.next;
		ref = list_entry(list, struct ttm_ref_object, head);
		ttm_ref_object_release(ref);
	}

	for (i = 0; i < TTM_REF_NUM; ++i)
		drm_ht_remove(&tfile->ref_hash[i]);

	rw_wunlock(&tfile->lock);
	ttm_object_file_unref(&tfile);
}

struct ttm_object_file *ttm_object_file_init(struct ttm_object_device *tdev,
					     unsigned int hash_order)
{
	struct ttm_object_file *tfile;
	unsigned int i;
	unsigned int j = 0;
	int ret;

	tfile = malloc(sizeof(*tfile), M_TTM_OBJ_FILE, M_WAITOK);
	rw_init(&tfile->lock, "ttmfo");
	tfile->tdev = tdev;
	refcount_init(&tfile->refcount, 1);
	INIT_LIST_HEAD(&tfile->ref_list);

	for (i = 0; i < TTM_REF_NUM; ++i) {
		ret = drm_ht_create(&tfile->ref_hash[i], hash_order);
		if (ret) {
			j = i;
			goto out_err;
		}
	}

	return tfile;
out_err:
	for (i = 0; i < j; ++i)
		drm_ht_remove(&tfile->ref_hash[i]);

	free(tfile, M_TTM_OBJ_FILE);

	return NULL;
}

MALLOC_DEFINE(M_TTM_OBJ_DEV, "ttm_obj_dev", "TTM Device Objects");

struct ttm_object_device *ttm_object_device_init(struct ttm_mem_global
						 *mem_glob,
						 unsigned int hash_order)
{
	struct ttm_object_device *tdev;
	int ret;

	tdev = malloc(sizeof(*tdev), M_TTM_OBJ_DEV, M_WAITOK);
	tdev->mem_glob = mem_glob;
	rw_init(&tdev->object_lock, "ttmdo");
	atomic_set(&tdev->object_count, 0);
	ret = drm_ht_create(&tdev->object_hash, hash_order);

	if (ret == 0)
		return tdev;

	free(tdev, M_TTM_OBJ_DEV);
	return NULL;
}

void ttm_object_device_release(struct ttm_object_device **p_tdev)
{
	struct ttm_object_device *tdev = *p_tdev;

	*p_tdev = NULL;

	rw_wlock(&tdev->object_lock);
	drm_ht_remove(&tdev->object_hash);
	rw_wunlock(&tdev->object_lock);

	free(tdev, M_TTM_OBJ_DEV);
}
