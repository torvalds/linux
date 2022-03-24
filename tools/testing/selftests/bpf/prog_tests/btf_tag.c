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
#include "btf_type_tag_percpu.skel.h"

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

/* loads vmlinux_btf as well as module_btf. If the caller passes NULL as
 * module_btf, it will not load module btf.
 *
 * Returns 0 on success.
 * Return -1 On error. In case of error, the loaded btf will be freed and the
 * input parameters will be set to pointing to NULL.
 */
static int load_btfs(struct btf **vmlinux_btf, struct btf **module_btf,
		     bool needs_vmlinux_tag)
{
	const char *module_name = "bpf_testmod";
	__s32 type_id;

	if (!env.has_testmod) {
		test__skip();
		return -1;
	}

	*vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(*vmlinux_btf, "could not load vmlinux BTF"))
		return -1;

	if (!needs_vmlinux_tag)
		goto load_module_btf;

	/* skip the test if the vmlinux does not have __user tags */
	type_id = btf__find_by_name_kind(*vmlinux_btf, "user", BTF_KIND_TYPE_TAG);
	if (type_id <= 0) {
		printf("%s:SKIP: btf_type_tag attribute not in vmlinux btf", __func__);
		test__skip();
		goto free_vmlinux_btf;
	}

load_module_btf:
	/* skip loading module_btf, if not requested by caller */
	if (!module_btf)
		return 0;

	*module_btf = btf__load_module_btf(module_name, *vmlinux_btf);
	if (!ASSERT_OK_PTR(*module_btf, "could not load module BTF"))
		goto free_vmlinux_btf;

	/* skip the test if the module does not have __user tags */
	type_id = btf__find_by_name_kind(*module_btf, "user", BTF_KIND_TYPE_TAG);
	if (type_id <= 0) {
		printf("%s:SKIP: btf_type_tag attribute not in %s", __func__, module_name);
		test__skip();
		goto free_module_btf;
	}

	return 0;

free_module_btf:
	btf__free(*module_btf);
free_vmlinux_btf:
	btf__free(*vmlinux_btf);

	*vmlinux_btf = NULL;
	if (module_btf)
		*module_btf = NULL;
	return -1;
}

static void test_btf_type_tag_mod_user(bool load_test_user1)
{
	struct btf *vmlinux_btf = NULL, *module_btf = NULL;
	struct btf_type_tag_user *skel;
	int err;

	if (load_btfs(&vmlinux_btf, &module_btf, /*needs_vmlinux_tag=*/false))
		return;

	skel = btf_type_tag_user__open();
	if (!ASSERT_OK_PTR(skel, "btf_type_tag_user"))
		goto cleanup;

	bpf_program__set_autoload(skel->progs.test_sys_getsockname, false);
	if (load_test_user1)
		bpf_program__set_autoload(skel->progs.test_user2, false);
	else
		bpf_program__set_autoload(skel->progs.test_user1, false);

	err = btf_type_tag_user__load(skel);
	ASSERT_ERR(err, "btf_type_tag_user");

	btf_type_tag_user__destroy(skel);

cleanup:
	btf__free(module_btf);
	btf__free(vmlinux_btf);
}

static void test_btf_type_tag_vmlinux_user(void)
{
	struct btf_type_tag_user *skel;
	struct btf *vmlinux_btf = NULL;
	int err;

	if (load_btfs(&vmlinux_btf, NULL, /*needs_vmlinux_tag=*/true))
		return;

	skel = btf_type_tag_user__open();
	if (!ASSERT_OK_PTR(skel, "btf_type_tag_user"))
		goto cleanup;

	bpf_program__set_autoload(skel->progs.test_user2, false);
	bpf_program__set_autoload(skel->progs.test_user1, false);

	err = btf_type_tag_user__load(skel);
	ASSERT_ERR(err, "btf_type_tag_user");

	btf_type_tag_user__destroy(skel);

cleanup:
	btf__free(vmlinux_btf);
}

static void test_btf_type_tag_mod_percpu(bool load_test_percpu1)
{
	struct btf *vmlinux_btf, *module_btf;
	struct btf_type_tag_percpu *skel;
	int err;

	if (load_btfs(&vmlinux_btf, &module_btf, /*needs_vmlinux_tag=*/false))
		return;

	skel = btf_type_tag_percpu__open();
	if (!ASSERT_OK_PTR(skel, "btf_type_tag_percpu"))
		goto cleanup;

	bpf_program__set_autoload(skel->progs.test_percpu_load, false);
	bpf_program__set_autoload(skel->progs.test_percpu_helper, false);
	if (load_test_percpu1)
		bpf_program__set_autoload(skel->progs.test_percpu2, false);
	else
		bpf_program__set_autoload(skel->progs.test_percpu1, false);

	err = btf_type_tag_percpu__load(skel);
	ASSERT_ERR(err, "btf_type_tag_percpu");

	btf_type_tag_percpu__destroy(skel);

cleanup:
	btf__free(module_btf);
	btf__free(vmlinux_btf);
}

static void test_btf_type_tag_vmlinux_percpu(bool load_test)
{
	struct btf_type_tag_percpu *skel;
	struct btf *vmlinux_btf = NULL;
	int err;

	if (load_btfs(&vmlinux_btf, NULL, /*needs_vmlinux_tag=*/true))
		return;

	skel = btf_type_tag_percpu__open();
	if (!ASSERT_OK_PTR(skel, "btf_type_tag_percpu"))
		goto cleanup;

	bpf_program__set_autoload(skel->progs.test_percpu2, false);
	bpf_program__set_autoload(skel->progs.test_percpu1, false);
	if (load_test) {
		bpf_program__set_autoload(skel->progs.test_percpu_helper, false);

		err = btf_type_tag_percpu__load(skel);
		ASSERT_ERR(err, "btf_type_tag_percpu_load");
	} else {
		bpf_program__set_autoload(skel->progs.test_percpu_load, false);

		err = btf_type_tag_percpu__load(skel);
		ASSERT_OK(err, "btf_type_tag_percpu_helper");
	}

	btf_type_tag_percpu__destroy(skel);

cleanup:
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

	if (test__start_subtest("btf_type_tag_percpu_mod1"))
		test_btf_type_tag_mod_percpu(true);
	if (test__start_subtest("btf_type_tag_percpu_mod2"))
		test_btf_type_tag_mod_percpu(false);
	if (test__start_subtest("btf_type_tag_percpu_vmlinux_load"))
		test_btf_type_tag_vmlinux_percpu(true);
	if (test__start_subtest("btf_type_tag_percpu_vmlinux_helper"))
		test_btf_type_tag_vmlinux_percpu(false);
}
