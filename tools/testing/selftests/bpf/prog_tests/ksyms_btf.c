// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Google */

#include <test_progs.h>
#include <bpf/libbpf.h>
#include <bpf/btf.h>
#include "test_ksyms_btf.skel.h"

static int duration;

void test_ksyms_btf(void)
{
	__u64 runqueues_addr, bpf_prog_active_addr;
	__u32 this_rq_cpu;
	int this_bpf_prog_active;
	struct test_ksyms_btf *skel = NULL;
	struct test_ksyms_btf__data *data;
	struct btf *btf;
	int percpu_datasec;
	int err;

	err = kallsyms_find("runqueues", &runqueues_addr);
	if (CHECK(err == -EINVAL, "kallsyms_fopen", "failed to open: %d\n", errno))
		return;
	if (CHECK(err == -ENOENT, "ksym_find", "symbol 'runqueues' not found\n"))
		return;

	err = kallsyms_find("bpf_prog_active", &bpf_prog_active_addr);
	if (CHECK(err == -EINVAL, "kallsyms_fopen", "failed to open: %d\n", errno))
		return;
	if (CHECK(err == -ENOENT, "ksym_find", "symbol 'bpf_prog_active' not found\n"))
		return;

	btf = libbpf_find_kernel_btf();
	if (CHECK(IS_ERR(btf), "btf_exists", "failed to load kernel BTF: %ld\n",
		  PTR_ERR(btf)))
		return;

	percpu_datasec = btf__find_by_name_kind(btf, ".data..percpu",
						BTF_KIND_DATASEC);
	if (percpu_datasec < 0) {
		printf("%s:SKIP:no PERCPU DATASEC in kernel btf\n",
		       __func__);
		test__skip();
		goto cleanup;
	}

	skel = test_ksyms_btf__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open and load skeleton\n"))
		goto cleanup;

	err = test_ksyms_btf__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	data = skel->data;
	CHECK(data->out__runqueues_addr != runqueues_addr, "runqueues_addr",
	      "got %llu, exp %llu\n",
	      (unsigned long long)data->out__runqueues_addr,
	      (unsigned long long)runqueues_addr);
	CHECK(data->out__bpf_prog_active_addr != bpf_prog_active_addr, "bpf_prog_active_addr",
	      "got %llu, exp %llu\n",
	      (unsigned long long)data->out__bpf_prog_active_addr,
	      (unsigned long long)bpf_prog_active_addr);

	CHECK(data->out__rq_cpu == -1, "rq_cpu",
	      "got %u, exp != -1\n", data->out__rq_cpu);
	CHECK(data->out__bpf_prog_active < 0, "bpf_prog_active",
	      "got %d, exp >= 0\n", data->out__bpf_prog_active);
	CHECK(data->out__cpu_0_rq_cpu != 0, "cpu_rq(0)->cpu",
	      "got %u, exp 0\n", data->out__cpu_0_rq_cpu);

	this_rq_cpu = data->out__this_rq_cpu;
	CHECK(this_rq_cpu != data->out__rq_cpu, "this_rq_cpu",
	      "got %u, exp %u\n", this_rq_cpu, data->out__rq_cpu);

	this_bpf_prog_active = data->out__this_bpf_prog_active;
	CHECK(this_bpf_prog_active != data->out__bpf_prog_active, "this_bpf_prog_active",
	      "got %d, exp %d\n", this_bpf_prog_active,
	      data->out__bpf_prog_active);

cleanup:
	btf__free(btf);
	test_ksyms_btf__destroy(skel);
}
