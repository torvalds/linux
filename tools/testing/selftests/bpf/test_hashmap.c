// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * Tests for libbpf's hashmap.
 *
 * Copyright (c) 2019 Facebook
 */
#include <stdio.h>
#include <errno.h>
#include <linux/err.h>
#include "hashmap.h"

#define CHECK(condition, format...) ({					\
	int __ret = !!(condition);					\
	if (__ret) {							\
		fprintf(stderr, "%s:%d:FAIL ", __func__, __LINE__);	\
		fprintf(stderr, format);				\
	}								\
	__ret;								\
})

size_t hash_fn(const void *k, void *ctx)
{
	return (long)k;
}

bool equal_fn(const void *a, const void *b, void *ctx)
{
	return (long)a == (long)b;
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

int test_hashmap_generic(void)
{
	struct hashmap_entry *entry, *tmp;
	int err, bkt, found_cnt, i;
	long long found_msk;
	struct hashmap *map;

	fprintf(stderr, "%s: ", __func__);

	map = hashmap__new(hash_fn, equal_fn, NULL);
	if (CHECK(IS_ERR(map), "failed to create map: %ld\n", PTR_ERR(map)))
		return 1;

	for (i = 0; i < ELEM_CNT; i++) {
		const void *oldk, *k = (const void *)(long)i;
		void *oldv, *v = (void *)(long)(1024 + i);

		err = hashmap__update(map, k, v, &oldk, &oldv);
		if (CHECK(err != -ENOENT, "unexpected result: %d\n", err))
			return 1;

		if (i % 2) {
			err = hashmap__add(map, k, v);
		} else {
			err = hashmap__set(map, k, v, &oldk, &oldv);
			if (CHECK(oldk != NULL || oldv != NULL,
				  "unexpected k/v: %p=%p\n", oldk, oldv))
				return 1;
		}

		if (CHECK(err, "failed to add k/v %ld = %ld: %d\n",
			       (long)k, (long)v, err))
			return 1;

		if (CHECK(!hashmap__find(map, k, &oldv),
			  "failed to find key %ld\n", (long)k))
			return 1;
		if (CHECK(oldv != v, "found value is wrong: %ld\n", (long)oldv))
			return 1;
	}

	if (CHECK(hashmap__size(map) != ELEM_CNT,
		  "invalid map size: %zu\n", hashmap__size(map)))
		return 1;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		return 1;

	found_msk = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		long k = (long)entry->key;
		long v = (long)entry->value;

		found_msk |= 1ULL << k;
		if (CHECK(v - k != 1024, "invalid k/v pair: %ld = %ld\n", k, v))
			return 1;
	}
	if (CHECK(found_msk != (1ULL << ELEM_CNT) - 1,
		  "not all keys iterated: %llx\n", found_msk))
		return 1;

	for (i = 0; i < ELEM_CNT; i++) {
		const void *oldk, *k = (const void *)(long)i;
		void *oldv, *v = (void *)(long)(256 + i);

		err = hashmap__add(map, k, v);
		if (CHECK(err != -EEXIST, "unexpected add result: %d\n", err))
			return 1;

		if (i % 2)
			err = hashmap__update(map, k, v, &oldk, &oldv);
		else
			err = hashmap__set(map, k, v, &oldk, &oldv);

		if (CHECK(err, "failed to update k/v %ld = %ld: %d\n",
			       (long)k, (long)v, err))
			return 1;
		if (CHECK(!hashmap__find(map, k, &oldv),
			  "failed to find key %ld\n", (long)k))
			return 1;
		if (CHECK(oldv != v, "found value is wrong: %ld\n", (long)oldv))
			return 1;
	}

	if (CHECK(hashmap__size(map) != ELEM_CNT,
		  "invalid updated map size: %zu\n", hashmap__size(map)))
		return 1;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		return 1;

	found_msk = 0;
	hashmap__for_each_entry_safe(map, entry, tmp, bkt) {
		long k = (long)entry->key;
		long v = (long)entry->value;

		found_msk |= 1ULL << k;
		if (CHECK(v - k != 256,
			  "invalid updated k/v pair: %ld = %ld\n", k, v))
			return 1;
	}
	if (CHECK(found_msk != (1ULL << ELEM_CNT) - 1,
		  "not all keys iterated after update: %llx\n", found_msk))
		return 1;

	found_cnt = 0;
	hashmap__for_each_key_entry(map, entry, (void *)0) {
		found_cnt++;
	}
	if (CHECK(!found_cnt, "didn't find any entries for key 0\n"))
		return 1;

	found_msk = 0;
	found_cnt = 0;
	hashmap__for_each_key_entry_safe(map, entry, tmp, (void *)0) {
		const void *oldk, *k;
		void *oldv, *v;

		k = entry->key;
		v = entry->value;

		found_cnt++;
		found_msk |= 1ULL << (long)k;

		if (CHECK(!hashmap__delete(map, k, &oldk, &oldv),
			  "failed to delete k/v %ld = %ld\n",
			  (long)k, (long)v))
			return 1;
		if (CHECK(oldk != k || oldv != v,
			  "invalid deleted k/v: expected %ld = %ld, got %ld = %ld\n",
			  (long)k, (long)v, (long)oldk, (long)oldv))
			return 1;
		if (CHECK(hashmap__delete(map, k, &oldk, &oldv),
			  "unexpectedly deleted k/v %ld = %ld\n",
			  (long)oldk, (long)oldv))
			return 1;
	}

	if (CHECK(!found_cnt || !found_msk,
		  "didn't delete any key entries\n"))
		return 1;
	if (CHECK(hashmap__size(map) != ELEM_CNT - found_cnt,
		  "invalid updated map size (already deleted: %d): %zu\n",
		  found_cnt, hashmap__size(map)))
		return 1;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		return 1;

	hashmap__for_each_entry_safe(map, entry, tmp, bkt) {
		const void *oldk, *k;
		void *oldv, *v;

		k = entry->key;
		v = entry->value;

		found_cnt++;
		found_msk |= 1ULL << (long)k;

		if (CHECK(!hashmap__delete(map, k, &oldk, &oldv),
			  "failed to delete k/v %ld = %ld\n",
			  (long)k, (long)v))
			return 1;
		if (CHECK(oldk != k || oldv != v,
			  "invalid old k/v: expect %ld = %ld, got %ld = %ld\n",
			  (long)k, (long)v, (long)oldk, (long)oldv))
			return 1;
		if (CHECK(hashmap__delete(map, k, &oldk, &oldv),
			  "unexpectedly deleted k/v %ld = %ld\n",
			  (long)k, (long)v))
			return 1;
	}

	if (CHECK(found_cnt != ELEM_CNT || found_msk != (1ULL << ELEM_CNT) - 1,
		  "not all keys were deleted: found_cnt:%d, found_msk:%llx\n",
		  found_cnt, found_msk))
		return 1;
	if (CHECK(hashmap__size(map) != 0,
		  "invalid updated map size (already deleted: %d): %zu\n",
		  found_cnt, hashmap__size(map)))
		return 1;

	found_cnt = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "unexpected map entries left: %ld = %ld\n",
			     (long)entry->key, (long)entry->value);
		return 1;
	}

	hashmap__free(map);
	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "unexpected map entries left: %ld = %ld\n",
			     (long)entry->key, (long)entry->value);
		return 1;
	}

	fprintf(stderr, "OK\n");
	return 0;
}

size_t collision_hash_fn(const void *k, void *ctx)
{
	return 0;
}

int test_hashmap_multimap(void)
{
	void *k1 = (void *)0, *k2 = (void *)1;
	struct hashmap_entry *entry;
	struct hashmap *map;
	long found_msk;
	int err, bkt;

	fprintf(stderr, "%s: ", __func__);

	/* force collisions */
	map = hashmap__new(collision_hash_fn, equal_fn, NULL);
	if (CHECK(IS_ERR(map), "failed to create map: %ld\n", PTR_ERR(map)))
		return 1;


	/* set up multimap:
	 * [0] -> 1, 2, 4;
	 * [1] -> 8, 16, 32;
	 */
	err = hashmap__append(map, k1, (void *)1);
	if (CHECK(err, "failed to add k/v: %d\n", err))
		return 1;
	err = hashmap__append(map, k1, (void *)2);
	if (CHECK(err, "failed to add k/v: %d\n", err))
		return 1;
	err = hashmap__append(map, k1, (void *)4);
	if (CHECK(err, "failed to add k/v: %d\n", err))
		return 1;

	err = hashmap__append(map, k2, (void *)8);
	if (CHECK(err, "failed to add k/v: %d\n", err))
		return 1;
	err = hashmap__append(map, k2, (void *)16);
	if (CHECK(err, "failed to add k/v: %d\n", err))
		return 1;
	err = hashmap__append(map, k2, (void *)32);
	if (CHECK(err, "failed to add k/v: %d\n", err))
		return 1;

	if (CHECK(hashmap__size(map) != 6,
		  "invalid map size: %zu\n", hashmap__size(map)))
		return 1;

	/* verify global iteration still works and sees all values */
	found_msk = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		found_msk |= (long)entry->value;
	}
	if (CHECK(found_msk != (1 << 6) - 1,
		  "not all keys iterated: %lx\n", found_msk))
		return 1;

	/* iterate values for key 1 */
	found_msk = 0;
	hashmap__for_each_key_entry(map, entry, k1) {
		found_msk |= (long)entry->value;
	}
	if (CHECK(found_msk != (1 | 2 | 4),
		  "invalid k1 values: %lx\n", found_msk))
		return 1;

	/* iterate values for key 2 */
	found_msk = 0;
	hashmap__for_each_key_entry(map, entry, k2) {
		found_msk |= (long)entry->value;
	}
	if (CHECK(found_msk != (8 | 16 | 32),
		  "invalid k2 values: %lx\n", found_msk))
		return 1;

	fprintf(stderr, "OK\n");
	return 0;
}

int test_hashmap_empty()
{
	struct hashmap_entry *entry;
	int bkt;
	struct hashmap *map;
	void *k = (void *)0;

	fprintf(stderr, "%s: ", __func__);

	/* force collisions */
	map = hashmap__new(hash_fn, equal_fn, NULL);
	if (CHECK(IS_ERR(map), "failed to create map: %ld\n", PTR_ERR(map)))
		return 1;

	if (CHECK(hashmap__size(map) != 0,
		  "invalid map size: %zu\n", hashmap__size(map)))
		return 1;
	if (CHECK(hashmap__capacity(map) != 0,
		  "invalid map capacity: %zu\n", hashmap__capacity(map)))
		return 1;
	if (CHECK(hashmap__find(map, k, NULL), "unexpected find\n"))
		return 1;
	if (CHECK(hashmap__delete(map, k, NULL, NULL), "unexpected delete\n"))
		return 1;

	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "unexpected iterated entry\n");
		return 1;
	}
	hashmap__for_each_key_entry(map, entry, k) {
		CHECK(false, "unexpected key entry\n");
		return 1;
	}

	fprintf(stderr, "OK\n");
	return 0;
}

int main(int argc, char **argv)
{
	bool failed = false;

	if (test_hashmap_generic())
		failed = true;
	if (test_hashmap_multimap())
		failed = true;
	if (test_hashmap_empty())
		failed = true;

	return failed;
}
