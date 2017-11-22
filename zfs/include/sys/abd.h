/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#ifndef _ABD_H
#define	_ABD_H

#include <sys/isa_defs.h>
#include <sys/int_types.h>
#include <sys/debug.h>
#include <sys/refcount.h>
#ifdef _KERNEL
#include <linux/mm.h>
#include <linux/bio.h>
#include <sys/uio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum abd_flags {
	ABD_FLAG_LINEAR	= 1 << 0,	/* is buffer linear (or scattered)? */
	ABD_FLAG_OWNER	= 1 << 1,	/* does it own its data buffers? */
	ABD_FLAG_META	= 1 << 2,	/* does this represent FS metadata? */
	ABD_FLAG_MULTI_ZONE  = 1 << 3,	/* pages split over memory zones */
	ABD_FLAG_MULTI_CHUNK = 1 << 4	/* pages split over multiple chunks */
} abd_flags_t;

typedef struct abd {
	abd_flags_t	abd_flags;
	uint_t		abd_size;	/* excludes scattered abd_offset */
	struct abd	*abd_parent;
	refcount_t	abd_children;
	union {
		struct abd_scatter {
			uint_t		abd_offset;
			uint_t		abd_nents;
			struct scatterlist *abd_sgl;
		} abd_scatter;
		struct abd_linear {
			void		*abd_buf;
		} abd_linear;
	} abd_u;
} abd_t;

typedef int abd_iter_func_t(void *buf, size_t len, void *private);
typedef int abd_iter_func2_t(void *bufa, void *bufb, size_t len, void *private);

extern int zfs_abd_scatter_enabled;

static inline boolean_t
abd_is_linear(abd_t *abd)
{
	return ((abd->abd_flags & ABD_FLAG_LINEAR) != 0 ? B_TRUE : B_FALSE);
}

/*
 * Allocations and deallocations
 */

abd_t *abd_alloc(size_t, boolean_t);
abd_t *abd_alloc_linear(size_t, boolean_t);
abd_t *abd_alloc_for_io(size_t, boolean_t);
abd_t *abd_alloc_sametype(abd_t *, size_t);
void abd_free(abd_t *);
abd_t *abd_get_offset(abd_t *, size_t);
abd_t *abd_get_offset_size(abd_t *, size_t, size_t);
abd_t *abd_get_from_buf(void *, size_t);
void abd_put(abd_t *);

/*
 * Conversion to and from a normal buffer
 */

void *abd_to_buf(abd_t *);
void *abd_borrow_buf(abd_t *, size_t);
void *abd_borrow_buf_copy(abd_t *, size_t);
void abd_return_buf(abd_t *, void *, size_t);
void abd_return_buf_copy(abd_t *, void *, size_t);
void abd_take_ownership_of_buf(abd_t *, boolean_t);
void abd_release_ownership_of_buf(abd_t *);

/*
 * ABD operations
 */

int abd_iterate_func(abd_t *, size_t, size_t, abd_iter_func_t *, void *);
int abd_iterate_func2(abd_t *, abd_t *, size_t, size_t, size_t,
    abd_iter_func2_t *, void *);
void abd_copy_off(abd_t *, abd_t *, size_t, size_t, size_t);
void abd_copy_from_buf_off(abd_t *, const void *, size_t, size_t);
void abd_copy_to_buf_off(void *, abd_t *, size_t, size_t);
int abd_cmp(abd_t *, abd_t *);
int abd_cmp_buf_off(abd_t *, const void *, size_t, size_t);
void abd_zero_off(abd_t *, size_t, size_t);

#if defined(_KERNEL) && defined(HAVE_SPL)
unsigned int abd_scatter_bio_map_off(struct bio *, abd_t *, unsigned int,
		size_t);
unsigned long abd_nr_pages_off(abd_t *, unsigned int, size_t);
#endif

void abd_raidz_gen_iterate(abd_t **cabds, abd_t *dabd,
	ssize_t csize, ssize_t dsize, const unsigned parity,
	void (*func_raidz_gen)(void **, const void *, size_t, size_t));
void abd_raidz_rec_iterate(abd_t **cabds, abd_t **tabds,
	ssize_t tsize, const unsigned parity,
	void (*func_raidz_rec)(void **t, const size_t tsize, void **c,
	const unsigned *mul),
	const unsigned *mul);

/*
 * Wrappers for calls with offsets of 0
 */

static inline void
abd_copy(abd_t *dabd, abd_t *sabd, size_t size)
{
	abd_copy_off(dabd, sabd, 0, 0, size);
}

static inline void
abd_copy_from_buf(abd_t *abd, const void *buf, size_t size)
{
	abd_copy_from_buf_off(abd, buf, 0, size);
}

static inline void
abd_copy_to_buf(void* buf, abd_t *abd, size_t size)
{
	abd_copy_to_buf_off(buf, abd, 0, size);
}

static inline int
abd_cmp_buf(abd_t *abd, const void *buf, size_t size)
{
	return (abd_cmp_buf_off(abd, buf, 0, size));
}

static inline void
abd_zero(abd_t *abd, size_t size)
{
	abd_zero_off(abd, 0, size);
}

/*
 * Module lifecycle
 */

void abd_init(void);
void abd_fini(void);

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
