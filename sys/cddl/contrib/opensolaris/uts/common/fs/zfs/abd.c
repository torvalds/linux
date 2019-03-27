/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

/*
 * ARC buffer data (ABD).
 *
 * ABDs are an abstract data structure for the ARC which can use two
 * different ways of storing the underlying data:
 *
 * (a) Linear buffer. In this case, all the data in the ABD is stored in one
 *     contiguous buffer in memory (from a zio_[data_]buf_* kmem cache).
 *
 *         +-------------------+
 *         | ABD (linear)      |
 *         |   abd_flags = ... |
 *         |   abd_size = ...  |     +--------------------------------+
 *         |   abd_buf ------------->| raw buffer of size abd_size    |
 *         +-------------------+     +--------------------------------+
 *              no abd_chunks
 *
 * (b) Scattered buffer. In this case, the data in the ABD is split into
 *     equal-sized chunks (from the abd_chunk_cache kmem_cache), with pointers
 *     to the chunks recorded in an array at the end of the ABD structure.
 *
 *         +-------------------+
 *         | ABD (scattered)   |
 *         |   abd_flags = ... |
 *         |   abd_size = ...  |
 *         |   abd_offset = 0  |                           +-----------+
 *         |   abd_chunks[0] ----------------------------->| chunk 0   |
 *         |   abd_chunks[1] ---------------------+        +-----------+
 *         |   ...             |                  |        +-----------+
 *         |   abd_chunks[N-1] ---------+         +------->| chunk 1   |
 *         +-------------------+        |                  +-----------+
 *                                      |                      ...
 *                                      |                  +-----------+
 *                                      +----------------->| chunk N-1 |
 *                                                         +-----------+
 *
 * Using a large proportion of scattered ABDs decreases ARC fragmentation since
 * when we are at the limit of allocatable space, using equal-size chunks will
 * allow us to quickly reclaim enough space for a new large allocation (assuming
 * it is also scattered).
 *
 * In addition to directly allocating a linear or scattered ABD, it is also
 * possible to create an ABD by requesting the "sub-ABD" starting at an offset
 * within an existing ABD. In linear buffers this is simple (set abd_buf of
 * the new ABD to the starting point within the original raw buffer), but
 * scattered ABDs are a little more complex. The new ABD makes a copy of the
 * relevant abd_chunks pointers (but not the underlying data). However, to
 * provide arbitrary rather than only chunk-aligned starting offsets, it also
 * tracks an abd_offset field which represents the starting point of the data
 * within the first chunk in abd_chunks. For both linear and scattered ABDs,
 * creating an offset ABD marks the original ABD as the offset's parent, and the
 * original ABD's abd_children refcount is incremented. This data allows us to
 * ensure the root ABD isn't deleted before its children.
 *
 * Most consumers should never need to know what type of ABD they're using --
 * the ABD public API ensures that it's possible to transparently switch from
 * using a linear ABD to a scattered one when doing so would be beneficial.
 *
 * If you need to use the data within an ABD directly, if you know it's linear
 * (because you allocated it) you can use abd_to_buf() to access the underlying
 * raw buffer. Otherwise, you should use one of the abd_borrow_buf* functions
 * which will allocate a raw buffer if necessary. Use the abd_return_buf*
 * functions to return any raw buffers that are no longer necessary when you're
 * done using them.
 *
 * There are a variety of ABD APIs that implement basic buffer operations:
 * compare, copy, read, write, and fill with zeroes. If you need a custom
 * function which progressively accesses the whole ABD, use the abd_iterate_*
 * functions.
 */

#include <sys/abd.h>
#include <sys/param.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>

typedef struct abd_stats {
	kstat_named_t abdstat_struct_size;
	kstat_named_t abdstat_scatter_cnt;
	kstat_named_t abdstat_scatter_data_size;
	kstat_named_t abdstat_scatter_chunk_waste;
	kstat_named_t abdstat_linear_cnt;
	kstat_named_t abdstat_linear_data_size;
} abd_stats_t;

static abd_stats_t abd_stats = {
	/* Amount of memory occupied by all of the abd_t struct allocations */
	{ "struct_size",			KSTAT_DATA_UINT64 },
	/*
	 * The number of scatter ABDs which are currently allocated, excluding
	 * ABDs which don't own their data (for instance the ones which were
	 * allocated through abd_get_offset()).
	 */
	{ "scatter_cnt",			KSTAT_DATA_UINT64 },
	/* Amount of data stored in all scatter ABDs tracked by scatter_cnt */
	{ "scatter_data_size",			KSTAT_DATA_UINT64 },
	/*
	 * The amount of space wasted at the end of the last chunk across all
	 * scatter ABDs tracked by scatter_cnt.
	 */
	{ "scatter_chunk_waste",		KSTAT_DATA_UINT64 },
	/*
	 * The number of linear ABDs which are currently allocated, excluding
	 * ABDs which don't own their data (for instance the ones which were
	 * allocated through abd_get_offset() and abd_get_from_buf()). If an
	 * ABD takes ownership of its buf then it will become tracked.
	 */
	{ "linear_cnt",				KSTAT_DATA_UINT64 },
	/* Amount of data stored in all linear ABDs tracked by linear_cnt */
	{ "linear_data_size",			KSTAT_DATA_UINT64 },
};

#define	ABDSTAT(stat)		(abd_stats.stat.value.ui64)
#define	ABDSTAT_INCR(stat, val) \
	atomic_add_64(&abd_stats.stat.value.ui64, (val))
#define	ABDSTAT_BUMP(stat)	ABDSTAT_INCR(stat, 1)
#define	ABDSTAT_BUMPDOWN(stat)	ABDSTAT_INCR(stat, -1)

/*
 * It is possible to make all future ABDs be linear by setting this to B_FALSE.
 * Otherwise, ABDs are allocated scattered by default unless the caller uses
 * abd_alloc_linear().
 */
boolean_t zfs_abd_scatter_enabled = B_TRUE;

/*
 * The size of the chunks ABD allocates. Because the sizes allocated from the
 * kmem_cache can't change, this tunable can only be modified at boot. Changing
 * it at runtime would cause ABD iteration to work incorrectly for ABDs which
 * were allocated with the old size, so a safeguard has been put in place which
 * will cause the machine to panic if you change it and try to access the data
 * within a scattered ABD.
 */
size_t zfs_abd_chunk_size = 4096;

#if defined(__FreeBSD__) && defined(_KERNEL)
SYSCTL_DECL(_vfs_zfs);

SYSCTL_INT(_vfs_zfs, OID_AUTO, abd_scatter_enabled, CTLFLAG_RWTUN,
    &zfs_abd_scatter_enabled, 0, "Enable scattered ARC data buffers");
SYSCTL_ULONG(_vfs_zfs, OID_AUTO, abd_chunk_size, CTLFLAG_RDTUN,
    &zfs_abd_chunk_size, 0, "The size of the chunks ABD allocates");
#endif

#ifdef _KERNEL
extern vmem_t *zio_alloc_arena;
#endif

kmem_cache_t *abd_chunk_cache;
static kstat_t *abd_ksp;

extern inline boolean_t abd_is_linear(abd_t *abd);
extern inline void abd_copy(abd_t *dabd, abd_t *sabd, size_t size);
extern inline void abd_copy_from_buf(abd_t *abd, const void *buf, size_t size);
extern inline void abd_copy_to_buf(void* buf, abd_t *abd, size_t size);
extern inline int abd_cmp_buf(abd_t *abd, const void *buf, size_t size);
extern inline void abd_zero(abd_t *abd, size_t size);

static void *
abd_alloc_chunk()
{
	void *c = kmem_cache_alloc(abd_chunk_cache, KM_PUSHPAGE);
	ASSERT3P(c, !=, NULL);
	return (c);
}

static void
abd_free_chunk(void *c)
{
	kmem_cache_free(abd_chunk_cache, c);
}

void
abd_init(void)
{
#ifdef illumos
	vmem_t *data_alloc_arena = NULL;

#ifdef _KERNEL
	data_alloc_arena = zio_alloc_arena;
#endif

	/*
	 * Since ABD chunks do not appear in crash dumps, we pass KMC_NOTOUCH
	 * so that no allocator metadata is stored with the buffers.
	 */
	abd_chunk_cache = kmem_cache_create("abd_chunk", zfs_abd_chunk_size, 0,
	    NULL, NULL, NULL, NULL, data_alloc_arena, KMC_NOTOUCH);
#else
	abd_chunk_cache = kmem_cache_create("abd_chunk", zfs_abd_chunk_size, 0,
	    NULL, NULL, NULL, NULL, 0, KMC_NOTOUCH | KMC_NODEBUG);
#endif
	abd_ksp = kstat_create("zfs", 0, "abdstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (abd_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (abd_ksp != NULL) {
		abd_ksp->ks_data = &abd_stats;
		kstat_install(abd_ksp);
	}
}

void
abd_fini(void)
{
	if (abd_ksp != NULL) {
		kstat_delete(abd_ksp);
		abd_ksp = NULL;
	}

	kmem_cache_destroy(abd_chunk_cache);
	abd_chunk_cache = NULL;
}

static inline size_t
abd_chunkcnt_for_bytes(size_t size)
{
	return (P2ROUNDUP(size, zfs_abd_chunk_size) / zfs_abd_chunk_size);
}

static inline size_t
abd_scatter_chunkcnt(abd_t *abd)
{
	ASSERT(!abd_is_linear(abd));
	return (abd_chunkcnt_for_bytes(
	    abd->abd_u.abd_scatter.abd_offset + abd->abd_size));
}

static inline void
abd_verify(abd_t *abd)
{
	ASSERT3U(abd->abd_size, >, 0);
	ASSERT3U(abd->abd_size, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(abd->abd_flags, ==, abd->abd_flags & (ABD_FLAG_LINEAR |
	    ABD_FLAG_OWNER | ABD_FLAG_META));
	IMPLY(abd->abd_parent != NULL, !(abd->abd_flags & ABD_FLAG_OWNER));
	IMPLY(abd->abd_flags & ABD_FLAG_META, abd->abd_flags & ABD_FLAG_OWNER);
	if (abd_is_linear(abd)) {
		ASSERT3P(abd->abd_u.abd_linear.abd_buf, !=, NULL);
	} else {
		ASSERT3U(abd->abd_u.abd_scatter.abd_offset, <,
		    zfs_abd_chunk_size);
		size_t n = abd_scatter_chunkcnt(abd);
		for (int i = 0; i < n; i++) {
			ASSERT3P(
			    abd->abd_u.abd_scatter.abd_chunks[i], !=, NULL);
		}
	}
}

static inline abd_t *
abd_alloc_struct(size_t chunkcnt)
{
	size_t size = offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]);
	abd_t *abd = kmem_alloc(size, KM_PUSHPAGE);
	ASSERT3P(abd, !=, NULL);
	ABDSTAT_INCR(abdstat_struct_size, size);

	return (abd);
}

static inline void
abd_free_struct(abd_t *abd)
{
	size_t chunkcnt = abd_is_linear(abd) ? 0 : abd_scatter_chunkcnt(abd);
	int size = offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]);
	kmem_free(abd, size);
	ABDSTAT_INCR(abdstat_struct_size, -size);
}

/*
 * Allocate an ABD, along with its own underlying data buffers. Use this if you
 * don't care whether the ABD is linear or not.
 */
abd_t *
abd_alloc(size_t size, boolean_t is_metadata)
{
	if (!zfs_abd_scatter_enabled)
		return (abd_alloc_linear(size, is_metadata));

	VERIFY3U(size, <=, SPA_MAXBLOCKSIZE);

	size_t n = abd_chunkcnt_for_bytes(size);
	abd_t *abd = abd_alloc_struct(n);

	abd->abd_flags = ABD_FLAG_OWNER;
	if (is_metadata) {
		abd->abd_flags |= ABD_FLAG_META;
	}
	abd->abd_size = size;
	abd->abd_parent = NULL;
	refcount_create(&abd->abd_children);

	abd->abd_u.abd_scatter.abd_offset = 0;
	abd->abd_u.abd_scatter.abd_chunk_size = zfs_abd_chunk_size;

	for (int i = 0; i < n; i++) {
		void *c = abd_alloc_chunk();
		ASSERT3P(c, !=, NULL);
		abd->abd_u.abd_scatter.abd_chunks[i] = c;
	}

	ABDSTAT_BUMP(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, size);
	ABDSTAT_INCR(abdstat_scatter_chunk_waste,
	    n * zfs_abd_chunk_size - size);

	return (abd);
}

static void
abd_free_scatter(abd_t *abd)
{
	size_t n = abd_scatter_chunkcnt(abd);
	for (int i = 0; i < n; i++) {
		abd_free_chunk(abd->abd_u.abd_scatter.abd_chunks[i]);
	}

	refcount_destroy(&abd->abd_children);
	ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, -(int)abd->abd_size);
	ABDSTAT_INCR(abdstat_scatter_chunk_waste,
	    abd->abd_size - n * zfs_abd_chunk_size);

	abd_free_struct(abd);
}

/*
 * Allocate an ABD that must be linear, along with its own underlying data
 * buffer. Only use this when it would be very annoying to write your ABD
 * consumer with a scattered ABD.
 */
abd_t *
abd_alloc_linear(size_t size, boolean_t is_metadata)
{
	abd_t *abd = abd_alloc_struct(0);

	VERIFY3U(size, <=, SPA_MAXBLOCKSIZE);

	abd->abd_flags = ABD_FLAG_LINEAR | ABD_FLAG_OWNER;
	if (is_metadata) {
		abd->abd_flags |= ABD_FLAG_META;
	}
	abd->abd_size = size;
	abd->abd_parent = NULL;
	refcount_create(&abd->abd_children);

	if (is_metadata) {
		abd->abd_u.abd_linear.abd_buf = zio_buf_alloc(size);
	} else {
		abd->abd_u.abd_linear.abd_buf = zio_data_buf_alloc(size);
	}

	ABDSTAT_BUMP(abdstat_linear_cnt);
	ABDSTAT_INCR(abdstat_linear_data_size, size);

	return (abd);
}

static void
abd_free_linear(abd_t *abd)
{
	if (abd->abd_flags & ABD_FLAG_META) {
		zio_buf_free(abd->abd_u.abd_linear.abd_buf, abd->abd_size);
	} else {
		zio_data_buf_free(abd->abd_u.abd_linear.abd_buf, abd->abd_size);
	}

	refcount_destroy(&abd->abd_children);
	ABDSTAT_BUMPDOWN(abdstat_linear_cnt);
	ABDSTAT_INCR(abdstat_linear_data_size, -(int)abd->abd_size);

	abd_free_struct(abd);
}

/*
 * Free an ABD. Only use this on ABDs allocated with abd_alloc() or
 * abd_alloc_linear().
 */
void
abd_free(abd_t *abd)
{
	abd_verify(abd);
	ASSERT3P(abd->abd_parent, ==, NULL);
	ASSERT(abd->abd_flags & ABD_FLAG_OWNER);
	if (abd_is_linear(abd))
		abd_free_linear(abd);
	else
		abd_free_scatter(abd);
}

/*
 * Allocate an ABD of the same format (same metadata flag, same scatterize
 * setting) as another ABD.
 */
abd_t *
abd_alloc_sametype(abd_t *sabd, size_t size)
{
	boolean_t is_metadata = (sabd->abd_flags & ABD_FLAG_META) != 0;
	if (abd_is_linear(sabd)) {
		return (abd_alloc_linear(size, is_metadata));
	} else {
		return (abd_alloc(size, is_metadata));
	}
}

/*
 * If we're going to use this ABD for doing I/O using the block layer, the
 * consumer of the ABD data doesn't care if it's scattered or not, and we don't
 * plan to store this ABD in memory for a long period of time, we should
 * allocate the ABD type that requires the least data copying to do the I/O.
 *
 * Currently this is linear ABDs, however if ldi_strategy() can ever issue I/Os
 * using a scatter/gather list we should switch to that and replace this call
 * with vanilla abd_alloc().
 */
abd_t *
abd_alloc_for_io(size_t size, boolean_t is_metadata)
{
	return (abd_alloc_linear(size, is_metadata));
}

/*
 * Allocate a new ABD to point to offset off of sabd. It shares the underlying
 * buffer data with sabd. Use abd_put() to free. sabd must not be freed while
 * any derived ABDs exist.
 */
abd_t *
abd_get_offset(abd_t *sabd, size_t off)
{
	abd_t *abd;

	abd_verify(sabd);
	ASSERT3U(off, <=, sabd->abd_size);

	if (abd_is_linear(sabd)) {
		abd = abd_alloc_struct(0);

		/*
		 * Even if this buf is filesystem metadata, we only track that
		 * if we own the underlying data buffer, which is not true in
		 * this case. Therefore, we don't ever use ABD_FLAG_META here.
		 */
		abd->abd_flags = ABD_FLAG_LINEAR;

		abd->abd_u.abd_linear.abd_buf =
		    (char *)sabd->abd_u.abd_linear.abd_buf + off;
	} else {
		size_t new_offset = sabd->abd_u.abd_scatter.abd_offset + off;
		size_t chunkcnt = abd_scatter_chunkcnt(sabd) -
		    (new_offset / zfs_abd_chunk_size);

		abd = abd_alloc_struct(chunkcnt);

		/*
		 * Even if this buf is filesystem metadata, we only track that
		 * if we own the underlying data buffer, which is not true in
		 * this case. Therefore, we don't ever use ABD_FLAG_META here.
		 */
		abd->abd_flags = 0;

		abd->abd_u.abd_scatter.abd_offset =
		    new_offset % zfs_abd_chunk_size;
		abd->abd_u.abd_scatter.abd_chunk_size = zfs_abd_chunk_size;

		/* Copy the scatterlist starting at the correct offset */
		(void) memcpy(&abd->abd_u.abd_scatter.abd_chunks,
		    &sabd->abd_u.abd_scatter.abd_chunks[new_offset /
		    zfs_abd_chunk_size],
		    chunkcnt * sizeof (void *));
	}

	abd->abd_size = sabd->abd_size - off;
	abd->abd_parent = sabd;
	refcount_create(&abd->abd_children);
	(void) refcount_add_many(&sabd->abd_children, abd->abd_size, abd);

	return (abd);
}

/*
 * Allocate a linear ABD structure for buf. You must free this with abd_put()
 * since the resulting ABD doesn't own its own buffer.
 */
abd_t *
abd_get_from_buf(void *buf, size_t size)
{
	abd_t *abd = abd_alloc_struct(0);

	VERIFY3U(size, <=, SPA_MAXBLOCKSIZE);

	/*
	 * Even if this buf is filesystem metadata, we only track that if we
	 * own the underlying data buffer, which is not true in this case.
	 * Therefore, we don't ever use ABD_FLAG_META here.
	 */
	abd->abd_flags = ABD_FLAG_LINEAR;
	abd->abd_size = size;
	abd->abd_parent = NULL;
	refcount_create(&abd->abd_children);

	abd->abd_u.abd_linear.abd_buf = buf;

	return (abd);
}

/*
 * Free an ABD allocated from abd_get_offset() or abd_get_from_buf(). Will not
 * free the underlying scatterlist or buffer.
 */
void
abd_put(abd_t *abd)
{
	abd_verify(abd);
	ASSERT(!(abd->abd_flags & ABD_FLAG_OWNER));

	if (abd->abd_parent != NULL) {
		(void) refcount_remove_many(&abd->abd_parent->abd_children,
		    abd->abd_size, abd);
	}

	refcount_destroy(&abd->abd_children);
	abd_free_struct(abd);
}

/*
 * Get the raw buffer associated with a linear ABD.
 */
void *
abd_to_buf(abd_t *abd)
{
	ASSERT(abd_is_linear(abd));
	abd_verify(abd);
	return (abd->abd_u.abd_linear.abd_buf);
}

/*
 * Borrow a raw buffer from an ABD without copying the contents of the ABD
 * into the buffer. If the ABD is scattered, this will allocate a raw buffer
 * whose contents are undefined. To copy over the existing data in the ABD, use
 * abd_borrow_buf_copy() instead.
 */
void *
abd_borrow_buf(abd_t *abd, size_t n)
{
	void *buf;
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, n);
	if (abd_is_linear(abd)) {
		buf = abd_to_buf(abd);
	} else {
		buf = zio_buf_alloc(n);
	}
	(void) refcount_add_many(&abd->abd_children, n, buf);

	return (buf);
}

void *
abd_borrow_buf_copy(abd_t *abd, size_t n)
{
	void *buf = abd_borrow_buf(abd, n);
	if (!abd_is_linear(abd)) {
		abd_copy_to_buf(buf, abd, n);
	}
	return (buf);
}

/*
 * Return a borrowed raw buffer to an ABD. If the ABD is scattered, this will
 * not change the contents of the ABD and will ASSERT that you didn't modify
 * the buffer since it was borrowed. If you want any changes you made to buf to
 * be copied back to abd, use abd_return_buf_copy() instead.
 */
void
abd_return_buf(abd_t *abd, void *buf, size_t n)
{
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, n);
	if (abd_is_linear(abd)) {
		ASSERT3P(buf, ==, abd_to_buf(abd));
	} else {
		ASSERT0(abd_cmp_buf(abd, buf, n));
		zio_buf_free(buf, n);
	}
	(void) refcount_remove_many(&abd->abd_children, n, buf);
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t n)
{
	if (!abd_is_linear(abd)) {
		abd_copy_from_buf(abd, buf, n);
	}
	abd_return_buf(abd, buf, n);
}

/*
 * Give this ABD ownership of the buffer that it's storing. Can only be used on
 * linear ABDs which were allocated via abd_get_from_buf(), or ones allocated
 * with abd_alloc_linear() which subsequently released ownership of their buf
 * with abd_release_ownership_of_buf().
 */
void
abd_take_ownership_of_buf(abd_t *abd, boolean_t is_metadata)
{
	ASSERT(abd_is_linear(abd));
	ASSERT(!(abd->abd_flags & ABD_FLAG_OWNER));
	abd_verify(abd);

	abd->abd_flags |= ABD_FLAG_OWNER;
	if (is_metadata) {
		abd->abd_flags |= ABD_FLAG_META;
	}

	ABDSTAT_BUMP(abdstat_linear_cnt);
	ABDSTAT_INCR(abdstat_linear_data_size, abd->abd_size);
}

void
abd_release_ownership_of_buf(abd_t *abd)
{
	ASSERT(abd_is_linear(abd));
	ASSERT(abd->abd_flags & ABD_FLAG_OWNER);
	abd_verify(abd);

	abd->abd_flags &= ~ABD_FLAG_OWNER;
	/* Disable this flag since we no longer own the data buffer */
	abd->abd_flags &= ~ABD_FLAG_META;

	ABDSTAT_BUMPDOWN(abdstat_linear_cnt);
	ABDSTAT_INCR(abdstat_linear_data_size, -(int)abd->abd_size);
}

struct abd_iter {
	abd_t		*iter_abd;	/* ABD being iterated through */
	size_t		iter_pos;	/* position (relative to abd_offset) */
	void		*iter_mapaddr;	/* addr corresponding to iter_pos */
	size_t		iter_mapsize;	/* length of data valid at mapaddr */
};

static inline size_t
abd_iter_scatter_chunk_offset(struct abd_iter *aiter)
{
	ASSERT(!abd_is_linear(aiter->iter_abd));
	return ((aiter->iter_abd->abd_u.abd_scatter.abd_offset +
	    aiter->iter_pos) % zfs_abd_chunk_size);
}

static inline size_t
abd_iter_scatter_chunk_index(struct abd_iter *aiter)
{
	ASSERT(!abd_is_linear(aiter->iter_abd));
	return ((aiter->iter_abd->abd_u.abd_scatter.abd_offset +
	    aiter->iter_pos) / zfs_abd_chunk_size);
}

/*
 * Initialize the abd_iter.
 */
static void
abd_iter_init(struct abd_iter *aiter, abd_t *abd)
{
	abd_verify(abd);
	aiter->iter_abd = abd;
	aiter->iter_pos = 0;
	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
}

/*
 * Advance the iterator by a certain amount. Cannot be called when a chunk is
 * in use. This can be safely called when the aiter has already exhausted, in
 * which case this does nothing.
 */
static void
abd_iter_advance(struct abd_iter *aiter, size_t amount)
{
	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	/* There's nothing left to advance to, so do nothing */
	if (aiter->iter_pos == aiter->iter_abd->abd_size)
		return;

	aiter->iter_pos += amount;
}

/*
 * Map the current chunk into aiter. This can be safely called when the aiter
 * has already exhausted, in which case this does nothing.
 */
static void
abd_iter_map(struct abd_iter *aiter)
{
	void *paddr;
	size_t offset = 0;

	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	/* Panic if someone has changed zfs_abd_chunk_size */
	IMPLY(!abd_is_linear(aiter->iter_abd), zfs_abd_chunk_size ==
	    aiter->iter_abd->abd_u.abd_scatter.abd_chunk_size);

	/* There's nothing left to iterate over, so do nothing */
	if (aiter->iter_pos == aiter->iter_abd->abd_size)
		return;

	if (abd_is_linear(aiter->iter_abd)) {
		offset = aiter->iter_pos;
		aiter->iter_mapsize = aiter->iter_abd->abd_size - offset;
		paddr = aiter->iter_abd->abd_u.abd_linear.abd_buf;
	} else {
		size_t index = abd_iter_scatter_chunk_index(aiter);
		offset = abd_iter_scatter_chunk_offset(aiter);
		aiter->iter_mapsize = zfs_abd_chunk_size - offset;
		paddr = aiter->iter_abd->abd_u.abd_scatter.abd_chunks[index];
	}
	aiter->iter_mapaddr = (char *)paddr + offset;
}

/*
 * Unmap the current chunk from aiter. This can be safely called when the aiter
 * has already exhausted, in which case this does nothing.
 */
static void
abd_iter_unmap(struct abd_iter *aiter)
{
	/* There's nothing left to unmap, so do nothing */
	if (aiter->iter_pos == aiter->iter_abd->abd_size)
		return;

	ASSERT3P(aiter->iter_mapaddr, !=, NULL);
	ASSERT3U(aiter->iter_mapsize, >, 0);

	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
}

int
abd_iterate_func(abd_t *abd, size_t off, size_t size,
    abd_iter_func_t *func, void *private)
{
	int ret = 0;
	struct abd_iter aiter;

	abd_verify(abd);
	ASSERT3U(off + size, <=, abd->abd_size);

	abd_iter_init(&aiter, abd);
	abd_iter_advance(&aiter, off);

	while (size > 0) {
		abd_iter_map(&aiter);

		size_t len = MIN(aiter.iter_mapsize, size);
		ASSERT3U(len, >, 0);

		ret = func(aiter.iter_mapaddr, len, private);

		abd_iter_unmap(&aiter);

		if (ret != 0)
			break;

		size -= len;
		abd_iter_advance(&aiter, len);
	}

	return (ret);
}

struct buf_arg {
	void *arg_buf;
};

static int
abd_copy_to_buf_off_cb(void *buf, size_t size, void *private)
{
	struct buf_arg *ba_ptr = private;

	(void) memcpy(ba_ptr->arg_buf, buf, size);
	ba_ptr->arg_buf = (char *)ba_ptr->arg_buf + size;

	return (0);
}

/*
 * Copy abd to buf. (off is the offset in abd.)
 */
void
abd_copy_to_buf_off(void *buf, abd_t *abd, size_t off, size_t size)
{
	struct buf_arg ba_ptr = { buf };

	(void) abd_iterate_func(abd, off, size, abd_copy_to_buf_off_cb,
	    &ba_ptr);
}

static int
abd_cmp_buf_off_cb(void *buf, size_t size, void *private)
{
	int ret;
	struct buf_arg *ba_ptr = private;

	ret = memcmp(buf, ba_ptr->arg_buf, size);
	ba_ptr->arg_buf = (char *)ba_ptr->arg_buf + size;

	return (ret);
}

/*
 * Compare the contents of abd to buf. (off is the offset in abd.)
 */
int
abd_cmp_buf_off(abd_t *abd, const void *buf, size_t off, size_t size)
{
	struct buf_arg ba_ptr = { (void *) buf };

	return (abd_iterate_func(abd, off, size, abd_cmp_buf_off_cb, &ba_ptr));
}

static int
abd_copy_from_buf_off_cb(void *buf, size_t size, void *private)
{
	struct buf_arg *ba_ptr = private;

	(void) memcpy(buf, ba_ptr->arg_buf, size);
	ba_ptr->arg_buf = (char *)ba_ptr->arg_buf + size;

	return (0);
}

/*
 * Copy from buf to abd. (off is the offset in abd.)
 */
void
abd_copy_from_buf_off(abd_t *abd, const void *buf, size_t off, size_t size)
{
	struct buf_arg ba_ptr = { (void *) buf };

	(void) abd_iterate_func(abd, off, size, abd_copy_from_buf_off_cb,
	    &ba_ptr);
}

/*ARGSUSED*/
static int
abd_zero_off_cb(void *buf, size_t size, void *private)
{
	(void) memset(buf, 0, size);
	return (0);
}

/*
 * Zero out the abd from a particular offset to the end.
 */
void
abd_zero_off(abd_t *abd, size_t off, size_t size)
{
	(void) abd_iterate_func(abd, off, size, abd_zero_off_cb, NULL);
}

/*
 * Iterate over two ABDs and call func incrementally on the two ABDs' data in
 * equal-sized chunks (passed to func as raw buffers). func could be called many
 * times during this iteration.
 */
int
abd_iterate_func2(abd_t *dabd, abd_t *sabd, size_t doff, size_t soff,
    size_t size, abd_iter_func2_t *func, void *private)
{
	int ret = 0;
	struct abd_iter daiter, saiter;

	abd_verify(dabd);
	abd_verify(sabd);

	ASSERT3U(doff + size, <=, dabd->abd_size);
	ASSERT3U(soff + size, <=, sabd->abd_size);

	abd_iter_init(&daiter, dabd);
	abd_iter_init(&saiter, sabd);
	abd_iter_advance(&daiter, doff);
	abd_iter_advance(&saiter, soff);

	while (size > 0) {
		abd_iter_map(&daiter);
		abd_iter_map(&saiter);

		size_t dlen = MIN(daiter.iter_mapsize, size);
		size_t slen = MIN(saiter.iter_mapsize, size);
		size_t len = MIN(dlen, slen);
		ASSERT(dlen > 0 || slen > 0);

		ret = func(daiter.iter_mapaddr, saiter.iter_mapaddr, len,
		    private);

		abd_iter_unmap(&saiter);
		abd_iter_unmap(&daiter);

		if (ret != 0)
			break;

		size -= len;
		abd_iter_advance(&daiter, len);
		abd_iter_advance(&saiter, len);
	}

	return (ret);
}

/*ARGSUSED*/
static int
abd_copy_off_cb(void *dbuf, void *sbuf, size_t size, void *private)
{
	(void) memcpy(dbuf, sbuf, size);
	return (0);
}

/*
 * Copy from sabd to dabd starting from soff and doff.
 */
void
abd_copy_off(abd_t *dabd, abd_t *sabd, size_t doff, size_t soff, size_t size)
{
	(void) abd_iterate_func2(dabd, sabd, doff, soff, size,
	    abd_copy_off_cb, NULL);
}

/*ARGSUSED*/
static int
abd_cmp_cb(void *bufa, void *bufb, size_t size, void *private)
{
	return (memcmp(bufa, bufb, size));
}

/*
 * Compares the first size bytes of two ABDs.
 */
int
abd_cmp(abd_t *dabd, abd_t *sabd, size_t size)
{
	return (abd_iterate_func2(dabd, sabd, 0, 0, size, abd_cmp_cb, NULL));
}
