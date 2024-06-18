/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef __TTM_KMAP_ITER_H__
#define __TTM_KMAP_ITER_H__

#include <linux/types.h>

struct ttm_kmap_iter;
struct iosys_map;

/**
 * struct ttm_kmap_iter_ops - Ops structure for a struct
 * ttm_kmap_iter.
 * @maps_tt: Whether the iterator maps TT memory directly, as opposed
 * mapping a TT through an aperture. Both these modes have
 * struct ttm_resource_manager::use_tt set, but the latter typically
 * returns is_iomem == true from ttm_mem_io_reserve.
 */
struct ttm_kmap_iter_ops {
	/**
	 * @map_local: Map a PAGE_SIZE part of the resource using
	 * kmap_local semantics.
	 * @res_iter: Pointer to the struct ttm_kmap_iter representing
	 * the resource.
	 * @dmap: The struct iosys_map holding the virtual address after
	 * the operation.
	 * @i: The location within the resource to map. PAGE_SIZE granularity.
	 */
	void (*map_local)(struct ttm_kmap_iter *res_iter,
			  struct iosys_map *dmap, pgoff_t i);
	/**
	 * @unmap_local: Unmap a PAGE_SIZE part of the resource previously
	 * mapped using kmap_local.
	 * @res_iter: Pointer to the struct ttm_kmap_iter representing
	 * the resource.
	 * @dmap: The struct iosys_map holding the virtual address after
	 * the operation.
	 */
	void (*unmap_local)(struct ttm_kmap_iter *res_iter,
			    struct iosys_map *dmap);
	bool maps_tt;
};

/**
 * struct ttm_kmap_iter - Iterator for kmap_local type operations on a
 * resource.
 * @ops: Pointer to the operations struct.
 *
 * This struct is intended to be embedded in a resource-specific specialization
 * implementing operations for the resource.
 *
 * Nothing stops us from extending the operations to vmap, vmap_pfn etc,
 * replacing some or parts of the ttm_bo_util. cpu-map functionality.
 */
struct ttm_kmap_iter {
	const struct ttm_kmap_iter_ops *ops;
};

#endif /* __TTM_KMAP_ITER_H__ */
