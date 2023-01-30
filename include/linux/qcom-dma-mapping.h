/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 & Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

/*
 * DMA_ATTR_NO_DELAYED_UNMAP: Used by msm specific lazy mapping to indicate
 * that the mapping can be freed on unmap, rather than when the ion_buffer
 * is freed.
 */
#define DMA_ATTR_NO_DELAYED_UNMAP	(1UL << 13)
/*
 * When passed to a DMA map call the DMA_ATTR_FORCE_COHERENT DMA
 * attribute can be used to force a buffer to be mapped as IO coherent.
 */
#define DMA_ATTR_FORCE_COHERENT			(1UL << 15)
/*
 * When passed to a DMA map call the DMA_ATTR_FORCE_NON_COHERENT DMA
 * attribute can be used to force a buffer to not be mapped as IO
 * coherent.
 */
#define DMA_ATTR_FORCE_NON_COHERENT		(1UL << 16)
/*
 * DMA_ATTR_DELAYED_UNMAP: Used by ION, it will ensure that mappings are not
 * removed on unmap but instead are removed when the ion_buffer is freed.
 */
#define DMA_ATTR_DELAYED_UNMAP		(1UL << 17)
/*
 * DMA_ATTR_QTI_SMMU_PROXY_MAP : Map this buffer in the TVM SMMU if supported
 * on the target.
 */
#define DMA_ATTR_QTI_SMMU_PROXY_MAP	(1UL << 18)

#ifndef DMA_ATTR_SYS_CACHE_ONLY
/* Attributes are not supported, so render them ineffective. */
#define DMA_ATTR_SYS_CACHE_ONLY		(0UL)
#define DMA_ATTR_SYS_CACHE_ONLY_NWA	(0UL)
#endif

/*
 * DMA_ATTR_IOMMU_USE_UPSTREAM_HINT: Normally an smmu will override any bus
 * attributes (i.e cacheablilty) provided by the client device. Some hardware
 * may be designed to use the original attributes instead.
 */
#define DMA_ATTR_IOMMU_USE_UPSTREAM_HINT	(DMA_ATTR_SYS_CACHE_ONLY)

/*
 * DMA_ATTR_IOMMU_USE_LLC_NWA: Overrides the bus attributes to use the System
 * Cache(LLC) with allocation policy as Inner Non-Cacheable, Outer Cacheable:
 * Write-Back, Read-Allocate, No Write-Allocate policy.
 */
#define DMA_ATTR_IOMMU_USE_LLC_NWA	(DMA_ATTR_SYS_CACHE_ONLY_NWA)
