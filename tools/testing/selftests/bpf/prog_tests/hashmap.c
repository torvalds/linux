// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * Tests for libbpf's hashmap.
 *
 * Copyright (c) 2019 Facebook
 */
#include "test_progs.h"
#include "bpf/hashmap.h"
#include <stddef.h>

static int duration = 0;

static size_t hash_fn(long k, void *ctx)
{
	return k;
}

static bool equal_fn(long a, long b, void *ctx)
{
	return a == b;
}

static inline size_t next_pow_2(size_t n)
{
	size_t r = 1;

	while (r < n)
		r <<= 1;
	return r;
}

static inline size_t exp_cap(size_t sz)
{
	size_t r = next_pow_2(sz);

	if (sz * 4 / 3 > r)
		r <<= 1;
	return r;
}

#define ELEM_CNT 62

static void test_hashmap_generic(void)
{
	struct hashmap_entry *entry, *tmp;
	int err, bkt, found_cnt, i;
	long long found_msk;
	struct hashmap *map;

	map = hashmap__new(hash_fn, equal_fn, NULL);
	if (!ASSERT_OK_PTR(map, "hashmap__new"))
		return;

	for (i = 0; i < ELEM_CNT; i++) {
		long oldk, k = i;
		long oldv, v = 1024 + i;

		err = hashmap__update(map, k, v, &oldk, &oldv);
		if (CHECK(err != -ENOENT, "hashmap__update",
			  "unexpected result: %d\n", err))
			goto cleanup;

		if (i % 2) {
			err = hashmap__add(map, k, v);
		} else {
			err = hashmap__set(map, k, v, &oldk, &oldv);
			if (CHECK(oldk != 0 || oldv != 0, "check_kv",
				  "unexpected k/v: %ld=%ld\n", oldk, oldv))
				goto cleanup;
		}

		if (CHECK(err, "elem_add", "failed to add k/v %ld = %ld: %d\n", k, v, err))
			goto cleanup;

		if (CHECK(!hashmap__find(map, k, &oldv), "elem_find",
			  "failed to find key %ld\n", k))
			goto cleanup;
		if (CHECK(oldv != v, "elem_val", "found value is wrong: %ld\n", oldv))
			goto cleanup;
	}

	if (CHECK(hashmap__size(map) != ELEM_CNT, "hashmap__size",
		  "invalid map size: %zu\n", hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "hashmap_cap",
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;

	found_msk = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		long k = entry->key;
		long v = entry->value;

		found_msk |= 1ULL << k;
		if (CHECK(v - k != 1024, "check_kv",
			  "invalid k/v pair: %ld = %ld\n", k, v))
			goto cleanup;
	}
	if (CHECK(found_msk != (1ULL << ELEM_CNT) - 1, "elem_cnt",
		  "not all keys iterated: %llx\n", found_msk))
		goto cleanup;

	for (i = 0; i < ELEM_CNT; i++) {
		long oldk, k = i;
		long oldv, v = 256 + i;

		err = hashmap__add(map, k, v);
		if (CHECK(err != -EEXIST, "hashmap__add",
			  "unexpected add result: %d\n", err))
			goto cleanup;

		if (i % 2)
			err = hashmap__update(map, k, v, &oldk, &oldv);
		else
			err = hashmap__set(map, k, v, &oldk, &oldv);

		if (CHECK(err, "elem_upd",
			  "failed to update k/v %ld = %ld: %d\n",
			  k, v, err))
			goto cleanup;
		if (CHECK(!hashmap__find(map, k, &oldv), "elem_find",
			  "failed to find key %ld\n", k))
			goto cleanup;
		if (CHECK(oldv != v, "elem_val",
			  "found value is wrong: %ld\n", oldv))
			goto cleanup;
	}

	if (CHECK(hashmap__size(map) != ELEM_CNT, "hashmap__size",
		  "invalid updated map size: %zu\n", hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "hashmap__capacity",
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;

	found_msk = 0;
	hashmap__for_each_entry_safe(map, entry, tmp, bkt) {
		long k = entry->key;
		long v = entry->value;

		found_msk |= 1ULL << k;
		if (CHECK(v - k != 256, "elem_check",
			  "invalid updated k/v pair: %ld = %ld\n", k, v))
			goto cleanup;
	}
	if (CHECK(found_msk != (1ULL << ELEM_CNT) - 1, "elem_cnt",
		  "not all keys iterated after update: %llx\n", found_msk))
		goto cleanup;

	found_cnt = 0;
	hashmap__for_each_key_entry(map, entry, 0) {
		found_cnt++;
	}
	if (CHECK(!found_cnt, "found_cnt",
		  "didn't find any entries for key 0\n"))
		goto cleanup;

	found_msk = 0;
	found_cnt = 0;
	hashmap__for_each_key_entry_safe(map, entry, tmp, 0) {
		long oldk, k;
		long oldv, v;

		k = entry->key;
		v = entry->value;

		found_cnt++;
		found_msk |= 1ULL << k;

		if (CHECK(!hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "failed to delete k/v %ld = %ld\n", k, v))
			goto cleanup;
		if (CHECK(oldk != k || oldv != v, "check_old",
			  "invalid deleted k/v: expected %ld = %ld, got %ld = %ld\n",
			  k, v, oldk, oldv))
			goto cleanup;
		if (CHECK(hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "unexpectedly deleted k/v %ld = %ld\n", oldk, oldv))
			goto cleanup;
	}

	if (CHECK(!found_cnt || !found_msk, "found_entries",
		  "didn't delete any key entries\n"))
		goto cleanup;
	if (CHECK(hashmap__size(map) != ELEM_CNT - found_cnt, "elem_cnt",
		  "invalid updated map size (already deleted: %d): %zu\n",
		  found_cnt, hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "hashmap__capacity",
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;

	hashmap__for_each_entry_safe(map, entry, tmp, bkt) {
		long oldk, k;
		long oldv, v;

		k = entry->key;
		v = entry->value;

		found_cnt++;
		found_msk |= 1ULL << k;

		if (CHECK(!hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "failed to delete k/v %ld = %ld\n", k, v))
			goto cleanup;
		if (CHECK(oldk != k || oldv != v, "elem_check",
			  "invalid old k/v: expect %ld = %ld, got %ld = %ld\n",
			  k, v, oldk, oldv))
			goto cleanup;
		if (CHECK(hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "unexpectedly deleted k/v %ld = %ld\n", k, v))
			goto cleanup;
	}

	if (CHECK(found_cnt != ELEM_CNT || found_msk != (1ULL << ELEM_CNT) - 1,
		  "found_cnt",
		  "not all keys were deleted: found_cnt:%d, found_msk:%llx\n",
		  found_cnt, found_msk))
		goto cleanup;
	if (CHECK(hashmap__size(map) != 0, "hashmap__size",
		  "invalid updated map size (already deleted: %d): %zu\n",
		  found_cnt, hashmap__size(map)))
		goto cleanup;

	found_cnt = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "elem_exists",
		      "unexpected map entries left: %ld = %ld\n",
		      entry->key, entry->value);
		goto cleanup;
	}

	hashmap__clear(map);
	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "elem_exists",
		      "unexpected map entries left: %ld = %ld\n",
		      entry->key, entry->value);
		goto cleanup;
	}

cleanup:
	hashmap__free(map);
}

static size_t str_hash_fn(long a, void *ctx)
{
	return str_hash((char *)a);
}

static bool str_equal_fn(long a, long b, void *ctx)
{
	return strcmp((char *)a, (char *)b) == 0;
}

/* Verify that hashmap interface works with pointer keys and values */
static void test_hashmap_ptr_iface(void)
{
	const char *key, *value, *old_key, *old_value;
	struct hashmap_entry *cur;
	struct hashmap *map;
	int err, i, bkt;

	map = hashmap__new(str_hash_fn, str_equal_fn, NULL);
	if (CHECK(!map, "hashmap__new", "can't allocate hashmap\n"))
		goto cleanup;

#define CHECK_STR(fn, var, expected)					\
	CHECK(strcmp(var, (expected)), (fn),				\
	      "wrong value of " #var ": '%s' instead of '%s'\n", var, (expected))

	err = hashmap__insert(map, "a", "apricot", HASHMAP_ADD, NULL, NULL);
	if (CHECK(err, "hashmap__insert", "unexpected error: %d\n", err))
		goto cleanup;

	err = hashmap__insert(map, "a", "apple", HASHMAP_SET, &old_key, &old_value);
	if (CHECK(err, "hashmap__insert", "unexpected error: %d\n", err))
		goto cleanup;
	CHECK_STR("hashmap__update", old_key, "a");
	CHECK_STR("hashmap__update", old_value, "apricot");

	err = hashmap__add(map, "b", "banana");
	if (CHECK(err, "hashmap__add", "unexpected error: %d\n", err))
		goto cleanup;

	err = hashmap__set(map, "b", "breadfruit", &old_key, &old_value);
	if (CHECK(err, "hashmap__set", "unexpected error: %d\n", err))
		goto cleanup;
	CHECK_STR("hashmap__set", old_key, "b");
	CHECK_STR("hashmap__set", old_value, "banana");

	err = hashmap__update(map, "b", "blueberry", &old_key, &old_value);
	if (CHECK(err, "hashmap__update", "unexpected error: %d\n", err))
		goto cleanup;
	CHECK_STR("hashmap__update", old_key, "b");
	CHECK_STR("hashmap__update", old_value, "breadfruit");

	err = hashmap__append(map, "c", "cherry");
	if (CHECK(err, "hashmap__append", "unexpected error: %d\n", err))
		goto cleanup;

	if (CHECK(!hashmap__delete(map, "c", &old_key, &old_value),
		  "hashmap__delete", "expected to have entry for 'c'\n"))
		goto cleanup;
	CHECK_STR("hashmap__delete", old_key, "c");
	CHECK_STR("hashmap__delete", old_value, "cherry");

	CHECK(!hashmap__find(map, "b", &value), "hashmap__find", "can't find value for 'b'\n");
	CHECK_STR("hashmap__find", value, "blueberry");

	if (CHECK(!hashmap__delete(map, "b", NULL, NULL),
		  "hashmap__delete", "expected to have entry for 'b'\n"))
		goto cleanup;

	i = 0;
	hashmap__for_each_entry(map, cur, bkt) {
		if (CHECK(i != 0, "hashmap__for_each_entry", "too many entries"))
			goto cleanup;
		key = cur->pkey;
		value = cur->pvalue;
		CHECK_STR("entry", key, "a");
		CHECK_STR("entry", value, "apple");
		i++;
	}
#undef CHECK_STR

cleanup:
	hashmap__free(map);
}

static size_t collision_hash_fn(long k, void *ctx)
{
	return 0;
}

static void test_hashmap_multimap(void)
{
	long k1 = 0, k2 = 1;
	struct hashmap_entry *entry;
	struct hashmap *map;
	long found_msk;
	int err, bkt;

	/* force collisions */
	map = hashmap__new(collision_hash_fn, equal_fn, NULL);
	if (!ASSERT_OK_PTR(map, "hashmap__new"))
		return;

	/* set up multimap:
	 * [0] -> 1, 2, 4;
	 * [1] -> 8, 16, 32;
	 */
	err = hashmap__append(map, k1, 1);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k1, 2);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k1, 4);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;

	err = hashmap__append(map, k2, 8);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k2, 16);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k2, 32);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;

	if (CHECK(hashmap__size(map) != 6, "hashmap_size",
		  "invalid map size: %zu\n", hashmap__size(map)))
		goto cleanup;

	/* verify global iteration still works and sees all values */
	found_msk = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		found_msk |= entry->value;
	}
	if (CHECK(found_msk != (1 << 6) - 1, "found_msk",
		  "not all keys iterated: %lx\n", found_msk))
		goto cleanup;

	/* iterate values for key 1 */
	found_msk = 0;
	hashmap__for_each_key_entry(map, entry, k1) {
		found_msk |= entry->value;
	}
	if (CHECK(found_msk != (1 | 2 | 4), "found_msk",
		  "invalid k1 values: %lx\n", found_msk))
		goto cleanup;

	/* iterate values for key 2 */
	found_msk = 0;
	hashmap__for_each_key_entry(map, entry, k2) {
		found_msk |= entry->value;
	}
	if (CHECK(found_msk != (8 | 16 | 32), "found_msk",
		  "invalid k2 values: %lx\n", found_msk))
		goto cleanup;

cleanup:
	hashmap__free(map);
}

static void test_hashmap_empty()
{
	struct hashmap_entry *entry;
	int bkt;
	struct hashmap *map;
	long k = 0;

	/* force collisions */
	map = hashmap__new(hash_fn, equal_fn, NULL);
	if (!ASSERT_OK_PTR(map, "hashmap__new"))
		goto cleanup;

	if (CHECK(hashmap__size(map) != 0, "hashmap__size",
		  "invalid map size: %zu\n", hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != 0, "hashmap__capacity",
		  "invalid map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;
	if (CHECK(hashmap__find(map, k, NULL), "elem_find",
		  "unexpected find\n"))
		goto cleanup;
	if (CHECK(hashmap__delete(map, k, NULL, NULL), "elem_del",
		  "unexpected delete\n"))
		goto cleanup;

	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "elem_found", "unexpected iterated entry\n");
		goto cleanup;
	}
	hashmap__for_each_key_entry(map, entry, k) {
		CHECK(false, "key_found", "unexpected key entry\n");
		goto cleanup;
	}

cleanup:
	hashmap__free(map);
}

void test_hashmap()
{
	if (test__start_subtest("generic"))
		test_hashmap_generic();
	if (test__start_subtest("multimap"))
		test_hashmap_multimap();
	if (test__start_subtest("empty"))
		test_hashmap_empty();
	if (test__start_subtest("ptr_iface"))
		test_hashmap_ptr_iface();
}
