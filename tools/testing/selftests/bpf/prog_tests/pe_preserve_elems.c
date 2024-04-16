// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include <linux/bpf.h>
#include "test_pe_preserve_elems.skel.h"

static int duration;

static void test_one_map(struct bpf_map *map, struct bpf_program *prog,
			 bool has_share_pe)
{
	int err, key = 0, pfd = -1, mfd = bpf_map__fd(map);
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct perf_event_attr attr = {
		.size = sizeof(struct perf_event_attr),
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_CPU_CLOCK,
	};

	pfd = syscall(__NR_perf_event_open, &attr, 0 /* pid */,
		      -1 /* cpu 0 */, -1 /* group id */, 0 /* flags */);
	if (CHECK(pfd < 0, "perf_event_open", "failed\n"))
		return;

	err = bpf_map_update_elem(mfd, &key, &pfd, BPF_ANY);
	close(pfd);
	if (CHECK(err < 0, "bpf_map_update_elem", "failed\n"))
		return;

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);
	if (CHECK(err < 0, "bpf_prog_test_run_opts", "failed\n"))
		return;
	if (CHECK(opts.retval != 0, "bpf_perf_event_read_value",
		  "failed with %d\n", opts.retval))
		return;

	/* closing mfd, prog still holds a reference on map */
	close(mfd);

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);
	if (CHECK(err < 0, "bpf_prog_test_run_opts", "failed\n"))
		return;

	if (has_share_pe) {
		CHECK(opts.retval != 0, "bpf_perf_event_read_value",
		      "failed with %d\n", opts.retval);
	} else {
		CHECK(opts.retval != -ENOENT, "bpf_perf_event_read_value",
		      "should have failed with %d, but got %d\n", -ENOENT,
		      opts.retval);
	}
}

void test_pe_preserve_elems(void)
{
	struct test_pe_preserve_elems *skel;

	skel = test_pe_preserve_elems__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	test_one_map(skel->maps.array_1, skel->progs.read_array_1, false);
	test_one_map(skel->maps.array_2, skel->progs.read_array_2, true);

	test_pe_preserve_elems__destroy(skel);
}
