#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

/*
 * Nodemasks provide a bitmap suitable for representing the
 * set of Node's in a system, one bit position per Node number.
 *
 * See detailed comments in the file linux/bitmap.h describing the
 * data type on which these nodemasks are based.
 *
 * For details of nodemask_scnprintf() and nodemask_parse_user(),
 * see bitmap_scnprintf() and bitmap_parse_user() in lib/bitmap.c.
 * For details of nodelist_scnprintf() and nodelist_parse(), see
 * bitmap_scnlistprintf() and bitmap_parselist(), also in bitmap.c.
 * For details of node_remap(), see bitmap_bitremap in lib/bitmap.c.
 * For details of nodes_remap(), see bitmap_remap in lib/bitmap.c.
 *
 * The available nodemask operations are:
 *
 * void node_set(node, mask)		turn on bit 'node' in mask
 * void node_clear(node, mask)		turn off bit 'node' in mask
 * void nodes_setall(mask)		set all bits
 * void nodes_clear(mask)		clear all bits
 * int node_isset(node, mask)		true iff bit 'node' set in mask
 * int node_test_and_set(node, mask)	test and set bit 'node' in mask
 *
 * void nodes_and(dst, src1, src2)	dst = src1 & src2  [intersection]
 * void nodes_or(dst, src1, src2)	dst = src1 | src2  [union]
 * void nodes_xor(dst, src1, src2)	dst = src1 ^ src2
 * void nodes_andnot(dst, src1, src2)	dst = src1 & ~src2
 * void nodes_complement(dst, src)	dst = ~src
 *
 * int nodes_equal(mask1, mask2)	Does mask1 == mask2?
 * int nodes_intersects(mask1, mask2)	Do mask1 and mask2 intersect?
 * int nodes_subset(mask1, mask2)	Is mask1 a subset of mask2?
 * int nodes_empty(mask)		Is mask empty (no bits sets)?
 * int nodes_full(mask)			Is mask full (all bits sets)?
 * int nodes_weight(mask)		Hamming weight - number of set bits
 *
 * void nodes_shift_right(dst, src, n)	Shift right
 * void nodes_shift_left(dst, src, n)	Shift left
 *
 * int first_node(mask)			Number lowest set bit, or MAX_NUMNODES
 * int next_node(node, mask)		Next node past 'node', or MAX_NUMNODES
 * int first_unset_node(mask)		First node not set in mask, or 
 *					MAX_NUMNODES.
 *
 * nodemask_t nodemask_of_node(node)	Return nodemask with bit 'node' set
 * NODE_MASK_ALL			Initializer - all bits set
 * NODE_MASK_NONE			Initializer - no bits set
 * unsigned long *nodes_addr(mask)	Array of unsigned long's in mask
 *
 * int nodemask_scnprintf(buf, len, mask) Format nodemask for printing
 * int nodemask_parse_user(ubuf, ulen, mask)	Parse ascii string as nodemask
 * int nodelist_scnprintf(buf, len, mask) Format nodemask as list for printing
 * int nodelist_parse(buf, map)		Parse ascii string as nodelist
 * int node_remap(oldbit, old, new)	newbit = map(old, new)(oldbit)
 * int nodes_remap(dst, src, old, new)	*dst = map(old, new)(dst)
 *
 * for_each_node_mask(node, mask)	for-loop node over mask
 *
 * int num_online_nodes()		Number of online Nodes
 * int num_possible_nodes()		Number of all possible Nodes
 *
 * int node_online(node)		Is some node online?
 * int node_possible(node)		Is some node possible?
 *
 * int any_online_node(mask)		First online node in mask
 *
 * node_set_online(node)		set bit 'node' in node_online_map
 * node_set_offline(node)		clear bit 'node' in node_online_map
 *
 * for_each_node(node)			for-loop node over node_possible_map
 * for_each_online_node(node)		for-loop node over node_online_map
 *
 * Subtlety:
 * 1) The 'type-checked' form of node_isset() causes gcc (3.3.2, anyway)
 *    to generate slightly worse code.  So use a simple one-line #define
 *    for node_isset(), instead of wrapping an inline inside a macro, the
 *    way we do the other calls.
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/numa.h>

typedef struct { DECLARE_BITMAP(bits, MAX_NUMNODES); } nodemask_t;
extern nodemask_t _unused_nodemask_arg_;

#define node_set(node, dst) __node_set((node), &(dst))
static inline void __node_set(int node, volatile nodemask_t *dstp)
{
	set_bit(node, dstp->bits);
}

#define node_clear(node, dst) __node_clear((node), &(dst))
static inline void __node_clear(int node, volatile nodemask_t *dstp)
{
	clear_bit(node, dstp->bits);
}

#define nodes_setall(dst) __nodes_setall(&(dst), MAX_NUMNODES)
static inline void __nodes_setall(nodemask_t *dstp, int nbits)
{
	bitmap_fill(dstp->bits, nbits);
}

#define nodes_clear(dst) __nodes_clear(&(dst), MAX_NUMNODES)
static inline void __nodes_clear(nodemask_t *dstp, int nbits)
{
	bitmap_zero(dstp->bits, nbits);
}

/* No static inline type checking - see Subtlety (1) above. */
#define node_isset(node, nodemask) test_bit((node), (nodemask).bits)

#define node_test_and_set(node, nodemask) \
			__node_test_and_set((node), &(nodemask))
static inline int __node_test_and_set(int node, nodemask_t *addr)
{
	return test_and_set_bit(node, addr->bits);
}

#define nodes_and(dst, src1, src2) \
			__nodes_and(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_and(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, int nbits)
{
	bitmap_and(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_or(dst, src1, src2) \
			__nodes_or(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_or(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, int nbits)
{
	bitmap_or(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_xor(dst, src1, src2) \
			__nodes_xor(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_xor(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, int nbits)
{
	bitmap_xor(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_andnot(dst, src1, src2) \
			__nodes_andnot(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_andnot(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, int nbits)
{
	bitmap_andnot(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_complement(dst, src) \
			__nodes_complement(&(dst), &(src), MAX_NUMNODES)
static inline void __nodes_complement(nodemask_t *dstp,
					const nodemask_t *srcp, int nbits)
{
	bitmap_complement(dstp->bits, srcp->bits, nbits);
}

#define nodes_equal(src1, src2) \
			__nodes_equal(&(src1), &(src2), MAX_NUMNODES)
static inline int __nodes_equal(const nodemask_t *src1p,
					const nodemask_t *src2p, int nbits)
{
	return bitmap_equal(src1p->bits, src2p->bits, nbits);
}

#define nodes_intersects(src1, src2) \
			__nodes_intersects(&(src1), &(src2), MAX_NUMNODES)
static inline int __nodes_intersects(const nodemask_t *src1p,
					const nodemask_t *src2p, int nbits)
{
	return bitmap_intersects(src1p->bits, src2p->bits, nbits);
}

#define nodes_subset(src1, src2) \
			__nodes_subset(&(src1), &(src2), MAX_NUMNODES)
static inline int __nodes_subset(const nodemask_t *src1p,
					const nodemask_t *src2p, int nbits)
{
	return bitmap_subset(src1p->bits, src2p->bits, nbits);
}

#define nodes_empty(src) __nodes_empty(&(src), MAX_NUMNODES)
static inline int __nodes_empty(const nodemask_t *srcp, int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

#define nodes_full(nodemask) __nodes_full(&(nodemask), MAX_NUMNODES)
static inline int __nodes_full(const nodemask_t *srcp, int nbits)
{
	return bitmap_full(srcp->bits, nbits);
}

#define nodes_weight(nodemask) __nodes_weight(&(nodemask), MAX_NUMNODES)
static inline int __nodes_weight(const nodemask_t *srcp, int nbits)
{
	return bitmap_weight(srcp->bits, nbits);
}

#define nodes_shift_right(dst, src, n) \
			__nodes_shift_right(&(dst), &(src), (n), MAX_NUMNODES)
static inline void __nodes_shift_right(nodemask_t *dstp,
					const nodemask_t *srcp, int n, int nbits)
{
	bitmap_shift_right(dstp->bits, srcp->bits, n, nbits);
}

#define nodes_shift_left(dst, src, n) \
			__nodes_shift_left(&(dst), &(src), (n), MAX_NUMNODES)
static inline void __nodes_shift_left(nodemask_t *dstp,
					const nodemask_t *srcp, int n, int nbits)
{
	bitmap_shift_left(dstp->bits, srcp->bits, n, nbits);
}

/* FIXME: better would be to fix all architectures to never return
          > MAX_NUMNODES, then the silly min_ts could be dropped. */

#define first_node(src) __first_node(&(src))
static inline int __first_node(const nodemask_t *srcp)
{
	return min_t(int, MAX_NUMNODES, find_first_bit(srcp->bits, MAX_NUMNODES));
}

#define next_node(n, src) __next_node((n), &(src))
static inline int __next_node(int n, const nodemask_t *srcp)
{
	return min_t(int,MAX_NUMNODES,find_next_bit(srcp->bits, MAX_NUMNODES, n+1));
}

#define nodemask_of_node(node)						\
({									\
	typeof(_unused_nodemask_arg_) m;				\
	if (sizeof(m) == sizeof(unsigned long)) {			\
		m.bits[0] = 1UL<<(node);				\
	} else {							\
		nodes_clear(m);						\
		node_set((node), m);					\
	}								\
	m;								\
})

#define first_unset_node(mask) __first_unset_node(&(mask))
static inline int __first_unset_node(const nodemask_t *maskp)
{
	return min_t(int,MAX_NUMNODES,
			find_first_zero_bit(maskp->bits, MAX_NUMNODES));
}

#define NODE_MASK_LAST_WORD BITMAP_LAST_WORD_MASK(MAX_NUMNODES)

#if MAX_NUMNODES <= BITS_PER_LONG

#define NODE_MASK_ALL							\
((nodemask_t) { {							\
	[BITS_TO_LONGS(MAX_NUMNODES)-1] = NODE_MASK_LAST_WORD		\
} })

#else

#define NODE_MASK_ALL							\
((nodemask_t) { {							\
	[0 ... BITS_TO_LONGS(MAX_NUMNODES)-2] = ~0UL,			\
	[BITS_TO_LONGS(MAX_NUMNODES)-1] = NODE_MASK_LAST_WORD		\
} })

#endif

#define NODE_MASK_NONE							\
((nodemask_t) { {							\
	[0 ... BITS_TO_LONGS(MAX_NUMNODES)-1] =  0UL			\
} })

#define nodes_addr(src) ((src).bits)

#define nodemask_scnprintf(buf, len, src) \
			__nodemask_scnprintf((buf), (len), &(src), MAX_NUMNODES)
static inline int __nodemask_scnprintf(char *buf, int len,
					const nodemask_t *srcp, int nbits)
{
	return bitmap_scnprintf(buf, len, srcp->bits, nbits);
}

#define nodemask_parse_user(ubuf, ulen, dst) \
		__nodemask_parse_user((ubuf), (ulen), &(dst), MAX_NUMNODES)
static inline int __nodemask_parse_user(const char __user *buf, int len,
					nodemask_t *dstp, int nbits)
{
	return bitmap_parse_user(buf, len, dstp->bits, nbits);
}

#define nodelist_scnprintf(buf, len, src) \
			__nodelist_scnprintf((buf), (len), &(src), MAX_NUMNODES)
static inline int __nodelist_scnprintf(char *buf, int len,
					const nodemask_t *srcp, int nbits)
{
	return bitmap_scnlistprintf(buf, len, srcp->bits, nbits);
}

#define nodelist_parse(buf, dst) __nodelist_parse((buf), &(dst), MAX_NUMNODES)
static inline int __nodelist_parse(const char *buf, nodemask_t *dstp, int nbits)
{
	return bitmap_parselist(buf, dstp->bits, nbits);
}

#define node_remap(oldbit, old, new) \
		__node_remap((oldbit), &(old), &(new), MAX_NUMNODES)
static inline int __node_remap(int oldbit,
		const nodemask_t *oldp, const nodemask_t *newp, int nbits)
{
	return bitmap_bitremap(oldbit, oldp->bits, newp->bits, nbits);
}

#define nodes_remap(dst, src, old, new) \
		__nodes_remap(&(dst), &(src), &(old), &(new), MAX_NUMNODES)
static inline void __nodes_remap(nodemask_t *dstp, const nodemask_t *srcp,
		const nodemask_t *oldp, const nodemask_t *newp, int nbits)
{
	bitmap_remap(dstp->bits, srcp->bits, oldp->bits, newp->bits, nbits);
}

#if MAX_NUMNODES > 1
#define for_each_node_mask(node, mask)			\
	for ((node) = first_node(mask);			\
		(node) < MAX_NUMNODES;			\
		(node) = next_node((node), (mask)))
#else /* MAX_NUMNODES == 1 */
#define for_each_node_mask(node, mask)			\
	if (!nodes_empty(mask))				\
		for ((node) = 0; (node) < 1; (node)++)
#endif /* MAX_NUMNODES */

/*
 * The following particular system nodemasks and operations
 * on them manage all possible and online nodes.
 */

extern nodemask_t node_online_map;
extern nodemask_t node_possible_map;

#if MAX_NUMNODES > 1
#define num_online_nodes()	nodes_weight(node_online_map)
#define num_possible_nodes()	nodes_weight(node_possible_map)
#define node_online(node)	node_isset((node), node_online_map)
#define node_possible(node)	node_isset((node), node_possible_map)
#define first_online_node	first_node(node_online_map)
#define next_online_node(nid)	next_node((nid), node_online_map)
int highest_possible_node_id(void);
#else
#define num_online_nodes()	1
#define num_possible_nodes()	1
#define node_online(node)	((node) == 0)
#define node_possible(node)	((node) == 0)
#define first_online_node	0
#define next_online_node(nid)	(MAX_NUMNODES)
#define highest_possible_node_id()	0
#endif

#define any_online_node(mask)			\
({						\
	int node;				\
	for_each_node_mask(node, (mask))	\
		if (node_online(node))		\
			break;			\
	node;					\
})

#define node_set_online(node)	   set_bit((node), node_online_map.bits)
#define node_set_offline(node)	   clear_bit((node), node_online_map.bits)

#define for_each_node(node)	   for_each_node_mask((node), node_possible_map)
#define for_each_online_node(node) for_each_node_mask((node), node_online_map)

#endif /* __LINUX_NODEMASK_H */
