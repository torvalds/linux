// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "tracing_failure.skel.h"

static void test_bpf_spin_lock(bool is_spin_lock)
{
	struct tracing_failure *skel;
	int err;

	skel = tracing_failure__open();
	if (!ASSERT_OK_PTR(skel, "tracing_failure__open"))
		return;

	if (is_spin_lock)
		bpf_program__set_autoload(skel->progs.test_spin_lock, true);
	else
		bpf_program__set_autoload(skel->progs.test_spin_unlock, true);

	err = tracing_failure__load(skel);
	if (!ASSERT_OK(err, "tracing_failure__load"))
		goto out;

	err = tracing_failure__attach(skel);
	ASSERT_ERR(err, "tracing_failure__attach");

out:
	tracing_failure__destroy(skel);
}

void test_tracing_failure(void)
{
	if (test__start_subtest("bpf_spin_lock"))
		test_bpf_spin_lock(true);
	if (test__start_subtest("bpf_spin_unlock"))
		test_bpf_spin_lock(false);
}
