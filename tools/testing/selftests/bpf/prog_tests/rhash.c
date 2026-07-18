// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <string.h>
#include <stdio.h>
#include "rhash.skel.h"
#include "bpf_iter_bpf_rhash_map.skel.h"
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

static void rhash_run(const char *prog_name)
{
	struct rhash *skel;
	struct bpf_program *prog;
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int err;

	skel = rhash__open();
	if (!ASSERT_OK_PTR(skel, "rhash__open"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;
	bpf_program__set_autoload(prog, true);

	err = rhash__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);
	if (!ASSERT_OK(err, "prog run"))
		goto cleanup;

	if (!ASSERT_OK(opts.retval, "prog retval"))
		goto cleanup;

	if (!ASSERT_OK(skel->bss->err, "bss->err"))
		goto cleanup;

cleanup:
	rhash__destroy(skel);
}

static int rhash_map_create(__u32 max_entries, __u64 map_extra)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .map_flags = BPF_F_NO_PREALLOC,
		    .map_extra = map_extra);

	return bpf_map_create(BPF_MAP_TYPE_RHASH, "rhash_extra",
			      sizeof(__u32), sizeof(__u64), max_entries, &opts);
}

static void rhash_map_extra_presize(void)
{
	const __u32 max_entries = 1024;
	const __u32 nelem_hint = 256;
	struct bpf_map_info info = {};
	__u32 info_len = sizeof(info);
	__u64 val = 0;
	__u32 key;
	int fd, i;

	fd = rhash_map_create(max_entries, nelem_hint);
	if (!ASSERT_GE(fd, 0, "rhash_map_create presize"))
		return;

	if (!ASSERT_OK(bpf_map_get_info_by_fd(fd, &info, &info_len), "info"))
		goto close;
	ASSERT_EQ(info.map_extra, nelem_hint, "info.map_extra");

	for (i = 0; i < (int)nelem_hint; i++) {
		key = i;
		if (!ASSERT_OK(bpf_map_update_elem(fd, &key, &val, BPF_NOEXIST),
			       "update"))
			goto close;
	}
close:
	close(fd);
}

static void rhash_map_extra_too_big(void)
{
	int fd;

	fd = rhash_map_create(1U << 20, 0x10000);
	if (!ASSERT_LT(fd, 0, "rhash_map_create hint > U16_MAX"))
		close(fd);
}

static void rhash_iter_test(void)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_bpf_rhash_map *skel;
	int err, i, len, map_fd, iter_fd;
	union bpf_iter_link_info linfo;
	u32 expected_key_sum = 0, key;
	struct bpf_link *link;
	u64 val = 0;
	char buf[64];

	skel = bpf_iter_bpf_rhash_map__open();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_rhash_map__open"))
		return;

	err = bpf_iter_bpf_rhash_map__load(skel);
	if (!ASSERT_OK(err, "bpf_iter_bpf_rhash_map__load"))
		goto out;

	map_fd = bpf_map__fd(skel->maps.rhashmap);

	/* Populate map with test data */
	for (i = 0; i < 64; i++) {
		key = i + 1;
		expected_key_sum += key;

		err = bpf_map_update_elem(map_fd, &key, &val, BPF_NOEXIST);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = map_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	link = bpf_program__attach_iter(skel->progs.dump_bpf_rhash_map, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	do {
		len = read(iter_fd, buf, sizeof(buf));
	} while (len > 0);

	ASSERT_EQ(skel->bss->key_sum, expected_key_sum, "key_sum");
	ASSERT_EQ(skel->bss->elem_count, 64, "elem_count");

	close(iter_fd);

free_link:
	bpf_link__destroy(link);
out:
	bpf_iter_bpf_rhash_map__destroy(skel);
}

void test_rhash(void)
{
	if (test__start_subtest("test_rhash_lookup_update"))
		rhash_run("test_rhash_lookup_update");

	if (test__start_subtest("test_rhash_update_delete"))
		rhash_run("test_rhash_update_delete");

	if (test__start_subtest("test_rhash_update_elements"))
		rhash_run("test_rhash_update_elements");

	if (test__start_subtest("test_rhash_update_exist"))
		rhash_run("test_rhash_update_exist");

	if (test__start_subtest("test_rhash_update_any"))
		rhash_run("test_rhash_update_any");

	if (test__start_subtest("test_rhash_noexist_duplicate"))
		rhash_run("test_rhash_noexist_duplicate");

	if (test__start_subtest("test_rhash_delete_nonexistent"))
		rhash_run("test_rhash_delete_nonexistent");

	if (test__start_subtest("test_rhash_map_extra_presize"))
		rhash_map_extra_presize();

	if (test__start_subtest("test_rhash_map_extra_too_big"))
		rhash_map_extra_too_big();

	if (test__start_subtest("test_rhash_iter"))
		rhash_iter_test();
}
