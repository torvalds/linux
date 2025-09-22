/*	$OpenBSD: art.c,v 1.35 2025/08/15 09:15:55 jsg Exp $ */

/*
 * Copyright (c) 2015 Martin Pieuchot
 * Copyright (c) 2001 Yoichi Hariguchi
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Allotment Routing Table (ART).
 *
 * Yoichi Hariguchi paper can be found at:
 *	http://www.hariguchi.org/art/art.pdf
 *
 * This implementation is tweaked to minimise pointer traversal
 * during lookups by only accessing the heaps. It avoids following
 * pointers to or through the art_table structures.
 *
 * The heap is an array of art_heap_entry values which have different
 * meanings depending on their location. The majority of entries in
 * the heap are used as pointers to nodes (struct an_node, aka leaf
 * entries), or the next heap to traverse. These pointers are typed/
 * tagged by the low bit in their heap value, and must be masked
 * appropriately before use.
 *
 * The first two entries in the heap are not used by the search
 * algorithm, so they are used to store data associated with this
 * part of the heap. The first entry (heap[0]) stores a pointer to
 * an art_table struct associated with this heap. The second entry
 * (heap[1]) stores the node pointer which would be in the parent
 * heap if this table wasn't using it. This node pointer is also known
 * as the default entry/route for the current table in the ART.
 *
 * Nodes contain the exact information about the prefix they represent
 * in the ART, ie, the address and prefix length, and a pointer to
 * data associated with that prefix (an_value).
 *
 * The relationships between the separate data structures looks
 * like the following:
 *
 * +------------------+
 * |    struct art    |
 * +------------------+              NULL
 *   |                                ^
 *   | art_root                       | at_parent
 *   v                    heap[0]     |
 * +------------------+ ----------> +------------------+
 * |       heap       |             | struct art_table |
 * +------------------+ <---------- +------------------+
 *   |                    at_heap     ^
 *   | heap entry                     | at_parent
 *   v                    heap[0]     |
 * +------------------+ ----------> +------------------+
 * |       heap       |             | struct art_table |
 * +------------------+ <---------- +------------------+
 *   |                    at_heap
 *   | node entry
 *   v
 * +------------------+
 * | struct art_node  |
 * +------------------+
 *   |
 *   | an_value
 *   v
 * "user" data
 */

#ifndef _KERNEL
#include "kern_compat.h"
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/task.h>
#include <sys/socket.h>
#include <sys/smr.h>
#endif

#include <net/art.h>

static void		 art_allot(struct art_table *at, unsigned int,
			     art_heap_entry, art_heap_entry);
struct art_table	*art_table_get(struct art *, struct art_table *,
			     unsigned int);
struct art_table	*art_table_put(struct art  *, struct art_table *);
struct art_table	*art_table_ref(struct art *, struct art_table *);
int			 art_table_free(struct art *, struct art_table *);
void			 art_table_gc(void *);
void			 art_gc(void *);

static struct pool	 an_pool, at_pool, at_heap_4_pool, at_heap_8_pool;

static struct art_table	*art_table_gc_list = NULL;
static struct mutex	 art_table_gc_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);
static struct task	 art_table_gc_task =
			     TASK_INITIALIZER(art_table_gc, NULL);

static struct art_node	*art_node_gc_list = NULL;
static struct mutex	 art_node_gc_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);
static struct task	 art_node_gc_task = TASK_INITIALIZER(art_gc, NULL);

void
art_boot(void)
{
	pool_init(&an_pool, sizeof(struct art_node), 0, IPL_SOFTNET, 0,
	    "art_node", NULL);
	pool_init(&at_pool, sizeof(struct art_table), 0, IPL_SOFTNET, 0,
	    "art_table", NULL);
	pool_init(&at_heap_4_pool, AT_HEAPSIZE(4), 0, IPL_SOFTNET, 0,
	    "art_heap4", NULL);
	pool_init(&at_heap_8_pool, AT_HEAPSIZE(8), 0, IPL_SOFTNET, 0,
	    "art_heap8", &pool_allocator_single);
}

/*
 * Per routing table initialization API function.
 */

static const unsigned int art_plen32_levels[] = {
	8,  4, 4,  4, 4,  4, 4
};

static const unsigned int art_plen128_levels[] = {
	4, 4,  4, 4,  4, 4,  4, 4,  4, 4,  4, 4,  4, 4,  4, 4,
	4, 4,  4, 4,  4, 4,  4, 4,  4, 4,  4, 4,  4, 4,  4, 4
};

static const unsigned int art_plen20_levels[] = {
	4, 4, 4, 4, 4
};

void
art_init(struct art *art, unsigned int alen)
{
	const unsigned int *levels;
	unsigned int nlevels;
#ifdef DIAGNOSTIC
	unsigned int		 i;
	unsigned int		 bits = 0;
#endif

	switch (alen) {
	case 32:
		levels = art_plen32_levels;
		nlevels = nitems(art_plen32_levels);
		break;
	case 128:
		levels = art_plen128_levels;
		nlevels = nitems(art_plen128_levels);
		break;
	case 20:
		levels = art_plen20_levels;
		nlevels = nitems(art_plen20_levels);
		break;
	default:
		panic("no configuration for alen %u", alen);
		/* NOTREACHED */
	}

#ifdef DIAGNOSTIC
	for (i = 0; i < nlevels; i++)
		bits += levels[i];

	if (alen != bits)
		panic("sum of levels %u != address len %u", bits, alen);
#endif /* DIAGNOSTIC */

	art->art_root = 0;
	art->art_levels = levels;
	art->art_nlevels = nlevels;
	art->art_alen = alen;
}

struct art *
art_alloc(unsigned int alen)
{
	struct art		*art;

	art = malloc(sizeof(*art), M_RTABLE, M_NOWAIT|M_ZERO);
	if (art == NULL)
		return (NULL);

	art_init(art, alen);

	return (art);
}

/*
 * Return the base index of the part of ``addr'' and ``plen''
 * corresponding to the range covered by the table ``at''.
 *
 * In other words, this function take the multi-level (complete)
 * address ``addr'' and prefix length ``plen'' and return the
 * single level base index for the table ``at''.
 *
 * For example with an address size of 32bit divided into four
 * 8bit-long tables, there's a maximum of 4 base indexes if the
 * prefix length is > 24.
 */
static unsigned int __pure
art_bindex(unsigned int offset, unsigned int bits,
    const uint8_t *addr, unsigned int plen)
{
	unsigned int		boff, bend;
	uint32_t		k;

	KASSERT(plen >= offset);
	KASSERT(plen <= (offset + bits));

	/*
	 * We are only interested in the part of the prefix length
	 * corresponding to the range of this table.
	 */
	plen -= offset;

	/*
	 * Jump to the first byte of the address containing bits
	 * covered by this table.
	 */
	addr += (offset / 8);

	/* ``at'' covers the bit range between ``boff'' & ``bend''. */
	boff = (offset % 8);
	bend = (bits + boff);

	KASSERT(bend <= 32);

	if (bend > 24) {
		k = (addr[0] & ((1 << (8 - boff)) - 1)) << (bend - 8);
		k |= addr[1] << (bend - 16);
		k |= addr[2] << (bend - 24);
		k |= addr[3] >> (32 - bend);
	} else if (bend > 16) {
		k = (addr[0] & ((1 << (8 - boff)) - 1)) << (bend - 8);
		k |= addr[1] << (bend - 16);
		k |= addr[2] >> (24 - bend);
	} else if (bend > 8) {
		k = (addr[0] & ((1 << (8 - boff)) - 1)) << (bend - 8);
		k |= addr[1] >> (16 - bend);
	} else {
		k = (addr[0] >> (8 - bend)) & ((1 << bits) - 1);
	}

	/*
	 * Single level base index formula:
	 */
	return ((k >> (bits - plen)) + (1 << plen));
}

/*
 * (Non-perfect) lookup API function.
 *
 * Return the best existing match for a destination.
 */
struct art_node *
art_match(struct art *art, const void *addr)
{
	unsigned int		 offset = 0;
	unsigned int		 level = 0;
	art_heap_entry		*heap;
	art_heap_entry		 ahe, dahe = 0;
	struct art_node		*an;
	int			 j;

	SMR_ASSERT_CRITICAL();

	heap = SMR_PTR_GET(&art->art_root);
	if (heap == NULL)
		return (NULL);

	/*
	 * Iterate until we find a leaf.
	 */
	for (;;) {
		unsigned int bits = art->art_levels[level];
		unsigned int p = offset + bits;

		/*
		 * Remember the default route of each table we visit in case
		 * we do not find a better matching route.
		 */
		ahe = SMR_PTR_GET(&heap[ART_HEAP_IDX_DEFAULT]);
		if (ahe != 0)
			dahe = ahe;
		
		/* Do a single level route lookup. */
		j = art_bindex(offset, bits, addr, p);
		ahe = SMR_PTR_GET(&heap[j]);

		/* If this is a leaf (NULL is a leaf) we're done. */
		if (art_heap_entry_is_node(ahe))
			break;

		heap = art_heap_entry_to_heap(ahe);
		offset = p;
		level++;

		KASSERT(level < art->art_nlevels);
	}

	an = art_heap_entry_to_node(ahe);
	if (an != NULL)
		return (an);

	return art_heap_entry_to_node(dahe);
}

static int
art_node_check(const struct art_node *an,
    const uint8_t *addr, unsigned int plen)
{
	const uint32_t *wan = (const uint32_t *)an->an_addr;
	const uint32_t *waddr = (const uint32_t *)addr;
	unsigned int i = 0;
	unsigned int shift;

	if (an->an_plen != plen)
		return (0);

	while (plen >= 32) {
		if (wan[i] != waddr[i])
			return (0);

		i++;
		plen -= 32;
	}
	if (plen == 0)
		return (1);

	i *= 4;
	while (plen >= 8) {
		if (an->an_addr[i] != addr[i])
			return (0);
		
		i++;
		plen -= 8;
	}
	if (plen == 0)
		return (1);

	shift = 8 - plen;

	return ((an->an_addr[i] >> shift) == (addr[i] >> shift));
}

/*
 * Perfect lookup API function.
 *
 * Return a perfect match for a destination/prefix-length pair or NULL if
 * it does not exist.
 */
struct art_node *
art_lookup(struct art *art, const void *addr, unsigned int plen)
{
	unsigned int		 offset = 0;
	unsigned int		 bits, p;
	unsigned int		 level = 0;
	art_heap_entry		*heap;
	art_heap_entry		 ahe;
	struct art_node		*an;
	unsigned int		 i, j;

	KASSERT(plen <= art->art_alen);

	SMR_ASSERT_CRITICAL();

	heap = SMR_PTR_GET(&art->art_root);
	if (heap == NULL)
		return (NULL);

	/* Default route */
	if (plen == 0) {
		ahe = SMR_PTR_GET(&heap[ART_HEAP_IDX_DEFAULT]);
		return art_heap_entry_to_node(ahe);
	}

	/*
	 * If the prefix length is smaller than the sum of
	 * the stride length at this level the entry must
	 * be in the current table.
	 */
	for (;;) {
		bits = art->art_levels[level];
		p = offset + bits;
		if (plen <= p)
			break;

		/* Do a single level route lookup. */
		j = art_bindex(offset, bits, addr, p);
		ahe = SMR_PTR_GET(&heap[j]);

		/* A leaf is a match, but not a perfect one, or NULL */
		if (art_heap_entry_is_node(ahe))
			return (NULL);

		heap = art_heap_entry_to_heap(ahe);
		offset = p;
		level++;

		KASSERT(level < art->art_nlevels);
	}

	i = art_bindex(offset, bits, addr, plen);
	ahe = SMR_PTR_GET(&heap[i]);
	if (!art_heap_entry_is_node(ahe)) {
		heap = art_heap_entry_to_heap(ahe);
		ahe = SMR_PTR_GET(&heap[ART_HEAP_IDX_DEFAULT]);
	}

	/* Make sure we've got a perfect match */
	an = art_heap_entry_to_node(ahe);
	if (an != NULL && art_node_check(an, addr, plen))
		return (an);

	return (NULL);
}

int
art_is_empty(struct art *art)
{
	return (SMR_PTR_GET_LOCKED(&art->art_root) == NULL);
}

/*
 * Insertion API function.
 *
 * Insert the given node or return an existing one if a node with the
 * same destination/mask pair is already present.
 */
struct art_node *
art_insert(struct art *art, struct art_node *an)
{
	unsigned int		 p;
	art_heap_entry		 ahe, oahe, *ahep;
	art_heap_entry		*heap;
	struct art_node		*oan;
	struct art_table	*at;
	unsigned int		 i, j;

	KASSERT(an->an_plen <= art->art_alen);

	heap = SMR_PTR_GET_LOCKED(&art->art_root);
	if (heap == NULL) {
		at = art_table_get(art, NULL, -1);
		if (at == NULL)
			return (NULL);

		heap = at->at_heap;
		SMR_PTR_SET_LOCKED(&art->art_root, heap);
	} else
		at = art_heap_to_table(heap);

	/* Default route */
	if (an->an_plen == 0) {
		ahep = &heap[ART_HEAP_IDX_DEFAULT];
		oahe = SMR_PTR_GET_LOCKED(ahep);
		oan = art_heap_entry_to_node(oahe);
		if (oan != NULL)
			return (oan);

		art_table_ref(art, at);
		SMR_PTR_SET_LOCKED(ahep, art_node_to_heap_entry(an));
		return (an);
	}

	/*
	 * If the prefix length is smaller than the sum of
	 * the stride length at this level the entry must
	 * be in the current table.
	 */
	while ((p = at->at_offset + at->at_bits) < an->an_plen) {
		/* Do a single level route lookup. */
		j = art_bindex(at->at_offset, at->at_bits, an->an_addr, p);
		ahep = &heap[j];
		ahe = SMR_PTR_GET_LOCKED(ahep);

		/*
		 * If the node corresponding to the fringe index is
		 * a leaf we need to allocate a subtable.  The route
		 * entry of this node will then become the default
		 * route of the subtable.
		 */
		if (art_heap_entry_is_node(ahe)) {
			struct art_table *child = art_table_get(art, at, j);
			if (child == NULL)
				return (NULL);

			art_table_ref(art, at);

			at = child;
			heap = at->at_heap;
			SMR_PTR_SET_LOCKED(&heap[ART_HEAP_IDX_DEFAULT], ahe);
			SMR_PTR_SET_LOCKED(ahep, art_heap_to_heap_entry(heap));
		} else {
			heap = art_heap_entry_to_heap(ahe);
			at = art_heap_to_table(heap);
		}
	}

	i = art_bindex(at->at_offset, at->at_bits, an->an_addr, an->an_plen);
	ahep = &heap[i];
	oahe = SMR_PTR_GET_LOCKED(ahep);
	if (!art_heap_entry_is_node(oahe)) {
		heap = art_heap_entry_to_heap(oahe);
		ahep = &heap[ART_HEAP_IDX_DEFAULT];
		oahe = SMR_PTR_GET_LOCKED(ahep);
	}

	/* Check if there's an existing node */
	oan = art_heap_entry_to_node(oahe);
	if (oan != NULL && art_node_check(oan, an->an_addr, an->an_plen))
		return (oan);

	/*
	 * If the index `i' of the route that we are inserting is not
	 * a fringe index, we need to allot this new route pointer to
	 * all the corresponding fringe indices.
	 */
	art_table_ref(art, at);
	ahe = art_node_to_heap_entry(an);
	if (i < at->at_minfringe)
		art_allot(at, i, oahe, ahe);
	else
		SMR_PTR_SET_LOCKED(ahep, ahe);

	return (an);
}

/*
 * Deletion API function.
 */
struct art_node *
art_delete(struct art *art, const void *addr, unsigned int plen)
{
	unsigned int		 p;
	art_heap_entry		*heap;
	struct art_table	*at;
	art_heap_entry		 ahe, pahe, *ahep;
	struct art_node		*an;
	unsigned int		 i, j;

	KASSERT(plen <= art->art_alen);

	heap = SMR_PTR_GET_LOCKED(&art->art_root);
	if (heap == NULL)
		return (NULL);

	at = art_heap_to_table(heap);

	/* Default route */
	if (plen == 0) {
		ahep = &heap[ART_HEAP_IDX_DEFAULT];

		ahe = SMR_PTR_GET_LOCKED(ahep);
		an = art_heap_entry_to_node(ahe);
		if (an == NULL)
			return (NULL);

		SMR_PTR_SET_LOCKED(ahep, 0);
		art_table_free(art, at);

		return (an);
	}

	/*
	 * If the prefix length is smaller than the sum of
	 * the stride length at this level the entry must
	 * be in the current table.
	 */
	while ((p = at->at_offset + at->at_bits) < plen) {
		/* Do a single level route lookup. */
		j = art_bindex(at->at_offset, at->at_bits, addr, p);
		ahe = SMR_PTR_GET_LOCKED(&heap[j]);

		/* If this is a leaf, there is no route to delete. */
		if (art_heap_entry_is_node(ahe))
			return (NULL);

		heap = art_heap_entry_to_heap(ahe);
		at = art_heap_to_table(heap);
	}

	i = art_bindex(at->at_offset, at->at_bits, addr, plen);
	ahep = &heap[i];
	ahe = SMR_PTR_GET_LOCKED(ahep);
	if (!art_heap_entry_is_node(ahe)) {
		art_heap_entry *nheap = art_heap_entry_to_heap(ahe);
		ahep = &nheap[ART_HEAP_IDX_DEFAULT];
		ahe = SMR_PTR_GET_LOCKED(ahep);
	}

	an = art_heap_entry_to_node(ahe);
	if (an == NULL) {
		/* No route to delete */
		return (NULL);
	}

	/* Get the next most specific route for the index `i'. */
	j = i >> 1;
	pahe = (j > 1) ? SMR_PTR_GET_LOCKED(&heap[j]) : 0;

	/*
	 * If the index `i' of the route that we are removing is not
	 * a fringe index, we need to allot the next most specific
	 * route pointer to all the corresponding fringe indices.
	 */
	if (i < at->at_minfringe)
		art_allot(at, i, ahe, pahe);
	else
		SMR_PTR_SET_LOCKED(ahep, pahe);

	/* We have removed an entry from this table. */
	art_table_free(art, at);

	return (an);
}

struct art_table *
art_table_ref(struct art *art, struct art_table *at)
{
	at->at_refcnt++;
	return (at);
}

static inline int
art_table_rele(struct art_table *at)
{
	if (at == NULL)
		return (0);

	return (--at->at_refcnt == 0);
}

int
art_table_free(struct art *art, struct art_table *at)
{
	if (art_table_rele(at)) {
		/*
		 * Garbage collect this table and all its parents
		 * that are empty.
		 */
		do {
			at = art_table_put(art, at);
		} while (art_table_rele(at));

		return (1);
	}

	return (0);
}

/*
 * Iteration API function.
 */

static struct art_node *
art_iter_descend(struct art_iter *ai, art_heap_entry *heap,
    art_heap_entry pahe)
{
	struct art_table *at;
	art_heap_entry ahe;

	at = art_heap_to_table(heap);
	ai->ai_table = art_table_ref(ai->ai_art, at);

	/*
	 * Start looking at non-fringe nodes.
	 */
	ai->ai_j = 1;

	/*
	 * The default route (index 1) is processed by the
	 * parent table (where it belongs) otherwise it could
	 * be processed more than once.
	 */
	ai->ai_i = 2;

	/*
	 * Process the default route now.
	 */
	ahe = SMR_PTR_GET_LOCKED(&heap[ART_HEAP_IDX_DEFAULT]);
	if (ahe != 0 && ahe != pahe)
		return (art_heap_entry_to_node(ahe));

	/*
	 * Tell the caller to proceed with art_iter_next.
	 */
	return (NULL);
}

struct art_node *
art_iter_open(struct art *art, struct art_iter *ai)
{
	art_heap_entry *heap = SMR_PTR_GET(&art->art_root);
	struct art_node *an;

	ai->ai_art = art;

	if (heap == NULL) {
		/* empty, we're already done */
		ai->ai_table = NULL;
		return (NULL);
	}

	an = art_iter_descend(ai, heap, 0);
	if (an != NULL)
		return (an); /* default route */

	return (art_iter_next(ai));
}

struct art_node *
art_iter_next(struct art_iter *ai)
{
	struct art_table *at = ai->ai_table;
	art_heap_entry *heap = at->at_heap;
	art_heap_entry ahe, pahe;
	unsigned int i;

descend:
	/*
	 * Iterate non-fringe nodes in ``natural'' order.
	 */
	if  (ai->ai_j < at->at_minfringe) {
		for (;;) {
			while ((i = ai->ai_i) < at->at_minfringe) {
				ai->ai_i = i << 1;

				pahe = SMR_PTR_GET_LOCKED(&heap[i >> 1]);
				ahe = SMR_PTR_GET_LOCKED(&heap[i]);
				if (ahe != 0 && ahe != pahe)
					return (art_heap_entry_to_node(ahe));
			}

			ai->ai_j += 2;
			if (ai->ai_j < at->at_minfringe)
				ai->ai_i = ai->ai_j;
			else {
				/* Set up the fringe loop */
				ai->ai_i = at->at_minfringe;
				break;
			}
		}
	}

	/*
	 * Descendent tables are only found on fringe nodes, and
	 * they record where they were placed in their parent. This
	 * allows the iterator to know where to resume traversal when
	 * it ascends back to the parent table. By keeping the table
	 * refs when going down the tree, the topology is preserved
	 * even if all the nodes are removed.
	 */

	for (;;) {
		unsigned int maxfringe = at->at_minfringe << 1;
		struct art_table *parent;

		/*
		 * Iterate fringe nodes.
		 */
		while ((i = ai->ai_i) < maxfringe) {
			ai->ai_i = i + 1;

			pahe = SMR_PTR_GET_LOCKED(&heap[i >> 1]);
			ahe = SMR_PTR_GET_LOCKED(&heap[i]);
			if (art_heap_entry_is_node(ahe)) {
				if (ahe != 0 && ahe != pahe)
					return (art_heap_entry_to_node(ahe));
			} else {
				struct art_node *an;
 
				heap = art_heap_entry_to_heap(ahe);

				an = art_iter_descend(ai, heap, pahe);
				if (an != NULL) /* default route? */
					return (an);

				/* Start looping over the child table */
				at = art_heap_to_table(heap);
				goto descend;
			}
		}

		/*
		 * Ascend back up to the parent
		 */
		parent = at->at_parent;
		ai->ai_i = at->at_index + 1;
		art_table_free(ai->ai_art, at);

		ai->ai_table = parent;
		if (parent == NULL) {
			/* The root table has no parent */
			break;
		}

		at = parent;
		ai->ai_j = at->at_minfringe;
		heap = at->at_heap;
	}

	return (NULL);
}

void
art_iter_close(struct art_iter *ai)
{
	struct art_table *at, *parent;

	for (at = ai->ai_table; at != NULL; at = parent) {
		parent = at->at_parent;
		art_table_free(ai->ai_art, at);
	}

	ai->ai_table = NULL;
}

int
art_walk(struct art *art, int (*f)(struct art_node *, void *), void *arg)
{
	struct art_iter		 ai;
	struct art_node		*an;
	int			 error = 0;

	ART_FOREACH(an, art, &ai) {
		error = f(an, arg);
		if (error != 0) {
			art_iter_close(&ai);
			return (error);
		}
	}

	return (0);
}

/*
 * Create a table and use the given index to set its default route.
 *
 * Note:  This function does not modify the root or the parent.
 */
struct art_table *
art_table_get(struct art *art, struct art_table *parent, unsigned int j)
{
	struct art_table	*at;
	art_heap_entry		*heap;
	unsigned int		 level;
	unsigned int		 bits;

	KASSERT(j != ART_HEAP_IDX_TABLE && j != ART_HEAP_IDX_DEFAULT);
	KASSERT(parent != NULL || j == -1);

	level = (parent != NULL) ? parent->at_level + 1 : 0;
	KASSERT(level < art->art_nlevels);

	at = pool_get(&at_pool, PR_NOWAIT|PR_ZERO);
	if (at == NULL)
		return (NULL);

	bits = art->art_levels[level];
	switch (bits) {
	case 4:
		heap = pool_get(&at_heap_4_pool, PR_NOWAIT|PR_ZERO);
		break;
	case 8:
		heap = pool_get(&at_heap_8_pool, PR_NOWAIT|PR_ZERO);
		break;
	default:
		panic("incorrect stride length %u", bits);
	}

	if (heap == NULL) {
		pool_put(&at_pool, at);
		return (NULL);
	}

	SMR_PTR_SET_LOCKED(&heap[ART_HEAP_IDX_TABLE], (art_heap_entry)at);

	at->at_parent = parent;
	at->at_index = j;
	at->at_minfringe = (1 << bits);
	at->at_level = level;
	at->at_bits = bits;
	at->at_heap = heap;
	at->at_refcnt = 0;

	if (parent != NULL) {
		art_heap_entry ahe;

		at->at_offset = (parent->at_offset + parent->at_bits);

		ahe = SMR_PTR_GET_LOCKED(&parent->at_heap[j]);
		SMR_PTR_SET_LOCKED(&heap[ART_HEAP_IDX_DEFAULT], ahe);
	}

	return (at);
}

/*
 * Delete a table and use its index to restore its parent's default route.
 *
 * Note:  Modify its parent to unlink the table from it.
 */
struct art_table *
art_table_put(struct art *art, struct art_table *at)
{
	struct art_table	*parent = at->at_parent;
	unsigned int		 j = at->at_index;

	KASSERT(at->at_refcnt == 0);
	KASSERT(j != 0 && j != 1);

	if (parent != NULL) {
		art_heap_entry ahe;

		KASSERT(j != -1);
		KASSERT(at->at_level == parent->at_level + 1);
		KASSERT(parent->at_refcnt >= 1);

		/* Give the route back to its parent. */
		ahe = SMR_PTR_GET_LOCKED(&at->at_heap[ART_HEAP_IDX_DEFAULT]);
		SMR_PTR_SET_LOCKED(&parent->at_heap[j], ahe);
	} else {
		KASSERT(j == -1);
		KASSERT(at->at_level == 0);
		SMR_PTR_SET_LOCKED(&art->art_root, NULL);
	}

	mtx_enter(&art_table_gc_mtx);
	at->at_parent = art_table_gc_list;
	art_table_gc_list = at;
	mtx_leave(&art_table_gc_mtx);

	task_add(systqmp, &art_table_gc_task);

	return (parent);
}

void
art_table_gc(void *null)
{
	struct art_table *at, *next;

	mtx_enter(&art_table_gc_mtx);
	at = art_table_gc_list;
	art_table_gc_list = NULL;
	mtx_leave(&art_table_gc_mtx);

	smr_barrier();

	while (at != NULL) {
		next = at->at_parent;

		switch (at->at_bits) {
		case 4:
			pool_put(&at_heap_4_pool, at->at_heap);
			break;
		case 8:
			pool_put(&at_heap_8_pool, at->at_heap);
			break;
		default:
			panic("incorrect stride length %u", at->at_bits);
		}

		pool_put(&at_pool, at);

		at = next;
	}
}

/*
 * Substitute a node by another in the subtree whose root index is given.
 *
 * This function iterates on the table ``at'' at index ``i'' until no
 * more ``old'' node can be replaced by ``new''.
 *
 * This function was originally written by Don Knuth in CWEB. The
 * complicated ``goto''s are the result of expansion of the two
 * following recursions:
 *
 *	art_allot(at, i, old, new)
 *	{
 *		int k = i;
 *		if (at->at_heap[k] == old)
 *			at->at_heap[k] = new;
 *		if (k >= at->at_minfringe)
 *			 return;
 *		k <<= 1;
 *		art_allot(at, k, old, new);
 *		k++;
 *		art_allot(at, k, old, new);
 *	}
 */
static void
art_allot(struct art_table *at, unsigned int i,
    art_heap_entry oahe, art_heap_entry nahe)
{
	art_heap_entry		 ahe, *ahep;
	art_heap_entry		*heap;
	unsigned int		 k = i;

	KASSERT(i < at->at_minfringe);

	heap = at->at_heap;

again:
	k <<= 1;
	if (k < at->at_minfringe)
		goto nonfringe;

	/* Change fringe nodes. */
	for (;;) {
		ahep = &heap[k];
		ahe = SMR_PTR_GET_LOCKED(ahep);

		if (!art_heap_entry_is_node(ahe)) {
			art_heap_entry *child = art_heap_entry_to_heap(ahe);
			ahep = &child[ART_HEAP_IDX_DEFAULT];
			ahe = SMR_PTR_GET_LOCKED(ahep);
		}

		if (ahe == oahe)
			SMR_PTR_SET_LOCKED(ahep, nahe);

		if (k % 2)
			goto moveup;
		k++;
	}

nonfringe:
	ahe = SMR_PTR_GET_LOCKED(&heap[k]);
	if (ahe == oahe)
		goto again;
moveon:
	if (k % 2)
		goto moveup;
	k++;
	goto nonfringe;
moveup:
	k >>= 1;
	SMR_PTR_SET_LOCKED(&heap[k], nahe);

	/* Change non-fringe node. */
	if (k != i)
		goto moveon;
}

struct art_node *
art_get(const uint8_t *addr, unsigned int plen)
{
	struct art_node		*an;

	an = pool_get(&an_pool, PR_NOWAIT | PR_ZERO);
	if (an == NULL)
		return (NULL);

	art_node_init(an, addr, plen);

	return (an);
}

void
art_node_init(struct art_node *an, const uint8_t *addr, unsigned int plen)
{
	size_t len;

	len = roundup(plen, 8) / 8;
	KASSERT(len <= sizeof(an->an_addr));
	memcpy(an->an_addr, addr, len);
	an->an_plen = plen;
}

void
art_put(struct art_node *an)
{
	mtx_enter(&art_node_gc_mtx);
	an->an_gc = art_node_gc_list;
	art_node_gc_list = an;
	mtx_leave(&art_node_gc_mtx);

	task_add(systqmp, &art_node_gc_task);
}

void
art_gc(void *null)
{
	struct art_node		*an;

	mtx_enter(&art_node_gc_mtx);
	an = art_node_gc_list;
	art_node_gc_list = NULL;
	mtx_leave(&art_node_gc_mtx);

	smr_barrier();

	while (an != NULL) {
		struct art_node *next = an->an_gc;

		pool_put(&an_pool, an);

		an = next;
	}
}
