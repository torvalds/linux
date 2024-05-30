// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <test_progs.h>
#include "test_ksyms.skel.h"
#include <sys/stat.h>

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
	if (!ASSERT_NEQ(err, -EINVAL, "bpf_link_fops: kallsyms_fopen"))
		return;
	if (!ASSERT_NEQ(err, -ENOENT, "bpf_link_fops: ksym_find"))
		return;

	err = kallsyms_find("__per_cpu_start", &per_cpu_start_addr);
	if (!ASSERT_NEQ(err, -EINVAL, "__per_cpu_start: kallsyms_fopen"))
		return;
	if (!ASSERT_NEQ(err, -ENOENT, "__per_cpu_start: ksym_find"))
		return;

	if (!ASSERT_OK(stat(btf_path, &st), "stat_btf"))
		return;
	btf_size = st.st_size;

	skel = test_ksyms__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_ksyms__open_and_load"))
		return;

	err = test_ksyms__attach(skel);
	if (!ASSERT_OK(err, "test_ksyms__attach"))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	data = skel->data;
	ASSERT_EQ(data->out__bpf_link_fops, link_fops_addr, "bpf_link_fops");
	ASSERT_EQ(data->out__bpf_link_fops1, 0, "bpf_link_fops1");
	ASSERT_EQ(data->out__btf_size, btf_size, "btf_size");
	ASSERT_EQ(data->out__per_cpu_start, per_cpu_start_addr, "__per_cpu_start");

cleanup:
	test_ksyms__destroy(skel);
}
