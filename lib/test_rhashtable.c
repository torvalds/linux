/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2014-2015 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**************************************************************************
 * Self Test
 **************************************************************************/

#include <linux/init.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/rhashtable.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#define MAX_ENTRIES	1000000
#define TEST_INSERT_FAIL INT_MAX

static int parm_entries = 50000;
module_param(parm_entries, int, 0);
MODULE_PARM_DESC(parm_entries, "Number of entries to add (default: 50000)");

static int runs = 4;
module_param(runs, int, 0);
MODULE_PARM_DESC(runs, "Number of test runs per variant (default: 4)");

static int max_size = 0;
module_param(max_size, int, 0);
MODULE_PARM_DESC(max_size, "Maximum table size (default: calculated)");

static bool shrinking = false;
module_param(shrinking, bool, 0);
MODULE_PARM_DESC(shrinking, "Enable automatic shrinking (default: off)");

static int size = 8;
module_param(size, int, 0);
MODULE_PARM_DESC(size, "Initial size hint of table (default: 8)");

static int tcount = 10;
module_param(tcount, int, 0);
MODULE_PARM_DESC(tcount, "Number of threads to spawn (default: 10)");

static bool enomem_retry = false;
module_param(enomem_retry, bool, 0);
MODULE_PARM_DESC(enomem_retry, "Retry insert even if -ENOMEM was returned (default: off)");

struct test_obj_val {
	int	id;
	int	tid;
};

struct test_obj {
	struct test_obj_val	value;
	struct rhash_head	node;
};

struct thread_data {
	unsigned int entries;
	int id;
	struct task_struct *task;
	struct test_obj *objs;
};

static struct rhashtable_params test_rht_params = {
	.head_offset = offsetof(struct test_obj, node),
	.key_offset = offsetof(struct test_obj, value),
	.key_len = sizeof(struct test_obj_val),
	.hashfn = jhash,
	.nulls_base = (3U << RHT_BASE_SHIFT),
};

static struct semaphore prestart_sem;
static struct semaphore startup_sem = __SEMAPHORE_INITIALIZER(startup_sem, 0);

static int insert_retry(struct rhashtable *ht, struct test_obj *obj,
                        const struct rhashtable_params params)
{
	int err, retries = -1, enomem_retries = 0;

	do {
		retries++;
		cond_resched();
		err = rhashtable_insert_fast(ht, &obj->node, params);
		if (err == -ENOMEM && enomem_retry) {
			enomem_retries++;
			err = -EBUSY;
		}
	} while (err == -EBUSY);

	if (enomem_retries)
		pr_info(" %u insertions retried after -ENOMEM\n",
			enomem_retries);

	return err ? : retries;
}

static int __init test_rht_lookup(struct rhashtable *ht, struct test_obj *array,
				  unsigned int entries)
{
	unsigned int i;

	for (i = 0; i < entries; i++) {
		struct test_obj *obj;
		bool expected = !(i % 2);
		struct test_obj_val key = {
			.id = i,
		};

		if (array[i / 2].value.id == TEST_INSERT_FAIL)
			expected = false;

		obj = rhashtable_lookup_fast(ht, &key, test_rht_params);

		if (expected && !obj) {
			pr_warn("Test failed: Could not find key %u\n", key.id);
			return -ENOENT;
		} else if (!expected && obj) {
			pr_warn("Test failed: Unexpected entry found for key %u\n",
				key.id);
			return -EEXIST;
		} else if (expected && obj) {
			if (obj->value.id != i) {
				pr_warn("Test failed: Lookup value mismatch %u!=%u\n",
					obj->value.id, i);
				return -EINVAL;
			}
		}

		cond_resched_rcu();
	}

	return 0;
}

static void test_bucket_stats(struct rhashtable *ht, unsigned int entries)
{
	unsigned int err, total = 0, chain_len = 0;
	struct rhashtable_iter hti;
	struct rhash_head *pos;

	err = rhashtable_walk_init(ht, &hti, GFP_KERNEL);
	if (err) {
		pr_warn("Test failed: allocation error");
		return;
	}

	err = rhashtable_walk_start(&hti);
	if (err && err != -EAGAIN) {
		pr_warn("Test failed: iterator failed: %d\n", err);
		return;
	}

	while ((pos = rhashtable_walk_next(&hti))) {
		if (PTR_ERR(pos) == -EAGAIN) {
			pr_info("Info: encountered resize\n");
			chain_len++;
			continue;
		} else if (IS_ERR(pos)) {
			pr_warn("Test failed: rhashtable_walk_next() error: %ld\n",
				PTR_ERR(pos));
			break;
		}

		total++;
	}

	rhashtable_walk_stop(&hti);
	rhashtable_walk_exit(&hti);

	pr_info("  Traversal complete: counted=%u, nelems=%u, entries=%d, table-jumps=%u\n",
		total, atomic_read(&ht->nelems), entries, chain_len);

	if (total != atomic_read(&ht->nelems) || total != entries)
		pr_warn("Test failed: Total count mismatch ^^^");
}

static s64 __init test_rhashtable(struct rhashtable *ht, struct test_obj *array,
				  unsigned int entries)
{
	struct test_obj *obj;
	int err;
	unsigned int i, insert_retries = 0;
	s64 start, end;

	/*
	 * Insertion Test:
	 * Insert entries into table with all keys even numbers
	 */
	pr_info("  Adding %d keys\n", entries);
	start = ktime_get_ns();
	for (i = 0; i < entries; i++) {
		struct test_obj *obj = &array[i];

		obj->value.id = i * 2;
		err = insert_retry(ht, obj, test_rht_params);
		if (err > 0)
			insert_retries += err;
		else if (err)
			return err;
	}

	if (insert_retries)
		pr_info("  %u insertions retried due to memory pressure\n",
			insert_retries);

	test_bucket_stats(ht, entries);
	rcu_read_lock();
	test_rht_lookup(ht, array, entries);
	rcu_read_unlock();

	test_bucket_stats(ht, entries);

	pr_info("  Deleting %d keys\n", entries);
	for (i = 0; i < entries; i++) {
		struct test_obj_val key = {
			.id = i * 2,
		};

		if (array[i].value.id != TEST_INSERT_FAIL) {
			obj = rhashtable_lookup_fast(ht, &key, test_rht_params);
			BUG_ON(!obj);

			rhashtable_remove_fast(ht, &obj->node, test_rht_params);
		}

		cond_resched();
	}

	end = ktime_get_ns();
	pr_info("  Duration of test: %lld ns\n", end - start);

	return end - start;
}

static struct rhashtable ht;

static int thread_lookup_test(struct thread_data *tdata)
{
	unsigned int entries = tdata->entries;
	int i, err = 0;

	for (i = 0; i < entries; i++) {
		struct test_obj *obj;
		struct test_obj_val key = {
			.id = i,
			.tid = tdata->id,
		};

		obj = rhashtable_lookup_fast(&ht, &key, test_rht_params);
		if (obj && (tdata->objs[i].value.id == TEST_INSERT_FAIL)) {
			pr_err("  found unexpected object %d-%d\n", key.tid, key.id);
			err++;
		} else if (!obj && (tdata->objs[i].value.id != TEST_INSERT_FAIL)) {
			pr_err("  object %d-%d not found!\n", key.tid, key.id);
			err++;
		} else if (obj && memcmp(&obj->value, &key, sizeof(key))) {
			pr_err("  wrong object returned (got %d-%d, expected %d-%d)\n",
			       obj->value.tid, obj->value.id, key.tid, key.id);
			err++;
		}

		cond_resched();
	}
	return err;
}

static int threadfunc(void *data)
{
	int i, step, err = 0, insert_retries = 0;
	struct thread_data *tdata = data;

	up(&prestart_sem);
	if (down_interruptible(&startup_sem))
		pr_err("  thread[%d]: down_interruptible failed\n", tdata->id);

	for (i = 0; i < tdata->entries; i++) {
		tdata->objs[i].value.id = i;
		tdata->objs[i].value.tid = tdata->id;
		err = insert_retry(&ht, &tdata->objs[i], test_rht_params);
		if (err > 0) {
			insert_retries += err;
		} else if (err) {
			pr_err("  thread[%d]: rhashtable_insert_fast failed\n",
			       tdata->id);
			goto out;
		}
	}
	if (insert_retries)
		pr_info("  thread[%d]: %u insertions retried due to memory pressure\n",
			tdata->id, insert_retries);

	err = thread_lookup_test(tdata);
	if (err) {
		pr_err("  thread[%d]: rhashtable_lookup_test failed\n",
		       tdata->id);
		goto out;
	}

	for (step = 10; step > 0; step--) {
		for (i = 0; i < tdata->entries; i += step) {
			if (tdata->objs[i].value.id == TEST_INSERT_FAIL)
				continue;
			err = rhashtable_remove_fast(&ht, &tdata->objs[i].node,
			                             test_rht_params);
			if (err) {
				pr_err("  thread[%d]: rhashtable_remove_fast failed\n",
				       tdata->id);
				goto out;
			}
			tdata->objs[i].value.id = TEST_INSERT_FAIL;

			cond_resched();
		}
		err = thread_lookup_test(tdata);
		if (err) {
			pr_err("  thread[%d]: rhashtable_lookup_test (2) failed\n",
			       tdata->id);
			goto out;
		}
	}
out:
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return err;
}

static int __init test_rht_init(void)
{
	unsigned int entries;
	int i, err, started_threads = 0, failed_threads = 0;
	u64 total_time = 0;
	struct thread_data *tdata;
	struct test_obj *objs;

	if (parm_entries < 0)
		parm_entries = 1;

	entries = min(parm_entries, MAX_ENTRIES);

	test_rht_params.automatic_shrinking = shrinking;
	test_rht_params.max_size = max_size ? : roundup_pow_of_two(entries);
	test_rht_params.nelem_hint = size;

	objs = vzalloc((test_rht_params.max_size + 1) * sizeof(struct test_obj));
	if (!objs)
		return -ENOMEM;

	pr_info("Running rhashtable test nelem=%d, max_size=%d, shrinking=%d\n",
		size, max_size, shrinking);

	for (i = 0; i < runs; i++) {
		s64 time;

		pr_info("Test %02d:\n", i);
		memset(objs, 0, test_rht_params.max_size * sizeof(struct test_obj));

		err = rhashtable_init(&ht, &test_rht_params);
		if (err < 0) {
			pr_warn("Test failed: Unable to initialize hashtable: %d\n",
				err);
			continue;
		}

		time = test_rhashtable(&ht, objs, entries);
		rhashtable_destroy(&ht);
		if (time < 0) {
			vfree(objs);
			pr_warn("Test failed: return code %lld\n", time);
			return -EINVAL;
		}

		total_time += time;
	}

	vfree(objs);
	do_div(total_time, runs);
	pr_info("Average test time: %llu\n", total_time);

	if (!tcount)
		return 0;

	pr_info("Testing concurrent rhashtable access from %d threads\n",
	        tcount);
	sema_init(&prestart_sem, 1 - tcount);
	tdata = vzalloc(tcount * sizeof(struct thread_data));
	if (!tdata)
		return -ENOMEM;
	objs  = vzalloc(tcount * entries * sizeof(struct test_obj));
	if (!objs) {
		vfree(tdata);
		return -ENOMEM;
	}

	test_rht_params.max_size = max_size ? :
	                           roundup_pow_of_two(tcount * entries);
	err = rhashtable_init(&ht, &test_rht_params);
	if (err < 0) {
		pr_warn("Test failed: Unable to initialize hashtable: %d\n",
			err);
		vfree(tdata);
		vfree(objs);
		return -EINVAL;
	}
	for (i = 0; i < tcount; i++) {
		tdata[i].id = i;
		tdata[i].entries = entries;
		tdata[i].objs = objs + i * entries;
		tdata[i].task = kthread_run(threadfunc, &tdata[i],
		                            "rhashtable_thrad[%d]", i);
		if (IS_ERR(tdata[i].task))
			pr_err(" kthread_run failed for thread %d\n", i);
		else
			started_threads++;
	}
	if (down_interruptible(&prestart_sem))
		pr_err("  down interruptible failed\n");
	for (i = 0; i < tcount; i++)
		up(&startup_sem);
	for (i = 0; i < tcount; i++) {
		if (IS_ERR(tdata[i].task))
			continue;
		if ((err = kthread_stop(tdata[i].task))) {
			pr_warn("Test failed: thread %d returned: %d\n",
			        i, err);
			failed_threads++;
		}
	}
	pr_info("Started %d threads, %d failed\n",
	        started_threads, failed_threads);
	rhashtable_destroy(&ht);
	vfree(tdata);
	vfree(objs);
	return 0;
}

static void __exit test_rht_exit(void)
{
}

module_init(test_rht_init);
module_exit(test_rht_exit);

MODULE_LICENSE("GPL v2");
