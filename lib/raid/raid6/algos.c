// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 * Algorithm list and algorithm selection for RAID-6
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/raid/pq.h>
#include <linux/slab.h>
#include <linux/static_call.h>
#include <kunit/visibility.h>
#include "algos.h"

#define RAID6_MAX_ALGOS		16
static const struct raid6_calls *raid6_algos[RAID6_MAX_ALGOS];
static unsigned int raid6_nr_algos;
static const struct raid6_recov_calls *raid6_recov_algo;

/* Selected algorithm */
DEFINE_STATIC_CALL_NULL(raid6_gen_syndrome_impl, *raid6_intx1.gen_syndrome);
DEFINE_STATIC_CALL_NULL(raid6_xor_syndrome_impl, *raid6_intx1.xor_syndrome);
DEFINE_STATIC_CALL_NULL(raid6_recov_2data_impl, *raid6_recov_intx1.data2);
DEFINE_STATIC_CALL_NULL(raid6_recov_datap_impl, *raid6_recov_intx1.datap);

/**
 * raid6_gen_syndrome - generate RAID6 P/Q parity
 * @disks:	number of "disks" to operate on including parity
 * @bytes:	length in bytes of each vector
 * @ptrs:	@disks size array of memory pointers
 *
 * Generate @bytes worth of RAID6 P and Q parity in @ptrs[@disks - 2] and
 * @ptrs[@disks - 1] respectively from the memory pointed to by @ptrs[0] to
 * @ptrs[@disks - 3].
 *
 * @disks must be at least 4, and the memory pointed to by each member of @ptrs
 * must be at least 64-byte aligned.  @bytes must be non-zero and a multiple of
 * 512.
 *
 * See https://kernel.org/pub/linux/kernel/people/hpa/raid6.pdf for underlying
 * algorithm.
 */
void raid6_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(disks < RAID6_MIN_DISKS);

	static_call(raid6_gen_syndrome_impl)(disks, bytes, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_gen_syndrome);

/**
 * raid6_xor_syndrome - update RAID6 P/Q parity
 * @disks:	number of "disks" to operate on including parity
 * @start:	first index into @disk to update
 * @stop:	last index into @disk to update
 * @bytes:	length in bytes of each vector
 * @ptrs:	@disks size array of memory pointers
 *
 * Update @bytes worth of RAID6 P and Q parity in @ptrs[@disks - 2] and
 * @ptrs[@disks - 1] respectively for the memory pointed to by
 * @ptrs[@start..@stop].
 *
 * This is used to update parity in place using the following sequence:
 *
 * 1) call raid6_xor_syndrome(disk, start, stop, ...) for the existing data.
 * 2) update the the data in @ptrs[@start..@stop].
 * 3) call raid6_xor_syndrome(disk, start, stop, ...) for the new data.
 *
 * Data between @start and @stop that is not changed should be filled
 * with a pointer to the kernel zero page.
 *
 * @disks must be at least 4, and the memory pointed to by each member of @ptrs
 * must be at least 64-byte aligned.  @bytes must be non-zero and a multiple of
 * 512.  @stop must be larger or equal to @start.
 */
void raid6_xor_syndrome(int disks, int start, int stop, size_t bytes,
		void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(disks < RAID6_MIN_DISKS);
	WARN_ON_ONCE(stop < start);

	static_call(raid6_xor_syndrome_impl)(disks, start, stop, bytes, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_xor_syndrome);

/*
 * raid6_can_xor_syndrome - check if raid6_xor_syndrome() can be used
 *
 * Returns %true if raid6_can_xor_syndrome() can be used, else %false.
 */
bool raid6_can_xor_syndrome(void)
{
	return !!static_call_query(raid6_xor_syndrome_impl);
}
EXPORT_SYMBOL_GPL(raid6_can_xor_syndrome);

/**
 * raid6_recov_2data - recover two missing data disks
 * @disks:	number of "disks" to operate on including parity
 * @bytes:	length in bytes of each vector
 * @faila:	first failed data disk index
 * @failb:	second failed data disk index
 * @ptrs:	@disks size array of memory pointers
 *
 * Rebuild @bytes of missing data in @ptrs[@faila] and @ptrs[@failb] from the
 * data in the remaining disks and the two parities pointed to by the other
 * indices between 0 and @disks - 1 in @ptrs.  @disks includes the data disks
 * and the two parities.  @faila must be smaller than @failb.
 *
 * Memory pointed to by each pointer in @ptrs must be page aligned and is
 * limited to %PAGE_SIZE.
 */
void raid6_recov_2data(int disks, size_t bytes, int faila, int failb,
		void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(bytes > PAGE_SIZE);
	WARN_ON_ONCE(failb <= faila);

	static_call(raid6_recov_2data_impl)(disks, bytes, faila, failb, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_recov_2data);

/**
 * raid6_recov_datap - recover a missing data disk and missing P-parity
 * @disks:	number of "disks" to operate on including parity
 * @bytes:	length in bytes of each vector
 * @faila:	failed data disk index
 * @ptrs:	@disks size array of memory pointers
 *
 * Rebuild @bytes of missing data in @ptrs[@faila] and the missing P-parity in
 * @ptrs[@disks - 2] from the data in the remaining disks and the Q-parity
 * pointed to by the other indices between 0 and @disks - 1 in @ptrs.  @disks
 * includes the data disks and the two parities.
 *
 * Memory pointed to by each pointer in @ptrs must be page aligned and is
 * limited to %PAGE_SIZE.
 */
void raid6_recov_datap(int disks, size_t bytes, int faila, void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(bytes > PAGE_SIZE);

	static_call(raid6_recov_datap_impl)(disks, bytes, faila, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_recov_datap);

#define RAID6_TIME_JIFFIES_LG2	4
#define RAID6_TEST_DISKS	8

static int raid6_choose_gen(void *(*const dptrs)[RAID6_TEST_DISKS],
		const int disks)
{
	/* work on the second half of the disks */
	int start = (disks >> 1) - 1, stop = disks - 3;
	const struct raid6_calls *best = NULL;
	unsigned long bestgenperf = 0;
	unsigned int i;

	for (i = 0; i < raid6_nr_algos; i++) {
		const struct raid6_calls *algo = raid6_algos[i];
		unsigned long perf = 0, j0, j1;

		preempt_disable();
		j0 = jiffies;
		while ((j1 = jiffies) == j0)
			cpu_relax();
		while (time_before(jiffies,
				    j1 + (1<<RAID6_TIME_JIFFIES_LG2))) {
			algo->gen_syndrome(disks, PAGE_SIZE, *dptrs);
			perf++;
		}
		preempt_enable();

		if (perf > bestgenperf) {
			bestgenperf = perf;
			best = algo;
		}
		pr_info("raid6: %-8s gen() %5ld MB/s\n", algo->name,
			(perf * HZ * (disks-2)) >>
			(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2));
	}

	if (!best) {
		pr_err("raid6: Yikes! No algorithm found!\n");
		return -EINVAL;
	}

	static_call_update(raid6_gen_syndrome_impl, best->gen_syndrome);
	static_call_update(raid6_xor_syndrome_impl, best->xor_syndrome);

	pr_info("raid6: using algorithm %s gen() %ld MB/s\n",
		best->name,
		(bestgenperf * HZ * (disks - 2)) >>
		(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2));

	if (best->xor_syndrome) {
		unsigned long perf = 0, j0, j1;

		preempt_disable();
		j0 = jiffies;
		while ((j1 = jiffies) == j0)
			cpu_relax();
		while (time_before(jiffies,
				   j1 + (1 << RAID6_TIME_JIFFIES_LG2))) {
			best->xor_syndrome(disks, start, stop,
					   PAGE_SIZE, *dptrs);
			perf++;
		}
		preempt_enable();

		pr_info("raid6: .... xor() %ld MB/s, rmw enabled\n",
			(perf * HZ * (disks - 2)) >>
			(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2 + 1));
	}

	return 0;
}


/* Try to pick the best algorithm */
/* This code uses the gfmul table as convenient data set to abuse */

static int __init raid6_select_algo(void)
{
	const int disks = RAID6_TEST_DISKS;
	char *disk_ptr, *p;
	void *dptrs[RAID6_TEST_DISKS];
	int i, cycle;
	int error;

	if (!IS_ENABLED(CONFIG_RAID6_PQ_BENCHMARK) || raid6_nr_algos == 1) {
		pr_info("raid6: skipped pq benchmark and selected %s\n",
			raid6_algos[raid6_nr_algos - 1]->name);
		static_call_update(raid6_gen_syndrome_impl,
				raid6_algos[raid6_nr_algos - 1]->gen_syndrome);
		static_call_update(raid6_xor_syndrome_impl,
				raid6_algos[raid6_nr_algos - 1]->xor_syndrome);
		return 0;
	}

	/* prepare the buffer and fill it circularly with gfmul table */
	disk_ptr = kmalloc(PAGE_SIZE * RAID6_TEST_DISKS, GFP_KERNEL);
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
	error = raid6_choose_gen(&dptrs, disks);

	kfree(disk_ptr);

	return error;
}

/*
 * Register a RAID6 P/Q generation algorithm.  The most optimized/unrolled
 * implementation should be registered last so it will be selected when the
 * boot-time benchmark is disabled.
 */
void __init raid6_algo_add(const struct raid6_calls *algo)
{
	if (WARN_ON_ONCE(raid6_nr_algos == RAID6_MAX_ALGOS))
		return;
	raid6_algos[raid6_nr_algos++] = algo;
}

void __init raid6_algo_add_default(void)
{
	raid6_algo_add(&raid6_intx1);
	raid6_algo_add(&raid6_intx2);
	raid6_algo_add(&raid6_intx4);
	raid6_algo_add(&raid6_intx8);
}

void __init raid6_recov_algo_add(const struct raid6_recov_calls *algo)
{
	if (WARN_ON_ONCE(raid6_recov_algo))
		return;
	raid6_recov_algo = algo;
}

#ifdef CONFIG_RAID6_PQ_ARCH
#include "pq_arch.h"
#else
static inline void arch_raid6_init(void)
{
	raid6_algo_add_default();
}
#endif /* CONFIG_RAID6_PQ_ARCH */

static int __init raid6_init(void)
{
	/*
	 * Architectures providing arch_raid6_init must add all PQ generation
	 * algorithms they want to consider in arch_raid6_init(), including
	 * the generic ones using raid6_algo_add_default() if wanted.
	 */
	arch_raid6_init();

	/*
	 * Architectures don't have to set a recovery algorithm, we'll just pick
	 * the generic integer one if none was set.
	 */
	if (!raid6_recov_algo)
		raid6_recov_algo = &raid6_recov_intx1;
	static_call_update(raid6_recov_2data_impl, raid6_recov_algo->data2);
	static_call_update(raid6_recov_datap_impl, raid6_recov_algo->datap);
	pr_info("raid6: using %s recovery algorithm\n", raid6_recov_algo->name);

	return raid6_select_algo();
}

static void __exit raid6_exit(void)
{
}

subsys_initcall(raid6_init);
module_exit(raid6_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID6 Q-syndrome calculations");

#if IS_ENABLED(CONFIG_RAID6_PQ_KUNIT_TEST)
const struct raid6_calls *raid6_algo_find(unsigned int idx)
{
	if (idx >= raid6_nr_algos) {
		/*
		 * Always include the simplest generic integer implementation in
		 * the unit tests as a baseline.
		 */
		if (idx == raid6_nr_algos &&
		    raid6_algos[0] != &raid6_intx1)
			return &raid6_intx1;
		return NULL;
	}
	return raid6_algos[idx];
}
EXPORT_SYMBOL_IF_KUNIT(raid6_algo_find);

const struct raid6_recov_calls *raid6_recov_algo_find(unsigned int idx)
{
	switch (idx) {
	case 0:
		/* always test the generic integer implementation */
		return &raid6_recov_intx1;
	case 1:
		/* test the optimized implementation if there is one */
		if (raid6_recov_algo != &raid6_recov_intx1)
			return raid6_recov_algo;
		return NULL;
	default:
		return NULL;
	}
}
EXPORT_SYMBOL_IF_KUNIT(raid6_recov_algo_find);
#endif /* CONFIG_RAID6_PQ_KUNIT_TEST */
