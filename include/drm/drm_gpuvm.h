/* SPDX-License-Identifier: GPL-2.0-only OR MIT */

#ifndef __DRM_GPUVM_H__
#define __DRM_GPUVM_H__

/*
 * Copyright (c) 2022 Red Hat.
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

#include <linux/dma-resv.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_exec.h>

struct drm_gpuvm;
struct drm_gpuvm_bo;
struct drm_gpuvm_ops;

/**
 * enum drm_gpuva_flags - flags for struct drm_gpuva
 */
enum drm_gpuva_flags {
	/**
	 * @DRM_GPUVA_INVALIDATED:
	 *
	 * Flag indicating that the &drm_gpuva's backing GEM is invalidated.
	 */
	DRM_GPUVA_INVALIDATED = (1 << 0),

	/**
	 * @DRM_GPUVA_SPARSE:
	 *
	 * Flag indicating that the &drm_gpuva is a sparse mapping.
	 */
	DRM_GPUVA_SPARSE = (1 << 1),

	/**
	 * @DRM_GPUVA_USERBITS: user defined bits
	 */
	DRM_GPUVA_USERBITS = (1 << 2),
};

/**
 * struct drm_gpuva - structure to track a GPU VA mapping
 *
 * This structure represents a GPU VA mapping and is associated with a
 * &drm_gpuvm.
 *
 * Typically, this structure is embedded in bigger driver structures.
 */
struct drm_gpuva {
	/**
	 * @vm: the &drm_gpuvm this object is associated with
	 */
	struct drm_gpuvm *vm;

	/**
	 * @vm_bo: the &drm_gpuvm_bo abstraction for the mapped
	 * &drm_gem_object
	 */
	struct drm_gpuvm_bo *vm_bo;

	/**
	 * @flags: the &drm_gpuva_flags for this mapping
	 */
	enum drm_gpuva_flags flags;

	/**
	 * @va: structure containing the address and range of the &drm_gpuva
	 */
	struct {
		/**
		 * @addr: the start address
		 */
		u64 addr;

		/*
		 * @range: the range
		 */
		u64 range;
	} va;

	/**
	 * @gem: structure containing the &drm_gem_object and it's offset
	 */
	struct {
		/**
		 * @offset: the offset within the &drm_gem_object
		 */
		u64 offset;

		/**
		 * @obj: the mapped &drm_gem_object
		 */
		struct drm_gem_object *obj;

		/**
		 * @entry: the &list_head to attach this object to a &drm_gpuvm_bo
		 */
		struct list_head entry;
	} gem;

	/**
	 * @rb: structure containing data to store &drm_gpuvas in a rb-tree
	 */
	struct {
		/**
		 * @rb: the rb-tree node
		 */
		struct rb_node node;

		/**
		 * @entry: The &list_head to additionally connect &drm_gpuvas
		 * in the same order they appear in the interval tree. This is
		 * useful to keep iterating &drm_gpuvas from a start node found
		 * through the rb-tree while doing modifications on the rb-tree
		 * itself.
		 */
		struct list_head entry;

		/**
		 * @__subtree_last: needed by the interval tree, holding last-in-subtree
		 */
		u64 __subtree_last;
	} rb;
};

int drm_gpuva_insert(struct drm_gpuvm *gpuvm, struct drm_gpuva *va);
void drm_gpuva_remove(struct drm_gpuva *va);

void drm_gpuva_link(struct drm_gpuva *va, struct drm_gpuvm_bo *vm_bo);
void drm_gpuva_unlink(struct drm_gpuva *va);

struct drm_gpuva *drm_gpuva_find(struct drm_gpuvm *gpuvm,
				 u64 addr, u64 range);
struct drm_gpuva *drm_gpuva_find_first(struct drm_gpuvm *gpuvm,
				       u64 addr, u64 range);
struct drm_gpuva *drm_gpuva_find_prev(struct drm_gpuvm *gpuvm, u64 start);
struct drm_gpuva *drm_gpuva_find_next(struct drm_gpuvm *gpuvm, u64 end);

static inline void drm_gpuva_init(struct drm_gpuva *va, u64 addr, u64 range,
				  struct drm_gem_object *obj, u64 offset)
{
	va->va.addr = addr;
	va->va.range = range;
	va->gem.obj = obj;
	va->gem.offset = offset;
}

/**
 * drm_gpuva_invalidate() - sets whether the backing GEM of this &drm_gpuva is
 * invalidated
 * @va: the &drm_gpuva to set the invalidate flag for
 * @invalidate: indicates whether the &drm_gpuva is invalidated
 */
static inline void drm_gpuva_invalidate(struct drm_gpuva *va, bool invalidate)
{
	if (invalidate)
		va->flags |= DRM_GPUVA_INVALIDATED;
	else
		va->flags &= ~DRM_GPUVA_INVALIDATED;
}

/**
 * drm_gpuva_invalidated() - indicates whether the backing BO of this &drm_gpuva
 * is invalidated
 * @va: the &drm_gpuva to check
 */
static inline bool drm_gpuva_invalidated(struct drm_gpuva *va)
{
	return va->flags & DRM_GPUVA_INVALIDATED;
}

/**
 * enum drm_gpuvm_flags - flags for struct drm_gpuvm
 */
enum drm_gpuvm_flags {
	/**
	 * @DRM_GPUVM_RESV_PROTECTED: GPUVM is protected externally by the
	 * GPUVM's &dma_resv lock
	 */
	DRM_GPUVM_RESV_PROTECTED = BIT(0),

	/**
	 * @DRM_GPUVM_USERBITS: user defined bits
	 */
	DRM_GPUVM_USERBITS = BIT(1),
};

/**
 * struct drm_gpuvm - DRM GPU VA Manager
 *
 * The DRM GPU VA Manager keeps track of a GPU's virtual address space by using
 * &maple_tree structures. Typically, this structure is embedded in bigger
 * driver structures.
 *
 * Drivers can pass addresses and ranges in an arbitrary unit, e.g. bytes or
 * pages.
 *
 * There should be one manager instance per GPU virtual address space.
 */
struct drm_gpuvm {
	/**
	 * @name: the name of the DRM GPU VA space
	 */
	const char *name;

	/**
	 * @flags: the &drm_gpuvm_flags of this GPUVM
	 */
	enum drm_gpuvm_flags flags;

	/**
	 * @drm: the &drm_device this VM lives in
	 */
	struct drm_device *drm;

	/**
	 * @mm_start: start of the VA space
	 */
	u64 mm_start;

	/**
	 * @mm_range: length of the VA space
	 */
	u64 mm_range;

	/**
	 * @rb: structures to track &drm_gpuva entries
	 */
	struct {
		/**
		 * @tree: the rb-tree to track GPU VA mappings
		 */
		struct rb_root_cached tree;

		/**
		 * @list: the &list_head to track GPU VA mappings
		 */
		struct list_head list;
	} rb;

	/**
	 * @kref: reference count of this object
	 */
	struct kref kref;

	/**
	 * @kernel_alloc_node:
	 *
	 * &drm_gpuva representing the address space cutout reserved for
	 * the kernel
	 */
	struct drm_gpuva kernel_alloc_node;

	/**
	 * @ops: &drm_gpuvm_ops providing the split/merge steps to drivers
	 */
	const struct drm_gpuvm_ops *ops;

	/**
	 * @r_obj: Resv GEM object; representing the GPUVM's common &dma_resv.
	 */
	struct drm_gem_object *r_obj;

	/**
	 * @extobj: structure holding the extobj list
	 */
	struct {
		/**
		 * @list: &list_head storing &drm_gpuvm_bos serving as
		 * external object
		 */
		struct list_head list;

		/**
		 * @local_list: pointer to the local list temporarily storing
		 * entries from the external object list
		 */
		struct list_head *local_list;

		/**
		 * @lock: spinlock to protect the extobj list
		 */
		spinlock_t lock;
	} extobj;

	/**
	 * @evict: structure holding the evict list and evict list lock
	 */
	struct {
		/**
		 * @list: &list_head storing &drm_gpuvm_bos currently being
		 * evicted
		 */
		struct list_head list;

		/**
		 * @local_list: pointer to the local list temporarily storing
		 * entries from the evicted object list
		 */
		struct list_head *local_list;

		/**
		 * @lock: spinlock to protect the evict list
		 */
		spinlock_t lock;
	} evict;
};

void drm_gpuvm_init(struct drm_gpuvm *gpuvm, const char *name,
		    enum drm_gpuvm_flags flags,
		    struct drm_device *drm,
		    struct drm_gem_object *r_obj,
		    u64 start_offset, u64 range,
		    u64 reserve_offset, u64 reserve_range,
		    const struct drm_gpuvm_ops *ops);

/**
 * drm_gpuvm_get() - acquire a struct drm_gpuvm reference
 * @gpuvm: the &drm_gpuvm to acquire the reference of
 *
 * This function acquires an additional reference to @gpuvm. It is illegal to
 * call this without already holding a reference. No locks required.
 */
static inline struct drm_gpuvm *
drm_gpuvm_get(struct drm_gpuvm *gpuvm)
{
	kref_get(&gpuvm->kref);

	return gpuvm;
}

void drm_gpuvm_put(struct drm_gpuvm *gpuvm);

bool drm_gpuvm_range_valid(struct drm_gpuvm *gpuvm, u64 addr, u64 range);
bool drm_gpuvm_interval_empty(struct drm_gpuvm *gpuvm, u64 addr, u64 range);

struct drm_gem_object *
drm_gpuvm_resv_object_alloc(struct drm_device *drm);

/**
 * drm_gpuvm_resv_protected() - indicates whether &DRM_GPUVM_RESV_PROTECTED is
 * set
 * @gpuvm: the &drm_gpuvm
 *
 * Returns: true if &DRM_GPUVM_RESV_PROTECTED is set, false otherwise.
 */
static inline bool
drm_gpuvm_resv_protected(struct drm_gpuvm *gpuvm)
{
	return gpuvm->flags & DRM_GPUVM_RESV_PROTECTED;
}

/**
 * drm_gpuvm_resv() - returns the &drm_gpuvm's &dma_resv
 * @gpuvm__: the &drm_gpuvm
 *
 * Returns: a pointer to the &drm_gpuvm's shared &dma_resv
 */
#define drm_gpuvm_resv(gpuvm__) ((gpuvm__)->r_obj->resv)

/**
 * drm_gpuvm_resv_obj() - returns the &drm_gem_object holding the &drm_gpuvm's
 * &dma_resv
 * @gpuvm__: the &drm_gpuvm
 *
 * Returns: a pointer to the &drm_gem_object holding the &drm_gpuvm's shared
 * &dma_resv
 */
#define drm_gpuvm_resv_obj(gpuvm__) ((gpuvm__)->r_obj)

#define drm_gpuvm_resv_held(gpuvm__) \
	dma_resv_held(drm_gpuvm_resv(gpuvm__))

#define drm_gpuvm_resv_assert_held(gpuvm__) \
	dma_resv_assert_held(drm_gpuvm_resv(gpuvm__))

#define drm_gpuvm_resv_held(gpuvm__) \
	dma_resv_held(drm_gpuvm_resv(gpuvm__))

#define drm_gpuvm_resv_assert_held(gpuvm__) \
	dma_resv_assert_held(drm_gpuvm_resv(gpuvm__))

/**
 * drm_gpuvm_is_extobj() - indicates whether the given &drm_gem_object is an
 * external object
 * @gpuvm: the &drm_gpuvm to check
 * @obj: the &drm_gem_object to check
 *
 * Returns: true if the &drm_gem_object &dma_resv differs from the
 * &drm_gpuvms &dma_resv, false otherwise
 */
static inline bool
drm_gpuvm_is_extobj(struct drm_gpuvm *gpuvm,
		    struct drm_gem_object *obj)
{
	return obj && obj->resv != drm_gpuvm_resv(gpuvm);
}

static inline struct drm_gpuva *
__drm_gpuva_next(struct drm_gpuva *va)
{
	if (va && !list_is_last(&va->rb.entry, &va->vm->rb.list))
		return list_next_entry(va, rb.entry);

	return NULL;
}

/**
 * drm_gpuvm_for_each_va_range() - iterate over a range of &drm_gpuvas
 * @va__: &drm_gpuva structure to assign to in each iteration step
 * @gpuvm__: &drm_gpuvm to walk over
 * @start__: starting offset, the first gpuva will overlap this
 * @end__: ending offset, the last gpuva will start before this (but may
 * overlap)
 *
 * This iterator walks over all &drm_gpuvas in the &drm_gpuvm that lie
 * between @start__ and @end__. It is implemented similarly to list_for_each(),
 * but is using the &drm_gpuvm's internal interval tree to accelerate
 * the search for the starting &drm_gpuva, and hence isn't safe against removal
 * of elements. It assumes that @end__ is within (or is the upper limit of) the
 * &drm_gpuvm. This iterator does not skip over the &drm_gpuvm's
 * @kernel_alloc_node.
 */
#define drm_gpuvm_for_each_va_range(va__, gpuvm__, start__, end__) \
	for (va__ = drm_gpuva_find_first((gpuvm__), (start__), (end__) - (start__)); \
	     va__ && (va__->va.addr < (end__)); \
	     va__ = __drm_gpuva_next(va__))

/**
 * drm_gpuvm_for_each_va_range_safe() - safely iterate over a range of
 * &drm_gpuvas
 * @va__: &drm_gpuva to assign to in each iteration step
 * @next__: another &drm_gpuva to use as temporary storage
 * @gpuvm__: &drm_gpuvm to walk over
 * @start__: starting offset, the first gpuva will overlap this
 * @end__: ending offset, the last gpuva will start before this (but may
 * overlap)
 *
 * This iterator walks over all &drm_gpuvas in the &drm_gpuvm that lie
 * between @start__ and @end__. It is implemented similarly to
 * list_for_each_safe(), but is using the &drm_gpuvm's internal interval
 * tree to accelerate the search for the starting &drm_gpuva, and hence is safe
 * against removal of elements. It assumes that @end__ is within (or is the
 * upper limit of) the &drm_gpuvm. This iterator does not skip over the
 * &drm_gpuvm's @kernel_alloc_node.
 */
#define drm_gpuvm_for_each_va_range_safe(va__, next__, gpuvm__, start__, end__) \
	for (va__ = drm_gpuva_find_first((gpuvm__), (start__), (end__) - (start__)), \
	     next__ = __drm_gpuva_next(va__); \
	     va__ && (va__->va.addr < (end__)); \
	     va__ = next__, next__ = __drm_gpuva_next(va__))

/**
 * drm_gpuvm_for_each_va() - iterate over all &drm_gpuvas
 * @va__: &drm_gpuva to assign to in each iteration step
 * @gpuvm__: &drm_gpuvm to walk over
 *
 * This iterator walks over all &drm_gpuva structures associated with the given
 * &drm_gpuvm.
 */
#define drm_gpuvm_for_each_va(va__, gpuvm__) \
	list_for_each_entry(va__, &(gpuvm__)->rb.list, rb.entry)

/**
 * drm_gpuvm_for_each_va_safe() - safely iterate over all &drm_gpuvas
 * @va__: &drm_gpuva to assign to in each iteration step
 * @next__: another &drm_gpuva to use as temporary storage
 * @gpuvm__: &drm_gpuvm to walk over
 *
 * This iterator walks over all &drm_gpuva structures associated with the given
 * &drm_gpuvm. It is implemented with list_for_each_entry_safe(), and
 * hence safe against the removal of elements.
 */
#define drm_gpuvm_for_each_va_safe(va__, next__, gpuvm__) \
	list_for_each_entry_safe(va__, next__, &(gpuvm__)->rb.list, rb.entry)

/**
 * struct drm_gpuvm_exec - &drm_gpuvm abstraction of &drm_exec
 *
 * This structure should be created on the stack as &drm_exec should be.
 *
 * Optionally, @extra can be set in order to lock additional &drm_gem_objects.
 */
struct drm_gpuvm_exec {
	/**
	 * @exec: the &drm_exec structure
	 */
	struct drm_exec exec;

	/**
	 * @flags: the flags for the struct drm_exec
	 */
	uint32_t flags;

	/**
	 * @vm: the &drm_gpuvm to lock its DMA reservations
	 */
	struct drm_gpuvm *vm;

	/**
	 * @num_fences: the number of fences to reserve for the &dma_resv of the
	 * locked &drm_gem_objects
	 */
	unsigned int num_fences;

	/**
	 * @extra: Callback and corresponding private data for the driver to
	 * lock arbitrary additional &drm_gem_objects.
	 */
	struct {
		/**
		 * @fn: The driver callback to lock additional &drm_gem_objects.
		 */
		int (*fn)(struct drm_gpuvm_exec *vm_exec);

		/**
		 * @priv: driver private data for the @fn callback
		 */
		void *priv;
	} extra;
};

int drm_gpuvm_prepare_vm(struct drm_gpuvm *gpuvm,
			 struct drm_exec *exec,
			 unsigned int num_fences);

int drm_gpuvm_prepare_objects(struct drm_gpuvm *gpuvm,
			      struct drm_exec *exec,
			      unsigned int num_fences);

int drm_gpuvm_prepare_range(struct drm_gpuvm *gpuvm,
			    struct drm_exec *exec,
			    u64 addr, u64 range,
			    unsigned int num_fences);

int drm_gpuvm_exec_lock(struct drm_gpuvm_exec *vm_exec);

int drm_gpuvm_exec_lock_array(struct drm_gpuvm_exec *vm_exec,
			      struct drm_gem_object **objs,
			      unsigned int num_objs);

int drm_gpuvm_exec_lock_range(struct drm_gpuvm_exec *vm_exec,
			      u64 addr, u64 range);

/**
 * drm_gpuvm_exec_unlock() - lock all dma-resv of all assoiciated BOs
 * @vm_exec: the &drm_gpuvm_exec wrapper
 *
 * Releases all dma-resv locks of all &drm_gem_objects previously acquired
 * through drm_gpuvm_exec_lock() or its variants.
 *
 * Returns: 0 on success, negative error code on failure.
 */
static inline void
drm_gpuvm_exec_unlock(struct drm_gpuvm_exec *vm_exec)
{
	drm_exec_fini(&vm_exec->exec);
}

int drm_gpuvm_validate(struct drm_gpuvm *gpuvm, struct drm_exec *exec);
void drm_gpuvm_resv_add_fence(struct drm_gpuvm *gpuvm,
			      struct drm_exec *exec,
			      struct dma_fence *fence,
			      enum dma_resv_usage private_usage,
			      enum dma_resv_usage extobj_usage);

/**
 * drm_gpuvm_exec_resv_add_fence()
 * @vm_exec: the &drm_gpuvm_exec wrapper
 * @fence: fence to add
 * @private_usage: private dma-resv usage
 * @extobj_usage: extobj dma-resv usage
 *
 * See drm_gpuvm_resv_add_fence().
 */
static inline void
drm_gpuvm_exec_resv_add_fence(struct drm_gpuvm_exec *vm_exec,
			      struct dma_fence *fence,
			      enum dma_resv_usage private_usage,
			      enum dma_resv_usage extobj_usage)
{
	drm_gpuvm_resv_add_fence(vm_exec->vm, &vm_exec->exec, fence,
				 private_usage, extobj_usage);
}

/**
 * drm_gpuvm_exec_validate()
 * @vm_exec: the &drm_gpuvm_exec wrapper
 *
 * See drm_gpuvm_validate().
 */
static inline int
drm_gpuvm_exec_validate(struct drm_gpuvm_exec *vm_exec)
{
	return drm_gpuvm_validate(vm_exec->vm, &vm_exec->exec);
}

/**
 * struct drm_gpuvm_bo - structure representing a &drm_gpuvm and
 * &drm_gem_object combination
 *
 * This structure is an abstraction representing a &drm_gpuvm and
 * &drm_gem_object combination. It serves as an indirection to accelerate
 * iterating all &drm_gpuvas within a &drm_gpuvm backed by the same
 * &drm_gem_object.
 *
 * Furthermore it is used cache evicted GEM objects for a certain GPU-VM to
 * accelerate validation.
 *
 * Typically, drivers want to create an instance of a struct drm_gpuvm_bo once
 * a GEM object is mapped first in a GPU-VM and release the instance once the
 * last mapping of the GEM object in this GPU-VM is unmapped.
 */
struct drm_gpuvm_bo {
	/**
	 * @vm: The &drm_gpuvm the @obj is mapped in. This is a reference
	 * counted pointer.
	 */
	struct drm_gpuvm *vm;

	/**
	 * @obj: The &drm_gem_object being mapped in @vm. This is a reference
	 * counted pointer.
	 */
	struct drm_gem_object *obj;

	/**
	 * @evicted: Indicates whether the &drm_gem_object is evicted; field
	 * protected by the &drm_gem_object's dma-resv lock.
	 */
	bool evicted;

	/**
	 * @kref: The reference count for this &drm_gpuvm_bo.
	 */
	struct kref kref;

	/**
	 * @list: Structure containing all &list_heads.
	 */
	struct {
		/**
		 * @gpuva: The list of linked &drm_gpuvas.
		 *
		 * It is safe to access entries from this list as long as the
		 * GEM's gpuva lock is held. See also struct drm_gem_object.
		 */
		struct list_head gpuva;

		/**
		 * @entry: Structure containing all &list_heads serving as
		 * entry.
		 */
		struct {
			/**
			 * @gem: List entry to attach to the &drm_gem_objects
			 * gpuva list.
			 */
			struct list_head gem;

			/**
			 * @evict: List entry to attach to the &drm_gpuvms
			 * extobj list.
			 */
			struct list_head extobj;

			/**
			 * @evict: List entry to attach to the &drm_gpuvms evict
			 * list.
			 */
			struct list_head evict;
		} entry;
	} list;
};

struct drm_gpuvm_bo *
drm_gpuvm_bo_create(struct drm_gpuvm *gpuvm,
		    struct drm_gem_object *obj);

struct drm_gpuvm_bo *
drm_gpuvm_bo_obtain(struct drm_gpuvm *gpuvm,
		    struct drm_gem_object *obj);
struct drm_gpuvm_bo *
drm_gpuvm_bo_obtain_prealloc(struct drm_gpuvm_bo *vm_bo);

/**
 * drm_gpuvm_bo_get() - acquire a struct drm_gpuvm_bo reference
 * @vm_bo: the &drm_gpuvm_bo to acquire the reference of
 *
 * This function acquires an additional reference to @vm_bo. It is illegal to
 * call this without already holding a reference. No locks required.
 */
static inline struct drm_gpuvm_bo *
drm_gpuvm_bo_get(struct drm_gpuvm_bo *vm_bo)
{
	kref_get(&vm_bo->kref);
	return vm_bo;
}

bool drm_gpuvm_bo_put(struct drm_gpuvm_bo *vm_bo);

struct drm_gpuvm_bo *
drm_gpuvm_bo_find(struct drm_gpuvm *gpuvm,
		  struct drm_gem_object *obj);

void drm_gpuvm_bo_evict(struct drm_gpuvm_bo *vm_bo, bool evict);

/**
 * drm_gpuvm_bo_gem_evict()
 * @obj: the &drm_gem_object
 * @evict: indicates whether @obj is evicted
 *
 * See drm_gpuvm_bo_evict().
 */
static inline void
drm_gpuvm_bo_gem_evict(struct drm_gem_object *obj, bool evict)
{
	struct drm_gpuvm_bo *vm_bo;

	drm_gem_gpuva_assert_lock_held(obj);
	drm_gem_for_each_gpuvm_bo(vm_bo, obj)
		drm_gpuvm_bo_evict(vm_bo, evict);
}

void drm_gpuvm_bo_extobj_add(struct drm_gpuvm_bo *vm_bo);

/**
 * drm_gpuvm_bo_for_each_va() - iterator to walk over a list of &drm_gpuva
 * @va__: &drm_gpuva structure to assign to in each iteration step
 * @vm_bo__: the &drm_gpuvm_bo the &drm_gpuva to walk are associated with
 *
 * This iterator walks over all &drm_gpuva structures associated with the
 * &drm_gpuvm_bo.
 *
 * The caller must hold the GEM's gpuva lock.
 */
#define drm_gpuvm_bo_for_each_va(va__, vm_bo__) \
	list_for_each_entry(va__, &(vm_bo)->list.gpuva, gem.entry)

/**
 * drm_gpuvm_bo_for_each_va_safe() - iterator to safely walk over a list of
 * &drm_gpuva
 * @va__: &drm_gpuva structure to assign to in each iteration step
 * @next__: &next &drm_gpuva to store the next step
 * @vm_bo__: the &drm_gpuvm_bo the &drm_gpuva to walk are associated with
 *
 * This iterator walks over all &drm_gpuva structures associated with the
 * &drm_gpuvm_bo. It is implemented with list_for_each_entry_safe(), hence
 * it is save against removal of elements.
 *
 * The caller must hold the GEM's gpuva lock.
 */
#define drm_gpuvm_bo_for_each_va_safe(va__, next__, vm_bo__) \
	list_for_each_entry_safe(va__, next__, &(vm_bo)->list.gpuva, gem.entry)

/**
 * enum drm_gpuva_op_type - GPU VA operation type
 *
 * Operations to alter the GPU VA mappings tracked by the &drm_gpuvm.
 */
enum drm_gpuva_op_type {
	/**
	 * @DRM_GPUVA_OP_MAP: the map op type
	 */
	DRM_GPUVA_OP_MAP,

	/**
	 * @DRM_GPUVA_OP_REMAP: the remap op type
	 */
	DRM_GPUVA_OP_REMAP,

	/**
	 * @DRM_GPUVA_OP_UNMAP: the unmap op type
	 */
	DRM_GPUVA_OP_UNMAP,

	/**
	 * @DRM_GPUVA_OP_PREFETCH: the prefetch op type
	 */
	DRM_GPUVA_OP_PREFETCH,
};

/**
 * struct drm_gpuva_op_map - GPU VA map operation
 *
 * This structure represents a single map operation generated by the
 * DRM GPU VA manager.
 */
struct drm_gpuva_op_map {
	/**
	 * @va: structure containing address and range of a map
	 * operation
	 */
	struct {
		/**
		 * @addr: the base address of the new mapping
		 */
		u64 addr;

		/**
		 * @range: the range of the new mapping
		 */
		u64 range;
	} va;

	/**
	 * @gem: structure containing the &drm_gem_object and it's offset
	 */
	struct {
		/**
		 * @offset: the offset within the &drm_gem_object
		 */
		u64 offset;

		/**
		 * @obj: the &drm_gem_object to map
		 */
		struct drm_gem_object *obj;
	} gem;
};

/**
 * struct drm_gpuva_op_unmap - GPU VA unmap operation
 *
 * This structure represents a single unmap operation generated by the
 * DRM GPU VA manager.
 */
struct drm_gpuva_op_unmap {
	/**
	 * @va: the &drm_gpuva to unmap
	 */
	struct drm_gpuva *va;

	/**
	 * @keep:
	 *
	 * Indicates whether this &drm_gpuva is physically contiguous with the
	 * original mapping request.
	 *
	 * Optionally, if &keep is set, drivers may keep the actual page table
	 * mappings for this &drm_gpuva, adding the missing page table entries
	 * only and update the &drm_gpuvm accordingly.
	 */
	bool keep;
};

/**
 * struct drm_gpuva_op_remap - GPU VA remap operation
 *
 * This represents a single remap operation generated by the DRM GPU VA manager.
 *
 * A remap operation is generated when an existing GPU VA mmapping is split up
 * by inserting a new GPU VA mapping or by partially unmapping existent
 * mapping(s), hence it consists of a maximum of two map and one unmap
 * operation.
 *
 * The @unmap operation takes care of removing the original existing mapping.
 * @prev is used to remap the preceding part, @next the subsequent part.
 *
 * If either a new mapping's start address is aligned with the start address
 * of the old mapping or the new mapping's end address is aligned with the
 * end address of the old mapping, either @prev or @next is NULL.
 *
 * Note, the reason for a dedicated remap operation, rather than arbitrary
 * unmap and map operations, is to give drivers the chance of extracting driver
 * specific data for creating the new mappings from the unmap operations's
 * &drm_gpuva structure which typically is embedded in larger driver specific
 * structures.
 */
struct drm_gpuva_op_remap {
	/**
	 * @prev: the preceding part of a split mapping
	 */
	struct drm_gpuva_op_map *prev;

	/**
	 * @next: the subsequent part of a split mapping
	 */
	struct drm_gpuva_op_map *next;

	/**
	 * @unmap: the unmap operation for the original existing mapping
	 */
	struct drm_gpuva_op_unmap *unmap;
};

/**
 * struct drm_gpuva_op_prefetch - GPU VA prefetch operation
 *
 * This structure represents a single prefetch operation generated by the
 * DRM GPU VA manager.
 */
struct drm_gpuva_op_prefetch {
	/**
	 * @va: the &drm_gpuva to prefetch
	 */
	struct drm_gpuva *va;
};

/**
 * struct drm_gpuva_op - GPU VA operation
 *
 * This structure represents a single generic operation.
 *
 * The particular type of the operation is defined by @op.
 */
struct drm_gpuva_op {
	/**
	 * @entry:
	 *
	 * The &list_head used to distribute instances of this struct within
	 * &drm_gpuva_ops.
	 */
	struct list_head entry;

	/**
	 * @op: the type of the operation
	 */
	enum drm_gpuva_op_type op;

	union {
		/**
		 * @map: the map operation
		 */
		struct drm_gpuva_op_map map;

		/**
		 * @remap: the remap operation
		 */
		struct drm_gpuva_op_remap remap;

		/**
		 * @unmap: the unmap operation
		 */
		struct drm_gpuva_op_unmap unmap;

		/**
		 * @prefetch: the prefetch operation
		 */
		struct drm_gpuva_op_prefetch prefetch;
	};
};

/**
 * struct drm_gpuva_ops - wraps a list of &drm_gpuva_op
 */
struct drm_gpuva_ops {
	/**
	 * @list: the &list_head
	 */
	struct list_head list;
};

/**
 * drm_gpuva_for_each_op() - iterator to walk over &drm_gpuva_ops
 * @op: &drm_gpuva_op to assign in each iteration step
 * @ops: &drm_gpuva_ops to walk
 *
 * This iterator walks over all ops within a given list of operations.
 */
#define drm_gpuva_for_each_op(op, ops) list_for_each_entry(op, &(ops)->list, entry)

/**
 * drm_gpuva_for_each_op_safe() - iterator to safely walk over &drm_gpuva_ops
 * @op: &drm_gpuva_op to assign in each iteration step
 * @next: &next &drm_gpuva_op to store the next step
 * @ops: &drm_gpuva_ops to walk
 *
 * This iterator walks over all ops within a given list of operations. It is
 * implemented with list_for_each_safe(), so save against removal of elements.
 */
#define drm_gpuva_for_each_op_safe(op, next, ops) \
	list_for_each_entry_safe(op, next, &(ops)->list, entry)

/**
 * drm_gpuva_for_each_op_from_reverse() - iterate backwards from the given point
 * @op: &drm_gpuva_op to assign in each iteration step
 * @ops: &drm_gpuva_ops to walk
 *
 * This iterator walks over all ops within a given list of operations beginning
 * from the given operation in reverse order.
 */
#define drm_gpuva_for_each_op_from_reverse(op, ops) \
	list_for_each_entry_from_reverse(op, &(ops)->list, entry)

/**
 * drm_gpuva_for_each_op_reverse - iterator to walk over &drm_gpuva_ops in reverse
 * @op: &drm_gpuva_op to assign in each iteration step
 * @ops: &drm_gpuva_ops to walk
 *
 * This iterator walks over all ops within a given list of operations in reverse
 */
#define drm_gpuva_for_each_op_reverse(op, ops) \
	list_for_each_entry_reverse(op, &(ops)->list, entry)

/**
 * drm_gpuva_first_op() - returns the first &drm_gpuva_op from &drm_gpuva_ops
 * @ops: the &drm_gpuva_ops to get the fist &drm_gpuva_op from
 */
#define drm_gpuva_first_op(ops) \
	list_first_entry(&(ops)->list, struct drm_gpuva_op, entry)

/**
 * drm_gpuva_last_op() - returns the last &drm_gpuva_op from &drm_gpuva_ops
 * @ops: the &drm_gpuva_ops to get the last &drm_gpuva_op from
 */
#define drm_gpuva_last_op(ops) \
	list_last_entry(&(ops)->list, struct drm_gpuva_op, entry)

/**
 * drm_gpuva_prev_op() - previous &drm_gpuva_op in the list
 * @op: the current &drm_gpuva_op
 */
#define drm_gpuva_prev_op(op) list_prev_entry(op, entry)

/**
 * drm_gpuva_next_op() - next &drm_gpuva_op in the list
 * @op: the current &drm_gpuva_op
 */
#define drm_gpuva_next_op(op) list_next_entry(op, entry)

struct drm_gpuva_ops *
drm_gpuvm_sm_map_ops_create(struct drm_gpuvm *gpuvm,
			    u64 addr, u64 range,
			    struct drm_gem_object *obj, u64 offset);
struct drm_gpuva_ops *
drm_gpuvm_sm_unmap_ops_create(struct drm_gpuvm *gpuvm,
			      u64 addr, u64 range);

struct drm_gpuva_ops *
drm_gpuvm_prefetch_ops_create(struct drm_gpuvm *gpuvm,
				 u64 addr, u64 range);

struct drm_gpuva_ops *
drm_gpuvm_bo_unmap_ops_create(struct drm_gpuvm_bo *vm_bo);

void drm_gpuva_ops_free(struct drm_gpuvm *gpuvm,
			struct drm_gpuva_ops *ops);

static inline void drm_gpuva_init_from_op(struct drm_gpuva *va,
					  struct drm_gpuva_op_map *op)
{
	drm_gpuva_init(va, op->va.addr, op->va.range,
		       op->gem.obj, op->gem.offset);
}

/**
 * struct drm_gpuvm_ops - callbacks for split/merge steps
 *
 * This structure defines the callbacks used by &drm_gpuvm_sm_map and
 * &drm_gpuvm_sm_unmap to provide the split/merge steps for map and unmap
 * operations to drivers.
 */
struct drm_gpuvm_ops {
	/**
	 * @vm_free: called when the last reference of a struct drm_gpuvm is
	 * dropped
	 *
	 * This callback is mandatory.
	 */
	void (*vm_free)(struct drm_gpuvm *gpuvm);

	/**
	 * @op_alloc: called when the &drm_gpuvm allocates
	 * a struct drm_gpuva_op
	 *
	 * Some drivers may want to embed struct drm_gpuva_op into driver
	 * specific structures. By implementing this callback drivers can
	 * allocate memory accordingly.
	 *
	 * This callback is optional.
	 */
	struct drm_gpuva_op *(*op_alloc)(void);

	/**
	 * @op_free: called when the &drm_gpuvm frees a
	 * struct drm_gpuva_op
	 *
	 * Some drivers may want to embed struct drm_gpuva_op into driver
	 * specific structures. By implementing this callback drivers can
	 * free the previously allocated memory accordingly.
	 *
	 * This callback is optional.
	 */
	void (*op_free)(struct drm_gpuva_op *op);

	/**
	 * @vm_bo_alloc: called when the &drm_gpuvm allocates
	 * a struct drm_gpuvm_bo
	 *
	 * Some drivers may want to embed struct drm_gpuvm_bo into driver
	 * specific structures. By implementing this callback drivers can
	 * allocate memory accordingly.
	 *
	 * This callback is optional.
	 */
	struct drm_gpuvm_bo *(*vm_bo_alloc)(void);

	/**
	 * @vm_bo_free: called when the &drm_gpuvm frees a
	 * struct drm_gpuvm_bo
	 *
	 * Some drivers may want to embed struct drm_gpuvm_bo into driver
	 * specific structures. By implementing this callback drivers can
	 * free the previously allocated memory accordingly.
	 *
	 * This callback is optional.
	 */
	void (*vm_bo_free)(struct drm_gpuvm_bo *vm_bo);

	/**
	 * @vm_bo_validate: called from drm_gpuvm_validate()
	 *
	 * Drivers receive this callback for every evicted &drm_gem_object being
	 * mapped in the corresponding &drm_gpuvm.
	 *
	 * Typically, drivers would call their driver specific variant of
	 * ttm_bo_validate() from within this callback.
	 */
	int (*vm_bo_validate)(struct drm_gpuvm_bo *vm_bo,
			      struct drm_exec *exec);

	/**
	 * @sm_step_map: called from &drm_gpuvm_sm_map to finally insert the
	 * mapping once all previous steps were completed
	 *
	 * The &priv pointer matches the one the driver passed to
	 * &drm_gpuvm_sm_map or &drm_gpuvm_sm_unmap, respectively.
	 *
	 * Can be NULL if &drm_gpuvm_sm_map is used.
	 */
	int (*sm_step_map)(struct drm_gpuva_op *op, void *priv);

	/**
	 * @sm_step_remap: called from &drm_gpuvm_sm_map and
	 * &drm_gpuvm_sm_unmap to split up an existent mapping
	 *
	 * This callback is called when existent mapping needs to be split up.
	 * This is the case when either a newly requested mapping overlaps or
	 * is enclosed by an existent mapping or a partial unmap of an existent
	 * mapping is requested.
	 *
	 * The &priv pointer matches the one the driver passed to
	 * &drm_gpuvm_sm_map or &drm_gpuvm_sm_unmap, respectively.
	 *
	 * Can be NULL if neither &drm_gpuvm_sm_map nor &drm_gpuvm_sm_unmap is
	 * used.
	 */
	int (*sm_step_remap)(struct drm_gpuva_op *op, void *priv);

	/**
	 * @sm_step_unmap: called from &drm_gpuvm_sm_map and
	 * &drm_gpuvm_sm_unmap to unmap an existent mapping
	 *
	 * This callback is called when existent mapping needs to be unmapped.
	 * This is the case when either a newly requested mapping encloses an
	 * existent mapping or an unmap of an existent mapping is requested.
	 *
	 * The &priv pointer matches the one the driver passed to
	 * &drm_gpuvm_sm_map or &drm_gpuvm_sm_unmap, respectively.
	 *
	 * Can be NULL if neither &drm_gpuvm_sm_map nor &drm_gpuvm_sm_unmap is
	 * used.
	 */
	int (*sm_step_unmap)(struct drm_gpuva_op *op, void *priv);
};

int drm_gpuvm_sm_map(struct drm_gpuvm *gpuvm, void *priv,
		     u64 addr, u64 range,
		     struct drm_gem_object *obj, u64 offset);

int drm_gpuvm_sm_unmap(struct drm_gpuvm *gpuvm, void *priv,
		       u64 addr, u64 range);

void drm_gpuva_map(struct drm_gpuvm *gpuvm,
		   struct drm_gpuva *va,
		   struct drm_gpuva_op_map *op);

void drm_gpuva_remap(struct drm_gpuva *prev,
		     struct drm_gpuva *next,
		     struct drm_gpuva_op_remap *op);

void drm_gpuva_unmap(struct drm_gpuva_op_unmap *op);

/**
 * drm_gpuva_op_remap_to_unmap_range() - Helper to get the start and range of
 * the unmap stage of a remap op.
 * @op: Remap op.
 * @start_addr: Output pointer for the start of the required unmap.
 * @range: Output pointer for the length of the required unmap.
 *
 * The given start address and range will be set such that they represent the
 * range of the address space that was previously covered by the mapping being
 * re-mapped, but is now empty.
 */
static inline void
drm_gpuva_op_remap_to_unmap_range(const struct drm_gpuva_op_remap *op,
				  u64 *start_addr, u64 *range)
{
	const u64 va_start = op->prev ?
			     op->prev->va.addr + op->prev->va.range :
			     op->unmap->va->va.addr;
	const u64 va_end = op->next ?
			   op->next->va.addr :
			   op->unmap->va->va.addr + op->unmap->va->va.range;

	if (start_addr)
		*start_addr = va_start;
	if (range)
		*range = va_end - va_start;
}

#endif /* __DRM_GPUVM_H__ */
