// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <bpf_util.h>
#include <test_maps.h>

static void map_batch_update(int map_fd, __u32 max_entries, int *keys,
			     void *values, bool is_pcpu)
{
	typedef BPF_DECLARE_PERCPU(int, value);
	value *v = NULL;
	int i, j, err;
	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	if (is_pcpu)
		v = (value *)values;

	for (i = 0; i < max_entries; i++) {
		keys[i] = i + 1;
		if (is_pcpu)
			for (j = 0; j < bpf_num_possible_cpus(); j++)
				bpf_percpu(v[i], j) = i + 2 + j;
		else
			((int *)values)[i] = i + 2;
	}

	err = bpf_map_update_batch(map_fd, keys, values, &max_entries, &opts);
	CHECK(err, "bpf_map_update_batch()", "error:%s\n", strerror(errno));
}

static void map_batch_verify(int *visited, __u32 max_entries,
			     int *keys, void *values, bool is_pcpu)
{
	typedef BPF_DECLARE_PERCPU(int, value);
	value *v = NULL;
	int i, j;

	if (is_pcpu)
		v = (value *)values;

	memset(visited, 0, max_entries * sizeof(*visited));
	for (i = 0; i < max_entries; i++) {

		if (is_pcpu) {
			for (j = 0; j < bpf_num_possible_cpus(); j++) {
				CHECK(keys[i] + 1 + j != bpf_percpu(v[i], j),
				      "key/value checking",
				      "error: i %d j %d key %d value %d\n",
				      i, j, keys[i], bpf_percpu(v[i],  j));
			}
		} else {
			CHECK(keys[i] + 1 != ((int *)values)[i],
			      "key/value checking",
			      "error: i %d key %d value %d\n", i, keys[i],
			      ((int *)values)[i]);
		}

		visited[i] = 1;

	}
	for (i = 0; i < max_entries; i++) {
		CHECK(visited[i] != 1, "visited checking",
		      "error: keys array at index %d missing\n", i);
	}
}

void __test_map_lookup_and_delete_batch(bool is_pcpu)
{
	__u32 batch, count, total, total_success;
	typedef BPF_DECLARE_PERCPU(int, value);
	int map_fd, *keys, *visited, key;
	const __u32 max_entries = 10;
	value pcpu_values[max_entries];
	int err, step, value_size;
	bool nospace_err;
	void *values;
	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	map_fd = bpf_map_create(is_pcpu ? BPF_MAP_TYPE_PERCPU_HASH : BPF_MAP_TYPE_HASH,
				"hash_map", sizeof(int), sizeof(int), max_entries, NULL);
	CHECK(map_fd == -1,
	      "bpf_map_create()", "error:%s\n", strerror(errno));

	value_size = is_pcpu ? sizeof(value) : sizeof(int);
	keys = malloc(max_entries * sizeof(int));
	if (is_pcpu)
		values = pcpu_values;
	else
		values = malloc(max_entries * sizeof(int));
	visited = malloc(max_entries * sizeof(int));
	CHECK(!keys || !values || !visited, "malloc()",
	      "error:%s\n", strerror(errno));

	/* test 1: lookup/delete an empty hash table, -ENOENT */
	count = max_entries;
	err = bpf_map_lookup_and_delete_batch(map_fd, NULL, &batch, keys,
					      values, &count, &opts);
	CHECK((err && errno != ENOENT), "empty map",
	      "error: %s\n", strerror(errno));

	/* populate elements to the map */
	map_batch_update(map_fd, max_entries, keys, values, is_pcpu);

	/* test 2: lookup/delete with count = 0, success */
	count = 0;
	err = bpf_map_lookup_and_delete_batch(map_fd, NULL, &batch, keys,
					      values, &count, &opts);
	CHECK(err, "count = 0", "error: %s\n", strerror(errno));

	/* test 3: lookup/delete with count = max_entries, success */
	memset(keys, 0, max_entries * sizeof(*keys));
	memset(values, 0, max_entries * value_size);
	count = max_entries;
	err = bpf_map_lookup_and_delete_batch(map_fd, NULL, &batch, keys,
					      values, &count, &opts);
	CHECK((err && errno != ENOENT), "count = max_entries",
	       "error: %s\n", strerror(errno));
	CHECK(count != max_entries, "count = max_entries",
	      "count = %u, max_entries = %u\n", count, max_entries);
	map_batch_verify(visited, max_entries, keys, values, is_pcpu);

	/* bpf_map_get_next_key() should return -ENOENT for an empty map. */
	err = bpf_map_get_next_key(map_fd, NULL, &key);
	CHECK(!err, "bpf_map_get_next_key()", "error: %s\n", strerror(errno));

	/* test 4: lookup/delete in a loop with various steps. */
	total_success = 0;
	for (step = 1; step < max_entries; step++) {
		map_batch_update(map_fd, max_entries, keys, values, is_pcpu);
		memset(keys, 0, max_entries * sizeof(*keys));
		memset(values, 0, max_entries * value_size);
		total = 0;
		/* iteratively lookup/delete elements with 'step'
		 * elements each
		 */
		count = step;
		nospace_err = false;
		while (true) {
			err = bpf_map_lookup_batch(map_fd,
						   total ? &batch : NULL,
						   &batch, keys + total,
						   values +
						   total * value_size,
						   &count, &opts);
			/* It is possible that we are failing due to buffer size
			 * not big enough. In such cases, let us just exit and
			 * go with large steps. Not that a buffer size with
			 * max_entries should always work.
			 */
			if (err && errno == ENOSPC) {
				nospace_err = true;
				break;
			}

			CHECK((err && errno != ENOENT), "lookup with steps",
			      "error: %s\n", strerror(errno));

			total += count;
			if (err)
				break;

		}
		if (nospace_err == true)
			continue;

		CHECK(total != max_entries, "lookup with steps",
		      "total = %u, max_entries = %u\n", total, max_entries);
		map_batch_verify(visited, max_entries, keys, values, is_pcpu);

		total = 0;
		count = step;
		while (total < max_entries) {
			if (max_entries - total < step)
				count = max_entries - total;
			err = bpf_map_delete_batch(map_fd,
						   keys + total,
						   &count, &opts);
			CHECK((err && errno != ENOENT), "delete batch",
			      "error: %s\n", strerror(errno));
			total += count;
			if (err)
				break;
		}
		CHECK(total != max_entries, "delete with steps",
		      "total = %u, max_entries = %u\n", total, max_entries);

		/* check map is empty, errono == ENOENT */
		err = bpf_map_get_next_key(map_fd, NULL, &key);
		CHECK(!err || errno != ENOENT, "bpf_map_get_next_key()",
		      "error: %s\n", strerror(errno));

		/* iteratively lookup/delete elements with 'step'
		 * elements each
		 */
		map_batch_update(map_fd, max_entries, keys, values, is_pcpu);
		memset(keys, 0, max_entries * sizeof(*keys));
		memset(values, 0, max_entries * value_size);
		total = 0;
		count = step;
		nospace_err = false;
		while (true) {
			err = bpf_map_lookup_and_delete_batch(map_fd,
							total ? &batch : NULL,
							&batch, keys + total,
							values +
							total * value_size,
							&count, &opts);
			/* It is possible that we are failing due to buffer size
			 * not big enough. In such cases, let us just exit and
			 * go with large steps. Not that a buffer size with
			 * max_entries should always work.
			 */
			if (err && errno == ENOSPC) {
				nospace_err = true;
				break;
			}

			CHECK((err && errno != ENOENT), "lookup with steps",
			      "error: %s\n", strerror(errno));

			total += count;
			if (err)
				break;
		}

		if (nospace_err == true)
			continue;

		CHECK(total != max_entries, "lookup/delete with steps",
		      "total = %u, max_entries = %u\n", total, max_entries);

		map_batch_verify(visited, max_entries, keys, values, is_pcpu);
		err = bpf_map_get_next_key(map_fd, NULL, &key);
		CHECK(!err, "bpf_map_get_next_key()", "error: %s\n",
		      strerror(errno));

		total_success++;
	}

	CHECK(total_success == 0, "check total_success",
	      "unexpected failure\n");
	free(keys);
	free(visited);
	if (!is_pcpu)
		free(values);
	close(map_fd);
}

void htab_map_batch_ops(void)
{
	__test_map_lookup_and_delete_batch(false);
	printf("test_%s:PASS\n", __func__);
}

void htab_percpu_map_batch_ops(void)
{
	__test_map_lookup_and_delete_batch(true);
	printf("test_%s:PASS\n", __func__);
}

void test_htab_map_batch_ops(void)
{
	htab_map_batch_ops();
	htab_percpu_map_batch_ops();
}
