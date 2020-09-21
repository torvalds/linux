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
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#ifndef _TTM_PLACEMENT_H_
#define _TTM_PLACEMENT_H_

#include <linux/types.h>

/*
 * Memory regions for data placement.
 */

#define TTM_PL_SYSTEM           0
#define TTM_PL_TT               1
#define TTM_PL_VRAM             2
#define TTM_PL_PRIV             3

/*
 * Other flags that affects data placement.
 * TTM_PL_FLAG_CACHED indicates cache-coherent mappings
 * if available.
 * TTM_PL_FLAG_SHARED means that another application may
 * reference the buffer.
 * TTM_PL_FLAG_NO_EVICT means that the buffer may never
 * be evicted to make room for other buffers.
 * TTM_PL_FLAG_TOPDOWN requests to be placed from the
 * top of the memory area, instead of the bottom.
 */

#define TTM_PL_FLAG_CACHED      (1 << 16)
#define TTM_PL_FLAG_UNCACHED    (1 << 17)
#define TTM_PL_FLAG_WC          (1 << 18)
#define TTM_PL_FLAG_CONTIGUOUS  (1 << 19)
#define TTM_PL_FLAG_TOPDOWN     (1 << 22)

#define TTM_PL_MASK_CACHING     (TTM_PL_FLAG_CACHED | \
				 TTM_PL_FLAG_UNCACHED | \
				 TTM_PL_FLAG_WC)

/**
 * struct ttm_place
 *
 * @fpfn:	first valid page frame number to put the object
 * @lpfn:	last valid page frame number to put the object
 * @flags:	memory domain and caching flags for the object
 *
 * Structure indicating a possible place to put an object.
 */
struct ttm_place {
	unsigned	fpfn;
	unsigned	lpfn;
	uint32_t	mem_type;
	uint32_t	flags;
};

/**
 * struct ttm_placement
 *
 * @num_placement:	number of preferred placements
 * @placement:		preferred placements
 * @num_busy_placement:	number of preferred placements when need to evict buffer
 * @busy_placement:	preferred placements when need to evict buffer
 *
 * Structure indicating the placement you request for an object.
 */
struct ttm_placement {
	unsigned		num_placement;
	const struct ttm_place	*placement;
	unsigned		num_busy_placement;
	const struct ttm_place	*busy_placement;
};

#endif
