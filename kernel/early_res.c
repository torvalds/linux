/*
 * early_res, could be used to replace bootmem
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/early_res.h>
#include <linux/slab.h>
#include <linux/kmemleak.h>

/*
 * Early reserved memory areas.
 */
/*
 * need to make sure this one is bigger enough before
 * find_fw_memmap_area could be used
 */
#define MAX_EARLY_RES_X 32

struct early_res {
	u64 start, end;
	char name[15];
	char overlap_ok;
};
static struct early_res early_res_x[MAX_EARLY_RES_X] __initdata;

static int max_early_res __initdata = MAX_EARLY_RES_X;
static struct early_res *early_res __initdata = &early_res_x[0];
static int early_res_count __initdata;

static int __init find_overlapped_early(u64 start, u64 end)
{
	int i;
	struct early_res *r;

	for (i = 0; i < max_early_res && early_res[i].end; i++) {
		r = &early_res[i];
		if (end > r->start && start < r->end)
			break;
	}

	return i;
}

/*
 * Drop the i-th range from the early reservation map,
 * by copying any higher ranges down one over it, and
 * clearing what had been the last slot.
 */
static void __init drop_range(int i)
{
	int j;

	for (j = i + 1; j < max_early_res && early_res[j].end; j++)
		;

	memmove(&early_res[i], &early_res[i + 1],
	       (j - 1 - i) * sizeof(struct early_res));

	early_res[j - 1].end = 0;
	early_res_count--;
}

static void __init drop_range_partial(int i, u64 start, u64 end)
{
	u64 common_start, common_end;
	u64 old_start, old_end;

	old_start = early_res[i].start;
	old_end = early_res[i].end;
	common_start = max(old_start, start);
	common_end = min(old_end, end);

	/* no overlap ? */
	if (common_start >= common_end)
		return;

	if (old_start < common_start) {
		/* make head segment */
		early_res[i].end = common_start;
		if (old_end > common_end) {
			char name[15];

			/*
			 * Save a local copy of the name, since the
			 * early_res array could get resized inside
			 * reserve_early_without_check() ->
			 * __check_and_double_early_res(), which would
			 * make the current name pointer invalid.
			 */
			strncpy(name, early_res[i].name,
					 sizeof(early_res[i].name) - 1);
			/* add another for left over on tail */
			reserve_early_without_check(common_end, old_end, name);
		}
		return;
	} else {
		if (old_end > common_end) {
			/* reuse the entry for tail left */
			early_res[i].start = common_end;
			return;
		}
		/* all covered */
		drop_range(i);
	}
}

/*
 * Split any existing ranges that:
 *  1) are marked 'overlap_ok', and
 *  2) overlap with the stated range [start, end)
 * into whatever portion (if any) of the existing range is entirely
 * below or entirely above the stated range.  Drop the portion
 * of the existing range that overlaps with the stated range,
 * which will allow the caller of this routine to then add that
 * stated range without conflicting with any existing range.
 */
static void __init drop_overlaps_that_are_ok(u64 start, u64 end)
{
	int i;
	struct early_res *r;
	u64 lower_start, lower_end;
	u64 upper_start, upper_end;
	char name[15];

	for (i = 0; i < max_early_res && early_res[i].end; i++) {
		r = &early_res[i];

		/* Continue past non-overlapping ranges */
		if (end <= r->start || start >= r->end)
			continue;

		/*
		 * Leave non-ok overlaps as is; let caller
		 * panic "Overlapping early reservations"
		 * when it hits this overlap.
		 */
		if (!r->overlap_ok)
			return;

		/*
		 * We have an ok overlap.  We will drop it from the early
		 * reservation map, and add back in any non-overlapping
		 * portions (lower or upper) as separate, overlap_ok,
		 * non-overlapping ranges.
		 */

		/* 1. Note any non-overlapping (lower or upper) ranges. */
		strncpy(name, r->name, sizeof(name) - 1);

		lower_start = lower_end = 0;
		upper_start = upper_end = 0;
		if (r->start < start) {
			lower_start = r->start;
			lower_end = start;
		}
		if (r->end > end) {
			upper_start = end;
			upper_end = r->end;
		}

		/* 2. Drop the original ok overlapping range */
		drop_range(i);

		i--;		/* resume for-loop on copied down entry */

		/* 3. Add back in any non-overlapping ranges. */
		if (lower_end)
			reserve_early_overlap_ok(lower_start, lower_end, name);
		if (upper_end)
			reserve_early_overlap_ok(upper_start, upper_end, name);
	}
}

static void __init __reserve_early(u64 start, u64 end, char *name,
						int overlap_ok)
{
	int i;
	struct early_res *r;

	i = find_overlapped_early(start, end);
	if (i >= max_early_res)
		panic("Too many early reservations");
	r = &early_res[i];
	if (r->end)
		panic("Overlapping early reservations "
		      "%llx-%llx %s to %llx-%llx %s\n",
		      start, end - 1, name ? name : "", r->start,
		      r->end - 1, r->name);
	r->start = start;
	r->end = end;
	r->overlap_ok = overlap_ok;
	if (name)
		strncpy(r->name, name, sizeof(r->name) - 1);
	early_res_count++;
}

/*
 * A few early reservtations come here.
 *
 * The 'overlap_ok' in the name of this routine does -not- mean it
 * is ok for these reservations to overlap an earlier reservation.
 * Rather it means that it is ok for subsequent reservations to
 * overlap this one.
 *
 * Use this entry point to reserve early ranges when you are doing
 * so out of "Paranoia", reserving perhaps more memory than you need,
 * just in case, and don't mind a subsequent overlapping reservation
 * that is known to be needed.
 *
 * The drop_overlaps_that_are_ok() call here isn't really needed.
 * It would be needed if we had two colliding 'overlap_ok'
 * reservations, so that the second such would not panic on the
 * overlap with the first.  We don't have any such as of this
 * writing, but might as well tolerate such if it happens in
 * the future.
 */
void __init reserve_early_overlap_ok(u64 start, u64 end, char *name)
{
	drop_overlaps_that_are_ok(start, end);
	__reserve_early(start, end, name, 1);
}

static void __init __check_and_double_early_res(u64 ex_start, u64 ex_end)
{
	u64 start, end, size, mem;
	struct early_res *new;

	/* do we have enough slots left ? */
	if ((max_early_res - early_res_count) > max(max_early_res/8, 2))
		return;

	/* double it */
	mem = -1ULL;
	size = sizeof(struct early_res) * max_early_res * 2;
	if (early_res == early_res_x)
		start = 0;
	else
		start = early_res[0].end;
	end = ex_start;
	if (start + size < end)
		mem = find_fw_memmap_area(start, end, size,
					 sizeof(struct early_res));
	if (mem == -1ULL) {
		start = ex_end;
		end = get_max_mapped();
		if (start + size < end)
			mem = find_fw_memmap_area(start, end, size,
						 sizeof(struct early_res));
	}
	if (mem == -1ULL)
		panic("can not find more space for early_res array");

	new = __va(mem);
	/* save the first one for own */
	new[0].start = mem;
	new[0].end = mem + size;
	new[0].overlap_ok = 0;
	/* copy old to new */
	if (early_res == early_res_x) {
		memcpy(&new[1], &early_res[0],
			 sizeof(struct early_res) * max_early_res);
		memset(&new[max_early_res+1], 0,
			 sizeof(struct early_res) * (max_early_res - 1));
		early_res_count++;
	} else {
		memcpy(&new[1], &early_res[1],
			 sizeof(struct early_res) * (max_early_res - 1));
		memset(&new[max_early_res], 0,
			 sizeof(struct early_res) * max_early_res);
	}
	memset(&early_res[0], 0, sizeof(struct early_res) * max_early_res);
	early_res = new;
	max_early_res *= 2;
	printk(KERN_DEBUG "early_res array is doubled to %d at [%llx - %llx]\n",
		max_early_res, mem, mem + size - 1);
}

/*
 * Most early reservations come here.
 *
 * We first have drop_overlaps_that_are_ok() drop any pre-existing
 * 'overlap_ok' ranges, so that we can then reserve this memory
 * range without risk of panic'ing on an overlapping overlap_ok
 * early reservation.
 */
void __init reserve_early(u64 start, u64 end, char *name)
{
	if (start >= end)
		return;

	__check_and_double_early_res(start, end);

	drop_overlaps_that_are_ok(start, end);
	__reserve_early(start, end, name, 0);
}

void __init reserve_early_without_check(u64 start, u64 end, char *name)
{
	struct early_res *r;

	if (start >= end)
		return;

	__check_and_double_early_res(start, end);

	r = &early_res[early_res_count];

	r->start = start;
	r->end = end;
	r->overlap_ok = 0;
	if (name)
		strncpy(r->name, name, sizeof(r->name) - 1);
	early_res_count++;
}

void __init free_early(u64 start, u64 end)
{
	struct early_res *r;
	int i;

	kmemleak_free_part(__va(start), end - start);

	i = find_overlapped_early(start, end);
	r = &early_res[i];
	if (i >= max_early_res || r->end != end || r->start != start)
		panic("free_early on not reserved area: %llx-%llx!",
			 start, end - 1);

	drop_range(i);
}

void __init free_early_partial(u64 start, u64 end)
{
	struct early_res *r;
	int i;

	kmemleak_free_part(__va(start), end - start);

	if (start == end)
		return;

	if (WARN_ONCE(start > end, "  wrong range [%#llx, %#llx]\n", start, end))
		return;

try_next:
	i = find_overlapped_early(start, end);
	if (i >= max_early_res)
		return;

	r = &early_res[i];
	/* hole ? */
	if (r->end >= end && r->start <= start) {
		drop_range_partial(i, start, end);
		return;
	}

	drop_range_partial(i, start, end);
	goto try_next;
}

#ifdef CONFIG_NO_BOOTMEM
static void __init subtract_early_res(struct range *range, int az)
{
	int i, count;
	u64 final_start, final_end;
	int idx = 0;

	count  = 0;
	for (i = 0; i < max_early_res && early_res[i].end; i++)
		count++;

	/* need to skip first one ?*/
	if (early_res != early_res_x)
		idx = 1;

#define DEBUG_PRINT_EARLY_RES 1

#if DEBUG_PRINT_EARLY_RES
	printk(KERN_INFO "Subtract (%d early reservations)\n", count);
#endif
	for (i = idx; i < count; i++) {
		struct early_res *r = &early_res[i];
#if DEBUG_PRINT_EARLY_RES
		printk(KERN_INFO "  #%d [%010llx - %010llx] %15s\n", i,
			r->start, r->end, r->name);
#endif
		final_start = PFN_DOWN(r->start);
		final_end = PFN_UP(r->end);
		if (final_start >= final_end)
			continue;
		subtract_range(range, az, final_start, final_end);
	}

}

int __init get_free_all_memory_range(struct range **rangep, int nodeid)
{
	int i, count;
	u64 start = 0, end;
	u64 size;
	u64 mem;
	struct range *range;
	int nr_range;

	count  = 0;
	for (i = 0; i < max_early_res && early_res[i].end; i++)
		count++;

	count *= 2;

	size = sizeof(struct range) * count;
	end = get_max_mapped();
#ifdef MAX_DMA32_PFN
	if (end > (MAX_DMA32_PFN << PAGE_SHIFT))
		start = MAX_DMA32_PFN << PAGE_SHIFT;
#endif
	mem = find_fw_memmap_area(start, end, size, sizeof(struct range));
	if (mem == -1ULL)
		panic("can not find more space for range free");

	range = __va(mem);
	/* use early_node_map[] and early_res to get range array at first */
	memset(range, 0, size);
	nr_range = 0;

	/* need to go over early_node_map to find out good range for node */
	nr_range = add_from_early_node_map(range, count, nr_range, nodeid);
#ifdef CONFIG_X86_32
	subtract_range(range, count, max_low_pfn, -1ULL);
#endif
	subtract_early_res(range, count);
	nr_range = clean_sort_range(range, count);

	/* need to clear it ? */
	if (nodeid == MAX_NUMNODES) {
		memset(&early_res[0], 0,
			 sizeof(struct early_res) * max_early_res);
		early_res = NULL;
		max_early_res = 0;
	}

	*rangep = range;
	return nr_range;
}
#else
void __init early_res_to_bootmem(u64 start, u64 end)
{
	int i, count;
	u64 final_start, final_end;
	int idx = 0;

	count  = 0;
	for (i = 0; i < max_early_res && early_res[i].end; i++)
		count++;

	/* need to skip first one ?*/
	if (early_res != early_res_x)
		idx = 1;

	printk(KERN_INFO "(%d/%d early reservations) ==> bootmem [%010llx - %010llx]\n",
			 count - idx, max_early_res, start, end);
	for (i = idx; i < count; i++) {
		struct early_res *r = &early_res[i];
		printk(KERN_INFO "  #%d [%010llx - %010llx] %16s", i,
			r->start, r->end, r->name);
		final_start = max(start, r->start);
		final_end = min(end, r->end);
		if (final_start >= final_end) {
			printk(KERN_CONT "\n");
			continue;
		}
		printk(KERN_CONT " ==> [%010llx - %010llx]\n",
			final_start, final_end);
		reserve_bootmem_generic(final_start, final_end - final_start,
				BOOTMEM_DEFAULT);
	}
	/* clear them */
	memset(&early_res[0], 0, sizeof(struct early_res) * max_early_res);
	early_res = NULL;
	max_early_res = 0;
	early_res_count = 0;
}
#endif

/* Check for already reserved areas */
static inline int __init bad_addr(u64 *addrp, u64 size, u64 align)
{
	int i;
	u64 addr = *addrp;
	int changed = 0;
	struct early_res *r;
again:
	i = find_overlapped_early(addr, addr + size);
	r = &early_res[i];
	if (i < max_early_res && r->end) {
		*addrp = addr = round_up(r->end, align);
		changed = 1;
		goto again;
	}
	return changed;
}

/* Check for already reserved areas */
static inline int __init bad_addr_size(u64 *addrp, u64 *sizep, u64 align)
{
	int i;
	u64 addr = *addrp, last;
	u64 size = *sizep;
	int changed = 0;
again:
	last = addr + size;
	for (i = 0; i < max_early_res && early_res[i].end; i++) {
		struct early_res *r = &early_res[i];
		if (last > r->start && addr < r->start) {
			size = r->start - addr;
			changed = 1;
			goto again;
		}
		if (last > r->end && addr < r->end) {
			addr = round_up(r->end, align);
			size = last - addr;
			changed = 1;
			goto again;
		}
		if (last <= r->end && addr >= r->start) {
			(*sizep)++;
			return 0;
		}
	}
	if (changed) {
		*addrp = addr;
		*sizep = size;
	}
	return changed;
}

/*
 * Find a free area with specified alignment in a specific range.
 * only with the area.between start to end is active range from early_node_map
 * so they are good as RAM
 */
u64 __init find_early_area(u64 ei_start, u64 ei_last, u64 start, u64 end,
			 u64 size, u64 align)
{
	u64 addr, last;

	addr = round_up(ei_start, align);
	if (addr < start)
		addr = round_up(start, align);
	if (addr >= ei_last)
		goto out;
	while (bad_addr(&addr, size, align) && addr+size <= ei_last)
		;
	last = addr + size;
	if (last > ei_last)
		goto out;
	if (last > end)
		goto out;

	return addr;

out:
	return -1ULL;
}

u64 __init find_early_area_size(u64 ei_start, u64 ei_last, u64 start,
			 u64 *sizep, u64 align)
{
	u64 addr, last;

	addr = round_up(ei_start, align);
	if (addr < start)
		addr = round_up(start, align);
	if (addr >= ei_last)
		goto out;
	*sizep = ei_last - addr;
	while (bad_addr_size(&addr, sizep, align) && addr + *sizep <= ei_last)
		;
	last = addr + *sizep;
	if (last > ei_last)
		goto out;

	return addr;

out:
	return -1ULL;
}
