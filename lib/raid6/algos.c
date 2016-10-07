/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6/algos.c
 *
 * Algorithm list and algorithm selection for RAID-6
 */

#include <linux/raid/pq.h>
#ifndef __KERNEL__
#include <sys/mman.h>
#include <stdio.h>
#else
#include <linux/module.h>
#include <linux/gfp.h>
#if !RAID6_USE_EMPTY_ZERO_PAGE
/* In .bss so it's zeroed */
const char raid6_empty_zero_page[PAGE_SIZE] __attribute__((aligned(256)));
EXPORT_SYMBOL(raid6_empty_zero_page);
#endif
#endif

struct raid6_calls raid6_call;
EXPORT_SYMBOL_GPL(raid6_call);

const struct raid6_calls * const raid6_algos[] = {
#if defined(__ia64__)
	&raid6_intx16,
	&raid6_intx32,
#endif
#if defined(__i386__) && !defined(__arch_um__)
	&raid6_mmxx1,
	&raid6_mmxx2,
	&raid6_sse1x1,
	&raid6_sse1x2,
	&raid6_sse2x1,
	&raid6_sse2x2,
#ifdef CONFIG_AS_AVX2
	&raid6_avx2x1,
	&raid6_avx2x2,
#endif
#ifdef CONFIG_AS_AVX512
	&raid6_avx512x1,
	&raid6_avx512x2,
#endif
#endif
#if defined(__x86_64__) && !defined(__arch_um__)
	&raid6_sse2x1,
	&raid6_sse2x2,
	&raid6_sse2x4,
#ifdef CONFIG_AS_AVX2
	&raid6_avx2x1,
	&raid6_avx2x2,
	&raid6_avx2x4,
#endif
#ifdef CONFIG_AS_AVX512
	&raid6_avx512x1,
	&raid6_avx512x2,
	&raid6_avx512x4,
#endif
#endif
#ifdef CONFIG_ALTIVEC
	&raid6_altivec1,
	&raid6_altivec2,
	&raid6_altivec4,
	&raid6_altivec8,
#endif
#if defined(CONFIG_TILEGX)
	&raid6_tilegx8,
#endif
#if defined(CONFIG_S390)
	&raid6_s390vx8,
#endif
	&raid6_intx1,
	&raid6_intx2,
	&raid6_intx4,
	&raid6_intx8,
#ifdef CONFIG_KERNEL_MODE_NEON
	&raid6_neonx1,
	&raid6_neonx2,
	&raid6_neonx4,
	&raid6_neonx8,
#endif
	NULL
};

void (*raid6_2data_recov)(int, size_t, int, int, void **);
EXPORT_SYMBOL_GPL(raid6_2data_recov);

void (*raid6_datap_recov)(int, size_t, int, void **);
EXPORT_SYMBOL_GPL(raid6_datap_recov);

const struct raid6_recov_calls *const raid6_recov_algos[] = {
#ifdef CONFIG_AS_AVX512
	&raid6_recov_avx512,
#endif
#ifdef CONFIG_AS_AVX2
	&raid6_recov_avx2,
#endif
#ifdef CONFIG_AS_SSSE3
	&raid6_recov_ssse3,
#endif
#ifdef CONFIG_S390
	&raid6_recov_s390xc,
#endif
	&raid6_recov_intx1,
	NULL
};

#ifdef __KERNEL__
#define RAID6_TIME_JIFFIES_LG2	4
#else
/* Need more time to be stable in userspace */
#define RAID6_TIME_JIFFIES_LG2	9
#define time_before(x, y) ((x) < (y))
#endif

static inline const struct raid6_recov_calls *raid6_choose_recov(void)
{
	const struct raid6_recov_calls *const *algo;
	const struct raid6_recov_calls *best;

	for (best = NULL, algo = raid6_recov_algos; *algo; algo++)
		if (!best || (*algo)->priority > best->priority)
			if (!(*algo)->valid || (*algo)->valid())
				best = *algo;

	if (best) {
		raid6_2data_recov = best->data2;
		raid6_datap_recov = best->datap;

		pr_info("raid6: using %s recovery algorithm\n", best->name);
	} else
		pr_err("raid6: Yikes! No recovery algorithm found!\n");

	return best;
}

static inline const struct raid6_calls *raid6_choose_gen(
	void *(*const dptrs)[(65536/PAGE_SIZE)+2], const int disks)
{
	unsigned long perf, bestgenperf, bestxorperf, j0, j1;
	int start = (disks>>1)-1, stop = disks-3;	/* work on the second half of the disks */
	const struct raid6_calls *const *algo;
	const struct raid6_calls *best;

	for (bestgenperf = 0, bestxorperf = 0, best = NULL, algo = raid6_algos; *algo; algo++) {
		if (!best || (*algo)->prefer >= best->prefer) {
			if ((*algo)->valid && !(*algo)->valid())
				continue;

			perf = 0;

			preempt_disable();
			j0 = jiffies;
			while ((j1 = jiffies) == j0)
				cpu_relax();
			while (time_before(jiffies,
					    j1 + (1<<RAID6_TIME_JIFFIES_LG2))) {
				(*algo)->gen_syndrome(disks, PAGE_SIZE, *dptrs);
				perf++;
			}
			preempt_enable();

			if (perf > bestgenperf) {
				bestgenperf = perf;
				best = *algo;
			}
			pr_info("raid6: %-8s gen() %5ld MB/s\n", (*algo)->name,
			       (perf*HZ) >> (20-16+RAID6_TIME_JIFFIES_LG2));

			if (!(*algo)->xor_syndrome)
				continue;

			perf = 0;

			preempt_disable();
			j0 = jiffies;
			while ((j1 = jiffies) == j0)
				cpu_relax();
			while (time_before(jiffies,
					    j1 + (1<<RAID6_TIME_JIFFIES_LG2))) {
				(*algo)->xor_syndrome(disks, start, stop,
						      PAGE_SIZE, *dptrs);
				perf++;
			}
			preempt_enable();

			if (best == *algo)
				bestxorperf = perf;

			pr_info("raid6: %-8s xor() %5ld MB/s\n", (*algo)->name,
				(perf*HZ) >> (20-16+RAID6_TIME_JIFFIES_LG2+1));
		}
	}

	if (best) {
		pr_info("raid6: using algorithm %s gen() %ld MB/s\n",
		       best->name,
		       (bestgenperf*HZ) >> (20-16+RAID6_TIME_JIFFIES_LG2));
		if (best->xor_syndrome)
			pr_info("raid6: .... xor() %ld MB/s, rmw enabled\n",
			       (bestxorperf*HZ) >> (20-16+RAID6_TIME_JIFFIES_LG2+1));
		raid6_call = *best;
	} else
		pr_err("raid6: Yikes!  No algorithm found!\n");

	return best;
}


/* Try to pick the best algorithm */
/* This code uses the gfmul table as convenient data set to abuse */

int __init raid6_select_algo(void)
{
	const int disks = (65536/PAGE_SIZE)+2;

	const struct raid6_calls *gen_best;
	const struct raid6_recov_calls *rec_best;
	char *syndromes;
	void *dptrs[(65536/PAGE_SIZE)+2];
	int i;

	for (i = 0; i < disks-2; i++)
		dptrs[i] = ((char *)raid6_gfmul) + PAGE_SIZE*i;

	/* Normal code - use a 2-page allocation to avoid D$ conflict */
	syndromes = (void *) __get_free_pages(GFP_KERNEL, 1);

	if (!syndromes) {
		pr_err("raid6: Yikes!  No memory available.\n");
		return -ENOMEM;
	}

	dptrs[disks-2] = syndromes;
	dptrs[disks-1] = syndromes + PAGE_SIZE;

	/* select raid gen_syndrome function */
	gen_best = raid6_choose_gen(&dptrs, disks);

	/* select raid recover functions */
	rec_best = raid6_choose_recov();

	free_pages((unsigned long)syndromes, 1);

	return gen_best && rec_best ? 0 : -EINVAL;
}

static void raid6_exit(void)
{
	do { } while (0);
}

subsys_initcall(raid6_select_algo);
module_exit(raid6_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID6 Q-syndrome calculations");
