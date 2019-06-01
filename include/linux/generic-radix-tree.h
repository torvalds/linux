#ifndef _LINUX_GENERIC_RADIX_TREE_H
#define _LINUX_GENERIC_RADIX_TREE_H

/**
 * DOC: Generic radix trees/sparse arrays
 *
 * Very simple and minimalistic, supporting arbitrary size entries up to
 * PAGE_SIZE.
 *
 * A genradix is defined with the type it will store, like so:
 *
 * static GENRADIX(struct foo) foo_genradix;
 *
 * The main operations are:
 *
 * - genradix_init(radix) - initialize an empty genradix
 *
 * - genradix_free(radix) - free all memory owned by the genradix and
 *   reinitialize it
 *
 * - genradix_ptr(radix, idx) - gets a pointer to the entry at idx, returning
 *   NULL if that entry does not exist
 *
 * - genradix_ptr_alloc(radix, idx, gfp) - gets a pointer to an entry,
 *   allocating it if necessary
 *
 * - genradix_for_each(radix, iter, p) - iterate over each entry in a genradix
 *
 * The radix tree allocates one page of entries at a time, so entries may exist
 * that were never explicitly allocated - they will be initialized to all
 * zeroes.
 *
 * Internally, a genradix is just a radix tree of pages, and indexing works in
 * terms of byte offsets. The wrappers in this header file use sizeof on the
 * type the radix contains to calculate a byte offset from the index - see
 * __idx_to_offset.
 */

#include <asm/page.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/log2.h>

struct genradix_root;

struct __genradix {
	struct genradix_root __rcu	*root;
};

/*
 * NOTE: currently, sizeof(_type) must not be larger than PAGE_SIZE:
 */

#define __GENRADIX_INITIALIZER					\
	{							\
		.tree = {					\
			.root = NULL,				\
		}						\
	}

/*
 * We use a 0 size array to stash the type we're storing without taking any
 * space at runtime - then the various accessor macros can use typeof() to get
 * to it for casts/sizeof - we also force the alignment so that storing a type
 * with a ridiculous alignment doesn't blow up the alignment or size of the
 * genradix.
 */

#define GENRADIX(_type)						\
struct {							\
	struct __genradix	tree;				\
	_type			type[0] __aligned(1);		\
}

#define DEFINE_GENRADIX(_name, _type)				\
	GENRADIX(_type) _name = __GENRADIX_INITIALIZER

/**
 * genradix_init - initialize a genradix
 * @_radix:	genradix to initialize
 *
 * Does not fail
 */
#define genradix_init(_radix)					\
do {								\
	*(_radix) = (typeof(*_radix)) __GENRADIX_INITIALIZER;	\
} while (0)

void __genradix_free(struct __genradix *);

/**
 * genradix_free: free all memory owned by a genradix
 * @_radix: the genradix to free
 *
 * After freeing, @_radix will be reinitialized and empty
 */
#define genradix_free(_radix)	__genradix_free(&(_radix)->tree)

static inline size_t __idx_to_offset(size_t idx, size_t obj_size)
{
	if (__builtin_constant_p(obj_size))
		BUILD_BUG_ON(obj_size > PAGE_SIZE);
	else
		BUG_ON(obj_size > PAGE_SIZE);

	if (!is_power_of_2(obj_size)) {
		size_t objs_per_page = PAGE_SIZE / obj_size;

		return (idx / objs_per_page) * PAGE_SIZE +
			(idx % objs_per_page) * obj_size;
	} else {
		return idx * obj_size;
	}
}

#define __genradix_cast(_radix)		(typeof((_radix)->type[0]) *)
#define __genradix_obj_size(_radix)	sizeof((_radix)->type[0])
#define __genradix_idx_to_offset(_radix, _idx)			\
	__idx_to_offset(_idx, __genradix_obj_size(_radix))

void *__genradix_ptr(struct __genradix *, size_t);

/**
 * genradix_ptr - get a pointer to a genradix entry
 * @_radix:	genradix to access
 * @_idx:	index to fetch
 *
 * Returns a pointer to entry at @_idx, or NULL if that entry does not exist.
 */
#define genradix_ptr(_radix, _idx)				\
	(__genradix_cast(_radix)				\
	 __genradix_ptr(&(_radix)->tree,			\
			__genradix_idx_to_offset(_radix, _idx)))

void *__genradix_ptr_alloc(struct __genradix *, size_t, gfp_t);

/**
 * genradix_ptr_alloc - get a pointer to a genradix entry, allocating it
 *			if necessary
 * @_radix:	genradix to access
 * @_idx:	index to fetch
 * @_gfp:	gfp mask
 *
 * Returns a pointer to entry at @_idx, or NULL on allocation failure
 */
#define genradix_ptr_alloc(_radix, _idx, _gfp)			\
	(__genradix_cast(_radix)				\
	 __genradix_ptr_alloc(&(_radix)->tree,			\
			__genradix_idx_to_offset(_radix, _idx),	\
			_gfp))

struct genradix_iter {
	size_t			offset;
	size_t			pos;
};

/**
 * genradix_iter_init - initialize a genradix_iter
 * @_radix:	genradix that will be iterated over
 * @_idx:	index to start iterating from
 */
#define genradix_iter_init(_radix, _idx)			\
	((struct genradix_iter) {				\
		.pos	= (_idx),				\
		.offset	= __genradix_idx_to_offset((_radix), (_idx)),\
	})

void *__genradix_iter_peek(struct genradix_iter *, struct __genradix *, size_t);

/**
 * genradix_iter_peek - get first entry at or above iterator's current
 *			position
 * @_iter:	a genradix_iter
 * @_radix:	genradix being iterated over
 *
 * If no more entries exist at or above @_iter's current position, returns NULL
 */
#define genradix_iter_peek(_iter, _radix)			\
	(__genradix_cast(_radix)				\
	 __genradix_iter_peek(_iter, &(_radix)->tree,		\
			      PAGE_SIZE / __genradix_obj_size(_radix)))

static inline void __genradix_iter_advance(struct genradix_iter *iter,
					   size_t obj_size)
{
	iter->offset += obj_size;

	if (!is_power_of_2(obj_size) &&
	    (iter->offset & (PAGE_SIZE - 1)) + obj_size > PAGE_SIZE)
		iter->offset = round_up(iter->offset, PAGE_SIZE);

	iter->pos++;
}

#define genradix_iter_advance(_iter, _radix)			\
	__genradix_iter_advance(_iter, __genradix_obj_size(_radix))

#define genradix_for_each_from(_radix, _iter, _p, _start)	\
	for (_iter = genradix_iter_init(_radix, _start);	\
	     (_p = genradix_iter_peek(&_iter, _radix)) != NULL;	\
	     genradix_iter_advance(&_iter, _radix))

/**
 * genradix_for_each - iterate over entry in a genradix
 * @_radix:	genradix to iterate over
 * @_iter:	a genradix_iter to track current position
 * @_p:		pointer to genradix entry type
 *
 * On every iteration, @_p will point to the current entry, and @_iter.pos
 * will be the current entry's index.
 */
#define genradix_for_each(_radix, _iter, _p)			\
	genradix_for_each_from(_radix, _iter, _p, 0)

int __genradix_prealloc(struct __genradix *, size_t, gfp_t);

/**
 * genradix_prealloc - preallocate entries in a generic radix tree
 * @_radix:	genradix to preallocate
 * @_nr:	number of entries to preallocate
 * @_gfp:	gfp mask
 *
 * Returns 0 on success, -ENOMEM on failure
 */
#define genradix_prealloc(_radix, _nr, _gfp)			\
	 __genradix_prealloc(&(_radix)->tree,			\
			__genradix_idx_to_offset(_radix, _nr + 1),\
			_gfp)


#endif /* _LINUX_GENERIC_RADIX_TREE_H */
