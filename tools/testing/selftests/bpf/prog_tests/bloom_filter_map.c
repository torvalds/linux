// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <sys/syscall.h>
#include <test_progs.h>
#include "bloom_filter_map.skel.h"

static void test_fail_cases(void)
{
	struct bpf_create_map_attr xattr = {
		.name = "bloom_filter_map",
		.map_type = BPF_MAP_TYPE_BLOOM_FILTER,
		.max_entries = 100,
		.value_size = 11,
	};
	__u32 value;
	int fd, err;

	/* Invalid key size */
	xattr.key_size = 4;
	fd = bpf_create_map_xattr(&xattr);
	if (!ASSERT_LT(fd, 0, "bpf_create_map bloom filter invalid key size"))
		close(fd);
	xattr.key_size = 0;

	/* Invalid value size */
	xattr.value_size = 0;
	fd = bpf_create_map_xattr(&xattr);
	if (!ASSERT_LT(fd, 0, "bpf_create_map bloom filter invalid value size 0"))
		close(fd);
	xattr.value_size = 11;

	/* Invalid max entries size */
	xattr.max_entries = 0;
	fd = bpf_create_map_xattr(&xattr);
	if (!ASSERT_LT(fd, 0, "bpf_create_map bloom filter invalid max entries size"))
		close(fd);
	xattr.max_entries = 100;

	/* Bloom filter maps do not support BPF_F_NO_PREALLOC */
	xattr.map_flags = BPF_F_NO_PREALLOC;
	fd = bpf_create_map_xattr(&xattr);
	if (!ASSERT_LT(fd, 0, "bpf_create_map bloom filter invalid flags"))
		close(fd);
	xattr.map_flags = 0;

	fd = bpf_create_map_xattr(&xattr);
	if (!ASSERT_GE(fd, 0, "bpf_create_map bloom filter"))
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
	struct bpf_create_map_attr xattr = {
		.name = "bloom_filter_inner_map",
		.map_type = BPF_MAP_TYPE_BLOOM_FILTER,
		.value_size = sizeof(__u32),
		.max_entries = nr_rand_vals,
	};
	struct bpf_link *link;

	/* Create a bloom filter map that will be used as the inner map */
	inner_map_fd = bpf_create_map_xattr(&xattr);
	if (!ASSERT_GE(inner_map_fd, 0, "bpf_create_map bloom filter inner map"))
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

	err = setup_progs(&skel, &rand_vals, &nr_rand_vals);
	if (err)
		return;

	test_inner_map(skel, rand_vals, nr_rand_vals);
	free(rand_vals);

	check_bloom(skel);

	bloom_filter_map__destroy(skel);
}
