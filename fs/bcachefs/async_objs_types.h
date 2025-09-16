/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ASYNC_OBJS_TYPES_H
#define _BCACHEFS_ASYNC_OBJS_TYPES_H

#define BCH_ASYNC_OBJ_LISTS()						\
	x(promote)							\
	x(rbio)								\
	x(write_op)							\
	x(btree_read_bio)						\
	x(btree_write_bio)

enum bch_async_obj_lists {
#define x(n)		BCH_ASYNC_OBJ_LIST_##n,
	BCH_ASYNC_OBJ_LISTS()
#undef x
	BCH_ASYNC_OBJ_NR
};

struct async_obj_list {
	struct fast_list	list;
	void			(*obj_to_text)(struct printbuf *, void *);
	unsigned		idx;
};

#endif /* _BCACHEFS_ASYNC_OBJS_TYPES_H */
