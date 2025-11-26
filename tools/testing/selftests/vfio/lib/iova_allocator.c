// SPDX-License-Identifier: GPL-2.0-only
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <uapi/linux/types.h>
#include <linux/iommufd.h>
#include <linux/limits.h>
#include <linux/mman.h>
#include <linux/overflow.h>
#include <linux/types.h>
#include <linux/vfio.h>

#include <libvfio.h>

struct iova_allocator *iova_allocator_init(struct iommu *iommu)
{
	struct iova_allocator *allocator;
	struct iommu_iova_range *ranges;
	u32 nranges;

	ranges = iommu_iova_ranges(iommu, &nranges);
	VFIO_ASSERT_NOT_NULL(ranges);

	allocator = malloc(sizeof(*allocator));
	VFIO_ASSERT_NOT_NULL(allocator);

	*allocator = (struct iova_allocator){
		.ranges = ranges,
		.nranges = nranges,
		.range_idx = 0,
		.range_offset = 0,
	};

	return allocator;
}

void iova_allocator_cleanup(struct iova_allocator *allocator)
{
	free(allocator->ranges);
	free(allocator);
}

iova_t iova_allocator_alloc(struct iova_allocator *allocator, size_t size)
{
	VFIO_ASSERT_GT(size, 0, "Invalid size arg, zero\n");
	VFIO_ASSERT_EQ(size & (size - 1), 0, "Invalid size arg, non-power-of-2\n");

	for (;;) {
		struct iommu_iova_range *range;
		iova_t iova, last;

		VFIO_ASSERT_LT(allocator->range_idx, allocator->nranges,
			       "IOVA allocator out of space\n");

		range = &allocator->ranges[allocator->range_idx];
		iova = range->start + allocator->range_offset;

		/* Check for sufficient space at the current offset */
		if (check_add_overflow(iova, size - 1, &last) ||
		    last > range->last)
			goto next_range;

		/* Align iova to size */
		iova = last & ~(size - 1);

		/* Check for sufficient space at the aligned iova */
		if (check_add_overflow(iova, size - 1, &last) ||
		    last > range->last)
			goto next_range;

		if (last == range->last) {
			allocator->range_idx++;
			allocator->range_offset = 0;
		} else {
			allocator->range_offset = last - range->start + 1;
		}

		return iova;

next_range:
		allocator->range_idx++;
		allocator->range_offset = 0;
	}
}

