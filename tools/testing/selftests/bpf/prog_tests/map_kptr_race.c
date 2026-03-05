// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <network_helpers.h>

#include "map_kptr_race.skel.h"

static int get_map_id(int map_fd)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);

	if (!ASSERT_OK(bpf_map_get_info_by_fd(map_fd, &info, &len), "get_map_info"))
		return -1;
	return info.id;
}

static int read_refs(struct map_kptr_race *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int ret;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.count_ref), &opts);
	if (!ASSERT_OK(ret, "count_ref run"))
		return -1;
	if (!ASSERT_OK(opts.retval, "count_ref retval"))
		return -1;
	return skel->bss->num_of_refs;
}

static void test_htab_leak(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);
	struct map_kptr_race *skel, *watcher;
	int ret, map_id;

	skel = map_kptr_race__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_htab_leak), &opts);
	if (!ASSERT_OK(ret, "test_htab_leak run"))
		goto out_skel;
	if (!ASSERT_OK(opts.retval, "test_htab_leak retval"))
		goto out_skel;

	map_id = get_map_id(bpf_map__fd(skel->maps.race_hash_map));
	if (!ASSERT_GE(map_id, 0, "map_id"))
		goto out_skel;

	watcher = map_kptr_race__open_and_load();
	if (!ASSERT_OK_PTR(watcher, "watcher open_and_load"))
		goto out_skel;

	watcher->bss->target_map_id = map_id;
	watcher->links.map_put = bpf_program__attach(watcher->progs.map_put);
	if (!ASSERT_OK_PTR(watcher->links.map_put, "attach fentry"))
		goto out_watcher;
	watcher->links.htab_map_free = bpf_program__attach(watcher->progs.htab_map_free);
	if (!ASSERT_OK_PTR(watcher->links.htab_map_free, "attach fexit"))
		goto out_watcher;

	map_kptr_race__destroy(skel);
	skel = NULL;

	kern_sync_rcu();

	while (!READ_ONCE(watcher->bss->map_freed))
		sched_yield();

	ASSERT_EQ(watcher->bss->map_freed, 1, "map_freed");
	ASSERT_EQ(read_refs(watcher), 2, "htab refcount");

out_watcher:
	map_kptr_race__destroy(watcher);
out_skel:
	map_kptr_race__destroy(skel);
}

static void test_percpu_htab_leak(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);
	struct map_kptr_race *skel, *watcher;
	int ret, map_id;

	skel = map_kptr_race__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	skel->rodata->nr_cpus = libbpf_num_possible_cpus();
	if (skel->rodata->nr_cpus > 16)
		skel->rodata->nr_cpus = 16;

	ret = map_kptr_race__load(skel);
	if (!ASSERT_OK(ret, "load"))
		goto out_skel;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_percpu_htab_leak), &opts);
	if (!ASSERT_OK(ret, "test_percpu_htab_leak run"))
		goto out_skel;
	if (!ASSERT_OK(opts.retval, "test_percpu_htab_leak retval"))
		goto out_skel;

	map_id = get_map_id(bpf_map__fd(skel->maps.race_percpu_hash_map));
	if (!ASSERT_GE(map_id, 0, "map_id"))
		goto out_skel;

	watcher = map_kptr_race__open_and_load();
	if (!ASSERT_OK_PTR(watcher, "watcher open_and_load"))
		goto out_skel;

	watcher->bss->target_map_id = map_id;
	watcher->links.map_put = bpf_program__attach(watcher->progs.map_put);
	if (!ASSERT_OK_PTR(watcher->links.map_put, "attach fentry"))
		goto out_watcher;
	watcher->links.htab_map_free = bpf_program__attach(watcher->progs.htab_map_free);
	if (!ASSERT_OK_PTR(watcher->links.htab_map_free, "attach fexit"))
		goto out_watcher;

	map_kptr_race__destroy(skel);
	skel = NULL;

	kern_sync_rcu();

	while (!READ_ONCE(watcher->bss->map_freed))
		sched_yield();

	ASSERT_EQ(watcher->bss->map_freed, 1, "map_freed");
	ASSERT_EQ(read_refs(watcher), 2, "percpu_htab refcount");

out_watcher:
	map_kptr_race__destroy(watcher);
out_skel:
	map_kptr_race__destroy(skel);
}

static void test_sk_ls_leak(void)
{
	struct map_kptr_race *skel, *watcher;
	int listen_fd = -1, client_fd = -1, map_id;

	skel = map_kptr_race__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	if (!ASSERT_OK(map_kptr_race__attach(skel), "attach"))
		goto out_skel;

	listen_fd = start_server(AF_INET6, SOCK_STREAM, "::1", 0, 0);
	if (!ASSERT_GE(listen_fd, 0, "start_server"))
		goto out_skel;

	client_fd = connect_to_fd(listen_fd, 0);
	if (!ASSERT_GE(client_fd, 0, "connect_to_fd"))
		goto out_skel;

	if (!ASSERT_EQ(skel->bss->sk_ls_leak_done, 1, "sk_ls_leak_done"))
		goto out_skel;

	close(client_fd);
	client_fd = -1;
	close(listen_fd);
	listen_fd = -1;

	map_id = get_map_id(bpf_map__fd(skel->maps.race_sk_ls_map));
	if (!ASSERT_GE(map_id, 0, "map_id"))
		goto out_skel;

	watcher = map_kptr_race__open_and_load();
	if (!ASSERT_OK_PTR(watcher, "watcher open_and_load"))
		goto out_skel;

	watcher->bss->target_map_id = map_id;
	watcher->links.map_put = bpf_program__attach(watcher->progs.map_put);
	if (!ASSERT_OK_PTR(watcher->links.map_put, "attach fentry"))
		goto out_watcher;
	watcher->links.sk_map_free = bpf_program__attach(watcher->progs.sk_map_free);
	if (!ASSERT_OK_PTR(watcher->links.sk_map_free, "attach fexit"))
		goto out_watcher;

	map_kptr_race__destroy(skel);
	skel = NULL;

	kern_sync_rcu();

	while (!READ_ONCE(watcher->bss->map_freed))
		sched_yield();

	ASSERT_EQ(watcher->bss->map_freed, 1, "map_freed");
	ASSERT_EQ(read_refs(watcher), 2, "sk_ls refcount");

out_watcher:
	map_kptr_race__destroy(watcher);
out_skel:
	if (client_fd >= 0)
		close(client_fd);
	if (listen_fd >= 0)
		close(listen_fd);
	map_kptr_race__destroy(skel);
}

void serial_test_map_kptr_race(void)
{
	if (test__start_subtest("htab_leak"))
		test_htab_leak();
	if (test__start_subtest("percpu_htab_leak"))
		test_percpu_htab_leak();
	if (test__start_subtest("sk_ls_leak"))
		test_sk_ls_leak();
}
