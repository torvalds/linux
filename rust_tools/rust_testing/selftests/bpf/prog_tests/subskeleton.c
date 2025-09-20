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

/* initialize and load through skeleton, then instantiate subskeleton out of it */
static void subtest_skel_subskeleton(void)
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

/* initialize and load through generic bpf_object API, then instantiate subskeleton out of it */
static void subtest_obj_subskeleton(void)
{
	int err, result;
	const void *elf_bytes;
	size_t elf_bytes_sz = 0, rodata_sz = 0, bss_sz = 0;
	struct bpf_object *obj;
	const struct bpf_map *map;
	const struct bpf_program *prog;
	struct bpf_link *link = NULL;
	struct test_subskeleton__rodata *rodata;
	struct test_subskeleton__bss *bss;

	elf_bytes = test_subskeleton__elf_bytes(&elf_bytes_sz);
	if (!ASSERT_OK_PTR(elf_bytes, "elf_bytes"))
		return;

	obj = bpf_object__open_mem(elf_bytes, elf_bytes_sz, NULL);
	if (!ASSERT_OK_PTR(obj, "obj_open_mem"))
		return;

	map = bpf_object__find_map_by_name(obj, ".rodata");
	if (!ASSERT_OK_PTR(map, "rodata_map_by_name"))
		goto cleanup;

	rodata = bpf_map__initial_value(map, &rodata_sz);
	if (!ASSERT_OK_PTR(rodata, "rodata_get"))
		goto cleanup;

	rodata->rovar1 = 10;
	rodata->var1 = 1;
	subskeleton_lib_setup(obj);

	err = bpf_object__load(obj);
	if (!ASSERT_OK(err, "obj_load"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(obj, "handler1");
	if (!ASSERT_OK_PTR(prog, "prog_by_name"))
		goto cleanup;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "prog_attach"))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	map = bpf_object__find_map_by_name(obj, ".bss");
	if (!ASSERT_OK_PTR(map, "bss_map_by_name"))
		goto cleanup;

	bss = bpf_map__initial_value(map, &bss_sz);
	if (!ASSERT_OK_PTR(rodata, "rodata_get"))
		goto cleanup;

	result = subskeleton_lib_subresult(obj) * 10;
	ASSERT_EQ(bss->out1, result, "out1");

cleanup:
	bpf_link__destroy(link);
	bpf_object__close(obj);
}


void test_subskeleton(void)
{
	if (test__start_subtest("skel_subskel"))
		subtest_skel_subskeleton();
	if (test__start_subtest("obj_subskel"))
		subtest_obj_subskeleton();
}
