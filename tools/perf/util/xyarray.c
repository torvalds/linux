// SPDX-License-Identifier: GPL-2.0
#include "xyarray.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

struct xyarray *xyarray__new(int xlen, int ylen, size_t entry_size)
{
	size_t row_size = ylen * entry_size;
	struct xyarray *xy = zalloc(sizeof(*xy) + xlen * row_size);

	if (xy != NULL) {
		xy->entry_size = entry_size;
		xy->row_size   = row_size;
		xy->entries    = xlen * ylen;
		xy->max_x      = xlen;
		xy->max_y      = ylen;
	}

	return xy;
}

void xyarray__reset(struct xyarray *xy)
{
	size_t n = xy->entries * xy->entry_size;

	memset(xy->contents, 0, n);
}

void xyarray__delete(struct xyarray *xy)
{
	free(xy);
}
