// SPDX-License-Identifier: GPL-2.0

/*
 * Test module for stress and analyze performance of vmalloc allocator.
 * (C) 2018 Uladzislau Rezki (Sony) <urezki@gmail.com>
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/rwsem.h>
#include <linux/mm.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>

#define __param(type, name, init, msg)		\
	static type name = init;				\
	module_param(name, type, 0444);			\
	MODULE_PARM_DESC(name, msg)				\

__param(int, nr_threads, 0,
	"Number of workers to perform tests(min: 1 max: USHRT_MAX)");

__param(bool, sequential_test_order, false,
	"Use sequential stress tests order");

__param(int, test_repeat_count, 1,
	"Set test repeat counter");

__param(int, test_loop_count, 1000000,
	"Set test loop counter");

__param(int, nr_pages, 0,
	"Set number of pages for fix_size_alloc_test(default: 1)");

__param(bool, use_huge, false,
	"Use vmalloc_huge in fix_size_alloc_test");

__param(int, run_test_mask, INT_MAX,
	"Set tests specified in the mask.\n\n"
		"\t\tid: 1,    name: fix_size_alloc_test\n"
		"\t\tid: 2,    name: full_fit_alloc_test\n"
		"\t\tid: 4,    name: long_busy_list_alloc_test\n"
		"\t\tid: 8,    name: random_size_alloc_test\n"
		"\t\tid: 16,   name: fix_align_alloc_test\n"
		"\t\tid: 32,   name: random_size_align_alloc_test\n"
		"\t\tid: 64,   name: align_shift_alloc_test\n"
		"\t\tid: 128,  name: pcpu_alloc_test\n"
		"\t\tid: 256,  name: kvfree_rcu_1_arg_vmalloc_test\n"
		"\t\tid: 512,  name: kvfree_rcu_2_arg_vmalloc_test\n"
		"\t\tid: 1024, name: vm_map_ram_test\n"
		/* Add a new test case description here. */
);

/*
 * Read write semaphore for synchronization of setup
 * phase that is done in main thread and workers.
 */
static DECLARE_RWSEM(prepare_for_test_rwsem);

/*
 * Completion tracking for worker threads.
 */
static DECLARE_COMPLETION(test_all_done_comp);
static atomic_t test_n_undone = ATOMIC_INIT(0);

static inline void
test_report_one_done(void)
{
	if (atomic_dec_and_test(&test_n_undone))
		complete(&test_all_done_comp);
}

static int random_size_align_alloc_test(void)
{
	unsigned long size, align;
	unsigned int rnd;
	void *ptr;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		rnd = get_random_u8();

		/*
		 * Maximum 1024 pages, if PAGE_SIZE is 4096.
		 */
		align = 1 << (rnd % 23);

		/*
		 * Maximum 10 pages.
		 */
		size = ((rnd % 10) + 1) * PAGE_SIZE;

		ptr = __vmalloc_node(size, align, GFP_KERNEL | __GFP_ZERO, 0,
				__builtin_return_address(0));
		if (!ptr)
			return -1;

		vfree(ptr);
	}

	return 0;
}

/*
 * This test case is supposed to be failed.
 */
static int align_shift_alloc_test(void)
{
	unsigned long align;
	void *ptr;
	int i;

	for (i = 0; i < BITS_PER_LONG; i++) {
		align = ((unsigned long) 1) << i;

		ptr = __vmalloc_node(PAGE_SIZE, align, GFP_KERNEL|__GFP_ZERO, 0,
				__builtin_return_address(0));
		if (!ptr)
			return -1;

		vfree(ptr);
	}

	return 0;
}

static int fix_align_alloc_test(void)
{
	void *ptr;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		ptr = __vmalloc_node(5 * PAGE_SIZE, THREAD_ALIGN << 1,
				GFP_KERNEL | __GFP_ZERO, 0,
				__builtin_return_address(0));
		if (!ptr)
			return -1;

		vfree(ptr);
	}

	return 0;
}

static int random_size_alloc_test(void)
{
	unsigned int n;
	void *p;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		n = get_random_u32_inclusive(1, 100);
		p = vmalloc(n * PAGE_SIZE);

		if (!p)
			return -1;

		*((__u8 *)p) = 1;
		vfree(p);
	}

	return 0;
}

static int long_busy_list_alloc_test(void)
{
	void *ptr_1, *ptr_2;
	void **ptr;
	int rv = -1;
	int i;

	ptr = vmalloc(sizeof(void *) * 15000);
	if (!ptr)
		return rv;

	for (i = 0; i < 15000; i++)
		ptr[i] = vmalloc(1 * PAGE_SIZE);

	for (i = 0; i < test_loop_count; i++) {
		ptr_1 = vmalloc(100 * PAGE_SIZE);
		if (!ptr_1)
			goto leave;

		ptr_2 = vmalloc(1 * PAGE_SIZE);
		if (!ptr_2) {
			vfree(ptr_1);
			goto leave;
		}

		*((__u8 *)ptr_1) = 0;
		*((__u8 *)ptr_2) = 1;

		vfree(ptr_1);
		vfree(ptr_2);
	}

	/*  Success */
	rv = 0;

leave:
	for (i = 0; i < 15000; i++)
		vfree(ptr[i]);

	vfree(ptr);
	return rv;
}

static int full_fit_alloc_test(void)
{
	void **ptr, **junk_ptr, *tmp;
	int junk_length;
	int rv = -1;
	int i;

	junk_length = fls(num_online_cpus());
	junk_length *= (32 * 1024 * 1024 / PAGE_SIZE);

	ptr = vmalloc(sizeof(void *) * junk_length);
	if (!ptr)
		return rv;

	junk_ptr = vmalloc(sizeof(void *) * junk_length);
	if (!junk_ptr) {
		vfree(ptr);
		return rv;
	}

	for (i = 0; i < junk_length; i++) {
		ptr[i] = vmalloc(1 * PAGE_SIZE);
		junk_ptr[i] = vmalloc(1 * PAGE_SIZE);
	}

	for (i = 0; i < junk_length; i++)
		vfree(junk_ptr[i]);

	for (i = 0; i < test_loop_count; i++) {
		tmp = vmalloc(1 * PAGE_SIZE);

		if (!tmp)
			goto error;

		*((__u8 *)tmp) = 1;
		vfree(tmp);
	}

	/* Success */
	rv = 0;

error:
	for (i = 0; i < junk_length; i++)
		vfree(ptr[i]);

	vfree(ptr);
	vfree(junk_ptr);

	return rv;
}

static int fix_size_alloc_test(void)
{
	void *ptr;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		if (use_huge)
			ptr = vmalloc_huge((nr_pages > 0 ? nr_pages:1) * PAGE_SIZE, GFP_KERNEL);
		else
			ptr = vmalloc((nr_pages > 0 ? nr_pages:1) * PAGE_SIZE);

		if (!ptr)
			return -1;

		*((__u8 *)ptr) = 0;

		vfree(ptr);
	}

	return 0;
}

static int
pcpu_alloc_test(void)
{
	int rv = 0;
#ifndef CONFIG_NEED_PER_CPU_KM
	void __percpu **pcpu;
	size_t size, align;
	int i;

	pcpu = vmalloc(sizeof(void __percpu *) * 35000);
	if (!pcpu)
		return -1;

	for (i = 0; i < 35000; i++) {
		size = get_random_u32_inclusive(1, PAGE_SIZE / 4);

		/*
		 * Maximum PAGE_SIZE
		 */
		align = 1 << get_random_u32_inclusive(1, 11);

		pcpu[i] = __alloc_percpu(size, align);
		if (!pcpu[i])
			rv = -1;
	}

	for (i = 0; i < 35000; i++)
		free_percpu(pcpu[i]);

	vfree(pcpu);
#endif
	return rv;
}

struct test_kvfree_rcu {
	struct rcu_head rcu;
	unsigned char array[20];
};

static int
kvfree_rcu_1_arg_vmalloc_test(void)
{
	struct test_kvfree_rcu *p;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		p = vmalloc(1 * PAGE_SIZE);
		if (!p)
			return -1;

		p->array[0] = 'a';
		kvfree_rcu_mightsleep(p);
	}

	return 0;
}

static int
kvfree_rcu_2_arg_vmalloc_test(void)
{
	struct test_kvfree_rcu *p;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		p = vmalloc(1 * PAGE_SIZE);
		if (!p)
			return -1;

		p->array[0] = 'a';
		kvfree_rcu(p, rcu);
	}

	return 0;
}

static int
vm_map_ram_test(void)
{
	unsigned long nr_allocated;
	unsigned int map_nr_pages;
	unsigned char *v_ptr;
	struct page **pages;
	int i;

	map_nr_pages = nr_pages > 0 ? nr_pages:1;
	pages = kmalloc(map_nr_pages * sizeof(struct page), GFP_KERNEL);
	if (!pages)
		return -1;

	nr_allocated = alloc_pages_bulk_array(GFP_KERNEL, map_nr_pages, pages);
	if (nr_allocated != map_nr_pages)
		goto cleanup;

	/* Run the test loop. */
	for (i = 0; i < test_loop_count; i++) {
		v_ptr = vm_map_ram(pages, map_nr_pages, NUMA_NO_NODE);
		*v_ptr = 'a';
		vm_unmap_ram(v_ptr, map_nr_pages);
	}

cleanup:
	for (i = 0; i < nr_allocated; i++)
		__free_page(pages[i]);

	kfree(pages);

	/* 0 indicates success. */
	return nr_allocated != map_nr_pages;
}

struct test_case_desc {
	const char *test_name;
	int (*test_func)(void);
};

static struct test_case_desc test_case_array[] = {
	{ "fix_size_alloc_test", fix_size_alloc_test },
	{ "full_fit_alloc_test", full_fit_alloc_test },
	{ "long_busy_list_alloc_test", long_busy_list_alloc_test },
	{ "random_size_alloc_test", random_size_alloc_test },
	{ "fix_align_alloc_test", fix_align_alloc_test },
	{ "random_size_align_alloc_test", random_size_align_alloc_test },
	{ "align_shift_alloc_test", align_shift_alloc_test },
	{ "pcpu_alloc_test", pcpu_alloc_test },
	{ "kvfree_rcu_1_arg_vmalloc_test", kvfree_rcu_1_arg_vmalloc_test },
	{ "kvfree_rcu_2_arg_vmalloc_test", kvfree_rcu_2_arg_vmalloc_test },
	{ "vm_map_ram_test", vm_map_ram_test },
	/* Add a new test case here. */
};

struct test_case_data {
	int test_failed;
	int test_passed;
	u64 time;
};

static struct test_driver {
	struct task_struct *task;
	struct test_case_data data[ARRAY_SIZE(test_case_array)];

	unsigned long start;
	unsigned long stop;
} *tdriver;

static void shuffle_array(int *arr, int n)
{
	int i, j;

	for (i = n - 1; i > 0; i--)  {
		/* Cut the range. */
		j = get_random_u32_below(i);

		/* Swap indexes. */
		swap(arr[i], arr[j]);
	}
}

static int test_func(void *private)
{
	struct test_driver *t = private;
	int random_array[ARRAY_SIZE(test_case_array)];
	int index, i, j;
	ktime_t kt;
	u64 delta;

	for (i = 0; i < ARRAY_SIZE(test_case_array); i++)
		random_array[i] = i;

	if (!sequential_test_order)
		shuffle_array(random_array, ARRAY_SIZE(test_case_array));

	/*
	 * Block until initialization is done.
	 */
	down_read(&prepare_for_test_rwsem);

	t->start = get_cycles();
	for (i = 0; i < ARRAY_SIZE(test_case_array); i++) {
		index = random_array[i];

		/*
		 * Skip tests if run_test_mask has been specified.
		 */
		if (!((run_test_mask & (1 << index)) >> index))
			continue;

		kt = ktime_get();
		for (j = 0; j < test_repeat_count; j++) {
			if (!test_case_array[index].test_func())
				t->data[index].test_passed++;
			else
				t->data[index].test_failed++;
		}

		/*
		 * Take an average time that test took.
		 */
		delta = (u64) ktime_us_delta(ktime_get(), kt);
		do_div(delta, (u32) test_repeat_count);

		t->data[index].time = delta;
	}
	t->stop = get_cycles();

	up_read(&prepare_for_test_rwsem);
	test_report_one_done();

	/*
	 * Wait for the kthread_stop() call.
	 */
	while (!kthread_should_stop())
		msleep(10);

	return 0;
}

static int
init_test_configurtion(void)
{
	/*
	 * A maximum number of workers is defined as hard-coded
	 * value and set to USHRT_MAX. We add such gap just in
	 * case and for potential heavy stressing.
	 */
	nr_threads = clamp(nr_threads, 1, (int) USHRT_MAX);

	/* Allocate the space for test instances. */
	tdriver = kvcalloc(nr_threads, sizeof(*tdriver), GFP_KERNEL);
	if (tdriver == NULL)
		return -1;

	if (test_repeat_count <= 0)
		test_repeat_count = 1;

	if (test_loop_count <= 0)
		test_loop_count = 1;

	return 0;
}

static void do_concurrent_test(void)
{
	int i, ret;

	/*
	 * Set some basic configurations plus sanity check.
	 */
	ret = init_test_configurtion();
	if (ret < 0)
		return;

	/*
	 * Put on hold all workers.
	 */
	down_write(&prepare_for_test_rwsem);

	for (i = 0; i < nr_threads; i++) {
		struct test_driver *t = &tdriver[i];

		t->task = kthread_run(test_func, t, "vmalloc_test/%d", i);

		if (!IS_ERR(t->task))
			/* Success. */
			atomic_inc(&test_n_undone);
		else
			pr_err("Failed to start %d kthread\n", i);
	}

	/*
	 * Now let the workers do their job.
	 */
	up_write(&prepare_for_test_rwsem);

	/*
	 * Sleep quiet until all workers are done with 1 second
	 * interval. Since the test can take a lot of time we
	 * can run into a stack trace of the hung task. That is
	 * why we go with completion_timeout and HZ value.
	 */
	do {
		ret = wait_for_completion_timeout(&test_all_done_comp, HZ);
	} while (!ret);

	for (i = 0; i < nr_threads; i++) {
		struct test_driver *t = &tdriver[i];
		int j;

		if (!IS_ERR(t->task))
			kthread_stop(t->task);

		for (j = 0; j < ARRAY_SIZE(test_case_array); j++) {
			if (!((run_test_mask & (1 << j)) >> j))
				continue;

			pr_info(
				"Summary: %s passed: %d failed: %d repeat: %d loops: %d avg: %llu usec\n",
				test_case_array[j].test_name,
				t->data[j].test_passed,
				t->data[j].test_failed,
				test_repeat_count, test_loop_count,
				t->data[j].time);
		}

		pr_info("All test took worker%d=%lu cycles\n",
			i, t->stop - t->start);
	}

	kvfree(tdriver);
}

static int vmalloc_test_init(void)
{
	do_concurrent_test();
	return -EAGAIN; /* Fail will directly unload the module */
}

static void vmalloc_test_exit(void)
{
}

module_init(vmalloc_test_init)
module_exit(vmalloc_test_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Uladzislau Rezki");
MODULE_DESCRIPTION("vmalloc test module");
