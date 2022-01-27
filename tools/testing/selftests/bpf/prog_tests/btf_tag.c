// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include <bpf/btf.h>
#include "test_btf_decl_tag.skel.h"

/* struct btf_type_tag_test is referenced in btf_type_tag.skel.h */
struct btf_type_tag_test {
        int **p;
};
#include "btf_type_tag.skel.h"
#include "btf_type_tag_user.skel.h"

static void test_btf_decl_tag(void)
{
	struct test_btf_decl_tag *skel;

	skel = test_btf_decl_tag__open_and_load();
	if (!ASSERT_OK_PTR(skel, "btf_decl_tag"))
		return;

	if (skel->rodata->skip_tests) {
		printf("%s:SKIP: btf_decl_tag attribute not supported", __func__);
		test__skip();
	}

	test_btf_decl_tag__destroy(skel);
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

static void test_btf_type_tag_mod_user(bool load_test_user1)
{
	const char *module_name = "bpf_testmod";
	struct btf *vmlinux_btf, *module_btf;
	struct btf_type_tag_user *skel;
	__s32 type_id;
	int err;

	if (!env.has_testmod) {
		test__skip();
		return;
	}

	/* skip the test if the module does not have __user tags */
	vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(vmlinux_btf, "could not load vmlinux BTF"))
		return;

	module_btf = btf__load_module_btf(module_name, vmlinux_btf);
	if (!ASSERT_OK_PTR(module_btf, "could not load module BTF"))
		goto free_vmlinux_btf;

	type_id = btf__find_by_name_kind(module_btf, "user", BTF_KIND_TYPE_TAG);
	if (type_id <= 0) {
		printf("%s:SKIP: btf_type_tag attribute not in %s", __func__, module_name);
		test__skip();
		goto free_module_btf;
	}

	skel = btf_type_tag_user__open();
	if (!ASSERT_OK_PTR(skel, "btf_type_tag_user"))
		goto free_module_btf;

	bpf_program__set_autoload(skel->progs.test_sys_getsockname, false);
	if (load_test_user1)
		bpf_program__set_autoload(skel->progs.test_user2, false);
	else
		bpf_program__set_autoload(skel->progs.test_user1, false);

	err = btf_type_tag_user__load(skel);
	ASSERT_ERR(err, "btf_type_tag_user");

	btf_type_tag_user__destroy(skel);

free_module_btf:
	btf__free(module_btf);
free_vmlinux_btf:
	btf__free(vmlinux_btf);
}

static void test_btf_type_tag_vmlinux_user(void)
{
	struct btf_type_tag_user *skel;
	struct btf *vmlinux_btf;
	__s32 type_id;
	int err;

	/* skip the test if the vmlinux does not have __user tags */
	vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(vmlinux_btf, "could not load vmlinux BTF"))
		return;

	type_id = btf__find_by_name_kind(vmlinux_btf, "user", BTF_KIND_TYPE_TAG);
	if (type_id <= 0) {
		printf("%s:SKIP: btf_type_tag attribute not in vmlinux btf", __func__);
		test__skip();
		goto free_vmlinux_btf;
	}

	skel = btf_type_tag_user__open();
	if (!ASSERT_OK_PTR(skel, "btf_type_tag_user"))
		goto free_vmlinux_btf;

	bpf_program__set_autoload(skel->progs.test_user2, false);
	bpf_program__set_autoload(skel->progs.test_user1, false);

	err = btf_type_tag_user__load(skel);
	ASSERT_ERR(err, "btf_type_tag_user");

	btf_type_tag_user__destroy(skel);

free_vmlinux_btf:
	btf__free(vmlinux_btf);
}

void test_btf_tag(void)
{
	if (test__start_subtest("btf_decl_tag"))
		test_btf_decl_tag();
	if (test__start_subtest("btf_type_tag"))
		test_btf_type_tag();
	if (test__start_subtest("btf_type_tag_user_mod1"))
		test_btf_type_tag_mod_user(true);
	if (test__start_subtest("btf_type_tag_user_mod2"))
		test_btf_type_tag_mod_user(false);
	if (test__start_subtest("btf_type_tag_sys_user_vmlinux"))
		test_btf_type_tag_vmlinux_user();
}
