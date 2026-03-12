// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Benjamin Tissoires */
#include <test_progs.h>
#include "wq.skel.h"
#include "wq_failures.skel.h"

void serial_test_wq(void)
{
	struct wq *wq_skel = NULL;
	int err, prog_fd;

	LIBBPF_OPTS(bpf_test_run_opts, topts);

	RUN_TESTS(wq);

	/* re-run the success test to check if the timer was actually executed */

	wq_skel = wq__open_and_load();
	if (!ASSERT_OK_PTR(wq_skel, "wq__open_and_load"))
		return;

	err = wq__attach(wq_skel);
	if (!ASSERT_OK(err, "wq_attach"))
		goto clean_up;

	prog_fd = bpf_program__fd(wq_skel->progs.test_syscall_array_sleepable);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	usleep(50); /* 10 usecs should be enough, but give it extra */

	ASSERT_EQ(wq_skel->bss->ok_sleepable, (1 << 1), "ok_sleepable");
clean_up:
	wq__destroy(wq_skel);
}

void serial_test_failures_wq(void)
{
	RUN_TESTS(wq_failures);
}

static void test_failure_map_no_btf(void)
{
	struct wq *skel = NULL;
	char log[8192];
	const struct bpf_insn *insns;
	size_t insn_cnt;
	int ret, err, map_fd;
	LIBBPF_OPTS(bpf_prog_load_opts, opts, .log_size = sizeof(log), .log_buf = log,
		    .log_level = 2);

	skel = wq__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	err = bpf_object__prepare(skel->obj);
	if (!ASSERT_OK(err, "skel__prepare"))
		goto out;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "map_no_btf", sizeof(__u32), sizeof(__u64), 100,
				NULL);
	if (!ASSERT_GT(map_fd, -1, "map create"))
		goto out;

	err = bpf_map__reuse_fd(skel->maps.array, map_fd);
	if (!ASSERT_OK(err, "map reuse fd")) {
		close(map_fd);
		goto out;
	}

	insns = bpf_program__insns(skel->progs.test_map_no_btf);
	if (!ASSERT_OK_PTR(insns, "insns ptr"))
		goto out;

	insn_cnt = bpf_program__insn_cnt(skel->progs.test_map_no_btf);
	if (!ASSERT_GT(insn_cnt, 0u, "insn cnt"))
		goto out;

	ret = bpf_prog_load(BPF_PROG_TYPE_TRACEPOINT, NULL, "GPL", insns, insn_cnt, &opts);
	if (!ASSERT_LT(ret, 0, "prog load failed")) {
		if (ret > 0)
			close(ret);
		goto out;
	}

	ASSERT_HAS_SUBSTR(log, "map 'map_no_btf' has to have BTF in order to use bpf_wq",
			  "log complains no map BTF");
out:
	wq__destroy(skel);
}

void test_wq_custom(void)
{
	if (test__start_subtest("test_failure_map_no_btf"))
		test_failure_map_no_btf();
}
