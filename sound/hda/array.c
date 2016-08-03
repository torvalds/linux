/*
 * generic arrays
 */

#include <linux/slab.h>
#include <sound/core.h>
#include <sound/hdaudio.h>

/**
 * snd_array_new - get a new element from the given array
 * @array: the array object
 *
 * Get a new element from the given array.  If it exceeds the
 * pre-allocated array size, re-allocate the array.
 *
 * Returns NULL if allocation failed.
 */
void *snd_array_new(struct snd_array *array)
{
	if (snd_BUG_ON(!array->elem_size))
		return NULL;
	if (array->used >= array->alloced) {
		int num = array->alloced + array->alloc_align;
		int oldsize = array->alloced * array->elem_size;
		int size = (num + 1) * array->elem_size;
		void *nlist;
		if (snd_BUG_ON(num >= 4096))
			return NULL;
		nlist = krealloc(array->list, size, GFP_KERNEL);
		if (!nlist)
			return NULL;
		memset(nlist + oldsize, 0, size - oldsize);
		array->list = nlist;
		array->alloced = num;
	}
	return snd_array_elem(array, array->used++);
}
EXPORT_SYMBOL_GPL(snd_array_new);

/**
 * snd_array_free - free the given array elements
 * @array: the array object
 */
void snd_array_free(struct snd_array *array)
{
	kfree(array->list);
	array->used = 0;
	array->alloced = 0;
	array->list = NULL;
}
EXPORT_SYMBOL_GPL(snd_array_free);
