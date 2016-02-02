/*
 * Testsuite for eBPF maps
 *
 * Copyright (c) 2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <stdio.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "libbpf.h"

/* sanity tests for map API */
static void test_hashmap_sanity(int i, void *data)
{
	long long key, next_key, value;
	int map_fd;

	map_fd = bpf_create_map(BPF_MAP_TYPE_HASH, sizeof(key), sizeof(value), 2);
	if (map_fd < 0) {
		printf("failed to create hashmap '%s'\n", strerror(errno));
		exit(1);
	}

	key = 1;
	value = 1234;
	/* insert key=1 element */
	assert(bpf_update_elem(map_fd, &key, &value, BPF_ANY) == 0);

	value = 0;
	/* BPF_NOEXIST means: add new element if it doesn't exist */
	assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == -1 &&
	       /* key=1 already exists */
	       errno == EEXIST);

	assert(bpf_update_elem(map_fd, &key, &value, -1) == -1 && errno == EINVAL);

	/* check that key=1 can be found */
	assert(bpf_lookup_elem(map_fd, &key, &value) == 0 && value == 1234);

	key = 2;
	/* check that key=2 is not found */
	assert(bpf_lookup_elem(map_fd, &key, &value) == -1 && errno == ENOENT);

	/* BPF_EXIST means: update existing element */
	assert(bpf_update_elem(map_fd, &key, &value, BPF_EXIST) == -1 &&
	       /* key=2 is not there */
	       errno == ENOENT);

	/* insert key=2 element */
	assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == 0);

	/* key=1 and key=2 were inserted, check that key=0 cannot be inserted
	 * due to max_entries limit
	 */
	key = 0;
	assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == E2BIG);

	/* check that key = 0 doesn't exist */
	assert(bpf_delete_elem(map_fd, &key) == -1 && errno == ENOENT);

	/* iterate over two elements */
	assert(bpf_get_next_key(map_fd, &key, &next_key) == 0 &&
	       (next_key == 1 || next_key == 2));
	assert(bpf_get_next_key(map_fd, &next_key, &next_key) == 0 &&
	       (next_key == 1 || next_key == 2));
	assert(bpf_get_next_key(map_fd, &next_key, &next_key) == -1 &&
	       errno == ENOENT);

	/* delete both elements */
	key = 1;
	assert(bpf_delete_elem(map_fd, &key) == 0);
	key = 2;
	assert(bpf_delete_elem(map_fd, &key) == 0);
	assert(bpf_delete_elem(map_fd, &key) == -1 && errno == ENOENT);

	key = 0;
	/* check that map is empty */
	assert(bpf_get_next_key(map_fd, &key, &next_key) == -1 &&
	       errno == ENOENT);
	close(map_fd);
}

/* sanity tests for percpu map API */
static void test_percpu_hashmap_sanity(int task, void *data)
{
	long long key, next_key;
	int expected_key_mask = 0;
	unsigned int nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	long long value[nr_cpus];
	int map_fd, i;

	map_fd = bpf_create_map(BPF_MAP_TYPE_PERCPU_HASH, sizeof(key),
				sizeof(value[0]), 2);
	if (map_fd < 0) {
		printf("failed to create hashmap '%s'\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++)
		value[i] = i + 100;
	key = 1;
	/* insert key=1 element */
	assert(!(expected_key_mask & key));
	assert(bpf_update_elem(map_fd, &key, value, BPF_ANY) == 0);
	expected_key_mask |= key;

	/* BPF_NOEXIST means: add new element if it doesn't exist */
	assert(bpf_update_elem(map_fd, &key, value, BPF_NOEXIST) == -1 &&
	       /* key=1 already exists */
	       errno == EEXIST);

	/* -1 is an invalid flag */
	assert(bpf_update_elem(map_fd, &key, value, -1) == -1 &&
	       errno == EINVAL);

	/* check that key=1 can be found. value could be 0 if the lookup
	 * was run from a different cpu.
	 */
	value[0] = 1;
	assert(bpf_lookup_elem(map_fd, &key, value) == 0 && value[0] == 100);

	key = 2;
	/* check that key=2 is not found */
	assert(bpf_lookup_elem(map_fd, &key, value) == -1 && errno == ENOENT);

	/* BPF_EXIST means: update existing element */
	assert(bpf_update_elem(map_fd, &key, value, BPF_EXIST) == -1 &&
	       /* key=2 is not there */
	       errno == ENOENT);

	/* insert key=2 element */
	assert(!(expected_key_mask & key));
	assert(bpf_update_elem(map_fd, &key, value, BPF_NOEXIST) == 0);
	expected_key_mask |= key;

	/* key=1 and key=2 were inserted, check that key=0 cannot be inserted
	 * due to max_entries limit
	 */
	key = 0;
	assert(bpf_update_elem(map_fd, &key, value, BPF_NOEXIST) == -1 &&
	       errno == E2BIG);

	/* check that key = 0 doesn't exist */
	assert(bpf_delete_elem(map_fd, &key) == -1 && errno == ENOENT);

	/* iterate over two elements */
	while (!bpf_get_next_key(map_fd, &key, &next_key)) {
		assert((expected_key_mask & next_key) == next_key);
		expected_key_mask &= ~next_key;

		assert(bpf_lookup_elem(map_fd, &next_key, value) == 0);
		for (i = 0; i < nr_cpus; i++)
			assert(value[i] == i + 100);

		key = next_key;
	}
	assert(errno == ENOENT);

	/* Update with BPF_EXIST */
	key = 1;
	assert(bpf_update_elem(map_fd, &key, value, BPF_EXIST) == 0);

	/* delete both elements */
	key = 1;
	assert(bpf_delete_elem(map_fd, &key) == 0);
	key = 2;
	assert(bpf_delete_elem(map_fd, &key) == 0);
	assert(bpf_delete_elem(map_fd, &key) == -1 && errno == ENOENT);

	key = 0;
	/* check that map is empty */
	assert(bpf_get_next_key(map_fd, &key, &next_key) == -1 &&
	       errno == ENOENT);
	close(map_fd);
}

static void test_arraymap_sanity(int i, void *data)
{
	int key, next_key, map_fd;
	long long value;

	map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(key), sizeof(value), 2);
	if (map_fd < 0) {
		printf("failed to create arraymap '%s'\n", strerror(errno));
		exit(1);
	}

	key = 1;
	value = 1234;
	/* insert key=1 element */
	assert(bpf_update_elem(map_fd, &key, &value, BPF_ANY) == 0);

	value = 0;
	assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == EEXIST);

	/* check that key=1 can be found */
	assert(bpf_lookup_elem(map_fd, &key, &value) == 0 && value == 1234);

	key = 0;
	/* check that key=0 is also found and zero initialized */
	assert(bpf_lookup_elem(map_fd, &key, &value) == 0 && value == 0);


	/* key=0 and key=1 were inserted, check that key=2 cannot be inserted
	 * due to max_entries limit
	 */
	key = 2;
	assert(bpf_update_elem(map_fd, &key, &value, BPF_EXIST) == -1 &&
	       errno == E2BIG);

	/* check that key = 2 doesn't exist */
	assert(bpf_lookup_elem(map_fd, &key, &value) == -1 && errno == ENOENT);

	/* iterate over two elements */
	assert(bpf_get_next_key(map_fd, &key, &next_key) == 0 &&
	       next_key == 0);
	assert(bpf_get_next_key(map_fd, &next_key, &next_key) == 0 &&
	       next_key == 1);
	assert(bpf_get_next_key(map_fd, &next_key, &next_key) == -1 &&
	       errno == ENOENT);

	/* delete shouldn't succeed */
	key = 1;
	assert(bpf_delete_elem(map_fd, &key) == -1 && errno == EINVAL);

	close(map_fd);
}

#define MAP_SIZE (32 * 1024)
static void test_map_large(void)
{
	struct bigkey {
		int a;
		char b[116];
		long long c;
	} key;
	int map_fd, i, value;

	/* allocate 4Mbyte of memory */
	map_fd = bpf_create_map(BPF_MAP_TYPE_HASH, sizeof(key), sizeof(value),
				MAP_SIZE);
	if (map_fd < 0) {
		printf("failed to create large map '%s'\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < MAP_SIZE; i++) {
		key = (struct bigkey) {.c = i};
		value = i;
		assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == 0);
	}
	key.c = -1;
	assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == E2BIG);

	/* iterate through all elements */
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_get_next_key(map_fd, &key, &key) == 0);
	assert(bpf_get_next_key(map_fd, &key, &key) == -1 && errno == ENOENT);

	key.c = 0;
	assert(bpf_lookup_elem(map_fd, &key, &value) == 0 && value == 0);
	key.a = 1;
	assert(bpf_lookup_elem(map_fd, &key, &value) == -1 && errno == ENOENT);

	close(map_fd);
}

/* fork N children and wait for them to complete */
static void run_parallel(int tasks, void (*fn)(int i, void *data), void *data)
{
	pid_t pid[tasks];
	int i;

	for (i = 0; i < tasks; i++) {
		pid[i] = fork();
		if (pid[i] == 0) {
			fn(i, data);
			exit(0);
		} else if (pid[i] == -1) {
			printf("couldn't spawn #%d process\n", i);
			exit(1);
		}
	}
	for (i = 0; i < tasks; i++) {
		int status;

		assert(waitpid(pid[i], &status, 0) == pid[i]);
		assert(status == 0);
	}
}

static void test_map_stress(void)
{
	run_parallel(100, test_hashmap_sanity, NULL);
	run_parallel(100, test_percpu_hashmap_sanity, NULL);
	run_parallel(100, test_arraymap_sanity, NULL);
}

#define TASKS 1024
#define DO_UPDATE 1
#define DO_DELETE 0
static void do_work(int fn, void *data)
{
	int map_fd = ((int *)data)[0];
	int do_update = ((int *)data)[1];
	int i;
	int key, value;

	for (i = fn; i < MAP_SIZE; i += TASKS) {
		key = value = i;
		if (do_update)
			assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == 0);
		else
			assert(bpf_delete_elem(map_fd, &key) == 0);
	}
}

static void test_map_parallel(void)
{
	int i, map_fd, key = 0, value = 0;
	int data[2];

	map_fd = bpf_create_map(BPF_MAP_TYPE_HASH, sizeof(key), sizeof(value),
				MAP_SIZE);
	if (map_fd < 0) {
		printf("failed to create map for parallel test '%s'\n",
		       strerror(errno));
		exit(1);
	}

	data[0] = map_fd;
	data[1] = DO_UPDATE;
	/* use the same map_fd in children to add elements to this map
	 * child_0 adds key=0, key=1024, key=2048, ...
	 * child_1 adds key=1, key=1025, key=2049, ...
	 * child_1023 adds key=1023, ...
	 */
	run_parallel(TASKS, do_work, data);

	/* check that key=0 is already there */
	assert(bpf_update_elem(map_fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == EEXIST);

	/* check that all elements were inserted */
	key = -1;
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_get_next_key(map_fd, &key, &key) == 0);
	assert(bpf_get_next_key(map_fd, &key, &key) == -1 && errno == ENOENT);

	/* another check for all elements */
	for (i = 0; i < MAP_SIZE; i++) {
		key = MAP_SIZE - i - 1;
		assert(bpf_lookup_elem(map_fd, &key, &value) == 0 &&
		       value == key);
	}

	/* now let's delete all elemenets in parallel */
	data[1] = DO_DELETE;
	run_parallel(TASKS, do_work, data);

	/* nothing should be left */
	key = -1;
	assert(bpf_get_next_key(map_fd, &key, &key) == -1 && errno == ENOENT);
}

int main(void)
{
	test_hashmap_sanity(0, NULL);
	test_percpu_hashmap_sanity(0, NULL);
	test_arraymap_sanity(0, NULL);
	test_map_large();
	test_map_parallel();
	test_map_stress();
	printf("test_maps: OK\n");
	return 0;
}
