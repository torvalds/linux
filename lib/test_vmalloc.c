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

__param(bool, single_cpu_test, false,
	"Use single first online CPU to run tests");

__param(bool, sequential_test_order, false,
	"Use sequential stress tests order");

__param(int, test_repeat_count, 1,
	"Set test repeat counter");

__param(int, test_loop_count, 1000000,
	"Set test loop counter");

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
		"\t\tid: 1024, name: kvfree_rcu_1_arg_slab_test\n"
		"\t\tid: 2048, name: kvfree_rcu_2_arg_slab_test\n"
		/* Add a new test case description here. */
);

/*
 * Depends on single_cpu_test parameter. If it is true, then
 * use first online CPU to trigger a test on, otherwise go with
 * all online CPUs.
 */
static cpumask_t cpus_run_test_mask = CPU_MASK_NONE;

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
	unsigned long size, align, rnd;
	void *ptr;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		get_random_bytes(&rnd, sizeof(rnd));

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
		get_random_bytes(&n, sizeof(i));
		n = (n % 100) + 1;

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
		ptr = vmalloc(3 * PAGE_SIZE);

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
		unsigned int r;

		get_random_bytes(&r, sizeof(i));
		size = (r % (PAGE_SIZE / 4)) + 1;

		/*
		 * Maximum PAGE_SIZE
		 */
		get_random_bytes(&r, sizeof(i));
		align = 1 << ((i % 11) + 1);

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
		kvfree_rcu(p);
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
kvfree_rcu_1_arg_slab_test(void)
{
	struct test_kvfree_rcu *p;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -1;

		p->array[0] = 'a';
		kvfree_rcu(p);
	}

	return 0;
}

static int
kvfree_rcu_2_arg_slab_test(void)
{
	struct test_kvfree_rcu *p;
	int i;

	for (i = 0; i < test_loop_count; i++) {
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -1;

		p->array[0] = 'a';
		kvfree_rcu(p, rcu);
	}

	return 0;
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
	{ "kvfree_rcu_1_arg_slab_test", kvfree_rcu_1_arg_slab_test },
	{ "kvfree_rcu_2_arg_slab_test", kvfree_rcu_2_arg_slab_test },
	/* Add a new test case here. */
};

struct test_case_data {
	int test_failed;
	int test_passed;
	u64 time;
};

/* Split it to get rid of: WARNING: line over 80 characters */
static struct test_case_data
	per_cpu_test_data[NR_CPUS][ARRAY_SIZE(test_case_array)];

static struct test_driver {
	struct task_struct *task;
	unsigned long start;
	unsigned long stop;
	int cpu;
} per_cpu_test_driver[NR_CPUS];

static void shuffle_array(int *arr, int n)
{
	unsigned int rnd;
	int i, j, x;

	for (i = n - 1; i > 0; i--)  {
		get_random_bytes(&rnd, sizeof(rnd));

		/* Cut the range. */
		j = rnd % i;

		/* Swap indexes. */
		x = arr[i];
		arr[i] = arr[j];
		arr[j] = x;
	}
}

static int test_func(void *private)
{
	struct test_driver *t = private;
	int random_array[ARRAY_SIZE(test_case_array)];
	int index, i, j;
	ktime_t kt;
	u64 delta;

	if (set_cpus_allowed_ptr(current, cpumask_of(t->cpu)) < 0)
		pr_err("Failed to set affinity to %d CPU\n", t->cpu);

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
				per_cpu_test_data[t->cpu][index].test_passed++;
			else
				per_cpu_test_data[t->cpu][index].test_failed++;
		}

		/*
		 * Take an average time that test took.
		 */
		delta = (u64) ktime_us_delta(ktime_get(), kt);
		do_div(delta, (u32) test_repeat_count);

		per_cpu_test_data[t->cpu][index].time = delta;
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

static void
init_test_configurtion(void)
{
	/*
	 * Reset all data of all CPUs.
	 */
	memset(per_cpu_test_data, 0, sizeof(per_cpu_test_data));

	if (single_cpu_test)
		cpumask_set_cpu(cpumask_first(cpu_online_mask),
			&cpus_run_test_mask);
	else
		cpumask_and(&cpus_run_test_mask, cpu_online_mask,
			cpu_online_mask);

	if (test_repeat_count <= 0)
		test_repeat_count = 1;

	if (test_loop_count <= 0)
		test_loop_count = 1;
}

static void do_concurrent_test(void)
{
	int cpu, ret;

	/*
	 * Set some basic configurations plus sanity check.
	 */
	init_test_configurtion();

	/*
	 * Put on hold all workers.
	 */
	down_write(&prepare_for_test_rwsem);

	for_each_cpu(cpu, &cpus_run_test_mask) {
		struct test_driver *t = &per_cpu_test_driver[cpu];

		t->cpu = cpu;
		t->task = kthread_run(test_func, t, "vmalloc_test/%d", cpu);

		if (!IS_ERR(t->task))
			/* Success. */
			atomic_inc(&test_n_undone);
		else
			pr_err("Failed to start kthread for %d CPU\n", cpu);
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

	for_each_cpu(cpu, &cpus_run_test_mask) {
		struct test_driver *t = &per_cpu_test_driver[cpu];
		int i;

		if (!IS_ERR(t->task))
			kthread_stop(t->task);

		for (i = 0; i < ARRAY_SIZE(test_case_array); i++) {
			if (!((run_test_mask & (1 << i)) >> i))
				continue;

			pr_info(
				"Summary: %s passed: %d failed: %d repeat: %d loops: %d avg: %llu usec\n",
				test_case_array[i].test_name,
				per_cpu_test_data[cpu][i].test_passed,
				per_cpu_test_data[cpu][i].test_failed,
				test_repeat_count, test_loop_count,
				per_cpu_test_data[cpu][i].time);
		}

		pr_info("All test took CPU%d=%lu cycles\n",
			cpu, t->stop - t->start);
	}
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
