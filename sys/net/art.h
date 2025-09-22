/* $OpenBSD: art.h,v 1.28 2025/07/10 05:28:13 dlg Exp $ */

/*
 * Copyright (c) 2015 Martin Pieuchot
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

#ifndef _NET_ART_H_
#define _NET_ART_H_

/*
 * Allotment Routing Table (ART)
 *
 * Yoichi Hariguchi paper can be found at:
 *      http://www.hariguchi.org/art/art.pdf
 *
 * Locking:
 *
 * Modification (ie, art_insert or art_delete) and iteration
 * (art_iter_next, etc) over the ART must be serialised by the caller.
 * Lookups (ie, art_match and art_lookup) run within an SMR critical
 * section.
 *
 * Iteration requires serialisation as it manipulates the reference
 * counts on tables as it traverses the tree. The iterator maintains
 * these references until it runs out of entries. This allows code
 * iterating over the ART to release locks in between calls to
 * art_iter_open and art_iter_next. The references may be dropped
 * early with art_iter_close.
 *
 * Note, the iterator does not hold a reference to the art_node
 * structure or the data hanging off the an_value pointer, they must
 * be accounted for separately or their use must be serialised with
 * art_delete.
 */

typedef uintptr_t		 art_heap_entry;

/*
 * Root of the ART, equivalent to the radix head.
 */

struct art {
	art_heap_entry		*art_root;
	const unsigned int	*art_levels;
	unsigned int		 art_nlevels;
	unsigned int		 art_alen;
};

/*
 * Allotment Table.
 */
struct art_table {
	art_heap_entry		*at_heap;
	struct art_table	*at_parent;	/* Parent table */

	unsigned int		 at_index;	/* Index in the parent table */
	unsigned int		 at_minfringe;	/* Index that fringe begins */

	unsigned int		 at_level;	/* Level of the table */
	unsigned int		 at_bits;	/* Stride length of the table */
	unsigned int		 at_offset;	/* Sum of parents' stride len */

	unsigned int		 at_refcnt;
};

#define ART_HEAP_IDX_TABLE	0
#define ART_HEAP_IDX_DEFAULT	1

#define AT_HEAPSIZE(bits)	((1 << ((bits) + 1)) * sizeof(art_heap_entry))

/*
 * A node is the internal representation of a route entry.
 */
struct art_node {
	void			*an_value;
	union {
		struct art_node		*an__gc;
		uint8_t			 an__addr[16];
	}			 an__u;
#define an_gc			 an__u.an__gc
#define an_addr			 an__u.an__addr
	unsigned int		 an_plen;
};

static inline struct art_table *
art_heap_to_table(art_heap_entry *heap)
{
	return ((struct art_table *)heap[ART_HEAP_IDX_TABLE]);
}

static inline int
art_heap_entry_is_node(art_heap_entry ahe)
{
	return ((ahe & 1UL) == 0);
}

static inline struct art_node *
art_heap_entry_to_node(art_heap_entry ahe)
{
	return ((struct art_node *)ahe);
}

static inline art_heap_entry *
art_heap_entry_to_heap(art_heap_entry ahe)
{
	return ((art_heap_entry *)(ahe & ~1UL));
}

static inline art_heap_entry
art_node_to_heap_entry(struct art_node *an)
{
	return ((art_heap_entry)an);
}

static inline art_heap_entry
art_heap_to_heap_entry(art_heap_entry *heap)
{
	return ((art_heap_entry)heap | 1UL);
}

#ifdef _KERNEL
void		 art_boot(void);
struct art	*art_alloc(unsigned int);
void		 art_init(struct art *, unsigned int);
struct art_node *art_insert(struct art *, struct art_node *);
struct art_node *art_delete(struct art *, const void *, unsigned int);
struct art_node *art_match(struct art *, const void *);
struct art_node *art_lookup(struct art *, const void *, unsigned int);
int		 art_is_empty(struct art *);

struct art_node	*art_get(const uint8_t *, unsigned int);
void		 art_node_init(struct art_node *,
		     const uint8_t *, unsigned int);
void		 art_put(struct art_node *);

struct art_iter {
	struct art		*ai_art;
	struct art_table	*ai_table;
	unsigned int		 ai_j;
	unsigned int		 ai_i;
};

struct art_node	*art_iter_open(struct art *, struct art_iter *);
struct art_node	*art_iter_next(struct art_iter *);
void		 art_iter_close(struct art_iter *);

#define ART_FOREACH(_an, _art, _ai)					\
	for ((_an) = art_iter_open((_art), (_ai));			\
	     (_an) != NULL;						\
	     (_an) = art_iter_next((_ai)))

int		 art_walk(struct art *,
		     int (*)(struct art_node *, void *), void *);

#endif /* _KERNEL */

#endif /* _NET_ART_H_ */
