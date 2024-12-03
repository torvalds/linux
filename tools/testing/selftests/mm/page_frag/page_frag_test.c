// SPDX-License-Identifier: GPL-2.0

/*
 * Test module for page_frag cache
 *
 * Copyright (C) 2024 Yunsheng Lin <linyunsheng@huawei.com>
 */

#include <linux/module.h>
#include <linux/cpumask.h>
#include <linux/completion.h>
#include <linux/ptr_ring.h>
#include <linux/kthread.h>
#include <linux/page_frag_cache.h>

#define TEST_FAILED_PREFIX	"page_frag_test failed: "

static struct ptr_ring ptr_ring;
static int nr_objs = 512;
static atomic_t nthreads;
static struct completion wait;
static struct page_frag_cache test_nc;
static int test_popped;
static int test_pushed;
static bool force_exit;

static int nr_test = 2000000;
module_param(nr_test, int, 0);
MODULE_PARM_DESC(nr_test, "number of iterations to test");

static bool test_align;
module_param(test_align, bool, 0);
MODULE_PARM_DESC(test_align, "use align API for testing");

static int test_alloc_len = 2048;
module_param(test_alloc_len, int, 0);
MODULE_PARM_DESC(test_alloc_len, "alloc len for testing");

static int test_push_cpu;
module_param(test_push_cpu, int, 0);
MODULE_PARM_DESC(test_push_cpu, "test cpu for pushing fragment");

static int test_pop_cpu;
module_param(test_pop_cpu, int, 0);
MODULE_PARM_DESC(test_pop_cpu, "test cpu for popping fragment");

static int page_frag_pop_thread(void *arg)
{
	struct ptr_ring *ring = arg;

	pr_info("page_frag pop test thread begins on cpu %d\n",
		smp_processor_id());

	while (test_popped < nr_test) {
		void *obj = __ptr_ring_consume(ring);

		if (obj) {
			test_popped++;
			page_frag_free(obj);
		} else {
			if (force_exit)
				break;

			cond_resched();
		}
	}

	if (atomic_dec_and_test(&nthreads))
		complete(&wait);

	pr_info("page_frag pop test thread exits on cpu %d\n",
		smp_processor_id());

	return 0;
}

static int page_frag_push_thread(void *arg)
{
	struct ptr_ring *ring = arg;

	pr_info("page_frag push test thread begins on cpu %d\n",
		smp_processor_id());

	while (test_pushed < nr_test && !force_exit) {
		void *va;
		int ret;

		if (test_align) {
			va = page_frag_alloc_align(&test_nc, test_alloc_len,
						   GFP_KERNEL, SMP_CACHE_BYTES);

			if ((unsigned long)va & (SMP_CACHE_BYTES - 1)) {
				force_exit = true;
				WARN_ONCE(true, TEST_FAILED_PREFIX "unaligned va returned\n");
			}
		} else {
			va = page_frag_alloc(&test_nc, test_alloc_len, GFP_KERNEL);
		}

		if (!va)
			continue;

		ret = __ptr_ring_produce(ring, va);
		if (ret) {
			page_frag_free(va);
			cond_resched();
		} else {
			test_pushed++;
		}
	}

	pr_info("page_frag push test thread exits on cpu %d\n",
		smp_processor_id());

	if (atomic_dec_and_test(&nthreads))
		complete(&wait);

	return 0;
}

static int __init page_frag_test_init(void)
{
	struct task_struct *tsk_push, *tsk_pop;
	int last_pushed = 0, last_popped = 0;
	ktime_t start;
	u64 duration;
	int ret;

	page_frag_cache_init(&test_nc);
	atomic_set(&nthreads, 2);
	init_completion(&wait);

	if (test_alloc_len > PAGE_SIZE || test_alloc_len <= 0 ||
	    !cpu_active(test_push_cpu) || !cpu_active(test_pop_cpu))
		return -EINVAL;

	ret = ptr_ring_init(&ptr_ring, nr_objs, GFP_KERNEL);
	if (ret)
		return ret;

	tsk_push = kthread_create_on_cpu(page_frag_push_thread, &ptr_ring,
					 test_push_cpu, "page_frag_push");
	if (IS_ERR(tsk_push))
		return PTR_ERR(tsk_push);

	tsk_pop = kthread_create_on_cpu(page_frag_pop_thread, &ptr_ring,
					test_pop_cpu, "page_frag_pop");
	if (IS_ERR(tsk_pop)) {
		kthread_stop(tsk_push);
		return PTR_ERR(tsk_pop);
	}

	start = ktime_get();
	wake_up_process(tsk_push);
	wake_up_process(tsk_pop);

	pr_info("waiting for test to complete\n");

	while (!wait_for_completion_timeout(&wait, msecs_to_jiffies(10000))) {
		/* exit if there is no progress for push or pop size */
		if (last_pushed == test_pushed || last_popped == test_popped) {
			WARN_ONCE(true, TEST_FAILED_PREFIX "no progress\n");
			force_exit = true;
			continue;
		}

		last_pushed = test_pushed;
		last_popped = test_popped;
		pr_info("page_frag_test progress: pushed = %d, popped = %d\n",
			test_pushed, test_popped);
	}

	if (force_exit) {
		pr_err(TEST_FAILED_PREFIX "exit with error\n");
		goto out;
	}

	duration = (u64)ktime_us_delta(ktime_get(), start);
	pr_info("%d of iterations for %s testing took: %lluus\n", nr_test,
		test_align ? "aligned" : "non-aligned", duration);

out:
	ptr_ring_cleanup(&ptr_ring, NULL);
	page_frag_cache_drain(&test_nc);

	return -EAGAIN;
}

static void __exit page_frag_test_exit(void)
{
}

module_init(page_frag_test_init);
module_exit(page_frag_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yunsheng Lin <linyunsheng@huawei.com>");
MODULE_DESCRIPTION("Test module for page_frag");
