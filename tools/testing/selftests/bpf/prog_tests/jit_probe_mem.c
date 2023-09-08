// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <network_helpers.h>

#include "jit_probe_mem.skel.h"

void test_jit_probe_mem(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);
	struct jit_probe_mem *skel;
	int ret;

	skel = jit_probe_mem__open_and_load();
	if (!ASSERT_OK_PTR(skel, "jit_probe_mem__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_jit_probe_mem), &opts);
	ASSERT_OK(ret, "jit_probe_mem ret");
	ASSERT_OK(opts.retval, "jit_probe_mem opts.retval");
	ASSERT_EQ(skel->data->total_sum, 192, "jit_probe_mem total_sum");

	jit_probe_mem__destroy(skel);
}
