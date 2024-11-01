// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "test_subskeleton.skel.h"
#include "test_subskeleton_lib.subskel.h"

static void subskeleton_lib_setup(struct bpf_object *obj)
{
	struct test_subskeleton_lib *lib = test_subskeleton_lib__open(obj);

	if (!ASSERT_OK_PTR(lib, "open subskeleton"))
		return;

	*lib->rodata.var1 = 1;
	*lib->data.var2 = 2;
	lib->bss.var3->var3_1 = 3;
	lib->bss.var3->var3_2 = 4;

	test_subskeleton_lib__destroy(lib);
}

static int subskeleton_lib_subresult(struct bpf_object *obj)
{
	struct test_subskeleton_lib *lib = test_subskeleton_lib__open(obj);
	int result;

	if (!ASSERT_OK_PTR(lib, "open subskeleton"))
		return -EINVAL;

	result = *lib->bss.libout1;
	ASSERT_EQ(result, 1 + 2 + 3 + 4 + 5 + 6, "lib subresult");

	ASSERT_OK_PTR(lib->progs.lib_perf_handler, "lib_perf_handler");
	ASSERT_STREQ(bpf_program__name(lib->progs.lib_perf_handler),
		     "lib_perf_handler", "program name");

	ASSERT_OK_PTR(lib->maps.map1, "map1");
	ASSERT_STREQ(bpf_map__name(lib->maps.map1), "map1", "map name");

	ASSERT_EQ(*lib->data.var5, 5, "__weak var5");
	ASSERT_EQ(*lib->data.var6, 6, "extern var6");
	ASSERT_TRUE(*lib->kconfig.CONFIG_BPF_SYSCALL, "CONFIG_BPF_SYSCALL");

	test_subskeleton_lib__destroy(lib);
	return result;
}

void test_subskeleton(void)
{
	int err, result;
	struct test_subskeleton *skel;

	skel = test_subskeleton__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->rodata->rovar1 = 10;
	skel->rodata->var1 = 1;
	subskeleton_lib_setup(skel->obj);

	err = test_subskeleton__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	err = test_subskeleton__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	result = subskeleton_lib_subresult(skel->obj) * 10;
	ASSERT_EQ(skel->bss->out1, result, "unexpected calculation");

cleanup:
	test_subskeleton__destroy(skel);
}
