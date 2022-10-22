/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FIFO_H
#define _BCACHEFS_FIFO_H

#include "util.h"

#define FIFO(type)							\
struct {								\
	size_t front, back, size, mask;					\
	type *data;							\
}

#define DECLARE_FIFO(type, name)	FIFO(type) name

#define fifo_buf_size(fifo)						\
	((fifo)->size							\
	 ? roundup_pow_of_two((fifo)->size) * sizeof((fifo)->data[0])	\
	 : 0)

#define init_fifo(fifo, _size, _gfp)					\
({									\
	(fifo)->front	= (fifo)->back = 0;				\
	(fifo)->size	= (_size);					\
	(fifo)->mask	= (fifo)->size					\
		? roundup_pow_of_two((fifo)->size) - 1			\
		: 0;							\
	(fifo)->data	= kvpmalloc(fifo_buf_size(fifo), (_gfp));	\
})

#define free_fifo(fifo)							\
do {									\
	kvpfree((fifo)->data, fifo_buf_size(fifo));			\
	(fifo)->data = NULL;						\
} while (0)

#define fifo_swap(l, r)							\
do {									\
	swap((l)->front, (r)->front);					\
	swap((l)->back, (r)->back);					\
	swap((l)->size, (r)->size);					\
	swap((l)->mask, (r)->mask);					\
	swap((l)->data, (r)->data);					\
} while (0)

#define fifo_move(dest, src)						\
do {									\
	typeof(*((dest)->data)) _t;					\
	while (!fifo_full(dest) &&					\
	       fifo_pop(src, _t))					\
		fifo_push(dest, _t);					\
} while (0)

#define fifo_used(fifo)		(((fifo)->back - (fifo)->front))
#define fifo_free(fifo)		((fifo)->size - fifo_used(fifo))

#define fifo_empty(fifo)	((fifo)->front == (fifo)->back)
#define fifo_full(fifo)		(fifo_used(fifo) == (fifo)->size)

#define fifo_peek_front(fifo)	((fifo)->data[(fifo)->front & (fifo)->mask])
#define fifo_peek_back(fifo)	((fifo)->data[((fifo)->back - 1) & (fifo)->mask])

#define fifo_entry_idx_abs(fifo, p)					\
	((((p) >= &fifo_peek_front(fifo)				\
	   ? (fifo)->front : (fifo)->back) & ~(fifo)->mask) +		\
	   (((p) - (fifo)->data)))

#define fifo_entry_idx(fifo, p)	(((p) - &fifo_peek_front(fifo)) & (fifo)->mask)
#define fifo_idx_entry(fifo, i)	((fifo)->data[((fifo)->front + (i)) & (fifo)->mask])

#define fifo_push_back_ref(f)						\
	(fifo_full((f)) ? NULL : &(f)->data[(f)->back++ & (f)->mask])

#define fifo_push_front_ref(f)						\
	(fifo_full((f)) ? NULL : &(f)->data[--(f)->front & (f)->mask])

#define fifo_push_back(fifo, new)					\
({									\
	typeof((fifo)->data) _r = fifo_push_back_ref(fifo);		\
	if (_r)								\
		*_r = (new);						\
	_r != NULL;							\
})

#define fifo_push_front(fifo, new)					\
({									\
	typeof((fifo)->data) _r = fifo_push_front_ref(fifo);		\
	if (_r)								\
		*_r = (new);						\
	_r != NULL;							\
})

#define fifo_pop_front(fifo, i)						\
({									\
	bool _r = !fifo_empty((fifo));					\
	if (_r)								\
		(i) = (fifo)->data[(fifo)->front++ & (fifo)->mask];	\
	_r;								\
})

#define fifo_pop_back(fifo, i)						\
({									\
	bool _r = !fifo_empty((fifo));					\
	if (_r)								\
		(i) = (fifo)->data[--(fifo)->back & (fifo)->mask];	\
	_r;								\
})

#define fifo_push_ref(fifo)	fifo_push_back_ref(fifo)
#define fifo_push(fifo, i)	fifo_push_back(fifo, (i))
#define fifo_pop(fifo, i)	fifo_pop_front(fifo, (i))
#define fifo_peek(fifo)		fifo_peek_front(fifo)

#define fifo_for_each_entry(_entry, _fifo, _iter)			\
	for (typecheck(typeof((_fifo)->front), _iter),			\
	     (_iter) = (_fifo)->front;					\
	     ((_iter != (_fifo)->back) &&				\
	      (_entry = (_fifo)->data[(_iter) & (_fifo)->mask], true));	\
	     (_iter)++)

#define fifo_for_each_entry_ptr(_ptr, _fifo, _iter)			\
	for (typecheck(typeof((_fifo)->front), _iter),			\
	     (_iter) = (_fifo)->front;					\
	     ((_iter != (_fifo)->back) &&				\
	      (_ptr = &(_fifo)->data[(_iter) & (_fifo)->mask], true));	\
	     (_iter)++)

#endif /* _BCACHEFS_FIFO_H */
