// SPDX-License-Identifier: GPL-2.0

#include <linux/cpuhplock.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/wait.h>

#include <kunit/resource.h>
#include <kunit/test.h>

#include "printk_ringbuffer.h"

/*
 * This KUnit tests the data integrity of the lockless printk_ringbuffer.
 * From multiple CPUs it writes messages of varying length and content while
 * a reader validates the correctness of the messages.
 *
 * IMPORTANT: The more CPUs you can use for this KUnit, the better!
 *
 * The test works by starting "num_online_cpus() - 1" writer threads, each
 * pinned to their own CPU. Each writer thread loops, writing data of varying
 * length into a printk_ringbuffer as fast as possible. The data content is
 * an embedded data struct followed by string content repeating the byte:
 *
 *      'A' + CPUID
 *
 * The reader is running on the remaining online CPU, or if there is only one
 * CPU on the same as the writer.
 * It ensures that the embedded struct content is consistent with the string
 * and that the string * is terminated and is composed of the same repeating
 * byte as its first byte.
 *
 * Because the threads are running in such tight loops, they will call
 * cond_resched() from time to time so the system stays functional.
 *
 * If the reader encounters an error, the test is aborted and some
 * information about the error is reported.
 * The runtime of the test can be configured with the runtime_ms module parameter.
 *
 * Note that the test is performed on a separate printk_ringbuffer instance
 * and not the instance used by printk().
 */

static unsigned long runtime_ms = 10 * MSEC_PER_SEC;
module_param(runtime_ms, ulong, 0400);

/* test data structure */
struct prbtest_rbdata {
	unsigned int size;
	char text[] __counted_by(size);
};

#define MAX_RBDATA_TEXT_SIZE 0x80
#define MAX_PRB_RECORD_SIZE (sizeof(struct prbtest_rbdata) + MAX_RBDATA_TEXT_SIZE)

struct prbtest_data {
	struct kunit *test;
	struct printk_ringbuffer *ringbuffer;
	/* used by writers to signal reader of new records */
	wait_queue_head_t new_record_wait;
};

struct prbtest_thread_data {
	unsigned long num;
	struct prbtest_data *test_data;
};

static void prbtest_fail_record(struct kunit *test, const struct prbtest_rbdata *dat, u64 seq)
{
	unsigned int len;

	len = dat->size - 1;

	KUNIT_FAIL(test, "BAD RECORD: seq=%llu size=%u text=%.*s\n",
		   seq, dat->size,
		   len < MAX_RBDATA_TEXT_SIZE ? len : -1,
		   len < MAX_RBDATA_TEXT_SIZE ? dat->text : "<invalid>");
}

static bool prbtest_check_data(const struct prbtest_rbdata *dat)
{
	unsigned int len;

	/* Sane size? At least one character + trailing '\0' */
	if (dat->size < 2 || dat->size > MAX_RBDATA_TEXT_SIZE)
		return false;

	len = dat->size - 1;
	if (dat->text[len] != '\0')
		return false;

	/* String repeats with the same character? */
	while (len--) {
		if (dat->text[len] != dat->text[0])
			return false;
	}

	return true;
}

static int prbtest_writer(void *data)
{
	struct prbtest_thread_data *tr = data;
	char text_id = 'A' + tr->num;
	struct prb_reserved_entry e;
	struct prbtest_rbdata *dat;
	u32 record_size, text_size;
	unsigned long count = 0;
	struct printk_record r;

	kunit_info(tr->test_data->test, "start thread %03lu (writer)\n", tr->num);

	for (;;) {
		/* ensure at least 1 character + trailing '\0' */
		text_size = get_random_u32_inclusive(2, MAX_RBDATA_TEXT_SIZE);
		if (WARN_ON_ONCE(text_size < 2))
			text_size = 2;
		if (WARN_ON_ONCE(text_size > MAX_RBDATA_TEXT_SIZE))
			text_size = MAX_RBDATA_TEXT_SIZE;

		record_size = sizeof(struct prbtest_rbdata) + text_size;
		WARN_ON_ONCE(record_size > MAX_PRB_RECORD_SIZE);

		/* specify the text sizes for reservation */
		prb_rec_init_wr(&r, record_size);

		/*
		 * Reservation can fail if:
		 *
		 *      - No free descriptor is available.
		 *      - The buffer is full, and the oldest record is reserved
		 *        but not yet committed.
		 *
		 * It actually happens in this test because all CPUs are trying
		 * to write an unbounded number of messages in a tight loop.
		 * These failures are intentionally ignored because this test
		 * focuses on races, ringbuffer consistency, and pushing system
		 * usability limits.
		 */
		if (prb_reserve(&e, tr->test_data->ringbuffer, &r)) {
			r.info->text_len = record_size;

			dat = (struct prbtest_rbdata *)r.text_buf;
			dat->size = text_size;
			memset(dat->text, text_id, text_size - 1);
			dat->text[text_size - 1] = '\0';

			prb_commit(&e);

			wake_up_interruptible(&tr->test_data->new_record_wait);
		}

		if ((count++ & 0x3fff) == 0)
			cond_resched();

		if (kthread_should_stop())
			break;
	}

	kunit_info(tr->test_data->test, "end thread %03lu: wrote=%lu\n", tr->num, count);

	return 0;
}

struct prbtest_wakeup_timer {
	struct timer_list timer;
	struct task_struct *task;
};

static void prbtest_wakeup_callback(struct timer_list *timer)
{
	struct prbtest_wakeup_timer *wakeup = timer_container_of(wakeup, timer, timer);

	set_tsk_thread_flag(wakeup->task, TIF_NOTIFY_SIGNAL);
	wake_up_process(wakeup->task);
}

static int prbtest_reader(struct prbtest_data *test_data, unsigned long timeout_ms)
{
	struct prbtest_wakeup_timer wakeup;
	char text_buf[MAX_PRB_RECORD_SIZE];
	unsigned long count = 0;
	struct printk_info info;
	struct printk_record r;
	u64 seq = 0;

	wakeup.task = current;
	timer_setup_on_stack(&wakeup.timer, prbtest_wakeup_callback, 0);
	mod_timer(&wakeup.timer, jiffies + msecs_to_jiffies(timeout_ms));

	prb_rec_init_rd(&r, &info, text_buf, sizeof(text_buf));

	kunit_info(test_data->test, "start reader\n");

	while (!wait_event_interruptible(test_data->new_record_wait,
					 prb_read_valid(test_data->ringbuffer, seq, &r))) {
		/* check/track the sequence */
		if (info.seq < seq)
			KUNIT_FAIL(test_data->test, "BAD SEQ READ: request=%llu read=%llu\n",
				   seq, info.seq);

		if (!prbtest_check_data((struct prbtest_rbdata *)r.text_buf))
			prbtest_fail_record(test_data->test,
					    (struct prbtest_rbdata *)r.text_buf, info.seq);

		if ((count++ & 0x3fff) == 0)
			cond_resched();

		seq = info.seq + 1;
	}

	timer_delete_sync(&wakeup.timer);
	timer_destroy_on_stack(&wakeup.timer);

	kunit_info(test_data->test, "end reader: read=%lu seq=%llu\n", count, info.seq);

	return 0;
}

KUNIT_DEFINE_ACTION_WRAPPER(prbtest_cpumask_cleanup, free_cpumask_var, struct cpumask *);
KUNIT_DEFINE_ACTION_WRAPPER(prbtest_kthread_cleanup, kthread_stop, struct task_struct *);

static void prbtest_add_cpumask_cleanup(struct kunit *test, cpumask_var_t mask)
{
	int err;

	err = kunit_add_action_or_reset(test, prbtest_cpumask_cleanup, mask);
	KUNIT_ASSERT_EQ(test, err, 0);
}

static void prbtest_add_kthread_cleanup(struct kunit *test, struct task_struct *kthread)
{
	int err;

	err = kunit_add_action_or_reset(test, prbtest_kthread_cleanup, kthread);
	KUNIT_ASSERT_EQ(test, err, 0);
}

static inline void prbtest_prb_reinit(struct printk_ringbuffer *rb)
{
	prb_init(rb, rb->text_data_ring.data, rb->text_data_ring.size_bits, rb->desc_ring.descs,
		 rb->desc_ring.count_bits, rb->desc_ring.infos);
}

static void test_readerwriter(struct kunit *test)
{
	/* Equivalent to CONFIG_LOG_BUF_SHIFT=13 */
	DEFINE_PRINTKRB(test_rb, 8, 5);

	struct prbtest_thread_data *thread_data;
	struct prbtest_data *test_data;
	struct task_struct *thread;
	cpumask_var_t test_cpus;
	int cpu, reader_cpu;

	KUNIT_ASSERT_TRUE(test, alloc_cpumask_var(&test_cpus, GFP_KERNEL));
	prbtest_add_cpumask_cleanup(test, test_cpus);

	cpus_read_lock();
	/*
	 * Failure of KUNIT_ASSERT() kills the current task
	 * so it can not be called while the CPU hotplug lock is held.
	 * Instead use a snapshot of the online CPUs.
	 * If they change during test execution it is unfortunate but not a grave error.
	 */
	cpumask_copy(test_cpus, cpu_online_mask);
	cpus_read_unlock();

	/* One CPU is for the reader, all others are writers */
	reader_cpu = cpumask_first(test_cpus);
	if (cpumask_weight(test_cpus) == 1)
		kunit_warn(test, "more than one CPU is recommended");
	else
		cpumask_clear_cpu(reader_cpu, test_cpus);

	/* KUnit test can get restarted more times. */
	prbtest_prb_reinit(&test_rb);

	test_data = kunit_kmalloc(test, sizeof(*test_data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, test_data);
	test_data->test = test;
	test_data->ringbuffer = &test_rb;
	init_waitqueue_head(&test_data->new_record_wait);

	kunit_info(test, "running for %lu ms\n", runtime_ms);

	for_each_cpu(cpu, test_cpus) {
		thread_data = kunit_kmalloc(test, sizeof(*thread_data), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, thread_data);
		thread_data->test_data = test_data;
		thread_data->num = cpu;

		thread = kthread_run_on_cpu(prbtest_writer, thread_data, cpu,
					    "prbtest writer %u");
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, thread);
		prbtest_add_kthread_cleanup(test, thread);
	}

	kunit_info(test, "starting test\n");

	set_cpus_allowed_ptr(current, cpumask_of(reader_cpu));
	prbtest_reader(test_data, runtime_ms);

	kunit_info(test, "completed test\n");
}

static struct kunit_case prb_test_cases[] = {
	KUNIT_CASE_SLOW(test_readerwriter),
	{}
};

static struct kunit_suite prb_test_suite = {
	.name       = "printk-ringbuffer",
	.test_cases = prb_test_cases,
};
kunit_test_suite(prb_test_suite);

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_AUTHOR("John Ogness <john.ogness@linutronix.de>");
MODULE_DESCRIPTION("printk_ringbuffer KUnit test");
MODULE_LICENSE("GPL");
