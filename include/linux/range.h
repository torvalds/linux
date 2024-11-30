/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RANGE_H
#define _LINUX_RANGE_H
#include <linux/types.h>

struct range {
	u64   start;
	u64   end;
};

static inline u64 range_len(const struct range *range)
{
	return range->end - range->start + 1;
}

/* True if r1 completely contains r2 */
static inline bool range_contains(const struct range *r1,
				  const struct range *r2)
{
	return r1->start <= r2->start && r1->end >= r2->end;
}

/* True if any part of r1 overlaps r2 */
static inline bool range_overlaps(const struct range *r1,
				  const struct range *r2)
{
	return r1->start <= r2->end && r1->end >= r2->start;
}

int add_range(struct range *range, int az, int nr_range,
		u64 start, u64 end);


int add_range_with_merge(struct range *range, int az, int nr_range,
				u64 start, u64 end);

void subtract_range(struct range *range, int az, u64 start, u64 end);

int clean_sort_range(struct range *range, int az);

void sort_range(struct range *range, int nr_range);

#define DEFINE_RANGE(_start, _end)		\
(struct range) {				\
		.start = (_start),		\
		.end = (_end),			\
	}

#endif
