// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <test_progs.h>

struct s {
	int a;
	long long b;
} __attribute__((packed));

#include "test_skeleton.skel.h"

void test_skeleton(void)
{
	int duration = 0, err;
	struct test_skeleton* skel;
	struct test_skeleton__bss *bss;
	struct test_skeleton__data *data;
	struct test_skeleton__data_dyn *data_dyn;
	struct test_skeleton__rodata *rodata;
	struct test_skeleton__rodata_dyn *rodata_dyn;
	struct test_skeleton__kconfig *kcfg;
	const void *elf_bytes;
	size_t elf_bytes_sz = 0;
	int i;

	skel = test_skeleton__open();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	if (CHECK(skel->kconfig, "skel_kconfig", "kconfig is mmaped()!\n"))
		goto cleanup;

	bss = skel->bss;
	data = skel->data;
	data_dyn = skel->data_dyn;
	rodata = skel->rodata;
	rodata_dyn = skel->rodata_dyn;

	ASSERT_STREQ(bpf_map__name(skel->maps.rodata_dyn), ".rodata.dyn", "rodata_dyn_name");
	ASSERT_STREQ(bpf_map__name(skel->maps.data_dyn), ".data.dyn", "data_dyn_name");

	/* validate values are pre-initialized correctly */
	CHECK(data->in1 != -1, "in1", "got %d != exp %d\n", data->in1, -1);
	CHECK(data->out1 != -1, "out1", "got %d != exp %d\n", data->out1, -1);
	CHECK(data->in2 != -1, "in2", "got %lld != exp %lld\n", data->in2, -1LL);
	CHECK(data->out2 != -1, "out2", "got %lld != exp %lld\n", data->out2, -1LL);

	CHECK(bss->in3 != 0, "in3", "got %d != exp %d\n", bss->in3, 0);
	CHECK(bss->out3 != 0, "out3", "got %d != exp %d\n", bss->out3, 0);
	CHECK(bss->in4 != 0, "in4", "got %lld != exp %lld\n", bss->in4, 0LL);
	CHECK(bss->out4 != 0, "out4", "got %lld != exp %lld\n", bss->out4, 0LL);

	CHECK(rodata->in.in6 != 0, "in6", "got %d != exp %d\n", rodata->in.in6, 0);
	CHECK(bss->out6 != 0, "out6", "got %d != exp %d\n", bss->out6, 0);

	ASSERT_EQ(rodata_dyn->in_dynarr_sz, 0, "in_dynarr_sz");
	for (i = 0; i < 4; i++)
		ASSERT_EQ(rodata_dyn->in_dynarr[i], -(i + 1), "in_dynarr");
	for (i = 0; i < 4; i++)
		ASSERT_EQ(data_dyn->out_dynarr[i], i + 1, "out_dynarr");

	/* validate we can pre-setup global variables, even in .bss */
	data->in1 = 10;
	data->in2 = 11;
	bss->in3 = 12;
	bss->in4 = 13;
	rodata->in.in6 = 14;

	rodata_dyn->in_dynarr_sz = 4;
	for (i = 0; i < 4; i++)
		rodata_dyn->in_dynarr[i] = i + 10;

	err = test_skeleton__load(skel);
	if (CHECK(err, "skel_load", "failed to load skeleton: %d\n", err))
		goto cleanup;

	/* validate pre-setup values are still there */
	CHECK(data->in1 != 10, "in1", "got %d != exp %d\n", data->in1, 10);
	CHECK(data->in2 != 11, "in2", "got %lld != exp %lld\n", data->in2, 11LL);
	CHECK(bss->in3 != 12, "in3", "got %d != exp %d\n", bss->in3, 12);
	CHECK(bss->in4 != 13, "in4", "got %lld != exp %lld\n", bss->in4, 13LL);
	CHECK(rodata->in.in6 != 14, "in6", "got %d != exp %d\n", rodata->in.in6, 14);

	ASSERT_EQ(rodata_dyn->in_dynarr_sz, 4, "in_dynarr_sz");
	for (i = 0; i < 4; i++)
		ASSERT_EQ(rodata_dyn->in_dynarr[i], i + 10, "in_dynarr");

	/* now set new values and attach to get them into outX variables */
	data->in1 = 1;
	data->in2 = 2;
	bss->in3 = 3;
	bss->in4 = 4;
	bss->in5.a = 5;
	bss->in5.b = 6;
	kcfg = skel->kconfig;

	skel->data_read_mostly->read_mostly_var = 123;

	err = test_skeleton__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	CHECK(data->out1 != 1, "res1", "got %d != exp %d\n", data->out1, 1);
	CHECK(data->out2 != 2, "res2", "got %lld != exp %d\n", data->out2, 2);
	CHECK(bss->out3 != 3, "res3", "got %d != exp %d\n", (int)bss->out3, 3);
	CHECK(bss->out4 != 4, "res4", "got %lld != exp %d\n", bss->out4, 4);
	CHECK(bss->out5.a != 5, "res5", "got %d != exp %d\n", bss->out5.a, 5);
	CHECK(bss->out5.b != 6, "res6", "got %lld != exp %d\n", bss->out5.b, 6);
	CHECK(bss->out6 != 14, "res7", "got %d != exp %d\n", bss->out6, 14);

	CHECK(bss->bpf_syscall != kcfg->CONFIG_BPF_SYSCALL, "ext1",
	      "got %d != exp %d\n", bss->bpf_syscall, kcfg->CONFIG_BPF_SYSCALL);
	CHECK(bss->kern_ver != kcfg->LINUX_KERNEL_VERSION, "ext2",
	      "got %d != exp %d\n", bss->kern_ver, kcfg->LINUX_KERNEL_VERSION);

	for (i = 0; i < 4; i++)
		ASSERT_EQ(data_dyn->out_dynarr[i], i + 10, "out_dynarr");

	ASSERT_EQ(skel->bss->out_mostly_var, 123, "out_mostly_var");

	elf_bytes = test_skeleton__elf_bytes(&elf_bytes_sz);
	ASSERT_OK_PTR(elf_bytes, "elf_bytes");
	ASSERT_GE(elf_bytes_sz, 0, "elf_bytes_sz");

cleanup:
	test_skeleton__destroy(skel);
}
