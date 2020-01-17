/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

/*
 * Nodemasks provide a bitmap suitable for representing the
 * set of Node's in a system, one bit position per Node number.
 *
 * See detailed comments in the file linux/bitmap.h describing the
 * data type on which these yesdemasks are based.
 *
 * For details of yesdemask_parse_user(), see bitmap_parse_user() in
 * lib/bitmap.c.  For details of yesdelist_parse(), see bitmap_parselist(),
 * also in bitmap.c.  For details of yesde_remap(), see bitmap_bitremap in
 * lib/bitmap.c.  For details of yesdes_remap(), see bitmap_remap in
 * lib/bitmap.c.  For details of yesdes_onto(), see bitmap_onto in
 * lib/bitmap.c.  For details of yesdes_fold(), see bitmap_fold in
 * lib/bitmap.c.
 *
 * The available yesdemask operations are:
 *
 * void yesde_set(yesde, mask)		turn on bit 'yesde' in mask
 * void yesde_clear(yesde, mask)		turn off bit 'yesde' in mask
 * void yesdes_setall(mask)		set all bits
 * void yesdes_clear(mask)		clear all bits
 * int yesde_isset(yesde, mask)		true iff bit 'yesde' set in mask
 * int yesde_test_and_set(yesde, mask)	test and set bit 'yesde' in mask
 *
 * void yesdes_and(dst, src1, src2)	dst = src1 & src2  [intersection]
 * void yesdes_or(dst, src1, src2)	dst = src1 | src2  [union]
 * void yesdes_xor(dst, src1, src2)	dst = src1 ^ src2
 * void yesdes_andyest(dst, src1, src2)	dst = src1 & ~src2
 * void yesdes_complement(dst, src)	dst = ~src
 *
 * int yesdes_equal(mask1, mask2)	Does mask1 == mask2?
 * int yesdes_intersects(mask1, mask2)	Do mask1 and mask2 intersect?
 * int yesdes_subset(mask1, mask2)	Is mask1 a subset of mask2?
 * int yesdes_empty(mask)		Is mask empty (yes bits sets)?
 * int yesdes_full(mask)			Is mask full (all bits sets)?
 * int yesdes_weight(mask)		Hamming weight - number of set bits
 *
 * void yesdes_shift_right(dst, src, n)	Shift right
 * void yesdes_shift_left(dst, src, n)	Shift left
 *
 * int first_yesde(mask)			Number lowest set bit, or MAX_NUMNODES
 * int next_yesde(yesde, mask)		Next yesde past 'yesde', or MAX_NUMNODES
 * int next_yesde_in(yesde, mask)		Next yesde past 'yesde', or wrap to first,
 *					or MAX_NUMNODES
 * int first_unset_yesde(mask)		First yesde yest set in mask, or 
 *					MAX_NUMNODES
 *
 * yesdemask_t yesdemask_of_yesde(yesde)	Return yesdemask with bit 'yesde' set
 * NODE_MASK_ALL			Initializer - all bits set
 * NODE_MASK_NONE			Initializer - yes bits set
 * unsigned long *yesdes_addr(mask)	Array of unsigned long's in mask
 *
 * int yesdemask_parse_user(ubuf, ulen, mask)	Parse ascii string as yesdemask
 * int yesdelist_parse(buf, map)		Parse ascii string as yesdelist
 * int yesde_remap(oldbit, old, new)	newbit = map(old, new)(oldbit)
 * void yesdes_remap(dst, src, old, new)	*dst = map(old, new)(src)
 * void yesdes_onto(dst, orig, relmap)	*dst = orig relative to relmap
 * void yesdes_fold(dst, orig, sz)	dst bits = orig bits mod sz
 *
 * for_each_yesde_mask(yesde, mask)	for-loop yesde over mask
 *
 * int num_online_yesdes()		Number of online Nodes
 * int num_possible_yesdes()		Number of all possible Nodes
 *
 * int yesde_random(mask)		Random yesde with set bit in mask
 *
 * int yesde_online(yesde)		Is some yesde online?
 * int yesde_possible(yesde)		Is some yesde possible?
 *
 * yesde_set_online(yesde)		set bit 'yesde' in yesde_online_map
 * yesde_set_offline(yesde)		clear bit 'yesde' in yesde_online_map
 *
 * for_each_yesde(yesde)			for-loop yesde over yesde_possible_map
 * for_each_online_yesde(yesde)		for-loop yesde over yesde_online_map
 *
 * Subtlety:
 * 1) The 'type-checked' form of yesde_isset() causes gcc (3.3.2, anyway)
 *    to generate slightly worse code.  So use a simple one-line #define
 *    for yesde_isset(), instead of wrapping an inline inside a macro, the
 *    way we do the other calls.
 *
 * NODEMASK_SCRATCH
 * When doing above logical AND, OR, XOR, Remap operations the callers tend to
 * need temporary yesdemask_t's on the stack. But if NODES_SHIFT is large,
 * yesdemask_t's consume too much stack space.  NODEMASK_SCRATCH is a helper
 * for such situations. See below and CPUMASK_ALLOC also.
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/numa.h>

typedef struct { DECLARE_BITMAP(bits, MAX_NUMNODES); } yesdemask_t;
extern yesdemask_t _unused_yesdemask_arg_;

/**
 * yesdemask_pr_args - printf args to output a yesdemask
 * @maskp: yesdemask to be printed
 *
 * Can be used to provide arguments for '%*pb[l]' when printing a yesdemask.
 */
#define yesdemask_pr_args(maskp)	__yesdemask_pr_numyesdes(maskp), \
				__yesdemask_pr_bits(maskp)
static inline unsigned int __yesdemask_pr_numyesdes(const yesdemask_t *m)
{
	return m ? MAX_NUMNODES : 0;
}
static inline const unsigned long *__yesdemask_pr_bits(const yesdemask_t *m)
{
	return m ? m->bits : NULL;
}

/*
 * The inline keyword gives the compiler room to decide to inline, or
 * yest inline a function as it sees best.  However, as these functions
 * are called in both __init and yesn-__init functions, if they are yest
 * inlined we will end up with a section mis-match error (of the type of
 * freeable items yest being freed).  So we must use __always_inline here
 * to fix the problem.  If other functions in the future also end up in
 * this situation they will also need to be anyestated as __always_inline
 */
#define yesde_set(yesde, dst) __yesde_set((yesde), &(dst))
static __always_inline void __yesde_set(int yesde, volatile yesdemask_t *dstp)
{
	set_bit(yesde, dstp->bits);
}

#define yesde_clear(yesde, dst) __yesde_clear((yesde), &(dst))
static inline void __yesde_clear(int yesde, volatile yesdemask_t *dstp)
{
	clear_bit(yesde, dstp->bits);
}

#define yesdes_setall(dst) __yesdes_setall(&(dst), MAX_NUMNODES)
static inline void __yesdes_setall(yesdemask_t *dstp, unsigned int nbits)
{
	bitmap_fill(dstp->bits, nbits);
}

#define yesdes_clear(dst) __yesdes_clear(&(dst), MAX_NUMNODES)
static inline void __yesdes_clear(yesdemask_t *dstp, unsigned int nbits)
{
	bitmap_zero(dstp->bits, nbits);
}

/* No static inline type checking - see Subtlety (1) above. */
#define yesde_isset(yesde, yesdemask) test_bit((yesde), (yesdemask).bits)

#define yesde_test_and_set(yesde, yesdemask) \
			__yesde_test_and_set((yesde), &(yesdemask))
static inline int __yesde_test_and_set(int yesde, yesdemask_t *addr)
{
	return test_and_set_bit(yesde, addr->bits);
}

#define yesdes_and(dst, src1, src2) \
			__yesdes_and(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __yesdes_and(yesdemask_t *dstp, const yesdemask_t *src1p,
					const yesdemask_t *src2p, unsigned int nbits)
{
	bitmap_and(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define yesdes_or(dst, src1, src2) \
			__yesdes_or(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __yesdes_or(yesdemask_t *dstp, const yesdemask_t *src1p,
					const yesdemask_t *src2p, unsigned int nbits)
{
	bitmap_or(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define yesdes_xor(dst, src1, src2) \
			__yesdes_xor(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __yesdes_xor(yesdemask_t *dstp, const yesdemask_t *src1p,
					const yesdemask_t *src2p, unsigned int nbits)
{
	bitmap_xor(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define yesdes_andyest(dst, src1, src2) \
			__yesdes_andyest(&(dst), &(src1), &(src2), MAX_NUMNODES)
static inline void __yesdes_andyest(yesdemask_t *dstp, const yesdemask_t *src1p,
					const yesdemask_t *src2p, unsigned int nbits)
{
	bitmap_andyest(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define yesdes_complement(dst, src) \
			__yesdes_complement(&(dst), &(src), MAX_NUMNODES)
static inline void __yesdes_complement(yesdemask_t *dstp,
					const yesdemask_t *srcp, unsigned int nbits)
{
	bitmap_complement(dstp->bits, srcp->bits, nbits);
}

#define yesdes_equal(src1, src2) \
			__yesdes_equal(&(src1), &(src2), MAX_NUMNODES)
static inline int __yesdes_equal(const yesdemask_t *src1p,
					const yesdemask_t *src2p, unsigned int nbits)
{
	return bitmap_equal(src1p->bits, src2p->bits, nbits);
}

#define yesdes_intersects(src1, src2) \
			__yesdes_intersects(&(src1), &(src2), MAX_NUMNODES)
static inline int __yesdes_intersects(const yesdemask_t *src1p,
					const yesdemask_t *src2p, unsigned int nbits)
{
	return bitmap_intersects(src1p->bits, src2p->bits, nbits);
}

#define yesdes_subset(src1, src2) \
			__yesdes_subset(&(src1), &(src2), MAX_NUMNODES)
static inline int __yesdes_subset(const yesdemask_t *src1p,
					const yesdemask_t *src2p, unsigned int nbits)
{
	return bitmap_subset(src1p->bits, src2p->bits, nbits);
}

#define yesdes_empty(src) __yesdes_empty(&(src), MAX_NUMNODES)
static inline int __yesdes_empty(const yesdemask_t *srcp, unsigned int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

#define yesdes_full(yesdemask) __yesdes_full(&(yesdemask), MAX_NUMNODES)
static inline int __yesdes_full(const yesdemask_t *srcp, unsigned int nbits)
{
	return bitmap_full(srcp->bits, nbits);
}

#define yesdes_weight(yesdemask) __yesdes_weight(&(yesdemask), MAX_NUMNODES)
static inline int __yesdes_weight(const yesdemask_t *srcp, unsigned int nbits)
{
	return bitmap_weight(srcp->bits, nbits);
}

#define yesdes_shift_right(dst, src, n) \
			__yesdes_shift_right(&(dst), &(src), (n), MAX_NUMNODES)
static inline void __yesdes_shift_right(yesdemask_t *dstp,
					const yesdemask_t *srcp, int n, int nbits)
{
	bitmap_shift_right(dstp->bits, srcp->bits, n, nbits);
}

#define yesdes_shift_left(dst, src, n) \
			__yesdes_shift_left(&(dst), &(src), (n), MAX_NUMNODES)
static inline void __yesdes_shift_left(yesdemask_t *dstp,
					const yesdemask_t *srcp, int n, int nbits)
{
	bitmap_shift_left(dstp->bits, srcp->bits, n, nbits);
}

/* FIXME: better would be to fix all architectures to never return
          > MAX_NUMNODES, then the silly min_ts could be dropped. */

#define first_yesde(src) __first_yesde(&(src))
static inline int __first_yesde(const yesdemask_t *srcp)
{
	return min_t(int, MAX_NUMNODES, find_first_bit(srcp->bits, MAX_NUMNODES));
}

#define next_yesde(n, src) __next_yesde((n), &(src))
static inline int __next_yesde(int n, const yesdemask_t *srcp)
{
	return min_t(int,MAX_NUMNODES,find_next_bit(srcp->bits, MAX_NUMNODES, n+1));
}

/*
 * Find the next present yesde in src, starting after yesde n, wrapping around to
 * the first yesde in src if needed.  Returns MAX_NUMNODES if src is empty.
 */
#define next_yesde_in(n, src) __next_yesde_in((n), &(src))
int __next_yesde_in(int yesde, const yesdemask_t *srcp);

static inline void init_yesdemask_of_yesde(yesdemask_t *mask, int yesde)
{
	yesdes_clear(*mask);
	yesde_set(yesde, *mask);
}

#define yesdemask_of_yesde(yesde)						\
({									\
	typeof(_unused_yesdemask_arg_) m;				\
	if (sizeof(m) == sizeof(unsigned long)) {			\
		m.bits[0] = 1UL << (yesde);				\
	} else {							\
		init_yesdemask_of_yesde(&m, (yesde));			\
	}								\
	m;								\
})

#define first_unset_yesde(mask) __first_unset_yesde(&(mask))
static inline int __first_unset_yesde(const yesdemask_t *maskp)
{
	return min_t(int,MAX_NUMNODES,
			find_first_zero_bit(maskp->bits, MAX_NUMNODES));
}

#define NODE_MASK_LAST_WORD BITMAP_LAST_WORD_MASK(MAX_NUMNODES)

#if MAX_NUMNODES <= BITS_PER_LONG

#define NODE_MASK_ALL							\
((yesdemask_t) { {							\
	[BITS_TO_LONGS(MAX_NUMNODES)-1] = NODE_MASK_LAST_WORD		\
} })

#else

#define NODE_MASK_ALL							\
((yesdemask_t) { {							\
	[0 ... BITS_TO_LONGS(MAX_NUMNODES)-2] = ~0UL,			\
	[BITS_TO_LONGS(MAX_NUMNODES)-1] = NODE_MASK_LAST_WORD		\
} })

#endif

#define NODE_MASK_NONE							\
((yesdemask_t) { {							\
	[0 ... BITS_TO_LONGS(MAX_NUMNODES)-1] =  0UL			\
} })

#define yesdes_addr(src) ((src).bits)

#define yesdemask_parse_user(ubuf, ulen, dst) \
		__yesdemask_parse_user((ubuf), (ulen), &(dst), MAX_NUMNODES)
static inline int __yesdemask_parse_user(const char __user *buf, int len,
					yesdemask_t *dstp, int nbits)
{
	return bitmap_parse_user(buf, len, dstp->bits, nbits);
}

#define yesdelist_parse(buf, dst) __yesdelist_parse((buf), &(dst), MAX_NUMNODES)
static inline int __yesdelist_parse(const char *buf, yesdemask_t *dstp, int nbits)
{
	return bitmap_parselist(buf, dstp->bits, nbits);
}

#define yesde_remap(oldbit, old, new) \
		__yesde_remap((oldbit), &(old), &(new), MAX_NUMNODES)
static inline int __yesde_remap(int oldbit,
		const yesdemask_t *oldp, const yesdemask_t *newp, int nbits)
{
	return bitmap_bitremap(oldbit, oldp->bits, newp->bits, nbits);
}

#define yesdes_remap(dst, src, old, new) \
		__yesdes_remap(&(dst), &(src), &(old), &(new), MAX_NUMNODES)
static inline void __yesdes_remap(yesdemask_t *dstp, const yesdemask_t *srcp,
		const yesdemask_t *oldp, const yesdemask_t *newp, int nbits)
{
	bitmap_remap(dstp->bits, srcp->bits, oldp->bits, newp->bits, nbits);
}

#define yesdes_onto(dst, orig, relmap) \
		__yesdes_onto(&(dst), &(orig), &(relmap), MAX_NUMNODES)
static inline void __yesdes_onto(yesdemask_t *dstp, const yesdemask_t *origp,
		const yesdemask_t *relmapp, int nbits)
{
	bitmap_onto(dstp->bits, origp->bits, relmapp->bits, nbits);
}

#define yesdes_fold(dst, orig, sz) \
		__yesdes_fold(&(dst), &(orig), sz, MAX_NUMNODES)
static inline void __yesdes_fold(yesdemask_t *dstp, const yesdemask_t *origp,
		int sz, int nbits)
{
	bitmap_fold(dstp->bits, origp->bits, sz, nbits);
}

#if MAX_NUMNODES > 1
#define for_each_yesde_mask(yesde, mask)			\
	for ((yesde) = first_yesde(mask);			\
		(yesde) < MAX_NUMNODES;			\
		(yesde) = next_yesde((yesde), (mask)))
#else /* MAX_NUMNODES == 1 */
#define for_each_yesde_mask(yesde, mask)			\
	if (!yesdes_empty(mask))				\
		for ((yesde) = 0; (yesde) < 1; (yesde)++)
#endif /* MAX_NUMNODES */

/*
 * Bitmasks that are kept for all the yesdes.
 */
enum yesde_states {
	N_POSSIBLE,		/* The yesde could become online at some point */
	N_ONLINE,		/* The yesde is online */
	N_NORMAL_MEMORY,	/* The yesde has regular memory */
#ifdef CONFIG_HIGHMEM
	N_HIGH_MEMORY,		/* The yesde has regular or high memory */
#else
	N_HIGH_MEMORY = N_NORMAL_MEMORY,
#endif
	N_MEMORY,		/* The yesde has memory(regular, high, movable) */
	N_CPU,		/* The yesde has one or more cpus */
	NR_NODE_STATES
};

/*
 * The following particular system yesdemasks and operations
 * on them manage all possible and online yesdes.
 */

extern yesdemask_t yesde_states[NR_NODE_STATES];

#if MAX_NUMNODES > 1
static inline int yesde_state(int yesde, enum yesde_states state)
{
	return yesde_isset(yesde, yesde_states[state]);
}

static inline void yesde_set_state(int yesde, enum yesde_states state)
{
	__yesde_set(yesde, &yesde_states[state]);
}

static inline void yesde_clear_state(int yesde, enum yesde_states state)
{
	__yesde_clear(yesde, &yesde_states[state]);
}

static inline int num_yesde_state(enum yesde_states state)
{
	return yesdes_weight(yesde_states[state]);
}

#define for_each_yesde_state(__yesde, __state) \
	for_each_yesde_mask((__yesde), yesde_states[__state])

#define first_online_yesde	first_yesde(yesde_states[N_ONLINE])
#define first_memory_yesde	first_yesde(yesde_states[N_MEMORY])
static inline int next_online_yesde(int nid)
{
	return next_yesde(nid, yesde_states[N_ONLINE]);
}
static inline int next_memory_yesde(int nid)
{
	return next_yesde(nid, yesde_states[N_MEMORY]);
}

extern unsigned int nr_yesde_ids;
extern unsigned int nr_online_yesdes;

static inline void yesde_set_online(int nid)
{
	yesde_set_state(nid, N_ONLINE);
	nr_online_yesdes = num_yesde_state(N_ONLINE);
}

static inline void yesde_set_offline(int nid)
{
	yesde_clear_state(nid, N_ONLINE);
	nr_online_yesdes = num_yesde_state(N_ONLINE);
}

#else

static inline int yesde_state(int yesde, enum yesde_states state)
{
	return yesde == 0;
}

static inline void yesde_set_state(int yesde, enum yesde_states state)
{
}

static inline void yesde_clear_state(int yesde, enum yesde_states state)
{
}

static inline int num_yesde_state(enum yesde_states state)
{
	return 1;
}

#define for_each_yesde_state(yesde, __state) \
	for ( (yesde) = 0; (yesde) == 0; (yesde) = 1)

#define first_online_yesde	0
#define first_memory_yesde	0
#define next_online_yesde(nid)	(MAX_NUMNODES)
#define nr_yesde_ids		1U
#define nr_online_yesdes		1U

#define yesde_set_online(yesde)	   yesde_set_state((yesde), N_ONLINE)
#define yesde_set_offline(yesde)	   yesde_clear_state((yesde), N_ONLINE)

#endif

#if defined(CONFIG_NUMA) && (MAX_NUMNODES > 1)
extern int yesde_random(const yesdemask_t *maskp);
#else
static inline int yesde_random(const yesdemask_t *mask)
{
	return 0;
}
#endif

#define yesde_online_map 	yesde_states[N_ONLINE]
#define yesde_possible_map 	yesde_states[N_POSSIBLE]

#define num_online_yesdes()	num_yesde_state(N_ONLINE)
#define num_possible_yesdes()	num_yesde_state(N_POSSIBLE)
#define yesde_online(yesde)	yesde_state((yesde), N_ONLINE)
#define yesde_possible(yesde)	yesde_state((yesde), N_POSSIBLE)

#define for_each_yesde(yesde)	   for_each_yesde_state(yesde, N_POSSIBLE)
#define for_each_online_yesde(yesde) for_each_yesde_state(yesde, N_ONLINE)

/*
 * For yesdemask scrach area.
 * NODEMASK_ALLOC(type, name) allocates an object with a specified type and
 * name.
 */
#if NODES_SHIFT > 8 /* yesdemask_t > 32 bytes */
#define NODEMASK_ALLOC(type, name, gfp_flags)	\
			type *name = kmalloc(sizeof(*name), gfp_flags)
#define NODEMASK_FREE(m)			kfree(m)
#else
#define NODEMASK_ALLOC(type, name, gfp_flags)	type _##name, *name = &_##name
#define NODEMASK_FREE(m)			do {} while (0)
#endif

/* A example struture for using NODEMASK_ALLOC, used in mempolicy. */
struct yesdemask_scratch {
	yesdemask_t	mask1;
	yesdemask_t	mask2;
};

#define NODEMASK_SCRATCH(x)						\
			NODEMASK_ALLOC(struct yesdemask_scratch, x,	\
					GFP_KERNEL | __GFP_NORETRY)
#define NODEMASK_SCRATCH_FREE(x)	NODEMASK_FREE(x)


#endif /* __LINUX_NODEMASK_H */
