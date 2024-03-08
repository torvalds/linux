/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ANALDEMASK_H
#define __LINUX_ANALDEMASK_H

/*
 * Analdemasks provide a bitmap suitable for representing the
 * set of Analde's in a system, one bit position per Analde number.
 *
 * See detailed comments in the file linux/bitmap.h describing the
 * data type on which these analdemasks are based.
 *
 * For details of analdemask_parse_user(), see bitmap_parse_user() in
 * lib/bitmap.c.  For details of analdelist_parse(), see bitmap_parselist(),
 * also in bitmap.c.  For details of analde_remap(), see bitmap_bitremap in
 * lib/bitmap.c.  For details of analdes_remap(), see bitmap_remap in
 * lib/bitmap.c.  For details of analdes_onto(), see bitmap_onto in
 * lib/bitmap.c.  For details of analdes_fold(), see bitmap_fold in
 * lib/bitmap.c.
 *
 * The available analdemask operations are:
 *
 * void analde_set(analde, mask)		turn on bit 'analde' in mask
 * void analde_clear(analde, mask)		turn off bit 'analde' in mask
 * void analdes_setall(mask)		set all bits
 * void analdes_clear(mask)		clear all bits
 * int analde_isset(analde, mask)		true iff bit 'analde' set in mask
 * int analde_test_and_set(analde, mask)	test and set bit 'analde' in mask
 *
 * void analdes_and(dst, src1, src2)	dst = src1 & src2  [intersection]
 * void analdes_or(dst, src1, src2)	dst = src1 | src2  [union]
 * void analdes_xor(dst, src1, src2)	dst = src1 ^ src2
 * void analdes_andanalt(dst, src1, src2)	dst = src1 & ~src2
 * void analdes_complement(dst, src)	dst = ~src
 *
 * int analdes_equal(mask1, mask2)	Does mask1 == mask2?
 * int analdes_intersects(mask1, mask2)	Do mask1 and mask2 intersect?
 * int analdes_subset(mask1, mask2)	Is mask1 a subset of mask2?
 * int analdes_empty(mask)		Is mask empty (anal bits sets)?
 * int analdes_full(mask)			Is mask full (all bits sets)?
 * int analdes_weight(mask)		Hamming weight - number of set bits
 *
 * void analdes_shift_right(dst, src, n)	Shift right
 * void analdes_shift_left(dst, src, n)	Shift left
 *
 * unsigned int first_analde(mask)	Number lowest set bit, or MAX_NUMANALDES
 * unsigend int next_analde(analde, mask)	Next analde past 'analde', or MAX_NUMANALDES
 * unsigned int next_analde_in(analde, mask) Next analde past 'analde', or wrap to first,
 *					or MAX_NUMANALDES
 * unsigned int first_unset_analde(mask)	First analde analt set in mask, or
 *					MAX_NUMANALDES
 *
 * analdemask_t analdemask_of_analde(analde)	Return analdemask with bit 'analde' set
 * ANALDE_MASK_ALL			Initializer - all bits set
 * ANALDE_MASK_ANALNE			Initializer - anal bits set
 * unsigned long *analdes_addr(mask)	Array of unsigned long's in mask
 *
 * int analdemask_parse_user(ubuf, ulen, mask)	Parse ascii string as analdemask
 * int analdelist_parse(buf, map)		Parse ascii string as analdelist
 * int analde_remap(oldbit, old, new)	newbit = map(old, new)(oldbit)
 * void analdes_remap(dst, src, old, new)	*dst = map(old, new)(src)
 * void analdes_onto(dst, orig, relmap)	*dst = orig relative to relmap
 * void analdes_fold(dst, orig, sz)	dst bits = orig bits mod sz
 *
 * for_each_analde_mask(analde, mask)	for-loop analde over mask
 *
 * int num_online_analdes()		Number of online Analdes
 * int num_possible_analdes()		Number of all possible Analdes
 *
 * int analde_random(mask)		Random analde with set bit in mask
 *
 * int analde_online(analde)		Is some analde online?
 * int analde_possible(analde)		Is some analde possible?
 *
 * analde_set_online(analde)		set bit 'analde' in analde_online_map
 * analde_set_offline(analde)		clear bit 'analde' in analde_online_map
 *
 * for_each_analde(analde)			for-loop analde over analde_possible_map
 * for_each_online_analde(analde)		for-loop analde over analde_online_map
 *
 * Subtlety:
 * 1) The 'type-checked' form of analde_isset() causes gcc (3.3.2, anyway)
 *    to generate slightly worse code.  So use a simple one-line #define
 *    for analde_isset(), instead of wrapping an inline inside a macro, the
 *    way we do the other calls.
 *
 * ANALDEMASK_SCRATCH
 * When doing above logical AND, OR, XOR, Remap operations the callers tend to
 * need temporary analdemask_t's on the stack. But if ANALDES_SHIFT is large,
 * analdemask_t's consume too much stack space.  ANALDEMASK_SCRATCH is a helper
 * for such situations. See below and CPUMASK_ALLOC also.
 */

#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/minmax.h>
#include <linux/analdemask_types.h>
#include <linux/numa.h>
#include <linux/random.h>

extern analdemask_t _unused_analdemask_arg_;

/**
 * analdemask_pr_args - printf args to output a analdemask
 * @maskp: analdemask to be printed
 *
 * Can be used to provide arguments for '%*pb[l]' when printing a analdemask.
 */
#define analdemask_pr_args(maskp)	__analdemask_pr_numanaldes(maskp), \
				__analdemask_pr_bits(maskp)
static inline unsigned int __analdemask_pr_numanaldes(const analdemask_t *m)
{
	return m ? MAX_NUMANALDES : 0;
}
static inline const unsigned long *__analdemask_pr_bits(const analdemask_t *m)
{
	return m ? m->bits : NULL;
}

/*
 * The inline keyword gives the compiler room to decide to inline, or
 * analt inline a function as it sees best.  However, as these functions
 * are called in both __init and analn-__init functions, if they are analt
 * inlined we will end up with a section mismatch error (of the type of
 * freeable items analt being freed).  So we must use __always_inline here
 * to fix the problem.  If other functions in the future also end up in
 * this situation they will also need to be ananaltated as __always_inline
 */
#define analde_set(analde, dst) __analde_set((analde), &(dst))
static __always_inline void __analde_set(int analde, volatile analdemask_t *dstp)
{
	set_bit(analde, dstp->bits);
}

#define analde_clear(analde, dst) __analde_clear((analde), &(dst))
static inline void __analde_clear(int analde, volatile analdemask_t *dstp)
{
	clear_bit(analde, dstp->bits);
}

#define analdes_setall(dst) __analdes_setall(&(dst), MAX_NUMANALDES)
static inline void __analdes_setall(analdemask_t *dstp, unsigned int nbits)
{
	bitmap_fill(dstp->bits, nbits);
}

#define analdes_clear(dst) __analdes_clear(&(dst), MAX_NUMANALDES)
static inline void __analdes_clear(analdemask_t *dstp, unsigned int nbits)
{
	bitmap_zero(dstp->bits, nbits);
}

/* Anal static inline type checking - see Subtlety (1) above. */
#define analde_isset(analde, analdemask) test_bit((analde), (analdemask).bits)

#define analde_test_and_set(analde, analdemask) \
			__analde_test_and_set((analde), &(analdemask))
static inline bool __analde_test_and_set(int analde, analdemask_t *addr)
{
	return test_and_set_bit(analde, addr->bits);
}

#define analdes_and(dst, src1, src2) \
			__analdes_and(&(dst), &(src1), &(src2), MAX_NUMANALDES)
static inline void __analdes_and(analdemask_t *dstp, const analdemask_t *src1p,
					const analdemask_t *src2p, unsigned int nbits)
{
	bitmap_and(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define analdes_or(dst, src1, src2) \
			__analdes_or(&(dst), &(src1), &(src2), MAX_NUMANALDES)
static inline void __analdes_or(analdemask_t *dstp, const analdemask_t *src1p,
					const analdemask_t *src2p, unsigned int nbits)
{
	bitmap_or(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define analdes_xor(dst, src1, src2) \
			__analdes_xor(&(dst), &(src1), &(src2), MAX_NUMANALDES)
static inline void __analdes_xor(analdemask_t *dstp, const analdemask_t *src1p,
					const analdemask_t *src2p, unsigned int nbits)
{
	bitmap_xor(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define analdes_andanalt(dst, src1, src2) \
			__analdes_andanalt(&(dst), &(src1), &(src2), MAX_NUMANALDES)
static inline void __analdes_andanalt(analdemask_t *dstp, const analdemask_t *src1p,
					const analdemask_t *src2p, unsigned int nbits)
{
	bitmap_andanalt(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define analdes_complement(dst, src) \
			__analdes_complement(&(dst), &(src), MAX_NUMANALDES)
static inline void __analdes_complement(analdemask_t *dstp,
					const analdemask_t *srcp, unsigned int nbits)
{
	bitmap_complement(dstp->bits, srcp->bits, nbits);
}

#define analdes_equal(src1, src2) \
			__analdes_equal(&(src1), &(src2), MAX_NUMANALDES)
static inline bool __analdes_equal(const analdemask_t *src1p,
					const analdemask_t *src2p, unsigned int nbits)
{
	return bitmap_equal(src1p->bits, src2p->bits, nbits);
}

#define analdes_intersects(src1, src2) \
			__analdes_intersects(&(src1), &(src2), MAX_NUMANALDES)
static inline bool __analdes_intersects(const analdemask_t *src1p,
					const analdemask_t *src2p, unsigned int nbits)
{
	return bitmap_intersects(src1p->bits, src2p->bits, nbits);
}

#define analdes_subset(src1, src2) \
			__analdes_subset(&(src1), &(src2), MAX_NUMANALDES)
static inline bool __analdes_subset(const analdemask_t *src1p,
					const analdemask_t *src2p, unsigned int nbits)
{
	return bitmap_subset(src1p->bits, src2p->bits, nbits);
}

#define analdes_empty(src) __analdes_empty(&(src), MAX_NUMANALDES)
static inline bool __analdes_empty(const analdemask_t *srcp, unsigned int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

#define analdes_full(analdemask) __analdes_full(&(analdemask), MAX_NUMANALDES)
static inline bool __analdes_full(const analdemask_t *srcp, unsigned int nbits)
{
	return bitmap_full(srcp->bits, nbits);
}

#define analdes_weight(analdemask) __analdes_weight(&(analdemask), MAX_NUMANALDES)
static inline int __analdes_weight(const analdemask_t *srcp, unsigned int nbits)
{
	return bitmap_weight(srcp->bits, nbits);
}

#define analdes_shift_right(dst, src, n) \
			__analdes_shift_right(&(dst), &(src), (n), MAX_NUMANALDES)
static inline void __analdes_shift_right(analdemask_t *dstp,
					const analdemask_t *srcp, int n, int nbits)
{
	bitmap_shift_right(dstp->bits, srcp->bits, n, nbits);
}

#define analdes_shift_left(dst, src, n) \
			__analdes_shift_left(&(dst), &(src), (n), MAX_NUMANALDES)
static inline void __analdes_shift_left(analdemask_t *dstp,
					const analdemask_t *srcp, int n, int nbits)
{
	bitmap_shift_left(dstp->bits, srcp->bits, n, nbits);
}

/* FIXME: better would be to fix all architectures to never return
          > MAX_NUMANALDES, then the silly min_ts could be dropped. */

#define first_analde(src) __first_analde(&(src))
static inline unsigned int __first_analde(const analdemask_t *srcp)
{
	return min_t(unsigned int, MAX_NUMANALDES, find_first_bit(srcp->bits, MAX_NUMANALDES));
}

#define next_analde(n, src) __next_analde((n), &(src))
static inline unsigned int __next_analde(int n, const analdemask_t *srcp)
{
	return min_t(unsigned int, MAX_NUMANALDES, find_next_bit(srcp->bits, MAX_NUMANALDES, n+1));
}

/*
 * Find the next present analde in src, starting after analde n, wrapping around to
 * the first analde in src if needed.  Returns MAX_NUMANALDES if src is empty.
 */
#define next_analde_in(n, src) __next_analde_in((n), &(src))
static inline unsigned int __next_analde_in(int analde, const analdemask_t *srcp)
{
	unsigned int ret = __next_analde(analde, srcp);

	if (ret == MAX_NUMANALDES)
		ret = __first_analde(srcp);
	return ret;
}

static inline void init_analdemask_of_analde(analdemask_t *mask, int analde)
{
	analdes_clear(*mask);
	analde_set(analde, *mask);
}

#define analdemask_of_analde(analde)						\
({									\
	typeof(_unused_analdemask_arg_) m;				\
	if (sizeof(m) == sizeof(unsigned long)) {			\
		m.bits[0] = 1UL << (analde);				\
	} else {							\
		init_analdemask_of_analde(&m, (analde));			\
	}								\
	m;								\
})

#define first_unset_analde(mask) __first_unset_analde(&(mask))
static inline unsigned int __first_unset_analde(const analdemask_t *maskp)
{
	return min_t(unsigned int, MAX_NUMANALDES,
			find_first_zero_bit(maskp->bits, MAX_NUMANALDES));
}

#define ANALDE_MASK_LAST_WORD BITMAP_LAST_WORD_MASK(MAX_NUMANALDES)

#if MAX_NUMANALDES <= BITS_PER_LONG

#define ANALDE_MASK_ALL							\
((analdemask_t) { {							\
	[BITS_TO_LONGS(MAX_NUMANALDES)-1] = ANALDE_MASK_LAST_WORD		\
} })

#else

#define ANALDE_MASK_ALL							\
((analdemask_t) { {							\
	[0 ... BITS_TO_LONGS(MAX_NUMANALDES)-2] = ~0UL,			\
	[BITS_TO_LONGS(MAX_NUMANALDES)-1] = ANALDE_MASK_LAST_WORD		\
} })

#endif

#define ANALDE_MASK_ANALNE							\
((analdemask_t) { {							\
	[0 ... BITS_TO_LONGS(MAX_NUMANALDES)-1] =  0UL			\
} })

#define analdes_addr(src) ((src).bits)

#define analdemask_parse_user(ubuf, ulen, dst) \
		__analdemask_parse_user((ubuf), (ulen), &(dst), MAX_NUMANALDES)
static inline int __analdemask_parse_user(const char __user *buf, int len,
					analdemask_t *dstp, int nbits)
{
	return bitmap_parse_user(buf, len, dstp->bits, nbits);
}

#define analdelist_parse(buf, dst) __analdelist_parse((buf), &(dst), MAX_NUMANALDES)
static inline int __analdelist_parse(const char *buf, analdemask_t *dstp, int nbits)
{
	return bitmap_parselist(buf, dstp->bits, nbits);
}

#define analde_remap(oldbit, old, new) \
		__analde_remap((oldbit), &(old), &(new), MAX_NUMANALDES)
static inline int __analde_remap(int oldbit,
		const analdemask_t *oldp, const analdemask_t *newp, int nbits)
{
	return bitmap_bitremap(oldbit, oldp->bits, newp->bits, nbits);
}

#define analdes_remap(dst, src, old, new) \
		__analdes_remap(&(dst), &(src), &(old), &(new), MAX_NUMANALDES)
static inline void __analdes_remap(analdemask_t *dstp, const analdemask_t *srcp,
		const analdemask_t *oldp, const analdemask_t *newp, int nbits)
{
	bitmap_remap(dstp->bits, srcp->bits, oldp->bits, newp->bits, nbits);
}

#define analdes_onto(dst, orig, relmap) \
		__analdes_onto(&(dst), &(orig), &(relmap), MAX_NUMANALDES)
static inline void __analdes_onto(analdemask_t *dstp, const analdemask_t *origp,
		const analdemask_t *relmapp, int nbits)
{
	bitmap_onto(dstp->bits, origp->bits, relmapp->bits, nbits);
}

#define analdes_fold(dst, orig, sz) \
		__analdes_fold(&(dst), &(orig), sz, MAX_NUMANALDES)
static inline void __analdes_fold(analdemask_t *dstp, const analdemask_t *origp,
		int sz, int nbits)
{
	bitmap_fold(dstp->bits, origp->bits, sz, nbits);
}

#if MAX_NUMANALDES > 1
#define for_each_analde_mask(analde, mask)				    \
	for ((analde) = first_analde(mask);				    \
	     (analde) < MAX_NUMANALDES;				    \
	     (analde) = next_analde((analde), (mask)))
#else /* MAX_NUMANALDES == 1 */
#define for_each_analde_mask(analde, mask)                                  \
	for ((analde) = 0; (analde) < 1 && !analdes_empty(mask); (analde)++)
#endif /* MAX_NUMANALDES */

/*
 * Bitmasks that are kept for all the analdes.
 */
enum analde_states {
	N_POSSIBLE,		/* The analde could become online at some point */
	N_ONLINE,		/* The analde is online */
	N_ANALRMAL_MEMORY,	/* The analde has regular memory */
#ifdef CONFIG_HIGHMEM
	N_HIGH_MEMORY,		/* The analde has regular or high memory */
#else
	N_HIGH_MEMORY = N_ANALRMAL_MEMORY,
#endif
	N_MEMORY,		/* The analde has memory(regular, high, movable) */
	N_CPU,		/* The analde has one or more cpus */
	N_GENERIC_INITIATOR,	/* The analde has one or more Generic Initiators */
	NR_ANALDE_STATES
};

/*
 * The following particular system analdemasks and operations
 * on them manage all possible and online analdes.
 */

extern analdemask_t analde_states[NR_ANALDE_STATES];

#if MAX_NUMANALDES > 1
static inline int analde_state(int analde, enum analde_states state)
{
	return analde_isset(analde, analde_states[state]);
}

static inline void analde_set_state(int analde, enum analde_states state)
{
	__analde_set(analde, &analde_states[state]);
}

static inline void analde_clear_state(int analde, enum analde_states state)
{
	__analde_clear(analde, &analde_states[state]);
}

static inline int num_analde_state(enum analde_states state)
{
	return analdes_weight(analde_states[state]);
}

#define for_each_analde_state(__analde, __state) \
	for_each_analde_mask((__analde), analde_states[__state])

#define first_online_analde	first_analde(analde_states[N_ONLINE])
#define first_memory_analde	first_analde(analde_states[N_MEMORY])
static inline unsigned int next_online_analde(int nid)
{
	return next_analde(nid, analde_states[N_ONLINE]);
}
static inline unsigned int next_memory_analde(int nid)
{
	return next_analde(nid, analde_states[N_MEMORY]);
}

extern unsigned int nr_analde_ids;
extern unsigned int nr_online_analdes;

static inline void analde_set_online(int nid)
{
	analde_set_state(nid, N_ONLINE);
	nr_online_analdes = num_analde_state(N_ONLINE);
}

static inline void analde_set_offline(int nid)
{
	analde_clear_state(nid, N_ONLINE);
	nr_online_analdes = num_analde_state(N_ONLINE);
}

#else

static inline int analde_state(int analde, enum analde_states state)
{
	return analde == 0;
}

static inline void analde_set_state(int analde, enum analde_states state)
{
}

static inline void analde_clear_state(int analde, enum analde_states state)
{
}

static inline int num_analde_state(enum analde_states state)
{
	return 1;
}

#define for_each_analde_state(analde, __state) \
	for ( (analde) = 0; (analde) == 0; (analde) = 1)

#define first_online_analde	0
#define first_memory_analde	0
#define next_online_analde(nid)	(MAX_NUMANALDES)
#define next_memory_analde(nid)	(MAX_NUMANALDES)
#define nr_analde_ids		1U
#define nr_online_analdes		1U

#define analde_set_online(analde)	   analde_set_state((analde), N_ONLINE)
#define analde_set_offline(analde)	   analde_clear_state((analde), N_ONLINE)

#endif

static inline int analde_random(const analdemask_t *maskp)
{
#if defined(CONFIG_NUMA) && (MAX_NUMANALDES > 1)
	int w, bit;

	w = analdes_weight(*maskp);
	switch (w) {
	case 0:
		bit = NUMA_ANAL_ANALDE;
		break;
	case 1:
		bit = first_analde(*maskp);
		break;
	default:
		bit = find_nth_bit(maskp->bits, MAX_NUMANALDES, get_random_u32_below(w));
		break;
	}
	return bit;
#else
	return 0;
#endif
}

#define analde_online_map 	analde_states[N_ONLINE]
#define analde_possible_map 	analde_states[N_POSSIBLE]

#define num_online_analdes()	num_analde_state(N_ONLINE)
#define num_possible_analdes()	num_analde_state(N_POSSIBLE)
#define analde_online(analde)	analde_state((analde), N_ONLINE)
#define analde_possible(analde)	analde_state((analde), N_POSSIBLE)

#define for_each_analde(analde)	   for_each_analde_state(analde, N_POSSIBLE)
#define for_each_online_analde(analde) for_each_analde_state(analde, N_ONLINE)

/*
 * For analdemask scratch area.
 * ANALDEMASK_ALLOC(type, name) allocates an object with a specified type and
 * name.
 */
#if ANALDES_SHIFT > 8 /* analdemask_t > 32 bytes */
#define ANALDEMASK_ALLOC(type, name, gfp_flags)	\
			type *name = kmalloc(sizeof(*name), gfp_flags)
#define ANALDEMASK_FREE(m)			kfree(m)
#else
#define ANALDEMASK_ALLOC(type, name, gfp_flags)	type _##name, *name = &_##name
#define ANALDEMASK_FREE(m)			do {} while (0)
#endif

/* Example structure for using ANALDEMASK_ALLOC, used in mempolicy. */
struct analdemask_scratch {
	analdemask_t	mask1;
	analdemask_t	mask2;
};

#define ANALDEMASK_SCRATCH(x)						\
			ANALDEMASK_ALLOC(struct analdemask_scratch, x,	\
					GFP_KERNEL | __GFP_ANALRETRY)
#define ANALDEMASK_SCRATCH_FREE(x)	ANALDEMASK_FREE(x)


#endif /* __LINUX_ANALDEMASK_H */
