/*
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>

#include <sys/wait.h>
#include <sys/resource.h>

#include "bpf_sys.h"
#include "bpf_util.h"

#define LOCAL_FREE_TARGET	(128)
#define PERCPU_FREE_TARGET	(16)

static int nr_cpus;

static int create_map(int map_type, int map_flags, unsigned int size)
{
	int map_fd;

	map_fd = bpf_map_create(map_type, sizeof(unsigned long long),
				sizeof(unsigned long long), size, map_flags);

	if (map_fd == -1)
		perror("bpf_map_create");

	return map_fd;
}

static int map_subset(int map0, int map1)
{
	unsigned long long next_key = 0;
	unsigned long long value0[nr_cpus], value1[nr_cpus];
	int ret;

	while (!bpf_map_next_key(map1, &next_key, &next_key)) {
		assert(!bpf_map_lookup(map1, &next_key, value1));
		ret = bpf_map_lookup(map0, &next_key, value0);
		if (ret) {
			printf("key:%llu not found from map. %s(%d)\n",
			       next_key, strerror(errno), errno);
			return 0;
		}
		if (value0[0] != value1[0]) {
			printf("key:%llu value0:%llu != value1:%llu\n",
			       next_key, value0[0], value1[0]);
			return 0;
		}
	}
	return 1;
}

static int map_equal(int lru_map, int expected)
{
	return map_subset(lru_map, expected) && map_subset(expected, lru_map);
}

static int sched_next_online(int pid, int *next_to_try)
{
	cpu_set_t cpuset;
	int next = *next_to_try;
	int ret = -1;

	while (next < nr_cpus) {
		CPU_ZERO(&cpuset);
		CPU_SET(next++, &cpuset);
		if (!sched_setaffinity(pid, sizeof(cpuset), &cpuset)) {
			ret = 0;
			break;
		}
	}

	*next_to_try = next;
	return ret;
}

/* Size of the LRU amp is 2
 * Add key=1 (+1 key)
 * Add key=2 (+1 key)
 * Lookup Key=1
 * Add Key=3
 *   => Key=2 will be removed by LRU
 * Iterate map.  Only found key=1 and key=3
 */
static void test_lru_sanity0(int map_type, int map_flags)
{
	unsigned long long key, value[nr_cpus];
	int lru_map_fd, expected_map_fd;
	int next_cpu = 0;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	assert(sched_next_online(0, &next_cpu) != -1);

	if (map_flags & BPF_F_NO_COMMON_LRU)
		lru_map_fd = create_map(map_type, map_flags, 2 * nr_cpus);
	else
		lru_map_fd = create_map(map_type, map_flags, 2);
	assert(lru_map_fd != -1);

	expected_map_fd = create_map(BPF_MAP_TYPE_HASH, 0, 2);
	assert(expected_map_fd != -1);

	value[0] = 1234;

	/* insert key=1 element */

	key = 1;
	assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
	assert(!bpf_map_update(expected_map_fd, &key, value, BPF_NOEXIST));

	/* BPF_NOEXIST means: add new element if it doesn't exist */
	assert(bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST) == -1 &&
	       /* key=1 already exists */
	       errno == EEXIST);

	assert(bpf_map_update(lru_map_fd, &key, value, -1) == -1 &&
	       errno == EINVAL);

	/* insert key=2 element */

	/* check that key=2 is not found */
	key = 2;
	assert(bpf_map_lookup(lru_map_fd, &key, value) == -1 &&
	       errno == ENOENT);

	/* BPF_EXIST means: update existing element */
	assert(bpf_map_update(lru_map_fd, &key, value, BPF_EXIST) == -1 &&
	       /* key=2 is not there */
	       errno == ENOENT);

	assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));

	/* insert key=3 element */

	/* check that key=3 is not found */
	key = 3;
	assert(bpf_map_lookup(lru_map_fd, &key, value) == -1 &&
	       errno == ENOENT);

	/* check that key=1 can be found and mark the ref bit to
	 * stop LRU from removing key=1
	 */
	key = 1;
	assert(!bpf_map_lookup(lru_map_fd, &key, value));
	assert(value[0] == 1234);

	key = 3;
	assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
	assert(!bpf_map_update(expected_map_fd, &key, value, BPF_NOEXIST));

	/* key=2 has been removed from the LRU */
	key = 2;
	assert(bpf_map_lookup(lru_map_fd, &key, value) == -1);

	assert(map_equal(lru_map_fd, expected_map_fd));

	close(expected_map_fd);
	close(lru_map_fd);

	printf("Pass\n");
}

/* Size of the LRU map is 1.5*tgt_free
 * Insert 1 to tgt_free (+tgt_free keys)
 * Lookup 1 to tgt_free/2
 * Insert 1+tgt_free to 2*tgt_free (+tgt_free keys)
 * => 1+tgt_free/2 to LOCALFREE_TARGET will be removed by LRU
 */
static void test_lru_sanity1(int map_type, int map_flags, unsigned int tgt_free)
{
	unsigned long long key, end_key, value[nr_cpus];
	int lru_map_fd, expected_map_fd;
	unsigned int batch_size;
	unsigned int map_size;
	int next_cpu = 0;

	if (map_flags & BPF_F_NO_COMMON_LRU)
		/* Ther percpu lru list (i.e each cpu has its own LRU
		 * list) does not have a local free list.  Hence,
		 * it will only free old nodes till there is no free
		 * from the LRU list.  Hence, this test does not apply
		 * to BPF_F_NO_COMMON_LRU
		 */
		return;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	assert(sched_next_online(0, &next_cpu) != -1);

	batch_size = tgt_free / 2;
	assert(batch_size * 2 == tgt_free);

	map_size = tgt_free + batch_size;
	lru_map_fd = create_map(map_type, map_flags, map_size);
	assert(lru_map_fd != -1);

	expected_map_fd = create_map(BPF_MAP_TYPE_HASH, 0, map_size);
	assert(expected_map_fd != -1);

	value[0] = 1234;

	/* Insert 1 to tgt_free (+tgt_free keys) */
	end_key = 1 + tgt_free;
	for (key = 1; key < end_key; key++)
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));

	/* Lookup 1 to tgt_free/2 */
	end_key = 1 + batch_size;
	for (key = 1; key < end_key; key++) {
		assert(!bpf_map_lookup(lru_map_fd, &key, value));
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	/* Insert 1+tgt_free to 2*tgt_free
	 * => 1+tgt_free/2 to LOCALFREE_TARGET will be
	 * removed by LRU
	 */
	key = 1 + tgt_free;
	end_key = key + tgt_free;
	for (; key < end_key; key++) {
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	assert(map_equal(lru_map_fd, expected_map_fd));

	close(expected_map_fd);
	close(lru_map_fd);

	printf("Pass\n");
}

/* Size of the LRU map 1.5 * tgt_free
 * Insert 1 to tgt_free (+tgt_free keys)
 * Update 1 to tgt_free/2
 *   => The original 1 to tgt_free/2 will be removed due to
 *      the LRU shrink process
 * Re-insert 1 to tgt_free/2 again and do a lookup immeidately
 * Insert 1+tgt_free to tgt_free*3/2
 * Insert 1+tgt_free*3/2 to tgt_free*5/2
 *   => Key 1+tgt_free to tgt_free*3/2
 *      will be removed from LRU because it has never
 *      been lookup and ref bit is not set
 */
static void test_lru_sanity2(int map_type, int map_flags, unsigned int tgt_free)
{
	unsigned long long key, value[nr_cpus];
	unsigned long long end_key;
	int lru_map_fd, expected_map_fd;
	unsigned int batch_size;
	unsigned int map_size;
	int next_cpu = 0;

	if (map_flags & BPF_F_NO_COMMON_LRU)
		/* Ther percpu lru list (i.e each cpu has its own LRU
		 * list) does not have a local free list.  Hence,
		 * it will only free old nodes till there is no free
		 * from the LRU list.  Hence, this test does not apply
		 * to BPF_F_NO_COMMON_LRU
		 */
		return;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	assert(sched_next_online(0, &next_cpu) != -1);

	batch_size = tgt_free / 2;
	assert(batch_size * 2 == tgt_free);

	map_size = tgt_free + batch_size;
	if (map_flags & BPF_F_NO_COMMON_LRU)
		lru_map_fd = create_map(map_type, map_flags,
					map_size * nr_cpus);
	else
		lru_map_fd = create_map(map_type, map_flags, map_size);
	assert(lru_map_fd != -1);

	expected_map_fd = create_map(BPF_MAP_TYPE_HASH, 0, map_size);
	assert(expected_map_fd != -1);

	value[0] = 1234;

	/* Insert 1 to tgt_free (+tgt_free keys) */
	end_key = 1 + tgt_free;
	for (key = 1; key < end_key; key++)
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));

	/* Any bpf_map_update will require to acquire a new node
	 * from LRU first.
	 *
	 * The local list is running out of free nodes.
	 * It gets from the global LRU list which tries to
	 * shrink the inactive list to get tgt_free
	 * number of free nodes.
	 *
	 * Hence, the oldest key 1 to tgt_free/2
	 * are removed from the LRU list.
	 */
	key = 1;
	if (map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH) {
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
		assert(!bpf_map_delete(lru_map_fd, &key));
	} else {
		assert(bpf_map_update(lru_map_fd, &key, value, BPF_EXIST));
	}

	/* Re-insert 1 to tgt_free/2 again and do a lookup
	 * immeidately.
	 */
	end_key = 1 + batch_size;
	value[0] = 4321;
	for (key = 1; key < end_key; key++) {
		assert(bpf_map_lookup(lru_map_fd, &key, value));
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
		assert(!bpf_map_lookup(lru_map_fd, &key, value));
		assert(value[0] == 4321);
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	value[0] = 1234;

	/* Insert 1+tgt_free to tgt_free*3/2 */
	end_key = 1 + tgt_free + batch_size;
	for (key = 1 + tgt_free; key < end_key; key++)
		/* These newly added but not referenced keys will be
		 * gone during the next LRU shrink.
		 */
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));

	/* Insert 1+tgt_free*3/2 to  tgt_free*5/2 */
	end_key = key + tgt_free;
	for (; key < end_key; key++) {
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	assert(map_equal(lru_map_fd, expected_map_fd));

	close(expected_map_fd);
	close(lru_map_fd);

	printf("Pass\n");
}

/* Size of the LRU map is 2*tgt_free
 * It is to test the active/inactive list rotation
 * Insert 1 to 2*tgt_free (+2*tgt_free keys)
 * Lookup key 1 to tgt_free*3/2
 * Add 1+2*tgt_free to tgt_free*5/2 (+tgt_free/2 keys)
 *  => key 1+tgt_free*3/2 to 2*tgt_free are removed from LRU
 */
static void test_lru_sanity3(int map_type, int map_flags, unsigned int tgt_free)
{
	unsigned long long key, end_key, value[nr_cpus];
	int lru_map_fd, expected_map_fd;
	unsigned int batch_size;
	unsigned int map_size;
	int next_cpu = 0;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	assert(sched_next_online(0, &next_cpu) != -1);

	batch_size = tgt_free / 2;
	assert(batch_size * 2 == tgt_free);

	map_size = tgt_free * 2;
	if (map_flags & BPF_F_NO_COMMON_LRU)
		lru_map_fd = create_map(map_type, map_flags,
					map_size * nr_cpus);
	else
		lru_map_fd = create_map(map_type, map_flags, map_size);
	assert(lru_map_fd != -1);

	expected_map_fd = create_map(BPF_MAP_TYPE_HASH, 0, map_size);
	assert(expected_map_fd != -1);

	value[0] = 1234;

	/* Insert 1 to 2*tgt_free (+2*tgt_free keys) */
	end_key = 1 + (2 * tgt_free);
	for (key = 1; key < end_key; key++)
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));

	/* Lookup key 1 to tgt_free*3/2 */
	end_key = tgt_free + batch_size;
	for (key = 1; key < end_key; key++) {
		assert(!bpf_map_lookup(lru_map_fd, &key, value));
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	/* Add 1+2*tgt_free to tgt_free*5/2
	 * (+tgt_free/2 keys)
	 */
	key = 2 * tgt_free + 1;
	end_key = key + batch_size;
	for (; key < end_key; key++) {
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	assert(map_equal(lru_map_fd, expected_map_fd));

	close(expected_map_fd);
	close(lru_map_fd);

	printf("Pass\n");
}

/* Test deletion */
static void test_lru_sanity4(int map_type, int map_flags, unsigned int tgt_free)
{
	int lru_map_fd, expected_map_fd;
	unsigned long long key, value[nr_cpus];
	unsigned long long end_key;
	int next_cpu = 0;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	assert(sched_next_online(0, &next_cpu) != -1);

	if (map_flags & BPF_F_NO_COMMON_LRU)
		lru_map_fd = create_map(map_type, map_flags,
					3 * tgt_free * nr_cpus);
	else
		lru_map_fd = create_map(map_type, map_flags, 3 * tgt_free);
	assert(lru_map_fd != -1);

	expected_map_fd = create_map(BPF_MAP_TYPE_HASH, 0,
				     3 * tgt_free);
	assert(expected_map_fd != -1);

	value[0] = 1234;

	for (key = 1; key <= 2 * tgt_free; key++)
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));

	key = 1;
	assert(bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));

	for (key = 1; key <= tgt_free; key++) {
		assert(!bpf_map_lookup(lru_map_fd, &key, value));
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	for (; key <= 2 * tgt_free; key++) {
		assert(!bpf_map_delete(lru_map_fd, &key));
		assert(bpf_map_delete(lru_map_fd, &key));
	}

	end_key = key + 2 * tgt_free;
	for (; key < end_key; key++) {
		assert(!bpf_map_update(lru_map_fd, &key, value, BPF_NOEXIST));
		assert(!bpf_map_update(expected_map_fd, &key, value,
				       BPF_NOEXIST));
	}

	assert(map_equal(lru_map_fd, expected_map_fd));

	close(expected_map_fd);
	close(lru_map_fd);

	printf("Pass\n");
}

static void do_test_lru_sanity5(unsigned long long last_key, int map_fd)
{
	unsigned long long key, value[nr_cpus];

	/* Ensure the last key inserted by previous CPU can be found */
	assert(!bpf_map_lookup(map_fd, &last_key, value));

	value[0] = 1234;

	key = last_key + 1;
	assert(!bpf_map_update(map_fd, &key, value, BPF_NOEXIST));
	assert(!bpf_map_lookup(map_fd, &key, value));

	/* Cannot find the last key because it was removed by LRU */
	assert(bpf_map_lookup(map_fd, &last_key, value));
}

/* Test map with only one element */
static void test_lru_sanity5(int map_type, int map_flags)
{
	unsigned long long key, value[nr_cpus];
	int next_cpu = 0;
	int map_fd;

	if (map_flags & BPF_F_NO_COMMON_LRU)
		return;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	map_fd = create_map(map_type, map_flags, 1);
	assert(map_fd != -1);

	value[0] = 1234;
	key = 0;
	assert(!bpf_map_update(map_fd, &key, value, BPF_NOEXIST));

	while (sched_next_online(0, &next_cpu) != -1) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			do_test_lru_sanity5(key, map_fd);
			exit(0);
		} else if (pid == -1) {
			printf("couldn't spawn process to test key:%llu\n",
			       key);
			exit(1);
		} else {
			int status;

			assert(waitpid(pid, &status, 0) == pid);
			assert(status == 0);
			key++;
		}
	}

	close(map_fd);
	/* At least one key should be tested */
	assert(key > 0);

	printf("Pass\n");
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	int map_types[] = {BPF_MAP_TYPE_LRU_HASH,
			     BPF_MAP_TYPE_LRU_PERCPU_HASH};
	int map_flags[] = {0, BPF_F_NO_COMMON_LRU};
	int t, f;

	setbuf(stdout, NULL);

	assert(!setrlimit(RLIMIT_MEMLOCK, &r));

	nr_cpus = bpf_num_possible_cpus();
	assert(nr_cpus != -1);
	printf("nr_cpus:%d\n\n", nr_cpus);

	for (f = 0; f < sizeof(map_flags) / sizeof(*map_flags); f++) {
		unsigned int tgt_free = (map_flags[f] & BPF_F_NO_COMMON_LRU) ?
			PERCPU_FREE_TARGET : LOCAL_FREE_TARGET;

		for (t = 0; t < sizeof(map_types) / sizeof(*map_types); t++) {
			test_lru_sanity0(map_types[t], map_flags[f]);
			test_lru_sanity1(map_types[t], map_flags[f], tgt_free);
			test_lru_sanity2(map_types[t], map_flags[f], tgt_free);
			test_lru_sanity3(map_types[t], map_flags[f], tgt_free);
			test_lru_sanity4(map_types[t], map_flags[f], tgt_free);
			test_lru_sanity5(map_types[t], map_flags[f]);

			printf("\n");
		}
	}

	return 0;
}
