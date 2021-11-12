// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "btf_decl_tag.skel.h"

/* struct btf_type_tag_test is referenced in btf_type_tag.skel.h */
struct btf_type_tag_test {
        int **p;
};
#include "btf_type_tag.skel.h"

static void test_btf_decl_tag(void)
{
	struct btf_decl_tag *skel;

	skel = btf_decl_tag__open_and_load();
	if (!ASSERT_OK_PTR(skel, "btf_decl_tag"))
		return;

	if (skel->rodata->skip_tests) {
		printf("%s:SKIP: btf_decl_tag attribute not supported", __func__);
		test__skip();
	}

	btf_decl_tag__destroy(skel);
}

static void test_btf_type_tag(void)
{
	struct btf_type_tag *skel;

	skel = btf_type_tag__open_and_load();
	if (!ASSERT_OK_PTR(skel, "btf_type_tag"))
		return;

	if (skel->rodata->skip_tests) {
		printf("%s:SKIP: btf_type_tag attribute not supported", __func__);
		test__skip();
	}

	btf_type_tag__destroy(skel);
}

void test_btf_tag(void)
{
	if (test__start_subtest("btf_decl_tag"))
		test_btf_decl_tag();
	if (test__start_subtest("btf_type_tag"))
		test_btf_type_tag();
}
