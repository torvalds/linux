/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DARRAY_H
#define _BCACHEFS_DARRAY_H

/*
 * Dynamic arrays:
 *
 * Inspired by CCAN's darray
 */

#include <linux/slab.h>

#define DARRAY_PREALLOCATED(_type, _nr)					\
struct {								\
	size_t nr, size;						\
	_type *data;							\
	_type preallocated[_nr];					\
}

#define DARRAY(_type) DARRAY_PREALLOCATED(_type, 0)

typedef DARRAY(char)	darray_char;
typedef DARRAY(char *) darray_str;

int __bch2_darray_resize_noprof(darray_char *, size_t, size_t, gfp_t);

#define __bch2_darray_resize(...)	alloc_hooks(__bch2_darray_resize_noprof(__VA_ARGS__))

#define __darray_resize(_d, _element_size, _new_size, _gfp)		\
	(unlikely((_new_size) > (_d)->size)				\
	 ? __bch2_darray_resize((_d), (_element_size), (_new_size), (_gfp))\
	 : 0)

#define darray_resize_gfp(_d, _new_size, _gfp)				\
	__darray_resize((darray_char *) (_d), sizeof((_d)->data[0]), (_new_size), _gfp)

#define darray_resize(_d, _new_size)					\
	darray_resize_gfp(_d, _new_size, GFP_KERNEL)

#define darray_make_room_gfp(_d, _more, _gfp)				\
	darray_resize_gfp((_d), (_d)->nr + (_more), _gfp)

#define darray_make_room(_d, _more)					\
	darray_make_room_gfp(_d, _more, GFP_KERNEL)

#define darray_room(_d)		((_d).size - (_d).nr)

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

#define darray_insert_item(_d, pos, _item)				\
({									\
	size_t _pos = (pos);						\
	int _ret = darray_make_room((_d), 1);				\
									\
	if (!_ret)							\
		array_insert_item((_d)->data, (_d)->nr, _pos, (_item));	\
	_ret;								\
})

#define darray_remove_item(_d, _pos)					\
	array_remove_item((_d)->data, (_d)->nr, (_pos) - (_d)->data)

#define __darray_for_each(_d, _i)						\
	for ((_i) = (_d).data; _i < (_d).data + (_d).nr; _i++)

#define darray_for_each(_d, _i)						\
	for (typeof(&(_d).data[0]) _i = (_d).data; _i < (_d).data + (_d).nr; _i++)

#define darray_for_each_reverse(_d, _i)					\
	for (typeof(&(_d).data[0]) _i = (_d).data + (_d).nr - 1; _i >= (_d).data; --_i)

#define darray_init(_d)							\
do {									\
	(_d)->nr = 0;							\
	(_d)->size = ARRAY_SIZE((_d)->preallocated);			\
	(_d)->data = (_d)->size ? (_d)->preallocated : NULL;		\
} while (0)

#define darray_exit(_d)							\
do {									\
	if (!ARRAY_SIZE((_d)->preallocated) ||				\
	    (_d)->data != (_d)->preallocated)				\
		kvfree((_d)->data);					\
	darray_init(_d);						\
} while (0)

#endif /* _BCACHEFS_DARRAY_H */
