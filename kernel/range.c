/*
 * Range add and subtract
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/range.h>

int add_range(struct range *range, int az, int nr_range, u64 start, u64 end)
{
	if (start >= end)
		return nr_range;

	/* Out of slots: */
	if (nr_range >= az)
		return nr_range;

	range[nr_range].start = start;
	range[nr_range].end = end;

	nr_range++;

	return nr_range;
}

int add_range_with_merge(struct range *range, int az, int nr_range,
		     u64 start, u64 end)
{
	int i;

	if (start >= end)
		return nr_range;

	/* get new start/end: */
	for (i = 0; i < nr_range; i++) {
		u64 common_start, common_end;

		if (!range[i].end)
			continue;

		common_start = max(range[i].start, start);
		common_end = min(range[i].end, end);
		if (common_start > common_end)
			continue;

		/* new start/end, will add it back at last */
		start = min(range[i].start, start);
		end = max(range[i].end, end);

		memmove(&range[i], &range[i + 1],
			(nr_range - (i + 1)) * sizeof(range[i]));
		range[nr_range - 1].start = 0;
		range[nr_range - 1].end   = 0;
		nr_range--;
		i--;
	}

	/* Need to add it: */
	return add_range(range, az, nr_range, start, end);
}

void subtract_range(struct range *range, int az, u64 start, u64 end)
{
	int i, j;

	if (start >= end)
		return;

	for (j = 0; j < az; j++) {
		if (!range[j].end)
			continue;

		if (start <= range[j].start && end >= range[j].end) {
			range[j].start = 0;
			range[j].end = 0;
			continue;
		}

		if (start <= range[j].start && end < range[j].end &&
		    range[j].start < end) {
			range[j].start = end;
			continue;
		}


		if (start > range[j].start && end >= range[j].end &&
		    range[j].end > start) {
			range[j].end = start;
			continue;
		}

		if (start > range[j].start && end < range[j].end) {
			/* Find the new spare: */
			for (i = 0; i < az; i++) {
				if (range[i].end == 0)
					break;
			}
			if (i < az) {
				range[i].end = range[j].end;
				range[i].start = end;
			} else {
				pr_err("%s: run out of slot in ranges\n",
					__func__);
			}
			range[j].end = start;
			continue;
		}
	}
}

static int cmp_range(const void *x1, const void *x2)
{
	const struct range *r1 = x1;
	const struct range *r2 = x2;

	if (r1->start < r2->start)
		return -1;
	if (r1->start > r2->start)
		return 1;
	return 0;
}

int clean_sort_range(struct range *range, int az)
{
	int i, j, k = az - 1, nr_range = az;

	for (i = 0; i < k; i++) {
		if (range[i].end)
			continue;
		for (j = k; j > i; j--) {
			if (range[j].end) {
				k = j;
				break;
			}
		}
		if (j == i)
			break;
		range[i].start = range[k].start;
		range[i].end   = range[k].end;
		range[k].start = 0;
		range[k].end   = 0;
		k--;
	}
	/* count it */
	for (i = 0; i < az; i++) {
		if (!range[i].end) {
			nr_range = i;
			break;
		}
	}

	/* sort them */
	sort(range, nr_range, sizeof(struct range), cmp_range, NULL);

	return nr_range;
}

void sort_range(struct range *range, int nr_range)
{
	/* sort them */
	sort(range, nr_range, sizeof(struct range), cmp_range, NULL);
}
