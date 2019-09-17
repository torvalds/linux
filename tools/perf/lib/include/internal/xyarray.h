/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_XYARRAY_H
#define __LIBPERF_INTERNAL_XYARRAY_H

#include <linux/compiler.h>
#include <sys/types.h>

struct xyarray {
	size_t row_size;
	size_t entry_size;
	size_t entries;
	size_t max_x;
	size_t max_y;
	char contents[] __aligned(8);
};

struct xyarray *xyarray__new(int xlen, int ylen, size_t entry_size);
void xyarray__delete(struct xyarray *xy);
void xyarray__reset(struct xyarray *xy);

static inline void *xyarray__entry(struct xyarray *xy, int x, int y)
{
	return &xy->contents[x * xy->row_size + y * xy->entry_size];
}

static inline int xyarray__max_y(struct xyarray *xy)
{
	return xy->max_y;
}

static inline int xyarray__max_x(struct xyarray *xy)
{
	return xy->max_x;
}

#endif /* __LIBPERF_INTERNAL_XYARRAY_H */
