/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright (c) 2025 Valve Corporation */

#ifndef _TTM_ALLOCATION_H_
#define _TTM_ALLOCATION_H_

#define TTM_ALLOCATION_POOL_BENEFICIAL_ORDER(n)	((n) & 0xff) /* Max order which caller can benefit from */
#define TTM_ALLOCATION_POOL_USE_DMA_ALLOC 	BIT(8) /* Use coherent DMA allocations. */
#define TTM_ALLOCATION_POOL_USE_DMA32		BIT(9) /* Use GFP_DMA32 allocations. */
#define TTM_ALLOCATION_PROPAGATE_ENOSPC		BIT(10) /* Do not convert ENOSPC from resource managers to ENOMEM. */

#endif
