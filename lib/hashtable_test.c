// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the Kernel Hashtable structures.
 *
 * Copyright (C) 2022, Google LLC.
 * Author: Rae Moar <rmoar@google.com>
 */
#include <kunit/test.h>

#include <linux/hashtable.h>

struct hashtable_test_entry {
	int key;
	int data;
	struct hlist_node node;
	int visited;
};

static void hashtable_test_hash_init(struct kunit *test)
{
	/* Test the different ways of initialising a hashtable. */
	DEFINE_HASHTABLE(hash1, 2);
	DECLARE_HASHTABLE(hash2, 3);

	/* When using DECLARE_HASHTABLE, must use hash_init to
	 * initialize the hashtable.
	 */
	hash_init(hash2);

	KUNIT_EXPECT_TRUE(test, hash_empty(hash1));
	KUNIT_EXPECT_TRUE(test, hash_empty(hash2));
}

static void hashtable_test_hash_empty(struct kunit *test)
{
	struct hashtable_test_entry a;
	DEFINE_HASHTABLE(hash, 1);

	KUNIT_EXPECT_TRUE(test, hash_empty(hash));

	a.key = 1;
	a.data = 13;
	hash_add(hash, &a.node, a.key);

	/* Hashtable should no longer be empty. */
	KUNIT_EXPECT_FALSE(test, hash_empty(hash));
}

static void hashtable_test_hash_hashed(struct kunit *test)
{
	struct hashtable_test_entry a, b;
	DEFINE_HASHTABLE(hash, 4);

	a.key = 1;
	a.data = 13;
	hash_add(hash, &a.node, a.key);
	b.key = 1;
	b.data = 2;
	hash_add(hash, &b.node, b.key);

	KUNIT_EXPECT_TRUE(test, hash_hashed(&a.node));
	KUNIT_EXPECT_TRUE(test, hash_hashed(&b.node));
}

static void hashtable_test_hash_add(struct kunit *test)
{
	struct hashtable_test_entry a, b, *x;
	int bkt;
	DEFINE_HASHTABLE(hash, 3);

	a.key = 1;
	a.data = 13;
	a.visited = 0;
	hash_add(hash, &a.node, a.key);
	b.key = 2;
	b.data = 10;
	b.visited = 0;
	hash_add(hash, &b.node, b.key);

	hash_for_each(hash, bkt, x, node) {
		x->visited++;
		if (x->key == a.key)
			KUNIT_EXPECT_EQ(test, x->data, 13);
		else if (x->key == b.key)
			KUNIT_EXPECT_EQ(test, x->data, 10);
		else
			KUNIT_FAIL(test, "Unexpected key in hashtable.");
	}

	/* Both entries should have been visited exactly once. */
	KUNIT_EXPECT_EQ(test, a.visited, 1);
	KUNIT_EXPECT_EQ(test, b.visited, 1);
}

static void hashtable_test_hash_del(struct kunit *test)
{
	struct hashtable_test_entry a, b, *x;
	DEFINE_HASHTABLE(hash, 6);

	a.key = 1;
	a.data = 13;
	hash_add(hash, &a.node, a.key);
	b.key = 2;
	b.data = 10;
	b.visited = 0;
	hash_add(hash, &b.node, b.key);

	hash_del(&b.node);
	hash_for_each_possible(hash, x, node, b.key) {
		x->visited++;
		KUNIT_EXPECT_NE(test, x->key, b.key);
	}

	/* The deleted entry should not have been visited. */
	KUNIT_EXPECT_EQ(test, b.visited, 0);

	hash_del(&a.node);

	/* The hashtable should be empty. */
	KUNIT_EXPECT_TRUE(test, hash_empty(hash));
}

static void hashtable_test_hash_for_each(struct kunit *test)
{
	struct hashtable_test_entry entries[3];
	struct hashtable_test_entry *x;
	int bkt, i, j, count;
	DEFINE_HASHTABLE(hash, 3);

	/* Add three entries to the hashtable. */
	for (i = 0; i < 3; i++) {
		entries[i].key = i;
		entries[i].data = i + 10;
		entries[i].visited = 0;
		hash_add(hash, &entries[i].node, entries[i].key);
	}

	count = 0;
	hash_for_each(hash, bkt, x, node) {
		x->visited += 1;
		KUNIT_ASSERT_GE_MSG(test, x->key, 0, "Unexpected key in hashtable.");
		KUNIT_ASSERT_LT_MSG(test, x->key, 3, "Unexpected key in hashtable.");
		count++;
	}

	/* Should have visited each entry exactly once. */
	KUNIT_EXPECT_EQ(test, count, 3);
	for (j = 0; j < 3; j++)
		KUNIT_EXPECT_EQ(test, entries[j].visited, 1);
}

static void hashtable_test_hash_for_each_safe(struct kunit *test)
{
	struct hashtable_test_entry entries[3];
	struct hashtable_test_entry *x;
	struct hlist_node *tmp;
	int bkt, i, j, count;
	DEFINE_HASHTABLE(hash, 3);

	/* Add three entries to the hashtable. */
	for (i = 0; i < 3; i++) {
		entries[i].key = i;
		entries[i].data = i + 10;
		entries[i].visited = 0;
		hash_add(hash, &entries[i].node, entries[i].key);
	}

	count = 0;
	hash_for_each_safe(hash, bkt, tmp, x, node) {
		x->visited += 1;
		KUNIT_ASSERT_GE_MSG(test, x->key, 0, "Unexpected key in hashtable.");
		KUNIT_ASSERT_LT_MSG(test, x->key, 3, "Unexpected key in hashtable.");
		count++;

		/* Delete entry during loop. */
		hash_del(&x->node);
	}

	/* Should have visited each entry exactly once. */
	KUNIT_EXPECT_EQ(test, count, 3);
	for (j = 0; j < 3; j++)
		KUNIT_EXPECT_EQ(test, entries[j].visited, 1);
}

static void hashtable_test_hash_for_each_possible(struct kunit *test)
{
	struct hashtable_test_entry entries[4];
	struct hashtable_test_entry *x, *y;
	int buckets[2];
	int bkt, i, j, count;
	DEFINE_HASHTABLE(hash, 5);

	/* Add three entries with key = 0 to the hashtable. */
	for (i = 0; i < 3; i++) {
		entries[i].key = 0;
		entries[i].data = i;
		entries[i].visited = 0;
		hash_add(hash, &entries[i].node, entries[i].key);
	}

	/* Add an entry with key = 1. */
	entries[3].key = 1;
	entries[3].data = 3;
	entries[3].visited = 0;
	hash_add(hash, &entries[3].node, entries[3].key);

	count = 0;
	hash_for_each_possible(hash, x, node, 0) {
		x->visited += 1;
		KUNIT_ASSERT_GE_MSG(test, x->data, 0, "Unexpected data in hashtable.");
		KUNIT_ASSERT_LT_MSG(test, x->data, 4, "Unexpected data in hashtable.");
		count++;
	}

	/* Should have visited each entry with key = 0 exactly once. */
	for (j = 0; j < 3; j++)
		KUNIT_EXPECT_EQ(test, entries[j].visited, 1);

	/* Save the buckets for the different keys. */
	hash_for_each(hash, bkt, y, node) {
		KUNIT_ASSERT_GE_MSG(test, y->key, 0, "Unexpected key in hashtable.");
		KUNIT_ASSERT_LE_MSG(test, y->key, 1, "Unexpected key in hashtable.");
		buckets[y->key] = bkt;
	}

	/* If entry with key = 1 is in the same bucket as the entries with
	 * key = 0, check it was visited. Otherwise ensure that only three
	 * entries were visited.
	 */
	if (buckets[0] == buckets[1]) {
		KUNIT_EXPECT_EQ(test, count, 4);
		KUNIT_EXPECT_EQ(test, entries[3].visited, 1);
	} else {
		KUNIT_EXPECT_EQ(test, count, 3);
		KUNIT_EXPECT_EQ(test, entries[3].visited, 0);
	}
}

static void hashtable_test_hash_for_each_possible_safe(struct kunit *test)
{
	struct hashtable_test_entry entries[4];
	struct hashtable_test_entry *x, *y;
	struct hlist_node *tmp;
	int buckets[2];
	int bkt, i, j, count;
	DEFINE_HASHTABLE(hash, 5);

	/* Add three entries with key = 0 to the hashtable. */
	for (i = 0; i < 3; i++) {
		entries[i].key = 0;
		entries[i].data = i;
		entries[i].visited = 0;
		hash_add(hash, &entries[i].node, entries[i].key);
	}

	/* Add an entry with key = 1. */
	entries[3].key = 1;
	entries[3].data = 3;
	entries[3].visited = 0;
	hash_add(hash, &entries[3].node, entries[3].key);

	count = 0;
	hash_for_each_possible_safe(hash, x, tmp, node, 0) {
		x->visited += 1;
		KUNIT_ASSERT_GE_MSG(test, x->data, 0, "Unexpected data in hashtable.");
		KUNIT_ASSERT_LT_MSG(test, x->data, 4, "Unexpected data in hashtable.");
		count++;

		/* Delete entry during loop. */
		hash_del(&x->node);
	}

	/* Should have visited each entry with key = 0 exactly once. */
	for (j = 0; j < 3; j++)
		KUNIT_EXPECT_EQ(test, entries[j].visited, 1);

	/* Save the buckets for the different keys. */
	hash_for_each(hash, bkt, y, node) {
		KUNIT_ASSERT_GE_MSG(test, y->key, 0, "Unexpected key in hashtable.");
		KUNIT_ASSERT_LE_MSG(test, y->key, 1, "Unexpected key in hashtable.");
		buckets[y->key] = bkt;
	}

	/* If entry with key = 1 is in the same bucket as the entries with
	 * key = 0, check it was visited. Otherwise ensure that only three
	 * entries were visited.
	 */
	if (buckets[0] == buckets[1]) {
		KUNIT_EXPECT_EQ(test, count, 4);
		KUNIT_EXPECT_EQ(test, entries[3].visited, 1);
	} else {
		KUNIT_EXPECT_EQ(test, count, 3);
		KUNIT_EXPECT_EQ(test, entries[3].visited, 0);
	}
}

static struct kunit_case hashtable_test_cases[] = {
	KUNIT_CASE(hashtable_test_hash_init),
	KUNIT_CASE(hashtable_test_hash_empty),
	KUNIT_CASE(hashtable_test_hash_hashed),
	KUNIT_CASE(hashtable_test_hash_add),
	KUNIT_CASE(hashtable_test_hash_del),
	KUNIT_CASE(hashtable_test_hash_for_each),
	KUNIT_CASE(hashtable_test_hash_for_each_safe),
	KUNIT_CASE(hashtable_test_hash_for_each_possible),
	KUNIT_CASE(hashtable_test_hash_for_each_possible_safe),
	{},
};

static struct kunit_suite hashtable_test_module = {
	.name = "hashtable",
	.test_cases = hashtable_test_cases,
};

kunit_test_suites(&hashtable_test_module);

MODULE_LICENSE("GPL");
