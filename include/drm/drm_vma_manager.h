#ifndef __DRM_VMA_MANAGER_H__
#define __DRM_VMA_MANAGER_H__

/*
 * Copyright (c) 2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright yestice and this permission yestice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drm_mm.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* We make up offsets for buffer objects so we can recognize them at
 * mmap time. pgoff in mmap is an unsigned long, so we need to make sure
 * that the faked up offset will fit
 */
#if BITS_PER_LONG == 64
#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFFUL >> PAGE_SHIFT) * 256)
#else
#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFUL >> PAGE_SHIFT) * 16)
#endif

struct drm_file;

struct drm_vma_offset_file {
	struct rb_yesde vm_rb;
	struct drm_file *vm_tag;
	unsigned long vm_count;
};

struct drm_vma_offset_yesde {
	rwlock_t vm_lock;
	struct drm_mm_yesde vm_yesde;
	struct rb_root vm_files;
	bool readonly:1;
};

struct drm_vma_offset_manager {
	rwlock_t vm_lock;
	struct drm_mm vm_addr_space_mm;
};

void drm_vma_offset_manager_init(struct drm_vma_offset_manager *mgr,
				 unsigned long page_offset, unsigned long size);
void drm_vma_offset_manager_destroy(struct drm_vma_offset_manager *mgr);

struct drm_vma_offset_yesde *drm_vma_offset_lookup_locked(struct drm_vma_offset_manager *mgr,
							   unsigned long start,
							   unsigned long pages);
int drm_vma_offset_add(struct drm_vma_offset_manager *mgr,
		       struct drm_vma_offset_yesde *yesde, unsigned long pages);
void drm_vma_offset_remove(struct drm_vma_offset_manager *mgr,
			   struct drm_vma_offset_yesde *yesde);

int drm_vma_yesde_allow(struct drm_vma_offset_yesde *yesde, struct drm_file *tag);
void drm_vma_yesde_revoke(struct drm_vma_offset_yesde *yesde,
			 struct drm_file *tag);
bool drm_vma_yesde_is_allowed(struct drm_vma_offset_yesde *yesde,
			     struct drm_file *tag);

/**
 * drm_vma_offset_exact_lookup_locked() - Look up yesde by exact address
 * @mgr: Manager object
 * @start: Start address (page-based, yest byte-based)
 * @pages: Size of object (page-based)
 *
 * Same as drm_vma_offset_lookup_locked() but does yest allow any offset into the yesde.
 * It only returns the exact object with the given start address.
 *
 * RETURNS:
 * Node at exact start address @start.
 */
static inline struct drm_vma_offset_yesde *
drm_vma_offset_exact_lookup_locked(struct drm_vma_offset_manager *mgr,
				   unsigned long start,
				   unsigned long pages)
{
	struct drm_vma_offset_yesde *yesde;

	yesde = drm_vma_offset_lookup_locked(mgr, start, pages);
	return (yesde && yesde->vm_yesde.start == start) ? yesde : NULL;
}

/**
 * drm_vma_offset_lock_lookup() - Lock lookup for extended private use
 * @mgr: Manager object
 *
 * Lock VMA manager for extended lookups. Only locked VMA function calls
 * are allowed while holding this lock. All other contexts are blocked from VMA
 * until the lock is released via drm_vma_offset_unlock_lookup().
 *
 * Use this if you need to take a reference to the objects returned by
 * drm_vma_offset_lookup_locked() before releasing this lock again.
 *
 * This lock must yest be used for anything else than extended lookups. You must
 * yest call any other VMA helpers while holding this lock.
 *
 * Note: You're in atomic-context while holding this lock!
 */
static inline void drm_vma_offset_lock_lookup(struct drm_vma_offset_manager *mgr)
{
	read_lock(&mgr->vm_lock);
}

/**
 * drm_vma_offset_unlock_lookup() - Unlock lookup for extended private use
 * @mgr: Manager object
 *
 * Release lookup-lock. See drm_vma_offset_lock_lookup() for more information.
 */
static inline void drm_vma_offset_unlock_lookup(struct drm_vma_offset_manager *mgr)
{
	read_unlock(&mgr->vm_lock);
}

/**
 * drm_vma_yesde_reset() - Initialize or reset yesde object
 * @yesde: Node to initialize or reset
 *
 * Reset a yesde to its initial state. This must be called before using it with
 * any VMA offset manager.
 *
 * This must yest be called on an already allocated yesde, or you will leak
 * memory.
 */
static inline void drm_vma_yesde_reset(struct drm_vma_offset_yesde *yesde)
{
	memset(yesde, 0, sizeof(*yesde));
	yesde->vm_files = RB_ROOT;
	rwlock_init(&yesde->vm_lock);
}

/**
 * drm_vma_yesde_start() - Return start address for page-based addressing
 * @yesde: Node to inspect
 *
 * Return the start address of the given yesde. This can be used as offset into
 * the linear VM space that is provided by the VMA offset manager. Note that
 * this can only be used for page-based addressing. If you need a proper offset
 * for user-space mappings, you must apply "<< PAGE_SHIFT" or use the
 * drm_vma_yesde_offset_addr() helper instead.
 *
 * RETURNS:
 * Start address of @yesde for page-based addressing. 0 if the yesde does yest
 * have an offset allocated.
 */
static inline unsigned long drm_vma_yesde_start(const struct drm_vma_offset_yesde *yesde)
{
	return yesde->vm_yesde.start;
}

/**
 * drm_vma_yesde_size() - Return size (page-based)
 * @yesde: Node to inspect
 *
 * Return the size as number of pages for the given yesde. This is the same size
 * that was passed to drm_vma_offset_add(). If yes offset is allocated for the
 * yesde, this is 0.
 *
 * RETURNS:
 * Size of @yesde as number of pages. 0 if the yesde does yest have an offset
 * allocated.
 */
static inline unsigned long drm_vma_yesde_size(struct drm_vma_offset_yesde *yesde)
{
	return yesde->vm_yesde.size;
}

/**
 * drm_vma_yesde_offset_addr() - Return sanitized offset for user-space mmaps
 * @yesde: Linked offset yesde
 *
 * Same as drm_vma_yesde_start() but returns the address as a valid offset that
 * can be used for user-space mappings during mmap().
 * This must yest be called on unlinked yesdes.
 *
 * RETURNS:
 * Offset of @yesde for byte-based addressing. 0 if the yesde does yest have an
 * object allocated.
 */
static inline __u64 drm_vma_yesde_offset_addr(struct drm_vma_offset_yesde *yesde)
{
	return ((__u64)yesde->vm_yesde.start) << PAGE_SHIFT;
}

/**
 * drm_vma_yesde_unmap() - Unmap offset yesde
 * @yesde: Offset yesde
 * @file_mapping: Address space to unmap @yesde from
 *
 * Unmap all userspace mappings for a given offset yesde. The mappings must be
 * associated with the @file_mapping address-space. If yes offset exists
 * yesthing is done.
 *
 * This call is unlocked. The caller must guarantee that drm_vma_offset_remove()
 * is yest called on this yesde concurrently.
 */
static inline void drm_vma_yesde_unmap(struct drm_vma_offset_yesde *yesde,
				      struct address_space *file_mapping)
{
	if (drm_mm_yesde_allocated(&yesde->vm_yesde))
		unmap_mapping_range(file_mapping,
				    drm_vma_yesde_offset_addr(yesde),
				    drm_vma_yesde_size(yesde) << PAGE_SHIFT, 1);
}

/**
 * drm_vma_yesde_verify_access() - Access verification helper for TTM
 * @yesde: Offset yesde
 * @tag: Tag of file to check
 *
 * This checks whether @tag is granted access to @yesde. It is the same as
 * drm_vma_yesde_is_allowed() but suitable as drop-in helper for TTM
 * verify_access() callbacks.
 *
 * RETURNS:
 * 0 if access is granted, -EACCES otherwise.
 */
static inline int drm_vma_yesde_verify_access(struct drm_vma_offset_yesde *yesde,
					     struct drm_file *tag)
{
	return drm_vma_yesde_is_allowed(yesde, tag) ? 0 : -EACCES;
}

#endif /* __DRM_VMA_MANAGER_H__ */
