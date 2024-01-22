// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#include <test_progs.h>

#include "normal_map_btf.skel.h"
#include "map_in_map_btf.skel.h"

static void do_test_normal_map_btf(void)
{
	struct normal_map_btf *skel;
	int i, err, new_fd = -1;
	int map_fd_arr[64];

	skel = normal_map_btf__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_load"))
		return;

	err = normal_map_btf__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto out;

	skel->bss->pid = getpid();
	usleep(1);
	ASSERT_TRUE(skel->bss->done, "done");

	/* Use percpu_array to slow bpf_map_free_deferred() down.
	 * The memory allocation may fail, so doesn't check the returned fd.
	 */
	for (i = 0; i < ARRAY_SIZE(map_fd_arr); i++)
		map_fd_arr[i] = bpf_map_create(BPF_MAP_TYPE_PERCPU_ARRAY, NULL, 4, 4, 256, NULL);

	/* Close array fd later */
	new_fd = dup(bpf_map__fd(skel->maps.array));
out:
	normal_map_btf__destroy(skel);
	if (new_fd < 0)
		return;
	/* Use kern_sync_rcu() to wait for the start of the free of the bpf
	 * program and use an assumed delay to wait for the release of the map
	 * btf which is held by other maps (e.g, bss). After that, array map
	 * holds the last reference of map btf.
	 */
	kern_sync_rcu();
	usleep(4000);
	/* Spawn multiple kworkers to delay the invocation of
	 * bpf_map_free_deferred() for array map.
	 */
	for (i = 0; i < ARRAY_SIZE(map_fd_arr); i++) {
		if (map_fd_arr[i] < 0)
			continue;
		close(map_fd_arr[i]);
	}
	close(new_fd);
}

static void do_test_map_in_map_btf(void)
{
	int err, zero = 0, new_fd = -1;
	struct map_in_map_btf *skel;

	skel = map_in_map_btf__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_load"))
		return;

	err = map_in_map_btf__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto out;

	skel->bss->pid = getpid();
	usleep(1);
	ASSERT_TRUE(skel->bss->done, "done");

	/* Close inner_array fd later */
	new_fd = dup(bpf_map__fd(skel->maps.inner_array));
	/* Defer the free of inner_array */
	err = bpf_map__delete_elem(skel->maps.outer_array, &zero, sizeof(zero), 0);
	ASSERT_OK(err, "delete inner map");
out:
	map_in_map_btf__destroy(skel);
	if (new_fd < 0)
		return;
	/* Use kern_sync_rcu() to wait for the start of the free of the bpf
	 * program and use an assumed delay to wait for the free of the outer
	 * map and the release of map btf. After that, inner map holds the last
	 * reference of map btf.
	 */
	kern_sync_rcu();
	usleep(10000);
	close(new_fd);
}

void test_map_btf(void)
{
	if (test__start_subtest("array_btf"))
		do_test_normal_map_btf();
	if (test__start_subtest("inner_array_btf"))
		do_test_map_in_map_btf();
}
