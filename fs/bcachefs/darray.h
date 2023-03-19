/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DARRAY_H
#define _BCACHEFS_DARRAY_H

/*
 * Dynamic arrays:
 *
 * Inspired by CCAN's darray
 */

#include "util.h"
#include <linux/slab.h>

#define DARRAY(type)							\
struct {								\
	size_t nr, size;						\
	type *data;							\
}

typedef DARRAY(void) darray_void;

static inline int __darray_make_room(darray_void *d, size_t t_size, size_t more, gfp_t gfp)
{
	if (d->nr + more > d->size) {
		size_t new_size = roundup_pow_of_two(d->nr + more);
		void *data = krealloc_array(d->data, new_size, t_size, gfp);

		if (!data)
			return -ENOMEM;

		d->data	= data;
		d->size = new_size;
	}

	return 0;
}

#define darray_make_room_gfp(_d, _more, _gfp)				\
	__darray_make_room((darray_void *) (_d), sizeof((_d)->data[0]), (_more), _gfp)

#define darray_make_room(_d, _more)					\
	darray_make_room_gfp(_d, _more, GFP_KERNEL)

#define darray_top(_d)		((_d).data[(_d).nr])

#define darray_push_gfp(_d, _item, _gfp)				\
({									\
	int _ret = darray_make_room_gfp((_d), 1, _gfp);			\
									\
	if (!_ret)							\
		(_d)->data[(_d)->nr++] = (_item);			\
	_ret;								\
})

#define darray_push(_d, _item)	darray_push_gfp(_d, _item, GFP_KERNEL)

#define darray_pop(_d)		((_d)->data[--(_d)->nr])

#define darray_first(_d)	((_d).data[0])
#define darray_last(_d)		((_d).data[(_d).nr - 1])

#define darray_insert_item(_d, _pos, _item)				\
({									\
	size_t pos = (_pos);						\
	int _ret = darray_make_room((_d), 1);				\
									\
	if (!_ret)							\
		array_insert_item((_d)->data, (_d)->nr, pos, (_item));	\
	_ret;								\
})

#define darray_for_each(_d, _i)						\
	for (_i = (_d).data; _i < (_d).data + (_d).nr; _i++)

#define darray_init(_d)							\
do {									\
	(_d)->data = NULL;						\
	(_d)->nr = (_d)->size = 0;					\
} while (0)

#define darray_exit(_d)							\
do {									\
	kfree((_d)->data);						\
	darray_init(_d);						\
} while (0)

#endif /* _BCACHEFS_DARRAY_H */
