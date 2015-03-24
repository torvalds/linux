/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2014 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
 *
 * Based on the following paper:
 * https://www.usenix.org/legacy/event/atc11/tech/final_files/Triplett.pdf
 *
 * Code partially derived from nft_hash
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


#define TEST_HT_SIZE	8
#define TEST_ENTRIES	2048
#define TEST_PTR	((void *) 0xdeadbeef)
#define TEST_NEXPANDS	4

struct test_obj {
	void			*ptr;
	int			value;
	struct rhash_head	node;
};

static const struct rhashtable_params test_rht_params = {
	.nelem_hint = TEST_HT_SIZE,
	.head_offset = offsetof(struct test_obj, node),
	.key_offset = offsetof(struct test_obj, value),
	.key_len = sizeof(int),
	.hashfn = jhash,
	.max_size = 2, /* we expand/shrink manually here */
	.nulls_base = (3U << RHT_BASE_SHIFT),
};

static int __init test_rht_lookup(struct rhashtable *ht)
{
	unsigned int i;

	for (i = 0; i < TEST_ENTRIES * 2; i++) {
		struct test_obj *obj;
		bool expected = !(i % 2);
		u32 key = i;

		obj = rhashtable_lookup_fast(ht, &key, test_rht_params);

		if (expected && !obj) {
			pr_warn("Test failed: Could not find key %u\n", key);
			return -ENOENT;
		} else if (!expected && obj) {
			pr_warn("Test failed: Unexpected entry found for key %u\n",
				key);
			return -EEXIST;
		} else if (expected && obj) {
			if (obj->ptr != TEST_PTR || obj->value != i) {
				pr_warn("Test failed: Lookup value mismatch %p!=%p, %u!=%u\n",
					obj->ptr, TEST_PTR, obj->value, i);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void test_bucket_stats(struct rhashtable *ht, bool quiet)
{
	unsigned int cnt, rcu_cnt, i, total = 0;
	struct rhash_head *pos;
	struct test_obj *obj;
	struct bucket_table *tbl;

	tbl = rht_dereference_rcu(ht->tbl, ht);
	for (i = 0; i < tbl->size; i++) {
		rcu_cnt = cnt = 0;

		if (!quiet)
			pr_info(" [%#4x/%u]", i, tbl->size);

		rht_for_each_entry_rcu(obj, pos, tbl, i, node) {
			cnt++;
			total++;
			if (!quiet)
				pr_cont(" [%p],", obj);
		}

		rht_for_each_entry_rcu(obj, pos, tbl, i, node)
			rcu_cnt++;

		if (rcu_cnt != cnt)
			pr_warn("Test failed: Chain count mismach %d != %d",
				cnt, rcu_cnt);

		if (!quiet)
			pr_cont("\n  [%#x] first element: %p, chain length: %u\n",
				i, tbl->buckets[i], cnt);
	}

	pr_info("  Traversal complete: counted=%u, nelems=%u, entries=%d\n",
		total, atomic_read(&ht->nelems), TEST_ENTRIES);

	if (total != atomic_read(&ht->nelems) || total != TEST_ENTRIES)
		pr_warn("Test failed: Total count mismatch ^^^");
}

static int __init test_rhashtable(struct rhashtable *ht)
{
	struct bucket_table *tbl;
	struct test_obj *obj;
	struct rhash_head *pos, *next;
	int err;
	unsigned int i;

	/*
	 * Insertion Test:
	 * Insert TEST_ENTRIES into table with all keys even numbers
	 */
	pr_info("  Adding %d keys\n", TEST_ENTRIES);
	for (i = 0; i < TEST_ENTRIES; i++) {
		struct test_obj *obj;

		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		if (!obj) {
			err = -ENOMEM;
			goto error;
		}

		obj->ptr = TEST_PTR;
		obj->value = i * 2;

		err = rhashtable_insert_fast(ht, &obj->node, test_rht_params);
		if (err) {
			kfree(obj);
			goto error;
		}
	}

	rcu_read_lock();
	test_bucket_stats(ht, true);
	test_rht_lookup(ht);
	rcu_read_unlock();

	rcu_read_lock();
	test_bucket_stats(ht, true);
	rcu_read_unlock();

	pr_info("  Deleting %d keys\n", TEST_ENTRIES);
	for (i = 0; i < TEST_ENTRIES; i++) {
		u32 key = i * 2;

		obj = rhashtable_lookup_fast(ht, &key, test_rht_params);
		BUG_ON(!obj);

		rhashtable_remove_fast(ht, &obj->node, test_rht_params);
		kfree(obj);
	}

	return 0;

error:
	tbl = rht_dereference_rcu(ht->tbl, ht);
	for (i = 0; i < tbl->size; i++)
		rht_for_each_entry_safe(obj, pos, next, tbl, i, node)
			kfree(obj);

	return err;
}

static struct rhashtable ht;

static int __init test_rht_init(void)
{
	int err;

	pr_info("Running resizable hashtable tests...\n");

	err = rhashtable_init(&ht, &test_rht_params);
	if (err < 0) {
		pr_warn("Test failed: Unable to initialize hashtable: %d\n",
			err);
		return err;
	}

	err = test_rhashtable(&ht);

	rhashtable_destroy(&ht);

	return err;
}

static void __exit test_rht_exit(void)
{
}

module_init(test_rht_init);
module_exit(test_rht_exit);

MODULE_LICENSE("GPL v2");
