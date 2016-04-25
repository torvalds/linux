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
 * The above copyright notice and this permission notice shall be included in
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
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct drm_vma_offset_file {
	struct rb_node vm_rb;
	struct file *vm_filp;
	unsigned long vm_count;
};

struct drm_vma_offset_node {
	rwlock_t vm_lock;
	struct drm_mm_node vm_node;
	struct rb_node vm_rb;
	struct rb_root vm_files;
};

struct drm_vma_offset_manager {
	rwlock_t vm_lock;
	struct rb_root vm_addr_space_rb;
	struct drm_mm vm_addr_space_mm;
};

void drm_vma_offset_manager_init(struct drm_vma_offset_manager *mgr,
				 unsigned long page_offset, unsigned long size);
void drm_vma_offset_manager_destroy(struct drm_vma_offset_manager *mgr);

struct drm_vma_offset_node *drm_vma_offset_lookup_locked(struct drm_vma_offset_manager *mgr,
							   unsigned long start,
							   unsigned long pages);
int drm_vma_offset_add(struct drm_vma_offset_manager *mgr,
		       struct drm_vma_offset_node *node, unsigned long pages);
void drm_vma_offset_remove(struct drm_vma_offset_manager *mgr,
			   struct drm_vma_offset_node *node);

int drm_vma_node_allow(struct drm_vma_offset_node *node, struct file *filp);
void drm_vma_node_revoke(struct drm_vma_offset_node *node, struct file *filp);
bool drm_vma_node_is_allowed(struct drm_vma_offset_node *node,
			     struct file *filp);

/**
 * drm_vma_offset_exact_lookup_locked() - Look up node by exact address
 * @mgr: Manager object
 * @start: Start address (page-based, not byte-based)
 * @pages: Size of object (page-based)
 *
 * Same as drm_vma_offset_lookup_locked() but does not allow any offset into the node.
 * It only returns the exact object with the given start address.
 *
 * RETURNS:
 * Node at exact start address @start.
 */
static inline struct drm_vma_offset_node *
drm_vma_offset_exact_lookup_locked(struct drm_vma_offset_manager *mgr,
				   unsigned long start,
				   unsigned long pages)
{
	struct drm_vma_offset_node *node;

	node = drm_vma_offset_lookup_locked(mgr, start, pages);
	return (node && node->vm_node.start == start) ? node : NULL;
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
 * This lock must not be used for anything else than extended lookups. You must
 * not call any other VMA helpers while holding this lock.
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
 * drm_vma_node_reset() - Initialize or reset node object
 * @node: Node to initialize or reset
 *
 * Reset a node to its initial state. This must be called before using it with
 * any VMA offset manager.
 *
 * This must not be called on an already allocated node, or you will leak
 * memory.
 */
static inline void drm_vma_node_reset(struct drm_vma_offset_node *node)
{
	memset(node, 0, sizeof(*node));
	node->vm_files = RB_ROOT;
	rwlock_init(&node->vm_lock);
}

/**
 * drm_vma_node_start() - Return start address for page-based addressing
 * @node: Node to inspect
 *
 * Return the start address of the given node. This can be used as offset into
 * the linear VM space that is provided by the VMA offset manager. Note that
 * this can only be used for page-based addressing. If you need a proper offset
 * for user-space mappings, you must apply "<< PAGE_SHIFT" or use the
 * drm_vma_node_offset_addr() helper instead.
 *
 * RETURNS:
 * Start address of @node for page-based addressing. 0 if the node does not
 * have an offset allocated.
 */
static inline unsigned long drm_vma_node_start(struct drm_vma_offset_node *node)
{
	return node->vm_node.start;
}

/**
 * drm_vma_node_size() - Return size (page-based)
 * @node: Node to inspect
 *
 * Return the size as number of pages for the given node. This is the same size
 * that was passed to drm_vma_offset_add(). If no offset is allocated for the
 * node, this is 0.
 *
 * RETURNS:
 * Size of @node as number of pages. 0 if the node does not have an offset
 * allocated.
 */
static inline unsigned long drm_vma_node_size(struct drm_vma_offset_node *node)
{
	return node->vm_node.size;
}

/**
 * drm_vma_node_has_offset() - Check whether node is added to offset manager
 * @node: Node to be checked
 *
 * RETURNS:
 * true iff the node was previously allocated an offset and added to
 * an vma offset manager.
 */
static inline bool drm_vma_node_has_offset(struct drm_vma_offset_node *node)
{
	return drm_mm_node_allocated(&node->vm_node);
}

/**
 * drm_vma_node_offset_addr() - Return sanitized offset for user-space mmaps
 * @node: Linked offset node
 *
 * Same as drm_vma_node_start() but returns the address as a valid offset that
 * can be used for user-space mappings during mmap().
 * This must not be called on unlinked nodes.
 *
 * RETURNS:
 * Offset of @node for byte-based addressing. 0 if the node does not have an
 * object allocated.
 */
static inline __u64 drm_vma_node_offset_addr(struct drm_vma_offset_node *node)
{
	return ((__u64)node->vm_node.start) << PAGE_SHIFT;
}

/**
 * drm_vma_node_unmap() - Unmap offset node
 * @node: Offset node
 * @file_mapping: Address space to unmap @node from
 *
 * Unmap all userspace mappings for a given offset node. The mappings must be
 * associated with the @file_mapping address-space. If no offset exists
 * nothing is done.
 *
 * This call is unlocked. The caller must guarantee that drm_vma_offset_remove()
 * is not called on this node concurrently.
 */
static inline void drm_vma_node_unmap(struct drm_vma_offset_node *node,
				      struct address_space *file_mapping)
{
	if (drm_vma_node_has_offset(node))
		unmap_mapping_range(file_mapping,
				    drm_vma_node_offset_addr(node),
				    drm_vma_node_size(node) << PAGE_SHIFT, 1);
}

/**
 * drm_vma_node_verify_access() - Access verification helper for TTM
 * @node: Offset node
 * @filp: Open-file
 *
 * This checks whether @filp is granted access to @node. It is the same as
 * drm_vma_node_is_allowed() but suitable as drop-in helper for TTM
 * verify_access() callbacks.
 *
 * RETURNS:
 * 0 if access is granted, -EACCES otherwise.
 */
static inline int drm_vma_node_verify_access(struct drm_vma_offset_node *node,
					     struct file *filp)
{
	return drm_vma_node_is_allowed(node, filp) ? 0 : -EACCES;
}

#endif /* __DRM_VMA_MANAGER_H__ */
