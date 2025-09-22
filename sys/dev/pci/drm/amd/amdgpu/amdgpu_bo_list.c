/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Christian König <deathsimple@vodafone.de>
 */

#include <linux/sort.h>
#include <linux/uaccess.h>

#include "amdgpu.h"
#include "amdgpu_trace.h"

#define AMDGPU_BO_LIST_MAX_PRIORITY	32u
#define AMDGPU_BO_LIST_NUM_BUCKETS	(AMDGPU_BO_LIST_MAX_PRIORITY + 1)

static void amdgpu_bo_list_free_rcu(struct rcu_head *rcu)
{
	struct amdgpu_bo_list *list = container_of(rcu, struct amdgpu_bo_list,
						   rhead);
	mutex_destroy(&list->bo_list_mutex);
	kvfree(list);
}

static void amdgpu_bo_list_free(struct kref *ref)
{
	struct amdgpu_bo_list *list = container_of(ref, struct amdgpu_bo_list,
						   refcount);
	struct amdgpu_bo_list_entry *e;

	amdgpu_bo_list_for_each_entry(e, list)
		amdgpu_bo_unref(&e->bo);
	call_rcu(&list->rhead, amdgpu_bo_list_free_rcu);
}

static int amdgpu_bo_list_entry_cmp(const void *_a, const void *_b)
{
	const struct amdgpu_bo_list_entry *a = _a, *b = _b;

	if (a->priority > b->priority)
		return 1;
	if (a->priority < b->priority)
		return -1;
	return 0;
}

int amdgpu_bo_list_create(struct amdgpu_device *adev, struct drm_file *filp,
			  struct drm_amdgpu_bo_list_entry *info,
			  size_t num_entries, struct amdgpu_bo_list **result)
{
	unsigned last_entry = 0, first_userptr = num_entries;
	struct amdgpu_bo_list_entry *array;
	struct amdgpu_bo_list *list;
	uint64_t total_size = 0;
	unsigned i;
	int r;

	list = kvzalloc(struct_size(list, entries, num_entries), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	kref_init(&list->refcount);

	list->num_entries = num_entries;
	array = list->entries;

	for (i = 0; i < num_entries; ++i) {
		struct amdgpu_bo_list_entry *entry;
		struct drm_gem_object *gobj;
		struct amdgpu_bo *bo;
		struct mm_struct *usermm;

		gobj = drm_gem_object_lookup(filp, info[i].bo_handle);
		if (!gobj) {
			r = -ENOENT;
			goto error_free;
		}

		bo = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
		drm_gem_object_put(gobj);

#ifdef notyet
		usermm = amdgpu_ttm_tt_get_usermm(bo->tbo.ttm);
		if (usermm) {
			if (usermm != current->mm) {
				amdgpu_bo_unref(&bo);
				r = -EPERM;
				goto error_free;
			}
			entry = &array[--first_userptr];
		} else {
			entry = &array[last_entry++];
		}
#else
			entry = &array[last_entry++];
#endif

		entry->priority = min(info[i].bo_priority,
				      AMDGPU_BO_LIST_MAX_PRIORITY);
		entry->bo = bo;

		if (bo->preferred_domains == AMDGPU_GEM_DOMAIN_GDS)
			list->gds_obj = bo;
		if (bo->preferred_domains == AMDGPU_GEM_DOMAIN_GWS)
			list->gws_obj = bo;
		if (bo->preferred_domains == AMDGPU_GEM_DOMAIN_OA)
			list->oa_obj = bo;

		total_size += amdgpu_bo_size(bo);
		trace_amdgpu_bo_list_set(list, bo);
	}

	list->first_userptr = first_userptr;
	sort(array, last_entry, sizeof(struct amdgpu_bo_list_entry),
	     amdgpu_bo_list_entry_cmp, NULL);

	trace_amdgpu_cs_bo_status(list->num_entries, total_size);

	rw_init(&list->bo_list_mutex, "agbl");
	*result = list;
	return 0;

error_free:
	for (i = 0; i < last_entry; ++i)
		amdgpu_bo_unref(&array[i].bo);
	for (i = first_userptr; i < num_entries; ++i)
		amdgpu_bo_unref(&array[i].bo);
	kvfree(list);
	return r;

}

static void amdgpu_bo_list_destroy(struct amdgpu_fpriv *fpriv, int id)
{
	struct amdgpu_bo_list *list;

	mutex_lock(&fpriv->bo_list_lock);
	list = idr_remove(&fpriv->bo_list_handles, id);
	mutex_unlock(&fpriv->bo_list_lock);
	if (list)
		kref_put(&list->refcount, amdgpu_bo_list_free);
}

int amdgpu_bo_list_get(struct amdgpu_fpriv *fpriv, int id,
		       struct amdgpu_bo_list **result)
{
	rcu_read_lock();
	*result = idr_find(&fpriv->bo_list_handles, id);

	if (*result && kref_get_unless_zero(&(*result)->refcount)) {
		rcu_read_unlock();
		return 0;
	}

	rcu_read_unlock();
	*result = NULL;
	return -ENOENT;
}

void amdgpu_bo_list_put(struct amdgpu_bo_list *list)
{
	kref_put(&list->refcount, amdgpu_bo_list_free);
}

int amdgpu_bo_create_list_entry_array(struct drm_amdgpu_bo_list_in *in,
				      struct drm_amdgpu_bo_list_entry **info_param)
{
	const void __user *uptr = u64_to_user_ptr(in->bo_info_ptr);
	const uint32_t info_size = sizeof(struct drm_amdgpu_bo_list_entry);
	struct drm_amdgpu_bo_list_entry *info;
	int r;

	info = kvmalloc_array(in->bo_number, info_size, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* copy the handle array from userspace to a kernel buffer */
	r = -EFAULT;
	if (likely(info_size == in->bo_info_size)) {
		unsigned long bytes = in->bo_number *
			in->bo_info_size;

		if (copy_from_user(info, uptr, bytes))
			goto error_free;

	} else {
		unsigned long bytes = min(in->bo_info_size, info_size);
		unsigned i;

		memset(info, 0, in->bo_number * info_size);
		for (i = 0; i < in->bo_number; ++i) {
			if (copy_from_user(&info[i], uptr, bytes))
				goto error_free;

			uptr += in->bo_info_size;
		}
	}

	*info_param = info;
	return 0;

error_free:
	kvfree(info);
	return r;
}

int amdgpu_bo_list_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	union drm_amdgpu_bo_list *args = data;
	uint32_t handle = args->in.list_handle;
	struct drm_amdgpu_bo_list_entry *info = NULL;
	struct amdgpu_bo_list *list, *old;
	int r;

	r = amdgpu_bo_create_list_entry_array(&args->in, &info);
	if (r)
		return r;

	switch (args->in.operation) {
	case AMDGPU_BO_LIST_OP_CREATE:
		r = amdgpu_bo_list_create(adev, filp, info, args->in.bo_number,
					  &list);
		if (r)
			goto error_free;

		mutex_lock(&fpriv->bo_list_lock);
		r = idr_alloc(&fpriv->bo_list_handles, list, 1, 0, GFP_KERNEL);
		mutex_unlock(&fpriv->bo_list_lock);
		if (r < 0) {
			goto error_put_list;
		}

		handle = r;
		break;

	case AMDGPU_BO_LIST_OP_DESTROY:
		amdgpu_bo_list_destroy(fpriv, handle);
		handle = 0;
		break;

	case AMDGPU_BO_LIST_OP_UPDATE:
		r = amdgpu_bo_list_create(adev, filp, info, args->in.bo_number,
					  &list);
		if (r)
			goto error_free;

		mutex_lock(&fpriv->bo_list_lock);
		old = idr_replace(&fpriv->bo_list_handles, list, handle);
		mutex_unlock(&fpriv->bo_list_lock);

		if (IS_ERR(old)) {
			r = PTR_ERR(old);
			goto error_put_list;
		}

		amdgpu_bo_list_put(old);
		break;

	default:
		r = -EINVAL;
		goto error_free;
	}

	memset(args, 0, sizeof(*args));
	args->out.list_handle = handle;
	kvfree(info);

	return 0;

error_put_list:
	amdgpu_bo_list_put(list);

error_free:
	kvfree(info);
	return r;
}
