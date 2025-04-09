// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta */

#include <test_progs.h>
#include <network_helpers.h>
#include "prepare.skel.h"

static bool check_prepared(struct bpf_object *obj)
{
	bool is_prepared = true;
	const struct bpf_map *map;

	bpf_object__for_each_map(map, obj) {
		if (bpf_map__fd(map) < 0)
			is_prepared = false;
	}

	return is_prepared;
}

static void test_prepare_no_load(void)
{
	struct prepare *skel;
	int err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
	);

	skel = prepare__open();
	if (!ASSERT_OK_PTR(skel, "prepare__open"))
		return;

	if (!ASSERT_FALSE(check_prepared(skel->obj), "not check_prepared"))
		goto cleanup;

	err = bpf_object__prepare(skel->obj);

	if (!ASSERT_TRUE(check_prepared(skel->obj), "check_prepared"))
		goto cleanup;

	if (!ASSERT_OK(err, "bpf_object__prepare"))
		goto cleanup;

cleanup:
	prepare__destroy(skel);
}

static void test_prepare_load(void)
{
	struct prepare *skel;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
	);

	skel = prepare__open();
	if (!ASSERT_OK_PTR(skel, "prepare__open"))
		return;

	if (!ASSERT_FALSE(check_prepared(skel->obj), "not check_prepared"))
		goto cleanup;

	err = bpf_object__prepare(skel->obj);
	if (!ASSERT_OK(err, "bpf_object__prepare"))
		goto cleanup;

	err = prepare__load(skel);
	if (!ASSERT_OK(err, "prepare__load"))
		goto cleanup;

	if (!ASSERT_TRUE(check_prepared(skel->obj), "check_prepared"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.program);
	if (!ASSERT_GE(prog_fd, 0, "prog_fd"))
		goto cleanup;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		goto cleanup;

	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		goto cleanup;

	ASSERT_EQ(skel->bss->err, 0, "err");

cleanup:
	prepare__destroy(skel);
}

void test_prepare(void)
{
	if (test__start_subtest("prepare_load"))
		test_prepare_load();
	if (test__start_subtest("prepare_no_load"))
		test_prepare_no_load();
}
