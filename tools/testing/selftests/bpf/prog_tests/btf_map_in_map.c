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

/*
 * Trigger synchronize_rcu() in kernel.
 *
 * ARRAY_OF_MAPS/HASH_OF_MAPS lookup/update operations trigger synchronize_rcu()
 * if looking up an existing non-NULL element or updating the map with a valid
 * inner map FD. Use this fact to trigger synchronize_rcu(): create map-in-map,
 * create a trivial ARRAY map, update map-in-map with ARRAY inner map. Then
 * cleanup. At the end, at least one synchronize_rcu() would be called.
 */
static int kern_sync_rcu(void)
{
	int inner_map_fd, outer_map_fd, err, zero = 0;

	inner_map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 4, 1, 0);
	if (CHECK(inner_map_fd < 0, "inner_map_create", "failed %d\n", -errno))
		return -1;

	outer_map_fd = bpf_create_map_in_map(BPF_MAP_TYPE_ARRAY_OF_MAPS, NULL,
					     sizeof(int), inner_map_fd, 1, 0);
	if (CHECK(outer_map_fd < 0, "outer_map_create", "failed %d\n", -errno)) {
		close(inner_map_fd);
		return -1;
	}

	err = bpf_map_update_elem(outer_map_fd, &zero, &inner_map_fd, 0);
	if (err)
		err = -errno;
	CHECK(err, "outer_map_update", "failed %d\n", err);
	close(inner_map_fd);
	close(outer_map_fd);
	return err;
}

void test_btf_map_in_map(void)
{
	int err, key = 0, val, i;
	struct test_btf_map_in_map *skel;
	int outer_arr_fd, outer_hash_fd;
	int fd, map1_fd, map2_fd, map1_id, map2_id;

	skel = test_btf_map_in_map__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open&load skeleton\n"))
		return;

	err = test_btf_map_in_map__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	map1_fd = bpf_map__fd(skel->maps.inner_map1);
	map2_fd = bpf_map__fd(skel->maps.inner_map2);
	outer_arr_fd = bpf_map__fd(skel->maps.outer_arr);
	outer_hash_fd = bpf_map__fd(skel->maps.outer_hash);

	/* inner1 = input, inner2 = input + 1 */
	map1_fd = bpf_map__fd(skel->maps.inner_map1);
	bpf_map_update_elem(outer_arr_fd, &key, &map1_fd, 0);
	map2_fd = bpf_map__fd(skel->maps.inner_map2);
	bpf_map_update_elem(outer_hash_fd, &key, &map2_fd, 0);
	skel->bss->input = 1;
	usleep(1);

	bpf_map_lookup_elem(map1_fd, &key, &val);
	CHECK(val != 1, "inner1", "got %d != exp %d\n", val, 1);
	bpf_map_lookup_elem(map2_fd, &key, &val);
	CHECK(val != 2, "inner2", "got %d != exp %d\n", val, 2);

	/* inner1 = input + 1, inner2 = input */
	bpf_map_update_elem(outer_arr_fd, &key, &map2_fd, 0);
	bpf_map_update_elem(outer_hash_fd, &key, &map1_fd, 0);
	skel->bss->input = 3;
	usleep(1);

	bpf_map_lookup_elem(map1_fd, &key, &val);
	CHECK(val != 4, "inner1", "got %d != exp %d\n", val, 4);
	bpf_map_lookup_elem(map2_fd, &key, &val);
	CHECK(val != 3, "inner2", "got %d != exp %d\n", val, 3);

	for (i = 0; i < 5; i++) {
		val = i % 2 ? map1_fd : map2_fd;
		err = bpf_map_update_elem(outer_hash_fd, &key, &val, 0);
		if (CHECK_FAIL(err)) {
			printf("failed to update hash_of_maps on iter #%d\n", i);
			goto cleanup;
		}
		err = bpf_map_update_elem(outer_arr_fd, &key, &val, 0);
		if (CHECK_FAIL(err)) {
			printf("failed to update hash_of_maps on iter #%d\n", i);
			goto cleanup;
		}
	}

	map1_id = bpf_map_id(skel->maps.inner_map1);
	map2_id = bpf_map_id(skel->maps.inner_map2);
	CHECK(map1_id == 0, "map1_id", "failed to get ID 1\n");
	CHECK(map2_id == 0, "map2_id", "failed to get ID 2\n");

	test_btf_map_in_map__destroy(skel);
	skel = NULL;

	/* we need to either wait for or force synchronize_rcu(), before
	 * checking for "still exists" condition, otherwise map could still be
	 * resolvable by ID, causing false positives.
	 *
	 * Older kernels (5.8 and earlier) freed map only after two
	 * synchronize_rcu()s, so trigger two, to be entirely sure.
	 */
	CHECK(kern_sync_rcu(), "sync_rcu", "failed\n");
	CHECK(kern_sync_rcu(), "sync_rcu", "failed\n");

	fd = bpf_map_get_fd_by_id(map1_id);
	if (CHECK(fd >= 0, "map1_leak", "inner_map1 leaked!\n")) {
		close(fd);
		goto cleanup;
	}
	fd = bpf_map_get_fd_by_id(map2_id);
	if (CHECK(fd >= 0, "map2_leak", "inner_map2 leaked!\n")) {
		close(fd);
		goto cleanup;
	}

cleanup:
	test_btf_map_in_map__destroy(skel);
}
