#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

/*
 * Cpumasks provide a bitmap suitable for representing the
 * set of CPU's in a system, one bit position per CPU number.
 *
 * The new cpumask_ ops take a "struct cpumask *"; the old ones
 * use cpumask_t.
 *
 * See detailed comments in the file linux/bitmap.h describing the
 * data type on which these cpumasks are based.
 *
 * For details of cpumask_scnprintf() and cpumask_parse_user(),
 * see bitmap_scnprintf() and bitmap_parse_user() in lib/bitmap.c.
 * For details of cpulist_scnprintf() and cpulist_parse(), see
 * bitmap_scnlistprintf() and bitmap_parselist(), also in bitmap.c.
 * For details of cpu_remap(), see bitmap_bitremap in lib/bitmap.c
 * For details of cpus_remap(), see bitmap_remap in lib/bitmap.c.
 * For details of cpus_onto(), see bitmap_onto in lib/bitmap.c.
 * For details of cpus_fold(), see bitmap_fold in lib/bitmap.c.
 *
 * . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
 * Note: The alternate operations with the suffix "_nr" are used
 *       to limit the range of the loop to nr_cpu_ids instead of
 *       NR_CPUS when NR_CPUS > 64 for performance reasons.
 *       If NR_CPUS is <= 64 then most assembler bitmask
 *       operators execute faster with a constant range, so
 *       the operator will continue to use NR_CPUS.
 *
 *       Another consideration is that nr_cpu_ids is initialized
 *       to NR_CPUS and isn't lowered until the possible cpus are
 *       discovered (including any disabled cpus).  So early uses
 *       will span the entire range of NR_CPUS.
 * . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
 *
 * The obsolescent cpumask operations are:
 *
 * void cpu_set(cpu, mask)		turn on bit 'cpu' in mask
 * void cpu_clear(cpu, mask)		turn off bit 'cpu' in mask
 * void cpus_setall(mask)		set all bits
 * void cpus_clear(mask)		clear all bits
 * int cpu_isset(cpu, mask)		true iff bit 'cpu' set in mask
 * int cpu_test_and_set(cpu, mask)	test and set bit 'cpu' in mask
 *
 * void cpus_and(dst, src1, src2)	dst = src1 & src2  [intersection]
 * void cpus_or(dst, src1, src2)	dst = src1 | src2  [union]
 * void cpus_xor(dst, src1, src2)	dst = src1 ^ src2
 * void cpus_andnot(dst, src1, src2)	dst = src1 & ~src2
 * void cpus_complement(dst, src)	dst = ~src
 *
 * int cpus_equal(mask1, mask2)		Does mask1 == mask2?
 * int cpus_intersects(mask1, mask2)	Do mask1 and mask2 intersect?
 * int cpus_subset(mask1, mask2)	Is mask1 a subset of mask2?
 * int cpus_empty(mask)			Is mask empty (no bits sets)?
 * int cpus_full(mask)			Is mask full (all bits sets)?
 * int cpus_weight(mask)		Hamming weigh - number of set bits
 * int cpus_weight_nr(mask)		Same using nr_cpu_ids instead of NR_CPUS
 *
 * void cpus_shift_right(dst, src, n)	Shift right
 * void cpus_shift_left(dst, src, n)	Shift left
 *
 * int first_cpu(mask)			Number lowest set bit, or NR_CPUS
 * int next_cpu(cpu, mask)		Next cpu past 'cpu', or NR_CPUS
 * int next_cpu_nr(cpu, mask)		Next cpu past 'cpu', or nr_cpu_ids
 *
 * cpumask_t cpumask_of_cpu(cpu)	Return cpumask with bit 'cpu' set
 *					(can be used as an lvalue)
 * CPU_MASK_ALL				Initializer - all bits set
 * CPU_MASK_NONE			Initializer - no bits set
 * unsigned long *cpus_addr(mask)	Array of unsigned long's in mask
 *
 * CPUMASK_ALLOC kmalloc's a structure that is a composite of many cpumask_t
 * variables, and CPUMASK_PTR provides pointers to each field.
 *
 * The structure should be defined something like this:
 * struct my_cpumasks {
 *	cpumask_t mask1;
 *	cpumask_t mask2;
 * };
 *
 * Usage is then:
 *	CPUMASK_ALLOC(my_cpumasks);
 *	CPUMASK_PTR(mask1, my_cpumasks);
 *	CPUMASK_PTR(mask2, my_cpumasks);
 *
 *	--- DO NOT reference cpumask_t pointers until this check ---
 *	if (my_cpumasks == NULL)
 *		"kmalloc failed"...
 *
 * References are now pointers to the cpumask_t variables (*mask1, ...)
 *
 *if NR_CPUS > BITS_PER_LONG
 *   CPUMASK_ALLOC(m)			Declares and allocates struct m *m =
 *						kmalloc(sizeof(*m), GFP_KERNEL)
 *   CPUMASK_FREE(m)			Macro for kfree(m)
 *else
 *   CPUMASK_ALLOC(m)			Declares struct m _m, *m = &_m
 *   CPUMASK_FREE(m)			Nop
 *endif
 *   CPUMASK_PTR(v, m)			Declares cpumask_t *v = &(m->v)
 * ------------------------------------------------------------------------
 *
 * int cpumask_scnprintf(buf, len, mask) Format cpumask for printing
 * int cpumask_parse_user(ubuf, ulen, mask)	Parse ascii string as cpumask
 * int cpulist_scnprintf(buf, len, mask) Format cpumask as list for printing
 * int cpulist_parse(buf, map)		Parse ascii string as cpulist
 * int cpu_remap(oldbit, old, new)	newbit = map(old, new)(oldbit)
 * void cpus_remap(dst, src, old, new)	*dst = map(old, new)(src)
 * void cpus_onto(dst, orig, relmap)	*dst = orig relative to relmap
 * void cpus_fold(dst, orig, sz)	dst bits = orig bits mod sz
 *
 * for_each_cpu_mask(cpu, mask)		for-loop cpu over mask using NR_CPUS
 * for_each_cpu_mask_nr(cpu, mask)	for-loop cpu over mask using nr_cpu_ids
 *
 * int num_online_cpus()		Number of online CPUs
 * int num_possible_cpus()		Number of all possible CPUs
 * int num_present_cpus()		Number of present CPUs
 *
 * int cpu_online(cpu)			Is some cpu online?
 * int cpu_possible(cpu)		Is some cpu possible?
 * int cpu_present(cpu)			Is some cpu present (can schedule)?
 *
 * int any_online_cpu(mask)		First online cpu in mask
 *
 * for_each_possible_cpu(cpu)		for-loop cpu over cpu_possible_map
 * for_each_online_cpu(cpu)		for-loop cpu over cpu_online_map
 * for_each_present_cpu(cpu)		for-loop cpu over cpu_present_map
 *
 * Subtlety:
 * 1) The 'type-checked' form of cpu_isset() causes gcc (3.3.2, anyway)
 *    to generate slightly worse code.  Note for example the additional
 *    40 lines of assembly code compiling the "for each possible cpu"
 *    loops buried in the disk_stat_read() macros calls when compiling
 *    drivers/block/genhd.c (arch i386, CONFIG_SMP=y).  So use a simple
 *    one-line #define for cpu_isset(), instead of wrapping an inline
 *    inside a macro, the way we do the other calls.
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/bitmap.h>

typedef struct cpumask { DECLARE_BITMAP(bits, NR_CPUS); } cpumask_t;
extern cpumask_t _unused_cpumask_arg_;

#ifndef CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS
#define cpu_set(cpu, dst) __cpu_set((cpu), &(dst))
static inline void __cpu_set(int cpu, volatile cpumask_t *dstp)
{
	set_bit(cpu, dstp->bits);
}

#define cpu_clear(cpu, dst) __cpu_clear((cpu), &(dst))
static inline void __cpu_clear(int cpu, volatile cpumask_t *dstp)
{
	clear_bit(cpu, dstp->bits);
}

#define cpus_setall(dst) __cpus_setall(&(dst), NR_CPUS)
static inline void __cpus_setall(cpumask_t *dstp, int nbits)
{
	bitmap_fill(dstp->bits, nbits);
}

#define cpus_clear(dst) __cpus_clear(&(dst), NR_CPUS)
static inline void __cpus_clear(cpumask_t *dstp, int nbits)
{
	bitmap_zero(dstp->bits, nbits);
}

/* No static inline type checking - see Subtlety (1) above. */
#define cpu_isset(cpu, cpumask) test_bit((cpu), (cpumask).bits)

#define cpu_test_and_set(cpu, cpumask) __cpu_test_and_set((cpu), &(cpumask))
static inline int __cpu_test_and_set(int cpu, cpumask_t *addr)
{
	return test_and_set_bit(cpu, addr->bits);
}

#define cpus_and(dst, src1, src2) __cpus_and(&(dst), &(src1), &(src2), NR_CPUS)
static inline void __cpus_and(cpumask_t *dstp, const cpumask_t *src1p,
					const cpumask_t *src2p, int nbits)
{
	bitmap_and(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define cpus_or(dst, src1, src2) __cpus_or(&(dst), &(src1), &(src2), NR_CPUS)
static inline void __cpus_or(cpumask_t *dstp, const cpumask_t *src1p,
					const cpumask_t *src2p, int nbits)
{
	bitmap_or(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define cpus_xor(dst, src1, src2) __cpus_xor(&(dst), &(src1), &(src2), NR_CPUS)
static inline void __cpus_xor(cpumask_t *dstp, const cpumask_t *src1p,
					const cpumask_t *src2p, int nbits)
{
	bitmap_xor(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define cpus_andnot(dst, src1, src2) \
				__cpus_andnot(&(dst), &(src1), &(src2), NR_CPUS)
static inline void __cpus_andnot(cpumask_t *dstp, const cpumask_t *src1p,
					const cpumask_t *src2p, int nbits)
{
	bitmap_andnot(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define cpus_complement(dst, src) __cpus_complement(&(dst), &(src), NR_CPUS)
static inline void __cpus_complement(cpumask_t *dstp,
					const cpumask_t *srcp, int nbits)
{
	bitmap_complement(dstp->bits, srcp->bits, nbits);
}

#define cpus_equal(src1, src2) __cpus_equal(&(src1), &(src2), NR_CPUS)
static inline int __cpus_equal(const cpumask_t *src1p,
					const cpumask_t *src2p, int nbits)
{
	return bitmap_equal(src1p->bits, src2p->bits, nbits);
}

#define cpus_intersects(src1, src2) __cpus_intersects(&(src1), &(src2), NR_CPUS)
static inline int __cpus_intersects(const cpumask_t *src1p,
					const cpumask_t *src2p, int nbits)
{
	return bitmap_intersects(src1p->bits, src2p->bits, nbits);
}

#define cpus_subset(src1, src2) __cpus_subset(&(src1), &(src2), NR_CPUS)
static inline int __cpus_subset(const cpumask_t *src1p,
					const cpumask_t *src2p, int nbits)
{
	return bitmap_subset(src1p->bits, src2p->bits, nbits);
}

#define cpus_empty(src) __cpus_empty(&(src), NR_CPUS)
static inline int __cpus_empty(const cpumask_t *srcp, int nbits)
{
	return bitmap_empty(srcp->bits, nbits);
}

#define cpus_full(cpumask) __cpus_full(&(cpumask), NR_CPUS)
static inline int __cpus_full(const cpumask_t *srcp, int nbits)
{
	return bitmap_full(srcp->bits, nbits);
}

#define cpus_weight(cpumask) __cpus_weight(&(cpumask), NR_CPUS)
static inline int __cpus_weight(const cpumask_t *srcp, int nbits)
{
	return bitmap_weight(srcp->bits, nbits);
}

#define cpus_shift_right(dst, src, n) \
			__cpus_shift_right(&(dst), &(src), (n), NR_CPUS)
static inline void __cpus_shift_right(cpumask_t *dstp,
					const cpumask_t *srcp, int n, int nbits)
{
	bitmap_shift_right(dstp->bits, srcp->bits, n, nbits);
}

#define cpus_shift_left(dst, src, n) \
			__cpus_shift_left(&(dst), &(src), (n), NR_CPUS)
static inline void __cpus_shift_left(cpumask_t *dstp,
					const cpumask_t *srcp, int n, int nbits)
{
	bitmap_shift_left(dstp->bits, srcp->bits, n, nbits);
}
#endif /* !CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS */

/**
 * to_cpumask - convert an NR_CPUS bitmap to a struct cpumask *
 * @bitmap: the bitmap
 *
 * There are a few places where cpumask_var_t isn't appropriate and
 * static cpumasks must be used (eg. very early boot), yet we don't
 * expose the definition of 'struct cpumask'.
 *
 * This does the conversion, and can be used as a constant initializer.
 */
#define to_cpumask(bitmap)						\
	((struct cpumask *)(1 ? (bitmap)				\
			    : (void *)sizeof(__check_is_bitmap(bitmap))))

static inline int __check_is_bitmap(const unsigned long *bitmap)
{
	return 1;
}

/*
 * Special-case data structure for "single bit set only" constant CPU masks.
 *
 * We pre-generate all the 64 (or 32) possible bit positions, with enough
 * padding to the left and the right, and return the constant pointer
 * appropriately offset.
 */
extern const unsigned long
	cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)];

static inline const struct cpumask *get_cpu_mask(unsigned int cpu)
{
	const unsigned long *p = cpu_bit_bitmap[1 + cpu % BITS_PER_LONG];
	p -= cpu / BITS_PER_LONG;
	return to_cpumask(p);
}

#ifndef CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS
/*
 * In cases where we take the address of the cpumask immediately,
 * gcc optimizes it out (it's a constant) and there's no huge stack
 * variable created:
 */
#define cpumask_of_cpu(cpu) (*get_cpu_mask(cpu))


#define CPU_MASK_LAST_WORD BITMAP_LAST_WORD_MASK(NR_CPUS)

#if NR_CPUS <= BITS_PER_LONG

#define CPU_MASK_ALL							\
(cpumask_t) { {								\
	[BITS_TO_LONGS(NR_CPUS)-1] = CPU_MASK_LAST_WORD			\
} }

#define CPU_MASK_ALL_PTR	(&CPU_MASK_ALL)

#else

#define CPU_MASK_ALL							\
(cpumask_t) { {								\
	[0 ... BITS_TO_LONGS(NR_CPUS)-2] = ~0UL,			\
	[BITS_TO_LONGS(NR_CPUS)-1] = CPU_MASK_LAST_WORD			\
} }

/* cpu_mask_all is in init/main.c */
extern cpumask_t cpu_mask_all;
#define CPU_MASK_ALL_PTR	(&cpu_mask_all)

#endif

#define CPU_MASK_NONE							\
(cpumask_t) { {								\
	[0 ... BITS_TO_LONGS(NR_CPUS)-1] =  0UL				\
} }

#define CPU_MASK_CPU0							\
(cpumask_t) { {								\
	[0] =  1UL							\
} }

#define cpus_addr(src) ((src).bits)

#if NR_CPUS > BITS_PER_LONG
#define	CPUMASK_ALLOC(m)	struct m *m = kmalloc(sizeof(*m), GFP_KERNEL)
#define	CPUMASK_FREE(m)		kfree(m)
#else
#define	CPUMASK_ALLOC(m)	struct m _m, *m = &_m
#define	CPUMASK_FREE(m)
#endif
#define	CPUMASK_PTR(v, m) 	cpumask_t *v = &(m->v)

#define cpu_remap(oldbit, old, new) \
		__cpu_remap((oldbit), &(old), &(new), NR_CPUS)
static inline int __cpu_remap(int oldbit,
		const cpumask_t *oldp, const cpumask_t *newp, int nbits)
{
	return bitmap_bitremap(oldbit, oldp->bits, newp->bits, nbits);
}

#define cpus_remap(dst, src, old, new) \
		__cpus_remap(&(dst), &(src), &(old), &(new), NR_CPUS)
static inline void __cpus_remap(cpumask_t *dstp, const cpumask_t *srcp,
		const cpumask_t *oldp, const cpumask_t *newp, int nbits)
{
	bitmap_remap(dstp->bits, srcp->bits, oldp->bits, newp->bits, nbits);
}

#define cpus_onto(dst, orig, relmap) \
		__cpus_onto(&(dst), &(orig), &(relmap), NR_CPUS)
static inline void __cpus_onto(cpumask_t *dstp, const cpumask_t *origp,
		const cpumask_t *relmapp, int nbits)
{
	bitmap_onto(dstp->bits, origp->bits, relmapp->bits, nbits);
}

#define cpus_fold(dst, orig, sz) \
		__cpus_fold(&(dst), &(orig), sz, NR_CPUS)
static inline void __cpus_fold(cpumask_t *dstp, const cpumask_t *origp,
		int sz, int nbits)
{
	bitmap_fold(dstp->bits, origp->bits, sz, nbits);
}
#endif /* !CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS */

#if NR_CPUS == 1

#define nr_cpu_ids		1
#ifndef CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS
#define first_cpu(src)		({ (void)(src); 0; })
#define next_cpu(n, src)	({ (void)(src); 1; })
#define any_online_cpu(mask)	0
#define for_each_cpu_mask(cpu, mask)	\
	for ((cpu) = 0; (cpu) < 1; (cpu)++, (void)mask)
#endif /* !CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS */
#else /* NR_CPUS > 1 */

extern int nr_cpu_ids;
#ifndef CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS
int __first_cpu(const cpumask_t *srcp);
int __next_cpu(int n, const cpumask_t *srcp);
int __any_online_cpu(const cpumask_t *mask);

#define first_cpu(src)		__first_cpu(&(src))
#define next_cpu(n, src)	__next_cpu((n), &(src))
#define any_online_cpu(mask) __any_online_cpu(&(mask))
#define for_each_cpu_mask(cpu, mask)			\
	for ((cpu) = -1;				\
		(cpu) = next_cpu((cpu), (mask)),	\
		(cpu) < NR_CPUS; )
#endif /* !CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS */
#endif

#ifndef CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS
#if NR_CPUS <= 64

#define next_cpu_nr(n, src)		next_cpu(n, src)
#define cpus_weight_nr(cpumask)		cpus_weight(cpumask)
#define for_each_cpu_mask_nr(cpu, mask)	for_each_cpu_mask(cpu, mask)

#else /* NR_CPUS > 64 */

int __next_cpu_nr(int n, const cpumask_t *srcp);
#define next_cpu_nr(n, src)	__next_cpu_nr((n), &(src))
#define cpus_weight_nr(cpumask)	__cpus_weight(&(cpumask), nr_cpu_ids)
#define for_each_cpu_mask_nr(cpu, mask)			\
	for ((cpu) = -1;				\
		(cpu) = next_cpu_nr((cpu), (mask)),	\
		(cpu) < nr_cpu_ids; )

#endif /* NR_CPUS > 64 */
#endif /* !CONFIG_DISABLE_OBSOLETE_CPUMASK_FUNCTIONS */

/*
 * The following particular system cpumasks and operations manage
 * possible, present, active and online cpus.
 *
 *     cpu_possible_mask- has bit 'cpu' set iff cpu is populatable
 *     cpu_present_mask - has bit 'cpu' set iff cpu is populated
 *     cpu_online_mask  - has bit 'cpu' set iff cpu available to scheduler
 *     cpu_active_mask  - has bit 'cpu' set iff cpu available to migration
 *
 *  If !CONFIG_HOTPLUG_CPU, present == possible, and active == online.
 *
 *  The cpu_possible_mask is fixed at boot time, as the set of CPU id's
 *  that it is possible might ever be plugged in at anytime during the
 *  life of that system boot.  The cpu_present_mask is dynamic(*),
 *  representing which CPUs are currently plugged in.  And
 *  cpu_online_mask is the dynamic subset of cpu_present_mask,
 *  indicating those CPUs available for scheduling.
 *
 *  If HOTPLUG is enabled, then cpu_possible_mask is forced to have
 *  all NR_CPUS bits set, otherwise it is just the set of CPUs that
 *  ACPI reports present at boot.
 *
 *  If HOTPLUG is enabled, then cpu_present_mask varies dynamically,
 *  depending on what ACPI reports as currently plugged in, otherwise
 *  cpu_present_mask is just a copy of cpu_possible_mask.
 *
 *  (*) Well, cpu_present_mask is dynamic in the hotplug case.  If not
 *      hotplug, it's a copy of cpu_possible_mask, hence fixed at boot.
 *
 * Subtleties:
 * 1) UP arch's (NR_CPUS == 1, CONFIG_SMP not defined) hardcode
 *    assumption that their single CPU is online.  The UP
 *    cpu_{online,possible,present}_masks are placebos.  Changing them
 *    will have no useful affect on the following num_*_cpus()
 *    and cpu_*() macros in the UP case.  This ugliness is a UP
 *    optimization - don't waste any instructions or memory references
 *    asking if you're online or how many CPUs there are if there is
 *    only one CPU.
 */

extern const struct cpumask *const cpu_possible_mask;
extern const struct cpumask *const cpu_online_mask;
extern const struct cpumask *const cpu_present_mask;
extern const struct cpumask *const cpu_active_mask;

/* These strip const, as traditionally they weren't const. */
#define cpu_possible_map	(*(cpumask_t *)cpu_possible_mask)
#define cpu_online_map		(*(cpumask_t *)cpu_online_mask)
#define cpu_present_map		(*(cpumask_t *)cpu_present_mask)
#define cpu_active_map		(*(cpumask_t *)cpu_active_mask)

#if NR_CPUS > 1
#define num_online_cpus()	cpumask_weight(cpu_online_mask)
#define num_possible_cpus()	cpumask_weight(cpu_possible_mask)
#define num_present_cpus()	cpumask_weight(cpu_present_mask)
#define cpu_online(cpu)		cpumask_test_cpu((cpu), cpu_online_mask)
#define cpu_possible(cpu)	cpumask_test_cpu((cpu), cpu_possible_mask)
#define cpu_present(cpu)	cpumask_test_cpu((cpu), cpu_present_mask)
#define cpu_active(cpu)		cpumask_test_cpu((cpu), cpu_active_mask)
#else
#define num_online_cpus()	1
#define num_possible_cpus()	1
#define num_present_cpus()	1
#define cpu_online(cpu)		((cpu) == 0)
#define cpu_possible(cpu)	((cpu) == 0)
#define cpu_present(cpu)	((cpu) == 0)
#define cpu_active(cpu)		((cpu) == 0)
#endif

#define cpu_is_offline(cpu)	unlikely(!cpu_online(cpu))

/* These are the new versions of the cpumask operators: passed by pointer.
 * The older versions will be implemented in terms of these, then deleted. */
#define cpumask_bits(maskp) ((maskp)->bits)

#if NR_CPUS <= BITS_PER_LONG
#define CPU_BITS_ALL						\
{								\
	[BITS_TO_LONGS(NR_CPUS)-1] = CPU_MASK_LAST_WORD	\
}

#else /* NR_CPUS > BITS_PER_LONG */

#define CPU_BITS_ALL						\
{								\
	[0 ... BITS_TO_LONGS(NR_CPUS)-2] = ~0UL,		\
	[BITS_TO_LONGS(NR_CPUS)-1] = CPU_MASK_LAST_WORD		\
}
#endif /* NR_CPUS > BITS_PER_LONG */

#ifdef CONFIG_CPUMASK_OFFSTACK
/* Assuming NR_CPUS is huge, a runtime limit is more efficient.  Also,
 * not all bits may be allocated. */
#define nr_cpumask_bits	nr_cpu_ids
#else
#define nr_cpumask_bits	NR_CPUS
#endif

/* verify cpu argument to cpumask_* operators */
static inline unsigned int cpumask_check(unsigned int cpu)
{
#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	WARN_ON_ONCE(cpu >= nr_cpumask_bits);
#endif /* CONFIG_DEBUG_PER_CPU_MAPS */
	return cpu;
}

#if NR_CPUS == 1
/* Uniprocessor.  Assume all masks are "1". */
static inline unsigned int cpumask_first(const struct cpumask *srcp)
{
	return 0;
}

/* Valid inputs for n are -1 and 0. */
static inline unsigned int cpumask_next(int n, const struct cpumask *srcp)
{
	return n+1;
}

static inline unsigned int cpumask_next_zero(int n, const struct cpumask *srcp)
{
	return n+1;
}

static inline unsigned int cpumask_next_and(int n,
					    const struct cpumask *srcp,
					    const struct cpumask *andp)
{
	return n+1;
}

/* cpu must be a valid cpu, ie 0, so there's no other choice. */
static inline unsigned int cpumask_any_but(const struct cpumask *mask,
					   unsigned int cpu)
{
	return 1;
}

#define for_each_cpu(cpu, mask)			\
	for ((cpu) = 0; (cpu) < 1; (cpu)++, (void)mask)
#define for_each_cpu_and(cpu, mask, and)	\
	for ((cpu) = 0; (cpu) < 1; (cpu)++, (void)mask, (void)and)
#else
/**
 * cpumask_first - get the first cpu in a cpumask
 * @srcp: the cpumask pointer
 *
 * Returns >= nr_cpu_ids if no cpus set.
 */
static inline unsigned int cpumask_first(const struct cpumask *srcp)
{
	return find_first_bit(cpumask_bits(srcp), nr_cpumask_bits);
}

/**
 * cpumask_next - get the next cpu in a cpumask
 * @n: the cpu prior to the place to search (ie. return will be > @n)
 * @srcp: the cpumask pointer
 *
 * Returns >= nr_cpu_ids if no further cpus set.
 */
static inline unsigned int cpumask_next(int n, const struct cpumask *srcp)
{
	/* -1 is a legal arg here. */
	if (n != -1)
		cpumask_check(n);
	return find_next_bit(cpumask_bits(srcp), nr_cpumask_bits, n+1);
}

/**
 * cpumask_next_zero - get the next unset cpu in a cpumask
 * @n: the cpu prior to the place to search (ie. return will be > @n)
 * @srcp: the cpumask pointer
 *
 * Returns >= nr_cpu_ids if no further cpus unset.
 */
static inline unsigned int cpumask_next_zero(int n, const struct cpumask *srcp)
{
	/* -1 is a legal arg here. */
	if (n != -1)
		cpumask_check(n);
	return find_next_zero_bit(cpumask_bits(srcp), nr_cpumask_bits, n+1);
}

int cpumask_next_and(int n, const struct cpumask *, const struct cpumask *);
int cpumask_any_but(const struct cpumask *mask, unsigned int cpu);

/**
 * for_each_cpu - iterate over every cpu in a mask
 * @cpu: the (optionally unsigned) integer iterator
 * @mask: the cpumask pointer
 *
 * After the loop, cpu is >= nr_cpu_ids.
 */
#define for_each_cpu(cpu, mask)				\
	for ((cpu) = -1;				\
		(cpu) = cpumask_next((cpu), (mask)),	\
		(cpu) < nr_cpu_ids;)

/**
 * for_each_cpu_and - iterate over every cpu in both masks
 * @cpu: the (optionally unsigned) integer iterator
 * @mask: the first cpumask pointer
 * @and: the second cpumask pointer
 *
 * This saves a temporary CPU mask in many places.  It is equivalent to:
 *	struct cpumask tmp;
 *	cpumask_and(&tmp, &mask, &and);
 *	for_each_cpu(cpu, &tmp)
 *		...
 *
 * After the loop, cpu is >= nr_cpu_ids.
 */
#define for_each_cpu_and(cpu, mask, and)				\
	for ((cpu) = -1;						\
		(cpu) = cpumask_next_and((cpu), (mask), (and)),		\
		(cpu) < nr_cpu_ids;)
#endif /* SMP */

#define CPU_BITS_NONE						\
{								\
	[0 ... BITS_TO_LONGS(NR_CPUS)-1] = 0UL			\
}

#define CPU_BITS_CPU0						\
{								\
	[0] =  1UL						\
}

/**
 * cpumask_set_cpu - set a cpu in a cpumask
 * @cpu: cpu number (< nr_cpu_ids)
 * @dstp: the cpumask pointer
 */
static inline void cpumask_set_cpu(unsigned int cpu, struct cpumask *dstp)
{
	set_bit(cpumask_check(cpu), cpumask_bits(dstp));
}

/**
 * cpumask_clear_cpu - clear a cpu in a cpumask
 * @cpu: cpu number (< nr_cpu_ids)
 * @dstp: the cpumask pointer
 */
static inline void cpumask_clear_cpu(int cpu, struct cpumask *dstp)
{
	clear_bit(cpumask_check(cpu), cpumask_bits(dstp));
}

/**
 * cpumask_test_cpu - test for a cpu in a cpumask
 * @cpu: cpu number (< nr_cpu_ids)
 * @cpumask: the cpumask pointer
 *
 * No static inline type checking - see Subtlety (1) above.
 */
#define cpumask_test_cpu(cpu, cpumask) \
	test_bit(cpumask_check(cpu), cpumask_bits((cpumask)))

/**
 * cpumask_test_and_set_cpu - atomically test and set a cpu in a cpumask
 * @cpu: cpu number (< nr_cpu_ids)
 * @cpumask: the cpumask pointer
 *
 * test_and_set_bit wrapper for cpumasks.
 */
static inline int cpumask_test_and_set_cpu(int cpu, struct cpumask *cpumask)
{
	return test_and_set_bit(cpumask_check(cpu), cpumask_bits(cpumask));
}

/**
 * cpumask_setall - set all cpus (< nr_cpu_ids) in a cpumask
 * @dstp: the cpumask pointer
 */
static inline void cpumask_setall(struct cpumask *dstp)
{
	bitmap_fill(cpumask_bits(dstp), nr_cpumask_bits);
}

/**
 * cpumask_clear - clear all cpus (< nr_cpu_ids) in a cpumask
 * @dstp: the cpumask pointer
 */
static inline void cpumask_clear(struct cpumask *dstp)
{
	bitmap_zero(cpumask_bits(dstp), nr_cpumask_bits);
}

/**
 * cpumask_and - *dstp = *src1p & *src2p
 * @dstp: the cpumask result
 * @src1p: the first input
 * @src2p: the second input
 */
static inline void cpumask_and(struct cpumask *dstp,
			       const struct cpumask *src1p,
			       const struct cpumask *src2p)
{
	bitmap_and(cpumask_bits(dstp), cpumask_bits(src1p),
				       cpumask_bits(src2p), nr_cpumask_bits);
}

/**
 * cpumask_or - *dstp = *src1p | *src2p
 * @dstp: the cpumask result
 * @src1p: the first input
 * @src2p: the second input
 */
static inline void cpumask_or(struct cpumask *dstp, const struct cpumask *src1p,
			      const struct cpumask *src2p)
{
	bitmap_or(cpumask_bits(dstp), cpumask_bits(src1p),
				      cpumask_bits(src2p), nr_cpumask_bits);
}

/**
 * cpumask_xor - *dstp = *src1p ^ *src2p
 * @dstp: the cpumask result
 * @src1p: the first input
 * @src2p: the second input
 */
static inline void cpumask_xor(struct cpumask *dstp,
			       const struct cpumask *src1p,
			       const struct cpumask *src2p)
{
	bitmap_xor(cpumask_bits(dstp), cpumask_bits(src1p),
				       cpumask_bits(src2p), nr_cpumask_bits);
}

/**
 * cpumask_andnot - *dstp = *src1p & ~*src2p
 * @dstp: the cpumask result
 * @src1p: the first input
 * @src2p: the second input
 */
static inline void cpumask_andnot(struct cpumask *dstp,
				  const struct cpumask *src1p,
				  const struct cpumask *src2p)
{
	bitmap_andnot(cpumask_bits(dstp), cpumask_bits(src1p),
					  cpumask_bits(src2p), nr_cpumask_bits);
}

/**
 * cpumask_complement - *dstp = ~*srcp
 * @dstp: the cpumask result
 * @srcp: the input to invert
 */
static inline void cpumask_complement(struct cpumask *dstp,
				      const struct cpumask *srcp)
{
	bitmap_complement(cpumask_bits(dstp), cpumask_bits(srcp),
					      nr_cpumask_bits);
}

/**
 * cpumask_equal - *src1p == *src2p
 * @src1p: the first input
 * @src2p: the second input
 */
static inline bool cpumask_equal(const struct cpumask *src1p,
				const struct cpumask *src2p)
{
	return bitmap_equal(cpumask_bits(src1p), cpumask_bits(src2p),
						 nr_cpumask_bits);
}

/**
 * cpumask_intersects - (*src1p & *src2p) != 0
 * @src1p: the first input
 * @src2p: the second input
 */
static inline bool cpumask_intersects(const struct cpumask *src1p,
				     const struct cpumask *src2p)
{
	return bitmap_intersects(cpumask_bits(src1p), cpumask_bits(src2p),
						      nr_cpumask_bits);
}

/**
 * cpumask_subset - (*src1p & ~*src2p) == 0
 * @src1p: the first input
 * @src2p: the second input
 */
static inline int cpumask_subset(const struct cpumask *src1p,
				 const struct cpumask *src2p)
{
	return bitmap_subset(cpumask_bits(src1p), cpumask_bits(src2p),
						  nr_cpumask_bits);
}

/**
 * cpumask_empty - *srcp == 0
 * @srcp: the cpumask to that all cpus < nr_cpu_ids are clear.
 */
static inline bool cpumask_empty(const struct cpumask *srcp)
{
	return bitmap_empty(cpumask_bits(srcp), nr_cpumask_bits);
}

/**
 * cpumask_full - *srcp == 0xFFFFFFFF...
 * @srcp: the cpumask to that all cpus < nr_cpu_ids are set.
 */
static inline bool cpumask_full(const struct cpumask *srcp)
{
	return bitmap_full(cpumask_bits(srcp), nr_cpumask_bits);
}

/**
 * cpumask_weight - Count of bits in *srcp
 * @srcp: the cpumask to count bits (< nr_cpu_ids) in.
 */
static inline unsigned int cpumask_weight(const struct cpumask *srcp)
{
	return bitmap_weight(cpumask_bits(srcp), nr_cpumask_bits);
}

/**
 * cpumask_shift_right - *dstp = *srcp >> n
 * @dstp: the cpumask result
 * @srcp: the input to shift
 * @n: the number of bits to shift by
 */
static inline void cpumask_shift_right(struct cpumask *dstp,
				       const struct cpumask *srcp, int n)
{
	bitmap_shift_right(cpumask_bits(dstp), cpumask_bits(srcp), n,
					       nr_cpumask_bits);
}

/**
 * cpumask_shift_left - *dstp = *srcp << n
 * @dstp: the cpumask result
 * @srcp: the input to shift
 * @n: the number of bits to shift by
 */
static inline void cpumask_shift_left(struct cpumask *dstp,
				      const struct cpumask *srcp, int n)
{
	bitmap_shift_left(cpumask_bits(dstp), cpumask_bits(srcp), n,
					      nr_cpumask_bits);
}

/**
 * cpumask_copy - *dstp = *srcp
 * @dstp: the result
 * @srcp: the input cpumask
 */
static inline void cpumask_copy(struct cpumask *dstp,
				const struct cpumask *srcp)
{
	bitmap_copy(cpumask_bits(dstp), cpumask_bits(srcp), nr_cpumask_bits);
}

/**
 * cpumask_any - pick a "random" cpu from *srcp
 * @srcp: the input cpumask
 *
 * Returns >= nr_cpu_ids if no cpus set.
 */
#define cpumask_any(srcp) cpumask_first(srcp)

/**
 * cpumask_first_and - return the first cpu from *srcp1 & *srcp2
 * @src1p: the first input
 * @src2p: the second input
 *
 * Returns >= nr_cpu_ids if no cpus set in both.  See also cpumask_next_and().
 */
#define cpumask_first_and(src1p, src2p) cpumask_next_and(-1, (src1p), (src2p))

/**
 * cpumask_any_and - pick a "random" cpu from *mask1 & *mask2
 * @mask1: the first input cpumask
 * @mask2: the second input cpumask
 *
 * Returns >= nr_cpu_ids if no cpus set.
 */
#define cpumask_any_and(mask1, mask2) cpumask_first_and((mask1), (mask2))

/**
 * cpumask_of - the cpumask containing just a given cpu
 * @cpu: the cpu (<= nr_cpu_ids)
 */
#define cpumask_of(cpu) (get_cpu_mask(cpu))

/**
 * cpumask_scnprintf - print a cpumask into a string as comma-separated hex
 * @buf: the buffer to sprintf into
 * @len: the length of the buffer
 * @srcp: the cpumask to print
 *
 * If len is zero, returns zero.  Otherwise returns the length of the
 * (nul-terminated) @buf string.
 */
static inline int cpumask_scnprintf(char *buf, int len,
				    const struct cpumask *srcp)
{
	return bitmap_scnprintf(buf, len, cpumask_bits(srcp), nr_cpumask_bits);
}

/**
 * cpumask_parse_user - extract a cpumask from a user string
 * @buf: the buffer to extract from
 * @len: the length of the buffer
 * @dstp: the cpumask to set.
 *
 * Returns -errno, or 0 for success.
 */
static inline int cpumask_parse_user(const char __user *buf, int len,
				     struct cpumask *dstp)
{
	return bitmap_parse_user(buf, len, cpumask_bits(dstp), nr_cpumask_bits);
}

/**
 * cpulist_scnprintf - print a cpumask into a string as comma-separated list
 * @buf: the buffer to sprintf into
 * @len: the length of the buffer
 * @srcp: the cpumask to print
 *
 * If len is zero, returns zero.  Otherwise returns the length of the
 * (nul-terminated) @buf string.
 */
static inline int cpulist_scnprintf(char *buf, int len,
				    const struct cpumask *srcp)
{
	return bitmap_scnlistprintf(buf, len, cpumask_bits(srcp),
				    nr_cpumask_bits);
}

/**
 * cpulist_parse_user - extract a cpumask from a user string of ranges
 * @buf: the buffer to extract from
 * @len: the length of the buffer
 * @dstp: the cpumask to set.
 *
 * Returns -errno, or 0 for success.
 */
static inline int cpulist_parse(const char *buf, struct cpumask *dstp)
{
	return bitmap_parselist(buf, cpumask_bits(dstp), nr_cpumask_bits);
}

/**
 * cpumask_size - size to allocate for a 'struct cpumask' in bytes
 *
 * This will eventually be a runtime variable, depending on nr_cpu_ids.
 */
static inline size_t cpumask_size(void)
{
	/* FIXME: Once all cpumask assignments are eliminated, this
	 * can be nr_cpumask_bits */
	return BITS_TO_LONGS(NR_CPUS) * sizeof(long);
}

/*
 * cpumask_var_t: struct cpumask for stack usage.
 *
 * Oh, the wicked games we play!  In order to make kernel coding a
 * little more difficult, we typedef cpumask_var_t to an array or a
 * pointer: doing &mask on an array is a noop, so it still works.
 *
 * ie.
 *	cpumask_var_t tmpmask;
 *	if (!alloc_cpumask_var(&tmpmask, GFP_KERNEL))
 *		return -ENOMEM;
 *
 *	  ... use 'tmpmask' like a normal struct cpumask * ...
 *
 *	free_cpumask_var(tmpmask);
 */
#ifdef CONFIG_CPUMASK_OFFSTACK
typedef struct cpumask *cpumask_var_t;

bool alloc_cpumask_var_node(cpumask_var_t *mask, gfp_t flags, int node);
bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags);
bool zalloc_cpumask_var_node(cpumask_var_t *mask, gfp_t flags, int node);
bool zalloc_cpumask_var(cpumask_var_t *mask, gfp_t flags);
void alloc_bootmem_cpumask_var(cpumask_var_t *mask);
void free_cpumask_var(cpumask_var_t mask);
void free_bootmem_cpumask_var(cpumask_var_t mask);

#else
typedef struct cpumask cpumask_var_t[1];

static inline bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	return true;
}

static inline bool alloc_cpumask_var_node(cpumask_var_t *mask, gfp_t flags,
					  int node)
{
	return true;
}

static inline bool zalloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	cpumask_clear(*mask);
	return true;
}

static inline bool zalloc_cpumask_var_node(cpumask_var_t *mask, gfp_t flags,
					  int node)
{
	cpumask_clear(*mask);
	return true;
}

static inline void alloc_bootmem_cpumask_var(cpumask_var_t *mask)
{
}

static inline void free_cpumask_var(cpumask_var_t mask)
{
}

static inline void free_bootmem_cpumask_var(cpumask_var_t mask)
{
}
#endif /* CONFIG_CPUMASK_OFFSTACK */

/* It's common to want to use cpu_all_mask in struct member initializers,
 * so it has to refer to an address rather than a pointer. */
extern const DECLARE_BITMAP(cpu_all_bits, NR_CPUS);
#define cpu_all_mask to_cpumask(cpu_all_bits)

/* First bits of cpu_bit_bitmap are in fact unset. */
#define cpu_none_mask to_cpumask(cpu_bit_bitmap[0])

#define for_each_possible_cpu(cpu) for_each_cpu((cpu), cpu_possible_mask)
#define for_each_online_cpu(cpu)   for_each_cpu((cpu), cpu_online_mask)
#define for_each_present_cpu(cpu)  for_each_cpu((cpu), cpu_present_mask)

/* Wrappers for arch boot code to manipulate normally-constant masks */
void set_cpu_possible(unsigned int cpu, bool possible);
void set_cpu_present(unsigned int cpu, bool present);
void set_cpu_online(unsigned int cpu, bool online);
void set_cpu_active(unsigned int cpu, bool active);
void init_cpu_present(const struct cpumask *src);
void init_cpu_possible(const struct cpumask *src);
void init_cpu_online(const struct cpumask *src);
#endif /* __LINUX_CPUMASK_H */
