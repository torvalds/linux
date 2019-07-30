// SPDX-License-Identifier: GPL-2.0-only
/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2014-2015 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
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
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

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

struct test_obj_rhl {
	struct test_obj_val	value;
	struct rhlist_head	list_node;
};

struct thread_data {
	unsigned int entries;
	int id;
	struct task_struct *task;
	struct test_obj *objs;
};

static u32 my_hashfn(const void *data, u32 len, u32 seed)
{
	const struct test_obj_rhl *obj = data;

	return (obj->value.id % 10);
}

static int my_cmpfn(struct rhashtable_compare_arg *arg, const void *obj)
{
	const struct test_obj_rhl *test_obj = obj;
	const struct test_obj_val *val = arg->key;

	return test_obj->value.id - val->id;
}

static struct rhashtable_params test_rht_params = {
	.head_offset = offsetof(struct test_obj, node),
	.key_offset = offsetof(struct test_obj, value),
	.key_len = sizeof(struct test_obj_val),
	.hashfn = jhash,
};

static struct rhashtable_params test_rht_params_dup = {
	.head_offset = offsetof(struct test_obj_rhl, list_node),
	.key_offset = offsetof(struct test_obj_rhl, value),
	.key_len = sizeof(struct test_obj_val),
	.hashfn = jhash,
	.obj_hashfn = my_hashfn,
	.obj_cmpfn = my_cmpfn,
	.nelem_hint = 128,
	.automatic_shrinking = false,
};

static atomic_t startup_count;
static DECLARE_WAIT_QUEUE_HEAD(startup_wait);

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
	unsigned int total = 0, chain_len = 0;
	struct rhashtable_iter hti;
	struct rhash_head *pos;

	rhashtable_walk_enter(ht, &hti);
	rhashtable_walk_start(&hti);

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
static struct rhltable rhlt;

static int __init test_rhltable(unsigned int entries)
{
	struct test_obj_rhl *rhl_test_objects;
	unsigned long *obj_in_table;
	unsigned int i, j, k;
	int ret, err;

	if (entries == 0)
		entries = 1;

	rhl_test_objects = vzalloc(array_size(entries,
					      sizeof(*rhl_test_objects)));
	if (!rhl_test_objects)
		return -ENOMEM;

	ret = -ENOMEM;
	obj_in_table = vzalloc(array_size(sizeof(unsigned long),
					  BITS_TO_LONGS(entries)));
	if (!obj_in_table)
		goto out_free;

	err = rhltable_init(&rhlt, &test_rht_params);
	if (WARN_ON(err))
		goto out_free;

	k = prandom_u32();
	ret = 0;
	for (i = 0; i < entries; i++) {
		rhl_test_objects[i].value.id = k;
		err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node,
				      test_rht_params);
		if (WARN(err, "error %d on element %d\n", err, i))
			break;
		if (err == 0)
			set_bit(i, obj_in_table);
	}

	if (err)
		ret = err;

	pr_info("test %d add/delete pairs into rhlist\n", entries);
	for (i = 0; i < entries; i++) {
		struct rhlist_head *h, *pos;
		struct test_obj_rhl *obj;
		struct test_obj_val key = {
			.id = k,
		};
		bool found;

		rcu_read_lock();
		h = rhltable_lookup(&rhlt, &key, test_rht_params);
		if (WARN(!h, "key not found during iteration %d of %d", i, entries)) {
			rcu_read_unlock();
			break;
		}

		if (i) {
			j = i - 1;
			rhl_for_each_entry_rcu(obj, pos, h, list_node) {
				if (WARN(pos == &rhl_test_objects[j].list_node, "old element found, should be gone"))
					break;
			}
		}

		cond_resched_rcu();

		found = false;

		rhl_for_each_entry_rcu(obj, pos, h, list_node) {
			if (pos == &rhl_test_objects[i].list_node) {
				found = true;
				break;
			}
		}

		rcu_read_unlock();

		if (WARN(!found, "element %d not found", i))
			break;

		err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		WARN(err, "rhltable_remove: err %d for iteration %d\n", err, i);
		if (err == 0)
			clear_bit(i, obj_in_table);
	}

	if (ret == 0 && err)
		ret = err;

	for (i = 0; i < entries; i++) {
		WARN(test_bit(i, obj_in_table), "elem %d allegedly still present", i);

		err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node,
				      test_rht_params);
		if (WARN(err, "error %d on element %d\n", err, i))
			break;
		if (err == 0)
			set_bit(i, obj_in_table);
	}

	pr_info("test %d random rhlist add/delete operations\n", entries);
	for (j = 0; j < entries; j++) {
		u32 i = prandom_u32_max(entries);
		u32 prand = prandom_u32();

		cond_resched();

		if (prand == 0)
			prand = prandom_u32();

		if (prand & 1) {
			prand >>= 1;
			continue;
		}

		err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		if (test_bit(i, obj_in_table)) {
			clear_bit(i, obj_in_table);
			if (WARN(err, "cannot remove element at slot %d", i))
				continue;
		} else {
			if (WARN(err != -ENOENT, "removed non-existent element %d, error %d not %d",
			     i, err, -ENOENT))
				continue;
		}

		if (prand & 1) {
			prand >>= 1;
			continue;
		}

		err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		if (err == 0) {
			if (WARN(test_and_set_bit(i, obj_in_table), "succeeded to insert same object %d", i))
				continue;
		} else {
			if (WARN(!test_bit(i, obj_in_table), "failed to insert object %d", i))
				continue;
		}

		if (prand & 1) {
			prand >>= 1;
			continue;
		}

		i = prandom_u32_max(entries);
		if (test_bit(i, obj_in_table)) {
			err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
			WARN(err, "cannot remove element at slot %d", i);
			if (err == 0)
				clear_bit(i, obj_in_table);
		} else {
			err = rhltable_insert(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
			WARN(err, "failed to insert object %d", i);
			if (err == 0)
				set_bit(i, obj_in_table);
		}
	}

	for (i = 0; i < entries; i++) {
		cond_resched();
		err = rhltable_remove(&rhlt, &rhl_test_objects[i].list_node, test_rht_params);
		if (test_bit(i, obj_in_table)) {
			if (WARN(err, "cannot remove element at slot %d", i))
				continue;
		} else {
			if (WARN(err != -ENOENT, "removed non-existent element, error %d not %d",
				 err, -ENOENT))
			continue;
		}
	}

	rhltable_destroy(&rhlt);
out_free:
	vfree(rhl_test_objects);
	vfree(obj_in_table);
	return ret;
}

static int __init test_rhashtable_max(struct test_obj *array,
				      unsigned int entries)
{
	unsigned int i, insert_retries = 0;
	int err;

	test_rht_params.max_size = roundup_pow_of_two(entries / 8);
	err = rhashtable_init(&ht, &test_rht_params);
	if (err)
		return err;

	for (i = 0; i < ht.max_elems; i++) {
		struct test_obj *obj = &array[i];

		obj->value.id = i * 2;
		err = insert_retry(&ht, obj, test_rht_params);
		if (err > 0)
			insert_retries += err;
		else if (err)
			return err;
	}

	err = insert_retry(&ht, &array[ht.max_elems], test_rht_params);
	if (err == -E2BIG) {
		err = 0;
	} else {
		pr_info("insert element %u should have failed with %d, got %d\n",
				ht.max_elems, -E2BIG, err);
		if (err == 0)
			err = -1;
	}

	rhashtable_destroy(&ht);

	return err;
}

static unsigned int __init print_ht(struct rhltable *rhlt)
{
	struct rhashtable *ht;
	const struct bucket_table *tbl;
	char buff[512] = "";
	unsigned int i, cnt = 0;

	ht = &rhlt->ht;
	/* Take the mutex to avoid RCU warning */
	mutex_lock(&ht->mutex);
	tbl = rht_dereference(ht->tbl, ht);
	for (i = 0; i < tbl->size; i++) {
		struct rhash_head *pos, *next;
		struct test_obj_rhl *p;

		pos = rht_ptr_exclusive(tbl->buckets + i);
		next = !rht_is_a_nulls(pos) ? rht_dereference(pos->next, ht) : NULL;

		if (!rht_is_a_nulls(pos)) {
			sprintf(buff, "%s\nbucket[%d] -> ", buff, i);
		}

		while (!rht_is_a_nulls(pos)) {
			struct rhlist_head *list = container_of(pos, struct rhlist_head, rhead);
			sprintf(buff, "%s[[", buff);
			do {
				pos = &list->rhead;
				list = rht_dereference(list->next, ht);
				p = rht_obj(ht, pos);

				sprintf(buff, "%s val %d (tid=%d)%s", buff, p->value.id, p->value.tid,
					list? ", " : " ");
				cnt++;
			} while (list);

			pos = next,
			next = !rht_is_a_nulls(pos) ?
				rht_dereference(pos->next, ht) : NULL;

			sprintf(buff, "%s]]%s", buff, !rht_is_a_nulls(pos) ? " -> " : "");
		}
	}
	printk(KERN_ERR "\n---- ht: ----%s\n-------------\n", buff);
	mutex_unlock(&ht->mutex);

	return cnt;
}

static int __init test_insert_dup(struct test_obj_rhl *rhl_test_objects,
				  int cnt, bool slow)
{
	struct rhltable *rhlt;
	unsigned int i, ret;
	const char *key;
	int err = 0;

	rhlt = kmalloc(sizeof(*rhlt), GFP_KERNEL);
	if (WARN_ON(!rhlt))
		return -EINVAL;

	err = rhltable_init(rhlt, &test_rht_params_dup);
	if (WARN_ON(err)) {
		kfree(rhlt);
		return err;
	}

	for (i = 0; i < cnt; i++) {
		rhl_test_objects[i].value.tid = i;
		key = rht_obj(&rhlt->ht, &rhl_test_objects[i].list_node.rhead);
		key += test_rht_params_dup.key_offset;

		if (slow) {
			err = PTR_ERR(rhashtable_insert_slow(&rhlt->ht, key,
							     &rhl_test_objects[i].list_node.rhead));
			if (err == -EAGAIN)
				err = 0;
		} else
			err = rhltable_insert(rhlt,
					      &rhl_test_objects[i].list_node,
					      test_rht_params_dup);
		if (WARN(err, "error %d on element %d/%d (%s)\n", err, i, cnt, slow? "slow" : "fast"))
			goto skip_print;
	}

	ret = print_ht(rhlt);
	WARN(ret != cnt, "missing rhltable elements (%d != %d, %s)\n", ret, cnt, slow? "slow" : "fast");

skip_print:
	rhltable_destroy(rhlt);
	kfree(rhlt);

	return 0;
}

static int __init test_insert_duplicates_run(void)
{
	struct test_obj_rhl rhl_test_objects[3] = {};

	pr_info("test inserting duplicates\n");

	/* two different values that map to same bucket */
	rhl_test_objects[0].value.id = 1;
	rhl_test_objects[1].value.id = 21;

	/* and another duplicate with same as [0] value
	 * which will be second on the bucket list */
	rhl_test_objects[2].value.id = rhl_test_objects[0].value.id;

	test_insert_dup(rhl_test_objects, 2, false);
	test_insert_dup(rhl_test_objects, 3, false);
	test_insert_dup(rhl_test_objects, 2, true);
	test_insert_dup(rhl_test_objects, 3, true);

	return 0;
}

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

	if (atomic_dec_and_test(&startup_count))
		wake_up(&startup_wait);
	if (wait_event_interruptible(startup_wait, atomic_read(&startup_count) == -1)) {
		pr_err("  thread[%d]: interrupted\n", tdata->id);
		goto out;
	}

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

	objs = vzalloc(array_size(sizeof(struct test_obj),
				  test_rht_params.max_size + 1));
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

	pr_info("test if its possible to exceed max_size %d: %s\n",
			test_rht_params.max_size, test_rhashtable_max(objs, entries) == 0 ?
			"no, ok" : "YES, failed");
	vfree(objs);

	do_div(total_time, runs);
	pr_info("Average test time: %llu\n", total_time);

	test_insert_duplicates_run();

	if (!tcount)
		return 0;

	pr_info("Testing concurrent rhashtable access from %d threads\n",
	        tcount);
	atomic_set(&startup_count, tcount);
	tdata = vzalloc(array_size(tcount, sizeof(struct thread_data)));
	if (!tdata)
		return -ENOMEM;
	objs  = vzalloc(array3_size(sizeof(struct test_obj), tcount, entries));
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
		if (IS_ERR(tdata[i].task)) {
			pr_err(" kthread_run failed for thread %d\n", i);
			atomic_dec(&startup_count);
		} else {
			started_threads++;
		}
	}
	if (wait_event_interruptible(startup_wait, atomic_read(&startup_count) == 0))
		pr_err("  wait_event interruptible failed\n");
	/* count is 0 now, set it to -1 and wake up all threads together */
	atomic_dec(&startup_count);
	wake_up_all(&startup_wait);
	for (i = 0; i < tcount; i++) {
		if (IS_ERR(tdata[i].task))
			continue;
		if ((err = kthread_stop(tdata[i].task))) {
			pr_warn("Test failed: thread %d returned: %d\n",
			        i, err);
			failed_threads++;
		}
	}
	rhashtable_destroy(&ht);
	vfree(tdata);
	vfree(objs);

	/*
	 * rhltable_remove is very expensive, default values can cause test
	 * to run for 2 minutes or more,  use a smaller number instead.
	 */
	err = test_rhltable(entries / 16);
	pr_info("Started %d threads, %d failed, rhltable test returns %d\n",
	        started_threads, failed_threads, err);
	return 0;
}

static void __exit test_rht_exit(void)
{
}

module_init(test_rht_init);
module_exit(test_rht_exit);

MODULE_LICENSE("GPL v2");
