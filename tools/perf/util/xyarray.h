#ifndef _PERF_XYARRAY_H_
#define _PERF_XYARRAY_H_ 1

#include <sys/types.h>

struct xyarray {
	size_t row_size;
	size_t entry_size;
	size_t entries;
	size_t max_x;
	size_t max_y;
	char contents[];
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

#endif /* _PERF_XYARRAY_H_ */
