// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <bpf/btf.h>
#include "linked_arena.skel.h"

static void test_skel(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct linked_arena *skel;
	int err;

	skel = linked_arena__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	if (skel->data->skip_tests) {
		printf("%s:SKIP: no addr_space_cast support in the BPF compiler\n", __func__);
		test__skip();
		goto cleanup;
	}

	err = linked_arena__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	ASSERT_EQ(skel->arena->a_val, 1, "a_val_init");
	ASSERT_EQ(skel->arena->b_val, 2, "b_val_init");

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.sum1), &opts);
	ASSERT_OK(err, "sum1_run");
	ASSERT_EQ(opts.retval, 1 + 2, "sum1_retval");

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.bump2), &opts);
	ASSERT_OK(err, "bump2_run");
	ASSERT_EQ(opts.retval, 11 + 22, "bump2_retval");

	ASSERT_EQ(skel->arena->a_val, 11, "a_val");
	ASSERT_EQ(skel->arena->b_val, 22, "b_val");

cleanup:
	linked_arena__destroy(skel);
}

static int link_objs(const char *out, const char *in1, const char *in2)
{
	struct bpf_linker *linker;
	int err;

	linker = bpf_linker__new(out, NULL);
	if (!ASSERT_OK_PTR(linker, "linker_new"))
		return -EINVAL;

	err = bpf_linker__add_file(linker, in1, NULL);
	if (!ASSERT_OK(err, in1))
		goto cleanup;
	if (in2) {
		err = bpf_linker__add_file(linker, in2, NULL);
		if (!ASSERT_OK(err, in2))
			goto cleanup;
	}
	err = bpf_linker__finalize(linker);
	ASSERT_OK(err, "finalize");

cleanup:
	bpf_linker__free(linker);
	return err;
}

/* link in1 on its own first, then link the result with in2 */
static void test_relink(const char *in1, const char *in2, const char *sec_name)
{
	char out1[] = "/tmp/linked_externs.XXXXXX", out2[] = "/tmp/linked_externs.XXXXXX";
	const struct btf_var_secinfo *vi;
	struct bpf_object *obj = NULL;
	const struct btf_type *t;
	struct btf *btf;
	int i, id, n;

	close(mkstemp(out1));
	close(mkstemp(out2));

	if (!ASSERT_OK(link_objs(out1, in1, NULL), "link_stage1") ||
	    !ASSERT_OK(link_objs(out2, out1, in2), "link_stage2"))
		goto cleanup;

	obj = bpf_object__open_file(out2, NULL);
	if (!ASSERT_OK_PTR(obj, "obj_open"))
		goto cleanup;

	btf = bpf_object__btf(obj);
	id = btf__find_by_name_kind(btf, sec_name, BTF_KIND_DATASEC);
	if (!ASSERT_GT(id, 0, "find_datasec"))
		goto cleanup;

	t = btf__type_by_id(btf, id);
	vi = btf_var_secinfos(t);
	n = btf_vlen(t);
	for (i = 0; i < n; i++) {
		t = btf__type_by_id(btf, vi[i].type);
		ASSERT_NEQ(btf_var(t)->linkage, BTF_VAR_GLOBAL_EXTERN, "var_resolved");
	}

	ASSERT_OK(bpf_object__load(obj), "obj_load");

cleanup:
	bpf_object__close(obj);
	unlink(out1);
	unlink(out2);
}

void test_linked_externs(void)
{
	if (test__start_subtest("skel_arena"))
		test_skel();
	if (test__start_subtest("relink_arena"))
		test_relink("linked_arena1.bpf.o", "linked_arena2.bpf.o", ".addr_space.1");
	if (test__start_subtest("relink_maps"))
		test_relink("linked_maps1.bpf.o", "linked_maps2.bpf.o", ".maps");
}
