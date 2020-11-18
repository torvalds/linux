// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <test_progs.h>
#include "test_ksyms.skel.h"
#include <sys/stat.h>

static int duration;

static __u64 kallsyms_find(const char *sym)
{
	char type, name[500];
	__u64 addr, res = 0;
	FILE *f;

	f = fopen("/proc/kallsyms", "r");
	if (CHECK(!f, "kallsyms_fopen", "failed to open: %d\n", errno))
		return 0;

	while (fscanf(f, "%llx %c %499s%*[^\n]\n", &addr, &type, name) > 0) {
		if (strcmp(name, sym) == 0) {
			res = addr;
			goto out;
		}
	}

	CHECK(false, "not_found", "symbol %s not found\n", sym);
out:
	fclose(f);
	return res;
}

void test_ksyms(void)
{
	__u64 link_fops_addr = kallsyms_find("bpf_link_fops");
	const char *btf_path = "/sys/kernel/btf/vmlinux";
	struct test_ksyms *skel;
	struct test_ksyms__data *data;
	struct stat st;
	__u64 btf_size;
	int err;

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
	CHECK(data->out__per_cpu_start != 0, "__per_cpu_start",
	      "got %llu, exp %llu\n", data->out__per_cpu_start, (__u64)0);

cleanup:
	test_ksyms__destroy(skel);
}
