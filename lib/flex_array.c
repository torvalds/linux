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
#include <linux/module.h>

struct flex_array_part {
	char elements[FLEX_ARRAY_PART_SIZE];
};

/*
 * If a user requests an allocation which is small
 * enough, we may simply use the space in the
 * flex_array->parts[] array to store the user
 * data.
 */
static inline int elements_fit_in_base(struct flex_array *fa)
{
	int data_size = fa->element_size * fa->total_nr_elements;
	if (data_size <= FLEX_ARRAY_BASE_BYTES_LEFT)
		return 1;
	return 0;
}

/**
 * flex_array_alloc - allocate a new flexible array
 * @element_size:	the size of individual elements in the array
 * @total:		total number of elements that this should hold
 * @flags:		page allocation flags to use for base array
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
	int max_size = FLEX_ARRAY_NR_BASE_PTRS *
				FLEX_ARRAY_ELEMENTS_PER_PART(element_size);

	/* max_size will end up 0 if element_size > PAGE_SIZE */
	if (total > max_size)
		return NULL;
	ret = kzalloc(sizeof(struct flex_array), flags);
	if (!ret)
		return NULL;
	ret->element_size = element_size;
	ret->total_nr_elements = total;
	if (elements_fit_in_base(ret) && !(flags & __GFP_ZERO))
		memset(&ret->parts[0], FLEX_ARRAY_FREE,
						FLEX_ARRAY_BASE_BYTES_LEFT);
	return ret;
}
EXPORT_SYMBOL(flex_array_alloc);

static int fa_element_to_part_nr(struct flex_array *fa,
					unsigned int element_nr)
{
	return element_nr / FLEX_ARRAY_ELEMENTS_PER_PART(fa->element_size);
}

/**
 * flex_array_free_parts - just free the second-level pages
 * @fa:		the flex array from which to free parts
 *
 * This is to be used in cases where the base 'struct flex_array'
 * has been statically allocated and should not be free.
 */
void flex_array_free_parts(struct flex_array *fa)
{
	int part_nr;

	if (elements_fit_in_base(fa))
		return;
	for (part_nr = 0; part_nr < FLEX_ARRAY_NR_BASE_PTRS; part_nr++)
		kfree(fa->parts[part_nr]);
}
EXPORT_SYMBOL(flex_array_free_parts);

void flex_array_free(struct flex_array *fa)
{
	flex_array_free_parts(fa);
	kfree(fa);
}
EXPORT_SYMBOL(flex_array_free);

static unsigned int index_inside_part(struct flex_array *fa,
					unsigned int element_nr)
{
	unsigned int part_offset;

	part_offset = element_nr %
				FLEX_ARRAY_ELEMENTS_PER_PART(fa->element_size);
	return part_offset * fa->element_size;
}

static struct flex_array_part *
__fa_get_part(struct flex_array *fa, int part_nr, gfp_t flags)
{
	struct flex_array_part *part = fa->parts[part_nr];
	if (!part) {
		part = kmalloc(sizeof(struct flex_array_part), flags);
		if (!part)
			return NULL;
		if (!(flags & __GFP_ZERO))
			memset(part, FLEX_ARRAY_FREE,
				sizeof(struct flex_array_part));
		fa->parts[part_nr] = part;
	}
	return part;
}

/**
 * flex_array_put - copy data into the array at @element_nr
 * @fa:		the flex array to copy data into
 * @element_nr:	index of the position in which to insert
 * 		the new element.
 * @src:	address of data to copy into the array
 * @flags:	page allocation flags to use for array expansion
 *
 *
 * Note that this *copies* the contents of @src into
 * the array.  If you are trying to store an array of
 * pointers, make sure to pass in &ptr instead of ptr.
 * You may instead wish to use the flex_array_put_ptr()
 * helper function.
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
EXPORT_SYMBOL(flex_array_put);

/**
 * flex_array_clear - clear element in array at @element_nr
 * @fa:		the flex array of the element.
 * @element_nr:	index of the position to clear.
 *
 * Locking must be provided by the caller.
 */
int flex_array_clear(struct flex_array *fa, unsigned int element_nr)
{
	int part_nr = fa_element_to_part_nr(fa, element_nr);
	struct flex_array_part *part;
	void *dst;

	if (element_nr >= fa->total_nr_elements)
		return -ENOSPC;
	if (elements_fit_in_base(fa))
		part = (struct flex_array_part *)&fa->parts[0];
	else {
		part = fa->parts[part_nr];
		if (!part)
			return -EINVAL;
	}
	dst = &part->elements[index_inside_part(fa, element_nr)];
	memset(dst, FLEX_ARRAY_FREE, fa->element_size);
	return 0;
}
EXPORT_SYMBOL(flex_array_clear);

/**
 * flex_array_prealloc - guarantee that array space exists
 * @fa:		the flex array for which to preallocate parts
 * @start:	index of first array element for which space is allocated
 * @end:	index of last (inclusive) element for which space is allocated
 * @flags:	page allocation flags
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
EXPORT_SYMBOL(flex_array_prealloc);

/**
 * flex_array_get - pull data back out of the array
 * @fa:		the flex array from which to extract data
 * @element_nr:	index of the element to fetch from the array
 *
 * Returns a pointer to the data at index @element_nr.  Note
 * that this is a copy of the data that was passed in.  If you
 * are using this to store pointers, you'll get back &ptr.  You
 * may instead wish to use the flex_array_get_ptr helper.
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
EXPORT_SYMBOL(flex_array_get);

/**
 * flex_array_get_ptr - pull a ptr back out of the array
 * @fa:		the flex array from which to extract data
 * @element_nr:	index of the element to fetch from the array
 *
 * Returns the pointer placed in the flex array at element_nr using
 * flex_array_put_ptr().  This function should not be called if the
 * element in question was not set using the _put_ptr() helper.
 */
void *flex_array_get_ptr(struct flex_array *fa, unsigned int element_nr)
{
	void **tmp;

	tmp = flex_array_get(fa, element_nr);
	if (!tmp)
		return NULL;

	return *tmp;
}
EXPORT_SYMBOL(flex_array_get_ptr);

static int part_is_free(struct flex_array_part *part)
{
	int i;

	for (i = 0; i < sizeof(struct flex_array_part); i++)
		if (part->elements[i] != FLEX_ARRAY_FREE)
			return 0;
	return 1;
}

/**
 * flex_array_shrink - free unused second-level pages
 * @fa:		the flex array to shrink
 *
 * Frees all second-level pages that consist solely of unused
 * elements.  Returns the number of pages freed.
 *
 * Locking must be provided by the caller.
 */
int flex_array_shrink(struct flex_array *fa)
{
	struct flex_array_part *part;
	int part_nr;
	int ret = 0;

	if (elements_fit_in_base(fa))
		return ret;
	for (part_nr = 0; part_nr < FLEX_ARRAY_NR_BASE_PTRS; part_nr++) {
		part = fa->parts[part_nr];
		if (!part)
			continue;
		if (part_is_free(part)) {
			fa->parts[part_nr] = NULL;
			kfree(part);
			ret++;
		}
	}
	return ret;
}
EXPORT_SYMBOL(flex_array_shrink);
