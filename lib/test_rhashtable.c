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
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/rhashtable.h>
#include <linux/slab.h>
#include <linux/sched.h>

#define MAX_ENTRIES	1000000
#define TEST_INSERT_FAIL INT_MAX

static int entries = 50000;
module_param(entries, int, 0);
MODULE_PARM_DESC(entries, "Number of entries to add (default: 50000)");

static int runs = 4;
module_param(runs, int, 0);
MODULE_PARM_DESC(runs, "Number of test runs per variant (default: 4)");

static int max_size = 65536;
module_param(max_size, int, 0);
MODULE_PARM_DESC(runs, "Maximum table size (default: 65536)");

static bool shrinking = false;
module_param(shrinking, bool, 0);
MODULE_PARM_DESC(shrinking, "Enable automatic shrinking (default: off)");

static int size = 8;
module_param(size, int, 0);
MODULE_PARM_DESC(size, "Initial size hint of table (default: 8)");

struct test_obj {
	int			value;
	struct rhash_head	node;
};

static struct test_obj array[MAX_ENTRIES];

static struct rhashtable_params test_rht_params = {
	.head_offset = offsetof(struct test_obj, node),
	.key_offset = offsetof(struct test_obj, value),
	.key_len = sizeof(int),
	.hashfn = jhash,
	.nulls_base = (3U << RHT_BASE_SHIFT),
};

static int __init test_rht_lookup(struct rhashtable *ht)
{
	unsigned int i;

	for (i = 0; i < entries * 2; i++) {
		struct test_obj *obj;
		bool expected = !(i % 2);
		u32 key = i;

		if (array[i / 2].value == TEST_INSERT_FAIL)
			expected = false;

		obj = rhashtable_lookup_fast(ht, &key, test_rht_params);

		if (expected && !obj) {
			pr_warn("Test failed: Could not find key %u\n", key);
			return -ENOENT;
		} else if (!expected && obj) {
			pr_warn("Test failed: Unexpected entry found for key %u\n",
				key);
			return -EEXIST;
		} else if (expected && obj) {
			if (obj->value != i) {
				pr_warn("Test failed: Lookup value mismatch %u!=%u\n",
					obj->value, i);
				return -EINVAL;
			}
		}

		cond_resched_rcu();
	}

	return 0;
}

static void test_bucket_stats(struct rhashtable *ht)
{
	unsigned int err, total = 0, chain_len = 0;
	struct rhashtable_iter hti;
	struct rhash_head *pos;

	err = rhashtable_walk_init(ht, &hti);
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

static s64 __init test_rhashtable(struct rhashtable *ht)
{
	struct test_obj *obj;
	int err;
	unsigned int i, insert_fails = 0;
	s64 start, end;

	/*
	 * Insertion Test:
	 * Insert entries into table with all keys even numbers
	 */
	pr_info("  Adding %d keys\n", entries);
	start = ktime_get_ns();
	for (i = 0; i < entries; i++) {
		struct test_obj *obj = &array[i];

		obj->value = i * 2;

		err = rhashtable_insert_fast(ht, &obj->node, test_rht_params);
		if (err == -ENOMEM || err == -EBUSY) {
			/* Mark failed inserts but continue */
			obj->value = TEST_INSERT_FAIL;
			insert_fails++;
		} else if (err) {
			return err;
		}

		cond_resched();
	}

	if (insert_fails)
		pr_info("  %u insertions failed due to memory pressure\n",
			insert_fails);

	test_bucket_stats(ht);
	rcu_read_lock();
	test_rht_lookup(ht);
	rcu_read_unlock();

	test_bucket_stats(ht);

	pr_info("  Deleting %d keys\n", entries);
	for (i = 0; i < entries; i++) {
		u32 key = i * 2;

		if (array[i].value != TEST_INSERT_FAIL) {
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

static int __init test_rht_init(void)
{
	int i, err;
	u64 total_time = 0;

	entries = min(entries, MAX_ENTRIES);

	test_rht_params.automatic_shrinking = shrinking;
	test_rht_params.max_size = max_size;
	test_rht_params.nelem_hint = size;

	pr_info("Running rhashtable test nelem=%d, max_size=%d, shrinking=%d\n",
		size, max_size, shrinking);

	for (i = 0; i < runs; i++) {
		s64 time;

		pr_info("Test %02d:\n", i);
		memset(&array, 0, sizeof(array));
		err = rhashtable_init(&ht, &test_rht_params);
		if (err < 0) {
			pr_warn("Test failed: Unable to initialize hashtable: %d\n",
				err);
			continue;
		}

		time = test_rhashtable(&ht);
		rhashtable_destroy(&ht);
		if (time < 0) {
			pr_warn("Test failed: return code %lld\n", time);
			return -EINVAL;
		}

		total_time += time;
	}

	do_div(total_time, runs);
	pr_info("Average test time: %llu\n", total_time);

	return 0;
}

static void __exit test_rht_exit(void)
{
}

module_init(test_rht_init);
module_exit(test_rht_exit);

MODULE_LICENSE("GPL v2");
