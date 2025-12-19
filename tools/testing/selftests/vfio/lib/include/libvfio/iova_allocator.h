/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_IOVA_ALLOCATOR_H
#define SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_IOVA_ALLOCATOR_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/iommufd.h>

#include <libvfio/iommu.h>

struct iova_allocator {
	struct iommu_iova_range *ranges;
	u32 nranges;
	u32 range_idx;
	u64 range_offset;
};

struct iova_allocator *iova_allocator_init(struct iommu *iommu);
void iova_allocator_cleanup(struct iova_allocator *allocator);
iova_t iova_allocator_alloc(struct iova_allocator *allocator, size_t size);

#endif /* SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_IOVA_ALLOCATOR_H */
