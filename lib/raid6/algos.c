// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
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
#if defined(__i386__) && !defined(__arch_um__)
#ifdef CONFIG_AS_AVX512
	&raid6_avx512x2,
	&raid6_avx512x1,
#endif
	&raid6_avx2x2,
	&raid6_avx2x1,
	&raid6_sse2x2,
	&raid6_sse2x1,
	&raid6_sse1x2,
	&raid6_sse1x1,
	&raid6_mmxx2,
	&raid6_mmxx1,
#endif
#if defined(__x86_64__) && !defined(__arch_um__)
#ifdef CONFIG_AS_AVX512
	&raid6_avx512x4,
	&raid6_avx512x2,
	&raid6_avx512x1,
#endif
	&raid6_avx2x4,
	&raid6_avx2x2,
	&raid6_avx2x1,
	&raid6_sse2x4,
	&raid6_sse2x2,
	&raid6_sse2x1,
#endif
#ifdef CONFIG_ALTIVEC
	&raid6_vpermxor8,
	&raid6_vpermxor4,
	&raid6_vpermxor2,
	&raid6_vpermxor1,
	&raid6_altivec8,
	&raid6_altivec4,
	&raid6_altivec2,
	&raid6_altivec1,
#endif
#if defined(CONFIG_S390)
	&raid6_s390vx8,
#endif
#ifdef CONFIG_KERNEL_MODE_NEON
	&raid6_neonx8,
	&raid6_neonx4,
	&raid6_neonx2,
	&raid6_neonx1,
#endif
#if defined(__ia64__)
	&raid6_intx32,
	&raid6_intx16,
#endif
	&raid6_intx8,
	&raid6_intx4,
	&raid6_intx2,
	&raid6_intx1,
	NULL
};

void (*raid6_2data_recov)(int, size_t, int, int, void **);
EXPORT_SYMBOL_GPL(raid6_2data_recov);

void (*raid6_datap_recov)(int, size_t, int, void **);
EXPORT_SYMBOL_GPL(raid6_datap_recov);

const struct raid6_recov_calls *const raid6_recov_algos[] = {
#ifdef CONFIG_X86
#ifdef CONFIG_AS_AVX512
	&raid6_recov_avx512,
#endif
	&raid6_recov_avx2,
	&raid6_recov_ssse3,
#endif
#ifdef CONFIG_S390
	&raid6_recov_s390xc,
#endif
#if defined(CONFIG_KERNEL_MODE_NEON)
	&raid6_recov_neon,
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

#define RAID6_TEST_DISKS	8
#define RAID6_TEST_DISKS_ORDER	3

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
	void *(*const dptrs)[RAID6_TEST_DISKS], const int disks)
{
	unsigned long perf, bestgenperf, bestxorperf, j0, j1;
	int start = (disks>>1)-1, stop = disks-3;	/* work on the second half of the disks */
	const struct raid6_calls *const *algo;
	const struct raid6_calls *best;

	for (bestgenperf = 0, bestxorperf = 0, best = NULL, algo = raid6_algos; *algo; algo++) {
		if (!best || (*algo)->prefer >= best->prefer) {
			if ((*algo)->valid && !(*algo)->valid())
				continue;

			if (!IS_ENABLED(CONFIG_RAID6_PQ_BENCHMARK)) {
				best = *algo;
				break;
			}

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
				(perf * HZ * (disks-2)) >>
				(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2));

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
				(perf * HZ * (disks-2)) >>
				(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2 + 1));
		}
	}

	if (best) {
		if (IS_ENABLED(CONFIG_RAID6_PQ_BENCHMARK)) {
			pr_info("raid6: using algorithm %s gen() %ld MB/s\n",
				best->name,
				(bestgenperf * HZ * (disks-2)) >>
				(20 - PAGE_SHIFT+RAID6_TIME_JIFFIES_LG2));
			if (best->xor_syndrome)
				pr_info("raid6: .... xor() %ld MB/s, rmw enabled\n",
					(bestxorperf * HZ * (disks-2)) >>
					(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2 + 1));
		} else
			pr_info("raid6: skip pq benchmark and using algorithm %s\n",
				best->name);
		raid6_call = *best;
	} else
		pr_err("raid6: Yikes!  No algorithm found!\n");

	return best;
}


/* Try to pick the best algorithm */
/* This code uses the gfmul table as convenient data set to abuse */

int __init raid6_select_algo(void)
{
	const int disks = RAID6_TEST_DISKS;

	const struct raid6_calls *gen_best;
	const struct raid6_recov_calls *rec_best;
	char *disk_ptr, *p;
	void *dptrs[RAID6_TEST_DISKS];
	int i, cycle;

	/* prepare the buffer and fill it circularly with gfmul table */
	disk_ptr = (char *)__get_free_pages(GFP_KERNEL, RAID6_TEST_DISKS_ORDER);
	if (!disk_ptr) {
		pr_err("raid6: Yikes!  No memory available.\n");
		return -ENOMEM;
	}

	p = disk_ptr;
	for (i = 0; i < disks; i++)
		dptrs[i] = p + PAGE_SIZE * i;

	cycle = ((disks - 2) * PAGE_SIZE) / 65536;
	for (i = 0; i < cycle; i++) {
		memcpy(p, raid6_gfmul, 65536);
		p += 65536;
	}

	if ((disks - 2) * PAGE_SIZE % 65536)
		memcpy(p, raid6_gfmul, (disks - 2) * PAGE_SIZE % 65536);

	/* select raid gen_syndrome function */
	gen_best = raid6_choose_gen(&dptrs, disks);

	/* select raid recover functions */
	rec_best = raid6_choose_recov();

	free_pages((unsigned long)disk_ptr, RAID6_TEST_DISKS_ORDER);

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
