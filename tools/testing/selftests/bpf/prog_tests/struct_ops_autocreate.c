// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "struct_ops_autocreate.skel.h"

static void cant_load_full_object(void)
{
	struct struct_ops_autocreate *skel;
	char *log = NULL;
	int err;

	skel = struct_ops_autocreate__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_autocreate__open"))
		return;

	if (start_libbpf_log_capture())
		goto cleanup;
	/* The testmod_2 map BTF type (struct bpf_testmod_ops___v2) doesn't
	 * match the BTF of the actual struct bpf_testmod_ops defined in the
	 * kernel, so we should fail to load it if we don't disable autocreate
	 * for that map.
	 */
	err = struct_ops_autocreate__load(skel);
	log = stop_libbpf_log_capture();
	if (!ASSERT_ERR(err, "struct_ops_autocreate__load"))
		goto cleanup;

	ASSERT_HAS_SUBSTR(log, "libbpf: struct_ops init_kern", "init_kern message");
	ASSERT_EQ(err, -ENOTSUP, "errno should be ENOTSUP");

cleanup:
	free(log);
	struct_ops_autocreate__destroy(skel);
}

static void can_load_partial_object(void)
{
	struct struct_ops_autocreate *skel;
	struct bpf_link *link = NULL;
	int err;

	skel = struct_ops_autocreate__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_autocreate__open_opts"))
		return;

	err = bpf_program__set_autoload(skel->progs.test_2, false);
	if (!ASSERT_OK(err, "bpf_program__set_autoload"))
		goto cleanup;

	err = bpf_map__set_autocreate(skel->maps.testmod_2, false);
	if (!ASSERT_OK(err, "bpf_map__set_autocreate"))
		goto cleanup;

	err = struct_ops_autocreate__load(skel);
	if (ASSERT_OK(err, "struct_ops_autocreate__load"))
		goto cleanup;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_1);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops"))
		goto cleanup;

	/* test_1() would be called from bpf_dummy_reg2() in bpf_testmod.c */
	ASSERT_EQ(skel->bss->test_1_result, 42, "test_1_result");

cleanup:
	bpf_link__destroy(link);
	struct_ops_autocreate__destroy(skel);
}

void test_struct_ops_autocreate(void)
{
	if (test__start_subtest("cant_load_full_object"))
		cant_load_full_object();
	if (test__start_subtest("can_load_partial_object"))
		can_load_partial_object();
}
