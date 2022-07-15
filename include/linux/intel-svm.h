/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2015 Intel Corporation.
 *
 * Authors: David Woodhouse <David.Woodhouse@intel.com>
 */

#ifndef __INTEL_SVM_H__
#define __INTEL_SVM_H__

/* Page Request Queue depth */
#define PRQ_ORDER	4
#define PRQ_RING_MASK	((0x1000 << PRQ_ORDER) - 0x20)
#define PRQ_DEPTH	((0x1000 << PRQ_ORDER) >> 5)

/*
 * The SVM_FLAG_SUPERVISOR_MODE flag requests a PASID which can be used only
 * for access to kernel addresses. No IOTLB flushes are automatically done
 * for kernel mappings; it is valid only for access to the kernel's static
 * 1:1 mapping of physical memory — not to vmalloc or even module mappings.
 * A future API addition may permit the use of such ranges, by means of an
 * explicit IOTLB flush call (akin to the DMA API's unmap method).
 *
 * It is unlikely that we will ever hook into flush_tlb_kernel_range() to
 * do such IOTLB flushes automatically.
 */
#define SVM_FLAG_SUPERVISOR_MODE	BIT(0)

#endif /* __INTEL_SVM_H__ */
