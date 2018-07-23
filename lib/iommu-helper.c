// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU helper functions for the free area management
 */

#include <linux/bitmap.h>
#include <linux/iommu-helper.h>

unsigned long iommu_area_alloc(unsigned long *map, unsigned long size,
			       unsigned long start, unsigned int nr,
			       unsigned long shift, unsigned long boundary_size,
			       unsigned long align_mask)
{
	unsigned long index;

	/* We don't want the last of the limit */
	size -= 1;
again:
	index = bitmap_find_next_zero_area(map, size, start, nr, align_mask);
	if (index < size) {
		if (iommu_is_span_boundary(index, nr, shift, boundary_size)) {
			start = ALIGN(shift + index, boundary_size) - shift;
			goto again;
		}
		bitmap_set(map, index, nr);
		return index;
	}
	return -1;
}
