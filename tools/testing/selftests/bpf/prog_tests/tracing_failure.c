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

static void test_tracing_fail_prog(const char *prog_name, const char *exp_msg)
{
	struct tracing_failure *skel;
	struct bpf_program *prog;
	char log_buf[256];
	int err;

	skel = tracing_failure__open();
	if (!ASSERT_OK_PTR(skel, "tracing_failure__open"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto out;

	bpf_program__set_autoload(prog, true);
	bpf_program__set_log_buf(prog, log_buf, sizeof(log_buf));

	err = tracing_failure__load(skel);
	if (!ASSERT_ERR(err, "tracing_failure__load"))
		goto out;

	ASSERT_HAS_SUBSTR(log_buf, exp_msg, "log_buf");
out:
	tracing_failure__destroy(skel);
}

static void test_tracing_deny(void)
{
	int btf_id;

	/* __rcu_read_lock depends on CONFIG_PREEMPT_RCU */
	btf_id = libbpf_find_vmlinux_btf_id("__rcu_read_lock", BPF_TRACE_FENTRY);
	if (btf_id <= 0) {
		test__skip();
		return;
	}

	test_tracing_fail_prog("tracing_deny",
			       "Attaching tracing programs to function '__rcu_read_lock' is rejected.");
}

static void test_fexit_noreturns(void)
{
	test_tracing_fail_prog("fexit_noreturns",
			       "Attaching fexit/fmod_ret to __noreturn function 'do_exit' is rejected.");
}

void test_tracing_failure(void)
{
	if (test__start_subtest("bpf_spin_lock"))
		test_bpf_spin_lock(true);
	if (test__start_subtest("bpf_spin_unlock"))
		test_bpf_spin_lock(false);
	if (test__start_subtest("tracing_deny"))
		test_tracing_deny();
	if (test__start_subtest("fexit_noreturns"))
		test_fexit_noreturns();
}
