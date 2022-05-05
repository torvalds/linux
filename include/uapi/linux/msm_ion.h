/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */
#ifndef _UAPI_LINUX_MSM_ION_H
#define _UAPI_LINUX_MSM_ION_H

#include <linux/types.h>
#include <linux/msm_ion_ids.h>

/**
 * TARGET_ION_ABI_VERSION can be used by user space clients to ensure that at
 * compile time only their code which uses the appropriate ION APIs for
 * this kernel is included.
 */
#define TARGET_ION_ABI_VERSION 3

enum msm_ion_heap_types {
	ION_HEAP_TYPE_MSM_START = 16,
	ION_HEAP_TYPE_SECURE_DMA = ION_HEAP_TYPE_MSM_START,
	ION_HEAP_TYPE_SYSTEM_SECURE,
	ION_HEAP_TYPE_HYP_CMA,
	ION_HEAP_TYPE_MSM_CARVEOUT,
	ION_HEAP_TYPE_SECURE_CARVEOUT,
	ION_HEAP_TYPE_MSM_SYSTEM,
};

/**
 * Flags to be used when allocating from the secure heap for
 * content protection
 */
#define ION_FLAG_CP_TRUSTED_VM		ION_BIT(15)
/* ION_FLAG_POOL_FORCE_ALLOC uses ION_BIT(16) */
#define ION_FLAG_CP_TOUCH		ION_BIT(17)
#define ION_FLAG_CP_BITSTREAM		ION_BIT(18)
#define ION_FLAG_CP_PIXEL		ION_BIT(19)
#define ION_FLAG_CP_NON_PIXEL		ION_BIT(20)
#define ION_FLAG_CP_CAMERA		ION_BIT(21)
#define ION_FLAG_CP_HLOS		ION_BIT(22)
#define ION_FLAG_CP_SPSS_SP		ION_BIT(23)
#define ION_FLAG_CP_SPSS_SP_SHARED	ION_BIT(24)
#define ION_FLAG_CP_SEC_DISPLAY		ION_BIT(25)
#define ION_FLAG_CP_APP			ION_BIT(26)
#define ION_FLAG_CP_CAMERA_PREVIEW	ION_BIT(27)
/* ION_FLAG_ALLOW_NON_CONTIG uses ION_BIT(28) */
#define ION_FLAG_CP_CDSP		ION_BIT(29)
#define ION_FLAG_CP_SPSS_HLOS_SHARED	ION_BIT(30)

#define ION_FLAGS_CP_MASK	0x6FFE8000

/**
 * Flag to allow non continguous allocation of memory from secure
 * heap
 */
#define ION_FLAG_ALLOW_NON_CONTIG       ION_BIT(28)

/**
 * Flag to use when allocating to indicate that a heap is secure.
 * Do NOT use BIT macro since it is defined in #ifdef __KERNEL__
 */
#define ION_FLAG_SECURE			ION_BIT(31)

/*
 * Used in conjunction with heap which pool memory to force an allocation
 * to come from the page allocator directly instead of from the pool allocation
 */
#define ION_FLAG_POOL_FORCE_ALLOC	ION_BIT(16)

/**
 * Macro should be used with ion_heap_ids defined above.
 */
#define ION_HEAP(bit)			bit

#define ION_IOC_MSM_MAGIC 'M'

struct ion_prefetch_regions {
	__u64 sizes;
	__u32 vmid;
	__u32 nr_sizes;
};

struct ion_prefetch_data {
	__u64 len;
	__u64 regions;
	__u32 heap_id;
	__u32 nr_regions;
};

#define ION_IOC_PREFETCH		_IOWR(ION_IOC_MSM_MAGIC, 3, \
						struct ion_prefetch_data)

#define ION_IOC_DRAIN			_IOWR(ION_IOC_MSM_MAGIC, 4, \
						struct ion_prefetch_data)

#endif /* _UAPI_LINUX_MSM_ION_H */
