/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/ceph/ceph_debug.h>

#include <linux/math64.h>
#include <linux/slab.h>

#include <linux/ceph/striper.h>
#include <linux/ceph/types.h>

/*
 * Map a file extent to a stripe unit within an object.
 * Fill in objno, offset into object, and object extent length (i.e. the
 * number of bytes mapped, less than or equal to @l->stripe_unit).
 *
 * Example for stripe_count = 3, stripes_per_object = 4:
 *
 * blockno   |  0  3  6  9 |  1  4  7 10 |  2  5  8 11 | 12 15 18 21 | 13 16 19
 * stripeno  |  0  1  2  3 |  0  1  2  3 |  0  1  2  3 |  4  5  6  7 |  4  5  6
 * stripepos |      0      |      1      |      2      |      0      |      1
 * objno     |      0      |      1      |      2      |      3      |      4
 * objsetno  |                    0                    |                    1
 */
void ceph_calc_file_object_mapping(struct ceph_file_layout *l,
				   u64 off, u64 len,
				   u64 *objno, u64 *objoff, u32 *xlen)
{
	u32 stripes_per_object = l->object_size / l->stripe_unit;
	u64 blockno;	/* which su in the file (i.e. globally) */
	u32 blockoff;	/* offset into su */
	u64 stripeno;	/* which stripe */
	u32 stripepos;	/* which su in the stripe,
			   which object in the object set */
	u64 objsetno;	/* which object set */
	u32 objsetpos;	/* which stripe in the object set */

	blockno = div_u64_rem(off, l->stripe_unit, &blockoff);
	stripeno = div_u64_rem(blockno, l->stripe_count, &stripepos);
	objsetno = div_u64_rem(stripeno, stripes_per_object, &objsetpos);

	*objno = objsetno * l->stripe_count + stripepos;
	*objoff = objsetpos * l->stripe_unit + blockoff;
	*xlen = min_t(u64, len, l->stripe_unit - blockoff);
}
EXPORT_SYMBOL(ceph_calc_file_object_mapping);

/*
 * Return the last extent with given objno (@object_extents is sorted
 * by objno).  If not found, return NULL and set @add_pos so that the
 * new extent can be added with list_add(add_pos, new_ex).
 */
static struct ceph_object_extent *
lookup_last(struct list_head *object_extents, u64 objno,
	    struct list_head **add_pos)
{
	struct list_head *pos;

	list_for_each_prev(pos, object_extents) {
		struct ceph_object_extent *ex =
		    list_entry(pos, typeof(*ex), oe_item);

		if (ex->oe_objno == objno)
			return ex;

		if (ex->oe_objno < objno)
			break;
	}

	*add_pos = pos;
	return NULL;
}

static struct ceph_object_extent *
lookup_containing(struct list_head *object_extents, u64 objno,
		  u64 objoff, u32 xlen)
{
	struct ceph_object_extent *ex;

	list_for_each_entry(ex, object_extents, oe_item) {
		if (ex->oe_objno == objno &&
		    ex->oe_off <= objoff &&
		    ex->oe_off + ex->oe_len >= objoff + xlen) /* paranoia */
			return ex;

		if (ex->oe_objno > objno)
			break;
	}

	return NULL;
}

/*
 * Map a file extent to a sorted list of object extents.
 *
 * We want only one (or as few as possible) object extents per object.
 * Adjacent object extents will be merged together, each returned object
 * extent may reverse map to multiple different file extents.
 *
 * Call @alloc_fn for each new object extent and @action_fn for each
 * mapped stripe unit, whether it was merged into an already allocated
 * object extent or started a new object extent.
 *
 * Newly allocated object extents are added to @object_extents.
 * To keep @object_extents sorted, successive calls to this function
 * must map successive file extents (i.e. the list of file extents that
 * are mapped using the same @object_extents must be sorted).
 *
 * The caller is responsible for @object_extents.
 */
int ceph_file_to_extents(struct ceph_file_layout *l, u64 off, u64 len,
			 struct list_head *object_extents,
			 struct ceph_object_extent *alloc_fn(void *arg),
			 void *alloc_arg,
			 ceph_object_extent_fn_t action_fn,
			 void *action_arg)
{
	struct ceph_object_extent *last_ex, *ex;

	while (len) {
		struct list_head *add_pos = NULL;
		u64 objno, objoff;
		u32 xlen;

		ceph_calc_file_object_mapping(l, off, len, &objno, &objoff,
					      &xlen);

		last_ex = lookup_last(object_extents, objno, &add_pos);
		if (!last_ex || last_ex->oe_off + last_ex->oe_len != objoff) {
			ex = alloc_fn(alloc_arg);
			if (!ex)
				return -ENOMEM;

			ex->oe_objno = objno;
			ex->oe_off = objoff;
			ex->oe_len = xlen;
			if (action_fn)
				action_fn(ex, xlen, action_arg);

			if (!last_ex)
				list_add(&ex->oe_item, add_pos);
			else
				list_add(&ex->oe_item, &last_ex->oe_item);
		} else {
			last_ex->oe_len += xlen;
			if (action_fn)
				action_fn(last_ex, xlen, action_arg);
		}

		off += xlen;
		len -= xlen;
	}

	for (last_ex = list_first_entry(object_extents, typeof(*ex), oe_item),
	     ex = list_next_entry(last_ex, oe_item);
	     &ex->oe_item != object_extents;
	     last_ex = ex, ex = list_next_entry(ex, oe_item)) {
		if (last_ex->oe_objno > ex->oe_objno ||
		    (last_ex->oe_objno == ex->oe_objno &&
		     last_ex->oe_off + last_ex->oe_len >= ex->oe_off)) {
			WARN(1, "%s: object_extents list not sorted!\n",
			     __func__);
			return -EINVAL;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ceph_file_to_extents);

/*
 * A stripped down, non-allocating version of ceph_file_to_extents(),
 * for when @object_extents is already populated.
 */
int ceph_iterate_extents(struct ceph_file_layout *l, u64 off, u64 len,
			 struct list_head *object_extents,
			 ceph_object_extent_fn_t action_fn,
			 void *action_arg)
{
	while (len) {
		struct ceph_object_extent *ex;
		u64 objno, objoff;
		u32 xlen;

		ceph_calc_file_object_mapping(l, off, len, &objno, &objoff,
					      &xlen);

		ex = lookup_containing(object_extents, objno, objoff, xlen);
		if (!ex) {
			WARN(1, "%s: objno %llu %llu~%u not found!\n",
			     __func__, objno, objoff, xlen);
			return -EINVAL;
		}

		action_fn(ex, xlen, action_arg);

		off += xlen;
		len -= xlen;
	}

	return 0;
}
EXPORT_SYMBOL(ceph_iterate_extents);

/*
 * Reverse map an object extent to a sorted list of file extents.
 *
 * On success, the caller is responsible for:
 *
 *     kfree(file_extents)
 */
int ceph_extent_to_file(struct ceph_file_layout *l,
			u64 objno, u64 objoff, u64 objlen,
			struct ceph_file_extent **file_extents,
			u32 *num_file_extents)
{
	u32 stripes_per_object = l->object_size / l->stripe_unit;
	u64 blockno;	/* which su */
	u32 blockoff;	/* offset into su */
	u64 stripeno;	/* which stripe */
	u32 stripepos;	/* which su in the stripe,
			   which object in the object set */
	u64 objsetno;	/* which object set */
	u32 i = 0;

	if (!objlen) {
		*file_extents = NULL;
		*num_file_extents = 0;
		return 0;
	}

	*num_file_extents = DIV_ROUND_UP_ULL(objoff + objlen, l->stripe_unit) -
				     DIV_ROUND_DOWN_ULL(objoff, l->stripe_unit);
	*file_extents = kmalloc_array(*num_file_extents, sizeof(**file_extents),
				      GFP_NOIO);
	if (!*file_extents)
		return -ENOMEM;

	div_u64_rem(objoff, l->stripe_unit, &blockoff);
	while (objlen) {
		u64 off, len;

		objsetno = div_u64_rem(objno, l->stripe_count, &stripepos);
		stripeno = div_u64(objoff, l->stripe_unit) +
						objsetno * stripes_per_object;
		blockno = stripeno * l->stripe_count + stripepos;
		off = blockno * l->stripe_unit + blockoff;
		len = min_t(u64, objlen, l->stripe_unit - blockoff);

		(*file_extents)[i].fe_off = off;
		(*file_extents)[i].fe_len = len;

		blockoff = 0;
		objoff += len;
		objlen -= len;
		i++;
	}

	BUG_ON(i != *num_file_extents);
	return 0;
}
EXPORT_SYMBOL(ceph_extent_to_file);
