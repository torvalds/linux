/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

/*
 * Nodemasks provide a bitmap suitable for representing the
 * set of Node's in a system, one bit position per Node number.
 *
 * See detailed comments in the file linux/bitmap.h describing the
 * data type on which these nodemasks are based.
 *
 * For details of nodemask_parse_user(), see bitmap_parse_user() in
 * lib/bitmap.c.  For details of nodelist_parse(), see bitmap_parselist(),
 * also in bitmap.c.  For details of node_remap(), see bitmap_bitremap in
 * lib/bitmap.c.  For details of nodes_remap(), see bitmap_remap in
 * lib/bitmap.c.  For details of nodes_onto(), see bitmap_onto in
 * lib/bitmap.c.  For details of nodes_fold(), see bitmap_fold in
 * lib/bitmap.c.
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
 * unsigned int first_node(mask)	Number lowest set bit, or MAX_NUMNODES
 * unsigend int next_node(node, mask)	Next node past 'node', or MAX_NUMNODES
 * unsigned int next_node_in(node, mask) Next node past 'node', or wrap to first,
 *					or MAX_NUMNODES
 * unsigned int first_unset_node(mask)	First node not set in mask, or
 *					MAX_NUMNODES
 *
 * nodemask_t nodemask_of_node(node)	Return nodemask with bit 'node' set
 * NODE_MASK_ALL			Initializer - all bits set
 * NODE_MASK_NONE			Initializer - no bits set
 * unsigned long *nodes_addr(mask)	Array of unsigned long's in mask
 *
 * int nodemask_parse_user(ubuf, ulen, mask)	Parse ascii string as nodemask
 * int nodelist_parse(buf, map)		Parse ascii string as nodelist
 * int node_remap(oldbit, old, new)	newbit = map(old, new)(oldbit)
 * void nodes_remap(dst, src, old, new)	*dst = map(old, new)(src)
 * void nodes_onto(dst, orig, relmap)	*dst = orig relative to relmap
 * void nodes_fold(dst, orig, sz)	dst bits = orig bits mod sz
 *
 * for_each_node_mask(node, mask)	for-loop node over mask
 *
 * int num_online_nodes()		Number of online Nodes
 * int num_possible_nodes()		Number of all possible Nodes
 *
 * int node_random(mask)		Random node with set bit in mask
 *
 * int node_online(node)		Is some node online?
 * int node_possible(node)		Is some node possible?
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
 *
 * NODEMASK_SCRATCH
 * When doing above logical AND, OR, XOR, Remap operations the callers tend to
 * need temporary nodemask_t's on the stack. But if NODES_SHIFT is large,
 * nodemask_t's consume too much stack space.  NODEMASK_SCRATCH is a helper
 * for such situations. See below and CPUMASK_ALLOC also.
 */

#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/minmax.h>
#include <linux/nodemask_types.h>
#include <linux/numa.h>
#include <linux/random.h>

extern nodemask_t _unused_nodemask_arg_;

/**
 * nodemask_pr_args - printf args to output a nodemask
 * @maskp: nodemask to be printed
 *
 * Can be used to provide arguments for '%*pb[l]' when printing a nodemask.
 */
#define nodemask_pr_args(maskp)	__nodemask_pr_numnodes(maskp), \
				__nodemask_pr_bits(maskp)
static inline unsigned int __nodemask_pr_numnodes(const nodemask_t *m)
{
	return m ? MAX_NUMNODES : 0;
}
static inline const unsigned long *__nodemask_pr_bits(const nodemask_t *m)
{
	return m ? m->bits : NULL;
}

/*
 * The inline keyword gives the compiler room to decide to inline, or
 * not inline a function as it sees best.  However, as these functions
 * are called in both __init and non-__init functions, if they are not
 * inlined we will end up with a section mismatch error (of the type of
 * freeable items not being freed).  So we must use __always_inline here
 * to fix the problem.  If other functions in the future also end up in
 * this situation they will also need to be annotated as __always_inline
 */
#define node_set(node, dst) __node_set((node), &(dst))
static __always_inline void __node_set(int node, volatile nodemask_t *dstp)
{
	set_bit(node, dstp->bits);
}

#define node_clear(node, dst) __node_clear((node), &(dst))
static inline void __node_clear(int node, volatile nodemask_t *dstp)
{
	clear_bit(node, dstp->bits);
}

#define nodes_setall(dst) __nodes_setall(&(dst), MAX_NUMNODES)
static inline void __nodes_setall(nodemask_t *dstp, unsigned int nbits)
{
	bitmap_fill(dstp->bits, nbits);
}

#define nodes_clear(dst) __nodes_clear(&(dst), MAX_NUMNODES)
static inline void __nodes_clear(nodemask_t *dstp, unsigned int nbits)
{
	bitmap_zero(dstp->bits, nbits);
}

/* No static inline type checking - see Subtlety (1) above. */
#define node_isset(node, nodemask) test_bit((node), (nodemask).bits)

#define node_test_and_set(node, nodemask) \
			__node_test_and_set((node), &(nodemask))
static inline bool __node_test_and_set(int node, nodemask_t *addr)
{
	return test_and_set_bit(node, addr->bits);
}

#define nodes_and(dst, src1, src2) \
			__nodes_and(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_and(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, unsigned int nbits)
{
	bitmap_and(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_or(dst, src1, src2) \
			__nodes_or(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_or(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, unsigned int nbits)
{
	bitmap_or(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_xor(dst, src1, src2) \
			__nodes_xor(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_xor(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, unsigned int nbits)
{
	bitmap_xor(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_andnot(dst, src1, src2) \
			__nodes_andnot(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __nodes_andnot(nodemask_t *dstp, const nodemask_t *src1p,
					const nodemask_t *src2p, unsigned int nbits)
{
	bitmap_andnot(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define nodes_complement(dst, src) \
			__nodes_complement(&(dst), &(src), MAX_NUMNODES)
static inline void __nodes_complement(nodemask_t *dstp,
					const nodemask_t *srcp, unsigned int nbits)
{
	bitmap_complement(dstp->bits, srcp->bits, nbits);
}

#define nodes_equal(src1, src2) \
			__nodes_equal(&(src1), &(src2), MAX_NUMNODES)
static inline bool __nodes_equal(const nodemask_t *src1p,
					const nodemask_t *src2p, unsigned int nbits)
{
	return bitmap_equal(src1p->bits, src2p->bits, nbits);
}

#define nodes_intersects(src1, src2) \
			__nodes_intersects(&(src1), &(src2), MAX_NUMNODES)
static inline bool __nodes_intersects(const nodemask_t *src1p,
					const nodemask_t *src2p, unsigned int nbits)
{
	return bitmap_intersects(src1p->bits, src2p->bits, nbits);
}

#define nodes_subset(src1, src2) \
			__nodes_subset(&(src1), &(src2), MAX_NUMNODES)
static inline bool __nodes_subset(const nodemask_t *src1p,
					const nodemask_t *src2p, unsigned int nbits)
{
	return bitmap_subset(src1p->bits, src2p->bits, nbits);
}

#define nodes_empty(src) __nodes_empty(&(src), MAX_NUMNODES)
static inline bool __nodes_empty(const nodemask_t *srcp, unsigned int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

#define nodes_full(nodemask) __nodes_full(&(nodemask), MAX_NUMNODES)
static inline bool __nodes_full(const nodemask_t *srcp, unsigned int nbits)
{
	return bitmap_full(srcp->bits, nbits);
}

#define nodes_weight(nodemask) __nodes_weight(&(nodemask), MAX_NUMNODES)
static inline int __nodes_weight(const nodemask_t *srcp, unsigned int nbits)
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
static inline unsigned int __first_node(const nodemask_t *srcp)
{
	return min_t(unsigned int, MAX_NUMNODES, find_first_bit(srcp->bits, MAX_NUMNODES));
}

#define next_node(n, src) __next_node((n), &(src))
static inline unsigned int __next_node(int n, const nodemask_t *srcp)
{
	return min_t(unsigned int, MAX_NUMNODES, find_next_bit(srcp->bits, MAX_NUMNODES, n+1));
}

/*
 * Find the next present node in src, starting after node n, wrapping around to
 * the first node in src if needed.  Returns MAX_NUMNODES if src is empty.
 */
#define next_node_in(n, src) __next_node_in((n), &(src))
static inline unsigned int __next_node_in(int node, const nodemask_t *srcp)
{
	unsigned int ret = __next_node(node, srcp);

	if (ret == MAX_NUMNODES)
		ret = __first_node(srcp);
	return ret;
}

static inline void init_nodemask_of_node(nodemask_t *mask, int node)
{
	nodes_clear(*mask);
	node_set(node, *mask);
}

#define nodemask_of_node(node)						\
({									\
	typeof(_unused_nodemask_arg_) m;				\
	if (sizeof(m) == sizeof(unsigned long)) {			\
		m.bits[0] = 1UL << (node);				\
	} else {							\
		init_nodemask_of_node(&m, (node));			\
	}								\
	m;								\
})

#define first_unset_node(mask) __first_unset_node(&(mask))
static inline unsigned int __first_unset_node(const nodemask_t *maskp)
{
	return min_t(unsigned int, MAX_NUMNODES,
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

#define nodemask_parse_user(ubuf, ulen, dst) \
		__nodemask_parse_user((ubuf), (ulen), &(dst), MAX_NUMNODES)
static inline int __nodemask_parse_user(const char __user *buf, int len,
					nodemask_t *dstp, int nbits)
{
	return bitmap_parse_user(buf, len, dstp->bits, nbits);
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

#define nodes_onto(dst, orig, relmap) \
		__nodes_onto(&(dst), &(orig), &(relmap), MAX_NUMNODES)
static inline void __nodes_onto(nodemask_t *dstp, const nodemask_t *origp,
		const nodemask_t *relmapp, int nbits)
{
	bitmap_onto(dstp->bits, origp->bits, relmapp->bits, nbits);
}

#define nodes_fold(dst, orig, sz) \
		__nodes_fold(&(dst), &(orig), sz, MAX_NUMNODES)
static inline void __nodes_fold(nodemask_t *dstp, const nodemask_t *origp,
		int sz, int nbits)
{
	bitmap_fold(dstp->bits, origp->bits, sz, nbits);
}

#if MAX_NUMNODES > 1
#define for_each_node_mask(node, mask)				    \
	for ((node) = first_node(mask);				    \
	     (node) < MAX_NUMNODES;				    \
	     (node) = next_node((node), (mask)))
#else /* MAX_NUMNODES == 1 */
#define for_each_node_mask(node, mask)                                  \
	for ((node) = 0; (node) < 1 && !nodes_empty(mask); (node)++)
#endif /* MAX_NUMNODES */

/*
 * Bitmasks that are kept for all the nodes.
 */
enum node_states {
	N_POSSIBLE,		/* The node could become online at some point */
	N_ONLINE,		/* The node is online */
	N_NORMAL_MEMORY,	/* The node has regular memory */
#ifdef CONFIG_HIGHMEM
	N_HIGH_MEMORY,		/* The node has regular or high memory */
#else
	N_HIGH_MEMORY = N_NORMAL_MEMORY,
#endif
	N_MEMORY,		/* The node has memory(regular, high, movable) */
	N_CPU,		/* The node has one or more cpus */
	N_GENERIC_INITIATOR,	/* The node has one or more Generic Initiators */
	NR_NODE_STATES
};

/*
 * The following particular system nodemasks and operations
 * on them manage all possible and online nodes.
 */

extern nodemask_t node_states[NR_NODE_STATES];

#if MAX_NUMNODES > 1
static inline int node_state(int node, enum node_states state)
{
	return node_isset(node, node_states[state]);
}

static inline void node_set_state(int node, enum node_states state)
{
	__node_set(node, &node_states[state]);
}

static inline void node_clear_state(int node, enum node_states state)
{
	__node_clear(node, &node_states[state]);
}

static inline int num_node_state(enum node_states state)
{
	return nodes_weight(node_states[state]);
}

#define for_each_node_state(__node, __state) \
	for_each_node_mask((__node), node_states[__state])

#define first_online_node	first_node(node_states[N_ONLINE])
#define first_memory_node	first_node(node_states[N_MEMORY])
static inline unsigned int next_online_node(int nid)
{
	return next_node(nid, node_states[N_ONLINE]);
}
static inline unsigned int next_memory_node(int nid)
{
	return next_node(nid, node_states[N_MEMORY]);
}

extern unsigned int nr_node_ids;
extern unsigned int nr_online_nodes;

static inline void node_set_online(int nid)
{
	node_set_state(nid, N_ONLINE);
	nr_online_nodes = num_node_state(N_ONLINE);
}

static inline void node_set_offline(int nid)
{
	node_clear_state(nid, N_ONLINE);
	nr_online_nodes = num_node_state(N_ONLINE);
}

#else

static inline int node_state(int node, enum node_states state)
{
	return node == 0;
}

static inline void node_set_state(int node, enum node_states state)
{
}

static inline void node_clear_state(int node, enum node_states state)
{
}

static inline int num_node_state(enum node_states state)
{
	return 1;
}

#define for_each_node_state(node, __state) \
	for ( (node) = 0; (node) == 0; (node) = 1)

#define first_online_node	0
#define first_memory_node	0
#define next_online_node(nid)	(MAX_NUMNODES)
#define next_memory_node(nid)	(MAX_NUMNODES)
#define nr_node_ids		1U
#define nr_online_nodes		1U

#define node_set_online(node)	   node_set_state((node), N_ONLINE)
#define node_set_offline(node)	   node_clear_state((node), N_ONLINE)

#endif

static inline int node_random(const nodemask_t *maskp)
{
#if defined(CONFIG_NUMA) && (MAX_NUMNODES > 1)
	int w, bit;

	w = nodes_weight(*maskp);
	switch (w) {
	case 0:
		bit = NUMA_NO_NODE;
		break;
	case 1:
		bit = first_node(*maskp);
		break;
	default:
		bit = find_nth_bit(maskp->bits, MAX_NUMNODES, get_random_u32_below(w));
		break;
	}
	return bit;
#else
	return 0;
#endif
}

#define node_online_map 	node_states[N_ONLINE]
#define node_possible_map 	node_states[N_POSSIBLE]

#define num_online_nodes()	num_node_state(N_ONLINE)
#define num_possible_nodes()	num_node_state(N_POSSIBLE)
#define node_online(node)	node_state((node), N_ONLINE)
#define node_possible(node)	node_state((node), N_POSSIBLE)

#define for_each_node(node)	   for_each_node_state(node, N_POSSIBLE)
#define for_each_online_node(node) for_each_node_state(node, N_ONLINE)

/*
 * For nodemask scratch area.
 * NODEMASK_ALLOC(type, name) allocates an object with a specified type and
 * name.
 */
#if NODES_SHIFT > 8 /* nodemask_t > 32 bytes */
#define NODEMASK_ALLOC(type, name, gfp_flags)	\
			type *name = kmalloc(sizeof(*name), gfp_flags)
#define NODEMASK_FREE(m)			kfree(m)
#else
#define NODEMASK_ALLOC(type, name, gfp_flags)	type _##name, *name = &_##name
#define NODEMASK_FREE(m)			do {} while (0)
#endif

/* Example structure for using NODEMASK_ALLOC, used in mempolicy. */
struct nodemask_scratch {
	nodemask_t	mask1;
	nodemask_t	mask2;
};

#define NODEMASK_SCRATCH(x)						\
			NODEMASK_ALLOC(struct nodemask_scratch, x,	\
					GFP_KERNEL | __GFP_NORETRY)
#define NODEMASK_SCRATCH_FREE(x)	NODEMASK_FREE(x)


#endif /* __LINUX_NODEMASK_H */
