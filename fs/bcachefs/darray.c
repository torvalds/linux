// SPDX-License-Identifier: GPL-2.0

#include <linux/log2.h>
#include <linux/slab.h>
#include "darray.h"

int __bch2_darray_resize(darray_char *d, size_t element_size, size_t new_size, gfp_t gfp)
{
	if (new_size > d->size) {
		new_size = roundup_pow_of_two(new_size);

		void *data = kvmalloc_array(new_size, element_size, gfp);
		if (!data)
			return -ENOMEM;

		memcpy(data, d->data, d->size * element_size);
		if (d->data != d->preallocated)
			kvfree(d->data);
		d->data	= data;
		d->size = new_size;
	}

	return 0;
}
