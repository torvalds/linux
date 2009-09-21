/*
 * Flexible array managed in PAGE_SIZE parts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2009
 *
 * Author: Dave Hansen <dave@linux.vnet.ibm.com>
 */

#include <linux/flex_array.h>
#include <linux/slab.h>
#include <linux/stddef.h>

struct flex_array_part {
	char elements[FLEX_ARRAY_PART_SIZE];
};

static inline int __elements_per_part(int element_size)
{
	return FLEX_ARRAY_PART_SIZE / element_size;
}

static inline int bytes_left_in_base(void)
{
	int element_offset = offsetof(struct flex_array, parts);
	int bytes_left = FLEX_ARRAY_BASE_SIZE - element_offset;
	return bytes_left;
}

static inline int nr_base_part_ptrs(void)
{
	return bytes_left_in_base() / sizeof(struct flex_array_part *);
}

/*
 * If a user requests an allocation which is small
 * enough, we may simply use the space in the
 * flex_array->parts[] array to store the user
 * data.
 */
static inline int elements_fit_in_base(struct flex_array *fa)
{
	int data_size = fa->element_size * fa->total_nr_elements;
	if (data_size <= bytes_left_in_base())
		return 1;
	return 0;
}

/**
 * flex_array_alloc - allocate a new flexible array
 * @element_size:	the size of individual elements in the array
 * @total:		total number of elements that this should hold
 *
 * Note: all locking must be provided by the caller.
 *
 * @total is used to size internal structures.  If the user ever
 * accesses any array indexes >=@total, it will produce errors.
 *
 * The maximum number of elements is defined as: the number of
 * elements that can be stored in a page times the number of
 * page pointers that we can fit in the base structure or (using
 * integer math):
 *
 * 	(PAGE_SIZE/element_size) * (PAGE_SIZE-8)/sizeof(void *)
 *
 * Here's a table showing example capacities.  Note that the maximum
 * index that the get/put() functions is just nr_objects-1.   This
 * basically means that you get 4MB of storage on 32-bit and 2MB on
 * 64-bit.
 *
 *
 * Element size | Objects | Objects |
 * PAGE_SIZE=4k |  32-bit |  64-bit |
 * ---------------------------------|
 *      1 bytes | 4186112 | 2093056 |
 *      2 bytes | 2093056 | 1046528 |
 *      3 bytes | 1395030 |  697515 |
 *      4 bytes | 1046528 |  523264 |
 *     32 bytes |  130816 |   65408 |
 *     33 bytes |  126728 |   63364 |
 *   2048 bytes |    2044 |    1022 |
 *   2049 bytes |    1022 |     511 |
 *       void * | 1046528 |  261632 |
 *
 * Since 64-bit pointers are twice the size, we lose half the
 * capacity in the base structure.  Also note that no effort is made
 * to efficiently pack objects across page boundaries.
 */
struct flex_array *flex_array_alloc(int element_size, unsigned int total,
					gfp_t flags)
{
	struct flex_array *ret;
	int max_size = nr_base_part_ptrs() * __elements_per_part(element_size);

	/* max_size will end up 0 if element_size > PAGE_SIZE */
	if (total > max_size)
		return NULL;
	ret = kzalloc(sizeof(struct flex_array), flags);
	if (!ret)
		return NULL;
	ret->element_size = element_size;
	ret->total_nr_elements = total;
	return ret;
}

static int fa_element_to_part_nr(struct flex_array *fa,
					unsigned int element_nr)
{
	return element_nr / __elements_per_part(fa->element_size);
}

/**
 * flex_array_free_parts - just free the second-level pages
 *
 * This is to be used in cases where the base 'struct flex_array'
 * has been statically allocated and should not be free.
 */
void flex_array_free_parts(struct flex_array *fa)
{
	int part_nr;
	int max_part = nr_base_part_ptrs();

	if (elements_fit_in_base(fa))
		return;
	for (part_nr = 0; part_nr < max_part; part_nr++)
		kfree(fa->parts[part_nr]);
}

void flex_array_free(struct flex_array *fa)
{
	flex_array_free_parts(fa);
	kfree(fa);
}

static unsigned int index_inside_part(struct flex_array *fa,
					unsigned int element_nr)
{
	unsigned int part_offset;

	part_offset = element_nr % __elements_per_part(fa->element_size);
	return part_offset * fa->element_size;
}

static struct flex_array_part *
__fa_get_part(struct flex_array *fa, int part_nr, gfp_t flags)
{
	struct flex_array_part *part = fa->parts[part_nr];
	if (!part) {
		/*
		 * This leaves the part pages uninitialized
		 * and with potentially random data, just
		 * as if the user had kmalloc()'d the whole.
		 * __GFP_ZERO can be used to zero it.
		 */
		part = kmalloc(FLEX_ARRAY_PART_SIZE, flags);
		if (!part)
			return NULL;
		fa->parts[part_nr] = part;
	}
	return part;
}

/**
 * flex_array_put - copy data into the array at @element_nr
 * @src:	address of data to copy into the array
 * @element_nr:	index of the position in which to insert
 * 		the new element.
 *
 * Note that this *copies* the contents of @src into
 * the array.  If you are trying to store an array of
 * pointers, make sure to pass in &ptr instead of ptr.
 *
 * Locking must be provided by the caller.
 */
int flex_array_put(struct flex_array *fa, unsigned int element_nr, void *src,
			gfp_t flags)
{
	int part_nr = fa_element_to_part_nr(fa, element_nr);
	struct flex_array_part *part;
	void *dst;

	if (element_nr >= fa->total_nr_elements)
		return -ENOSPC;
	if (elements_fit_in_base(fa))
		part = (struct flex_array_part *)&fa->parts[0];
	else {
		part = __fa_get_part(fa, part_nr, flags);
		if (!part)
			return -ENOMEM;
	}
	dst = &part->elements[index_inside_part(fa, element_nr)];
	memcpy(dst, src, fa->element_size);
	return 0;
}

/**
 * flex_array_prealloc - guarantee that array space exists
 * @start:	index of first array element for which space is allocated
 * @end:	index of last (inclusive) element for which space is allocated
 *
 * This will guarantee that no future calls to flex_array_put()
 * will allocate memory.  It can be used if you are expecting to
 * be holding a lock or in some atomic context while writing
 * data into the array.
 *
 * Locking must be provided by the caller.
 */
int flex_array_prealloc(struct flex_array *fa, unsigned int start,
			unsigned int end, gfp_t flags)
{
	int start_part;
	int end_part;
	int part_nr;
	struct flex_array_part *part;

	if (start >= fa->total_nr_elements || end >= fa->total_nr_elements)
		return -ENOSPC;
	if (elements_fit_in_base(fa))
		return 0;
	start_part = fa_element_to_part_nr(fa, start);
	end_part = fa_element_to_part_nr(fa, end);
	for (part_nr = start_part; part_nr <= end_part; part_nr++) {
		part = __fa_get_part(fa, part_nr, flags);
		if (!part)
			return -ENOMEM;
	}
	return 0;
}

/**
 * flex_array_get - pull data back out of the array
 * @element_nr:	index of the element to fetch from the array
 *
 * Returns a pointer to the data at index @element_nr.  Note
 * that this is a copy of the data that was passed in.  If you
 * are using this to store pointers, you'll get back &ptr.
 *
 * Locking must be provided by the caller.
 */
void *flex_array_get(struct flex_array *fa, unsigned int element_nr)
{
	int part_nr = fa_element_to_part_nr(fa, element_nr);
	struct flex_array_part *part;

	if (element_nr >= fa->total_nr_elements)
		return NULL;
	if (elements_fit_in_base(fa))
		part = (struct flex_array_part *)&fa->parts[0];
	else {
		part = fa->parts[part_nr];
		if (!part)
			return NULL;
	}
	return &part->elements[index_inside_part(fa, element_nr)];
}
