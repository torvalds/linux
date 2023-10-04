/* SPDX-License-Identifier: GPL-2.0-only */

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

#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/types.h>

#include <drm/drm_gem.h>

struct drm_gpuvm;
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
		 * @entry: the &list_head to attach this object to a &drm_gem_object
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

void drm_gpuva_link(struct drm_gpuva *va);
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
};

void drm_gpuvm_init(struct drm_gpuvm *gpuvm, const char *name,
		    u64 start_offset, u64 range,
		    u64 reserve_offset, u64 reserve_range,
		    const struct drm_gpuvm_ops *ops);
void drm_gpuvm_destroy(struct drm_gpuvm *gpuvm);

bool drm_gpuvm_interval_empty(struct drm_gpuvm *gpuvm, u64 addr, u64 range);

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
drm_gpuvm_gem_unmap_ops_create(struct drm_gpuvm *gpuvm,
			       struct drm_gem_object *obj);

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

#endif /* __DRM_GPUVM_H__ */
