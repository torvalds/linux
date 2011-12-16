/*
 * Range add and subtract
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sort.h>

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

	/* Try to merge it with old one: */
	for (i = 0; i < nr_range; i++) {
		u64 final_start, final_end;
		u64 common_start, common_end;

		if (!range[i].end)
			continue;

		common_start = max(range[i].start, start);
		common_end = min(range[i].end, end);
		if (common_start > common_end)
			continue;

		final_start = min(range[i].start, start);
		final_end = max(range[i].end, end);

		range[i].start = final_start;
		range[i].end =  final_end;
		return nr_range;
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
				printk(KERN_ERR "run of slot in ranges\n");
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
	s64 start1, start2;

	start1 = r1->start;
	start2 = r2->start;

	return start1 - start2;
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
