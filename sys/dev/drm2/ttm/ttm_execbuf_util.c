/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>
#include <dev/drm2/ttm/ttm_execbuf_util.h>
#include <dev/drm2/ttm/ttm_bo_driver.h>
#include <dev/drm2/ttm/ttm_placement.h>

static void ttm_eu_backoff_reservation_locked(struct list_head *list)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;
		if (!entry->reserved)
			continue;

		if (entry->removed) {
			ttm_bo_add_to_lru(bo);
			entry->removed = false;

		}
		entry->reserved = false;
		atomic_set(&bo->reserved, 0);
		wakeup(bo);
	}
}

static void ttm_eu_del_from_lru_locked(struct list_head *list)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;
		if (!entry->reserved)
			continue;

		if (!entry->removed) {
			entry->put_count = ttm_bo_del_from_lru(bo);
			entry->removed = true;
		}
	}
}

static void ttm_eu_list_ref_sub(struct list_head *list)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;

		if (entry->put_count) {
			ttm_bo_list_ref_sub(bo, entry->put_count, true);
			entry->put_count = 0;
		}
	}
}

void ttm_eu_backoff_reservation(struct list_head *list)
{
	struct ttm_validate_buffer *entry;
	struct ttm_bo_global *glob;

	if (list_empty(list))
		return;

	entry = list_first_entry(list, struct ttm_validate_buffer, head);
	glob = entry->bo->glob;
	mtx_lock(&glob->lru_lock);
	ttm_eu_backoff_reservation_locked(list);
	mtx_unlock(&glob->lru_lock);
}

/*
 * Reserve buffers for validation.
 *
 * If a buffer in the list is marked for CPU access, we back off and
 * wait for that buffer to become free for GPU access.
 *
 * If a buffer is reserved for another validation, the validator with
 * the highest validation sequence backs off and waits for that buffer
 * to become unreserved. This prevents deadlocks when validating multiple
 * buffers in different orders.
 */

int ttm_eu_reserve_buffers(struct list_head *list)
{
	struct ttm_bo_global *glob;
	struct ttm_validate_buffer *entry;
	int ret;
	uint32_t val_seq;

	if (list_empty(list))
		return 0;

	list_for_each_entry(entry, list, head) {
		entry->reserved = false;
		entry->put_count = 0;
		entry->removed = false;
	}

	entry = list_first_entry(list, struct ttm_validate_buffer, head);
	glob = entry->bo->glob;

	mtx_lock(&glob->lru_lock);
	val_seq = entry->bo->bdev->val_seq++;

retry_locked:
	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;

		/* already slowpath reserved? */
		if (entry->reserved)
			continue;

		ret = ttm_bo_reserve_nolru(bo, true, true, true, val_seq);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
			ttm_eu_del_from_lru_locked(list);
			ret = ttm_bo_reserve_nolru(bo, true, false,
						   true, val_seq);
			if (!ret)
				break;

			if (unlikely(ret != -EAGAIN))
				goto err;

			/* fallthrough */
		case -EAGAIN:
			ttm_eu_backoff_reservation_locked(list);

			/*
			 * temporarily increase sequence number every retry,
			 * to prevent us from seeing our old reservation
			 * sequence when someone else reserved the buffer,
			 * but hasn't updated the seq_valid/seqno members yet.
			 */
			val_seq = entry->bo->bdev->val_seq++;

			ttm_eu_list_ref_sub(list);
			ret = ttm_bo_reserve_slowpath_nolru(bo, true, val_seq);
			if (unlikely(ret != 0)) {
				mtx_unlock(&glob->lru_lock);
				return ret;
			}
			entry->reserved = true;
			if (unlikely(atomic_read(&bo->cpu_writers) > 0)) {
				ret = -EBUSY;
				goto err;
			}
			goto retry_locked;
		default:
			goto err;
		}

		entry->reserved = true;
		if (unlikely(atomic_read(&bo->cpu_writers) > 0)) {
			ret = -EBUSY;
			goto err;
		}
	}

	ttm_eu_del_from_lru_locked(list);
	mtx_unlock(&glob->lru_lock);
	ttm_eu_list_ref_sub(list);

	return 0;

err:
	ttm_eu_backoff_reservation_locked(list);
	mtx_unlock(&glob->lru_lock);
	ttm_eu_list_ref_sub(list);
	return ret;
}

void ttm_eu_fence_buffer_objects(struct list_head *list, void *sync_obj)
{
	struct ttm_validate_buffer *entry;
	struct ttm_buffer_object *bo;
	struct ttm_bo_global *glob;
	struct ttm_bo_device *bdev;
	struct ttm_bo_driver *driver;

	if (list_empty(list))
		return;

	bo = list_first_entry(list, struct ttm_validate_buffer, head)->bo;
	bdev = bo->bdev;
	driver = bdev->driver;
	glob = bo->glob;

	mtx_lock(&glob->lru_lock);
	mtx_lock(&bdev->fence_lock);

	list_for_each_entry(entry, list, head) {
		bo = entry->bo;
		entry->old_sync_obj = bo->sync_obj;
		bo->sync_obj = driver->sync_obj_ref(sync_obj);
		ttm_bo_unreserve_locked(bo);
		entry->reserved = false;
	}
	mtx_unlock(&bdev->fence_lock);
	mtx_unlock(&glob->lru_lock);

	list_for_each_entry(entry, list, head) {
		if (entry->old_sync_obj)
			driver->sync_obj_unref(&entry->old_sync_obj);
	}
}
