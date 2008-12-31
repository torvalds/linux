/*
 * IOMMU helper functions for the free area management
 */

#include <linux/module.h>
#include <linux/bitops.h>

static unsigned long find_next_zero_area(unsigned long *map,
					 unsigned long size,
					 unsigned long start,
					 unsigned int nr,
					 unsigned long align_mask)
{
	unsigned long index, end, i;
again:
	index = find_next_zero_bit(map, size, start);

	/* Align allocation */
	index = (index + align_mask) & ~align_mask;

	end = index + nr;
	if (end >= size)
		return -1;
	for (i = index; i < end; i++) {
		if (test_bit(i, map)) {
			start = i+1;
			goto again;
		}
	}
	return index;
}

void iommu_area_reserve(unsigned long *map, unsigned long i, int len)
{
	unsigned long end = i + len;
	while (i < end) {
		__set_bit(i, map);
		i++;
	}
}

int iommu_is_span_boundary(unsigned int index, unsigned int nr,
			   unsigned long shift,
			   unsigned long boundary_size)
{
	BUG_ON(!is_power_of_2(boundary_size));

	shift = (shift + index) & (boundary_size - 1);
	return shift + nr > boundary_size;
}

unsigned long iommu_area_alloc(unsigned long *map, unsigned long size,
			       unsigned long start, unsigned int nr,
			       unsigned long shift, unsigned long boundary_size,
			       unsigned long align_mask)
{
	unsigned long index;
again:
	index = find_next_zero_area(map, size, start, nr, align_mask);
	if (index != -1) {
		if (iommu_is_span_boundary(index, nr, shift, boundary_size)) {
			/* we could do more effectively */
			start = index + 1;
			goto again;
		}
		iommu_area_reserve(map, index, nr);
	}
	return index;
}
EXPORT_SYMBOL(iommu_area_alloc);

void iommu_area_free(unsigned long *map, unsigned long start, unsigned int nr)
{
	unsigned long end = start + nr;

	while (start < end) {
		__clear_bit(start, map);
		start++;
	}
}
EXPORT_SYMBOL(iommu_area_free);

unsigned long iommu_num_pages(unsigned long addr, unsigned long len,
			      unsigned long io_page_size)
{
	unsigned long size = (addr & (io_page_size - 1)) + len;

	return DIV_ROUND_UP(size, io_page_size);
}
EXPORT_SYMBOL(iommu_num_pages);
