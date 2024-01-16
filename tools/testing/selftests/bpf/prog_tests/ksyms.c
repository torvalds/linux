// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <test_progs.h>
#include "test_ksyms.skel.h"
#include <sys/stat.h>

static int duration;

void test_ksyms(void)
{
	const char *btf_path = "/sys/kernel/btf/vmlinux";
	struct test_ksyms *skel;
	struct test_ksyms__data *data;
	__u64 link_fops_addr, per_cpu_start_addr;
	struct stat st;
	__u64 btf_size;
	int err;

	err = kallsyms_find("bpf_link_fops", &link_fops_addr);
	if (CHECK(err == -EINVAL, "kallsyms_fopen", "failed to open: %d\n", errno))
		return;
	if (CHECK(err == -ENOENT, "ksym_find", "symbol 'bpf_link_fops' not found\n"))
		return;

	err = kallsyms_find("__per_cpu_start", &per_cpu_start_addr);
	if (CHECK(err == -EINVAL, "kallsyms_fopen", "failed to open: %d\n", errno))
		return;
	if (CHECK(err == -ENOENT, "ksym_find", "symbol 'per_cpu_start' not found\n"))
		return;

	if (CHECK(stat(btf_path, &st), "stat_btf", "err %d\n", errno))
		return;
	btf_size = st.st_size;

	skel = test_ksyms__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open and load skeleton\n"))
		return;

	err = test_ksyms__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	data = skel->data;
	CHECK(data->out__bpf_link_fops != link_fops_addr, "bpf_link_fops",
	      "got 0x%llx, exp 0x%llx\n",
	      data->out__bpf_link_fops, link_fops_addr);
	CHECK(data->out__bpf_link_fops1 != 0, "bpf_link_fops1",
	      "got %llu, exp %llu\n", data->out__bpf_link_fops1, (__u64)0);
	CHECK(data->out__btf_size != btf_size, "btf_size",
	      "got %llu, exp %llu\n", data->out__btf_size, btf_size);
	CHECK(data->out__per_cpu_start != per_cpu_start_addr, "__per_cpu_start",
	      "got %llu, exp %llu\n", data->out__per_cpu_start,
	      per_cpu_start_addr);

cleanup:
	test_ksyms__destroy(skel);
}
