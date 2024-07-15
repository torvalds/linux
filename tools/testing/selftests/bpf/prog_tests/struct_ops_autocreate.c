// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "struct_ops_autocreate.skel.h"
#include "struct_ops_autocreate2.skel.h"

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

static int check_test_1_link(struct struct_ops_autocreate *skel, struct bpf_map *map)
{
	struct bpf_link *link;
	int err;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_1);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops"))
		return -1;

	/* test_1() would be called from bpf_dummy_reg2() in bpf_testmod.c */
	err = ASSERT_EQ(skel->bss->test_1_result, 42, "test_1_result");
	bpf_link__destroy(link);
	return err;
}

static void can_load_partial_object(void)
{
	struct struct_ops_autocreate *skel;
	int err;

	skel = struct_ops_autocreate__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_autocreate__open_opts"))
		return;

	err = bpf_map__set_autocreate(skel->maps.testmod_2, false);
	if (!ASSERT_OK(err, "bpf_map__set_autocreate"))
		goto cleanup;

	ASSERT_TRUE(bpf_program__autoload(skel->progs.test_1), "test_1 default autoload");
	ASSERT_TRUE(bpf_program__autoload(skel->progs.test_2), "test_2 default autoload");

	err = struct_ops_autocreate__load(skel);
	if (ASSERT_OK(err, "struct_ops_autocreate__load"))
		goto cleanup;

	ASSERT_TRUE(bpf_program__autoload(skel->progs.test_1), "test_1 actual autoload");
	ASSERT_FALSE(bpf_program__autoload(skel->progs.test_2), "test_2 actual autoload");

	check_test_1_link(skel, skel->maps.testmod_1);

cleanup:
	struct_ops_autocreate__destroy(skel);
}

static void optional_maps(void)
{
	struct struct_ops_autocreate *skel;
	int err;

	skel = struct_ops_autocreate__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_autocreate__open"))
		return;

	ASSERT_TRUE(bpf_map__autocreate(skel->maps.testmod_1), "testmod_1 autocreate");
	ASSERT_TRUE(bpf_map__autocreate(skel->maps.testmod_2), "testmod_2 autocreate");
	ASSERT_FALSE(bpf_map__autocreate(skel->maps.optional_map), "optional_map autocreate");
	ASSERT_FALSE(bpf_map__autocreate(skel->maps.optional_map2), "optional_map2 autocreate");

	err  = bpf_map__set_autocreate(skel->maps.testmod_1, false);
	err |= bpf_map__set_autocreate(skel->maps.testmod_2, false);
	err |= bpf_map__set_autocreate(skel->maps.optional_map2, true);
	if (!ASSERT_OK(err, "bpf_map__set_autocreate"))
		goto cleanup;

	err = struct_ops_autocreate__load(skel);
	if (ASSERT_OK(err, "struct_ops_autocreate__load"))
		goto cleanup;

	check_test_1_link(skel, skel->maps.optional_map2);

cleanup:
	struct_ops_autocreate__destroy(skel);
}

/* Swap test_mod1->test_1 program from 'bar' to 'foo' using shadow vars.
 * test_mod1 load should enable autoload for 'foo'.
 */
static void autoload_and_shadow_vars(void)
{
	struct struct_ops_autocreate2 *skel = NULL;
	struct bpf_link *link = NULL;
	int err;

	skel = struct_ops_autocreate2__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_autocreate__open_opts"))
		return;

	ASSERT_FALSE(bpf_program__autoload(skel->progs.foo), "foo default autoload");
	ASSERT_FALSE(bpf_program__autoload(skel->progs.bar), "bar default autoload");

	/* loading map testmod_1 would switch foo's autoload to true */
	skel->struct_ops.testmod_1->test_1 = skel->progs.foo;

	err = struct_ops_autocreate2__load(skel);
	if (ASSERT_OK(err, "struct_ops_autocreate__load"))
		goto cleanup;

	ASSERT_TRUE(bpf_program__autoload(skel->progs.foo), "foo actual autoload");
	ASSERT_FALSE(bpf_program__autoload(skel->progs.bar), "bar actual autoload");

	link = bpf_map__attach_struct_ops(skel->maps.testmod_1);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops"))
		goto cleanup;

	/* test_1() would be called from bpf_dummy_reg2() in bpf_testmod.c */
	err = ASSERT_EQ(skel->bss->test_1_result, 42, "test_1_result");

cleanup:
	bpf_link__destroy(link);
	struct_ops_autocreate2__destroy(skel);
}

void test_struct_ops_autocreate(void)
{
	if (test__start_subtest("cant_load_full_object"))
		cant_load_full_object();
	if (test__start_subtest("can_load_partial_object"))
		can_load_partial_object();
	if (test__start_subtest("autoload_and_shadow_vars"))
		autoload_and_shadow_vars();
	if (test__start_subtest("optional_maps"))
		optional_maps();
}
