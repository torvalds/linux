// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>

#include "test_btf_map_in_map.skel.h"

static int duration;

static __u32 bpf_map_id(struct bpf_map *map)
{
	struct bpf_map_info info;
	__u32 info_len = sizeof(info);
	int err;

	memset(&info, 0, info_len);
	err = bpf_obj_get_info_by_fd(bpf_map__fd(map), &info, &info_len);
	if (err)
		return 0;
	return info.id;
}

static void test_lookup_update(void)
{
	int map1_fd, map2_fd, map3_fd, map4_fd, map5_fd, map1_id, map2_id;
	int outer_arr_fd, outer_hash_fd, outer_arr_dyn_fd;
	struct test_btf_map_in_map *skel;
	int err, key = 0, val, i;

	skel = test_btf_map_in_map__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open&load skeleton\n"))
		return;

	err = test_btf_map_in_map__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	map1_fd = bpf_map__fd(skel->maps.inner_map1);
	map2_fd = bpf_map__fd(skel->maps.inner_map2);
	map3_fd = bpf_map__fd(skel->maps.inner_map3);
	map4_fd = bpf_map__fd(skel->maps.inner_map4);
	map5_fd = bpf_map__fd(skel->maps.inner_map5);
	outer_arr_dyn_fd = bpf_map__fd(skel->maps.outer_arr_dyn);
	outer_arr_fd = bpf_map__fd(skel->maps.outer_arr);
	outer_hash_fd = bpf_map__fd(skel->maps.outer_hash);

	/* inner1 = input, inner2 = input + 1, inner3 = input + 2 */
	bpf_map_update_elem(outer_arr_fd, &key, &map1_fd, 0);
	bpf_map_update_elem(outer_hash_fd, &key, &map2_fd, 0);
	bpf_map_update_elem(outer_arr_dyn_fd, &key, &map3_fd, 0);
	skel->bss->input = 1;
	usleep(1);
	bpf_map_lookup_elem(map1_fd, &key, &val);
	CHECK(val != 1, "inner1", "got %d != exp %d\n", val, 1);
	bpf_map_lookup_elem(map2_fd, &key, &val);
	CHECK(val != 2, "inner2", "got %d != exp %d\n", val, 2);
	bpf_map_lookup_elem(map3_fd, &key, &val);
	CHECK(val != 3, "inner3", "got %d != exp %d\n", val, 3);

	/* inner2 = input, inner1 = input + 1, inner4 = input + 2 */
	bpf_map_update_elem(outer_arr_fd, &key, &map2_fd, 0);
	bpf_map_update_elem(outer_hash_fd, &key, &map1_fd, 0);
	bpf_map_update_elem(outer_arr_dyn_fd, &key, &map4_fd, 0);
	skel->bss->input = 3;
	usleep(1);
	bpf_map_lookup_elem(map1_fd, &key, &val);
	CHECK(val != 4, "inner1", "got %d != exp %d\n", val, 4);
	bpf_map_lookup_elem(map2_fd, &key, &val);
	CHECK(val != 3, "inner2", "got %d != exp %d\n", val, 3);
	bpf_map_lookup_elem(map4_fd, &key, &val);
	CHECK(val != 5, "inner4", "got %d != exp %d\n", val, 5);

	/* inner5 = input + 2 */
	bpf_map_update_elem(outer_arr_dyn_fd, &key, &map5_fd, 0);
	skel->bss->input = 5;
	usleep(1);
	bpf_map_lookup_elem(map5_fd, &key, &val);
	CHECK(val != 7, "inner5", "got %d != exp %d\n", val, 7);

	for (i = 0; i < 5; i++) {
		val = i % 2 ? map1_fd : map2_fd;
		err = bpf_map_update_elem(outer_hash_fd, &key, &val, 0);
		if (CHECK_FAIL(err)) {
			printf("failed to update hash_of_maps on iter #%d\n", i);
			goto cleanup;
		}
		err = bpf_map_update_elem(outer_arr_fd, &key, &val, 0);
		if (CHECK_FAIL(err)) {
			printf("failed to update array_of_maps on iter #%d\n", i);
			goto cleanup;
		}
		val = i % 2 ? map4_fd : map5_fd;
		err = bpf_map_update_elem(outer_arr_dyn_fd, &key, &val, 0);
		if (CHECK_FAIL(err)) {
			printf("failed to update array_of_maps (dyn) on iter #%d\n", i);
			goto cleanup;
		}
	}

	map1_id = bpf_map_id(skel->maps.inner_map1);
	map2_id = bpf_map_id(skel->maps.inner_map2);
	CHECK(map1_id == 0, "map1_id", "failed to get ID 1\n");
	CHECK(map2_id == 0, "map2_id", "failed to get ID 2\n");

cleanup:
	test_btf_map_in_map__destroy(skel);
}

static void test_diff_size(void)
{
	struct test_btf_map_in_map *skel;
	int err, inner_map_fd, zero = 0;

	skel = test_btf_map_in_map__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open&load skeleton\n"))
		return;

	inner_map_fd = bpf_map__fd(skel->maps.sockarr_sz2);
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.outer_sockarr), &zero,
				  &inner_map_fd, 0);
	CHECK(err, "outer_sockarr inner map size check",
	      "cannot use a different size inner_map\n");

	inner_map_fd = bpf_map__fd(skel->maps.inner_map_sz2);
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.outer_arr), &zero,
				  &inner_map_fd, 0);
	CHECK(!err, "outer_arr inner map size check",
	      "incorrectly updated with a different size inner_map\n");

	test_btf_map_in_map__destroy(skel);
}

void test_btf_map_in_map(void)
{
	if (test__start_subtest("lookup_update"))
		test_lookup_update();

	if (test__start_subtest("diff_size"))
		test_diff_size();
}
