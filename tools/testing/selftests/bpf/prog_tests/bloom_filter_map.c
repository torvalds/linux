// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <sys/syscall.h>
#include <test_progs.h>
#include "bloom_filter_map.skel.h"

static void test_fail_cases(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	__u32 value;
	int fd, err;

	/* Invalid key size */
	fd = bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER, NULL, 4, sizeof(value), 100, NULL);
	if (!ASSERT_LT(fd, 0, "bpf_map_create bloom filter invalid key size"))
		close(fd);

	/* Invalid value size */
	fd = bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER, NULL, 0, 0, 100, NULL);
	if (!ASSERT_LT(fd, 0, "bpf_map_create bloom filter invalid value size 0"))
		close(fd);

	/* Invalid max entries size */
	fd = bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER, NULL, 0, sizeof(value), 0, NULL);
	if (!ASSERT_LT(fd, 0, "bpf_map_create bloom filter invalid max entries size"))
		close(fd);

	/* Bloom filter maps do not support BPF_F_NO_PREALLOC */
	opts.map_flags = BPF_F_NO_PREALLOC;
	fd = bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER, NULL, 0, sizeof(value), 100, &opts);
	if (!ASSERT_LT(fd, 0, "bpf_map_create bloom filter invalid flags"))
		close(fd);

	fd = bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER, NULL, 0, sizeof(value), 100, NULL);
	if (!ASSERT_GE(fd, 0, "bpf_map_create bloom filter"))
		return;

	/* Test invalid flags */
	err = bpf_map_update_elem(fd, NULL, &value, -1);
	ASSERT_EQ(err, -EINVAL, "bpf_map_update_elem bloom filter invalid flags");

	err = bpf_map_update_elem(fd, NULL, &value, BPF_EXIST);
	ASSERT_EQ(err, -EINVAL, "bpf_map_update_elem bloom filter invalid flags");

	err = bpf_map_update_elem(fd, NULL, &value, BPF_F_LOCK);
	ASSERT_EQ(err, -EINVAL, "bpf_map_update_elem bloom filter invalid flags");

	err = bpf_map_update_elem(fd, NULL, &value, BPF_NOEXIST);
	ASSERT_EQ(err, -EINVAL, "bpf_map_update_elem bloom filter invalid flags");

	err = bpf_map_update_elem(fd, NULL, &value, 10000);
	ASSERT_EQ(err, -EINVAL, "bpf_map_update_elem bloom filter invalid flags");

	close(fd);
}

static void test_success_cases(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	char value[11];
	int fd, err;

	/* Create a map */
	opts.map_flags = BPF_F_ZERO_SEED | BPF_F_NUMA_NODE;
	fd = bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER, NULL, 0, sizeof(value), 100, &opts);
	if (!ASSERT_GE(fd, 0, "bpf_map_create bloom filter success case"))
		return;

	/* Add a value to the bloom filter */
	err = bpf_map_update_elem(fd, NULL, &value, 0);
	if (!ASSERT_OK(err, "bpf_map_update_elem bloom filter success case"))
		goto done;

	 /* Lookup a value in the bloom filter */
	err = bpf_map_lookup_elem(fd, NULL, &value);
	ASSERT_OK(err, "bpf_map_update_elem bloom filter success case");

done:
	close(fd);
}

static void check_bloom(struct bloom_filter_map *skel)
{
	struct bpf_link *link;

	link = bpf_program__attach(skel->progs.check_bloom);
	if (!ASSERT_OK_PTR(link, "link"))
		return;

	syscall(SYS_getpgid);

	ASSERT_EQ(skel->bss->error, 0, "error");

	bpf_link__destroy(link);
}

static void test_inner_map(struct bloom_filter_map *skel, const __u32 *rand_vals,
			   __u32 nr_rand_vals)
{
	int outer_map_fd, inner_map_fd, err, i, key = 0;
	struct bpf_link *link;

	/* Create a bloom filter map that will be used as the inner map */
	inner_map_fd = bpf_map_create(BPF_MAP_TYPE_BLOOM_FILTER, NULL, 0, sizeof(*rand_vals),
				      nr_rand_vals, NULL);
	if (!ASSERT_GE(inner_map_fd, 0, "bpf_map_create bloom filter inner map"))
		return;

	for (i = 0; i < nr_rand_vals; i++) {
		err = bpf_map_update_elem(inner_map_fd, NULL, rand_vals + i, BPF_ANY);
		if (!ASSERT_OK(err, "Add random value to inner_map_fd"))
			goto done;
	}

	/* Add the bloom filter map to the outer map */
	outer_map_fd = bpf_map__fd(skel->maps.outer_map);
	err = bpf_map_update_elem(outer_map_fd, &key, &inner_map_fd, BPF_ANY);
	if (!ASSERT_OK(err, "Add bloom filter map to outer map"))
		goto done;

	/* Attach the bloom_filter_inner_map prog */
	link = bpf_program__attach(skel->progs.inner_map);
	if (!ASSERT_OK_PTR(link, "link"))
		goto delete_inner_map;

	syscall(SYS_getpgid);

	ASSERT_EQ(skel->bss->error, 0, "error");

	bpf_link__destroy(link);

delete_inner_map:
	/* Ensure the inner bloom filter map can be deleted */
	err = bpf_map_delete_elem(outer_map_fd, &key);
	ASSERT_OK(err, "Delete inner bloom filter map");

done:
	close(inner_map_fd);
}

static int setup_progs(struct bloom_filter_map **out_skel, __u32 **out_rand_vals,
		       __u32 *out_nr_rand_vals)
{
	struct bloom_filter_map *skel;
	int random_data_fd, bloom_fd;
	__u32 *rand_vals = NULL;
	__u32 map_size, val;
	int err, i;

	/* Set up a bloom filter map skeleton */
	skel = bloom_filter_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bloom_filter_map__open_and_load"))
		return -EINVAL;

	/* Set up rand_vals */
	map_size = bpf_map__max_entries(skel->maps.map_random_data);
	rand_vals = malloc(sizeof(*rand_vals) * map_size);
	if (!rand_vals) {
		err = -ENOMEM;
		goto error;
	}

	/* Generate random values and populate both skeletons */
	random_data_fd = bpf_map__fd(skel->maps.map_random_data);
	bloom_fd = bpf_map__fd(skel->maps.map_bloom);
	for (i = 0; i < map_size; i++) {
		val = rand();

		err = bpf_map_update_elem(random_data_fd, &i, &val, BPF_ANY);
		if (!ASSERT_OK(err, "Add random value to map_random_data"))
			goto error;

		err = bpf_map_update_elem(bloom_fd, NULL, &val, BPF_ANY);
		if (!ASSERT_OK(err, "Add random value to map_bloom"))
			goto error;

		rand_vals[i] = val;
	}

	*out_skel = skel;
	*out_rand_vals = rand_vals;
	*out_nr_rand_vals = map_size;

	return 0;

error:
	bloom_filter_map__destroy(skel);
	if (rand_vals)
		free(rand_vals);
	return err;
}

void test_bloom_filter_map(void)
{
	__u32 *rand_vals, nr_rand_vals;
	struct bloom_filter_map *skel;
	int err;

	test_fail_cases();
	test_success_cases();

	err = setup_progs(&skel, &rand_vals, &nr_rand_vals);
	if (err)
		return;

	test_inner_map(skel, rand_vals, nr_rand_vals);
	free(rand_vals);

	check_bloom(skel);

	bloom_filter_map__destroy(skel);
}
