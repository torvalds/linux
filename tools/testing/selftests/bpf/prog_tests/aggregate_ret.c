// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <bpf/btf.h>
#include "aggregate_ret_func.skel.h"
#include "aggregate_ret_kfunc.skel.h"
#include "aggregate_ret_kfunc_arena.skel.h"

static bool testmod_has_arena_tagged_member(void)
{
	struct btf *vmlinux_btf, *module_btf = NULL;
	const struct btf_type *t;
	bool tagged = false;
	__s32 id;

	vmlinux_btf = btf__load_vmlinux_btf();
	if (!vmlinux_btf)
		return false;

	module_btf = btf__load_module_btf("bpf_testmod", vmlinux_btf);
	if (!module_btf)
		goto out;

	/* prog_test_ret_arena::a is 'void __arena_tag *': PTR -> TYPE_TAG -> void */
	id = btf__find_by_name_kind(module_btf, "prog_test_ret_arena", BTF_KIND_STRUCT);
	if (id <= 0)
		goto out;

	t = btf__type_by_id(module_btf, btf_members(btf__type_by_id(module_btf, id))[0].type);
	if (!t || !btf_is_ptr(t))
		goto out;

	t = btf__type_by_id(module_btf, t->type);
	tagged = t && btf_is_type_tag(t) &&
		 !strcmp(btf__name_by_offset(module_btf, t->name_off), "arena");

out:
	btf__free(module_btf);
	btf__free(vmlinux_btf);

	return tagged;
}

void test_aggregate_ret(void)
{
	RUN_TESTS(aggregate_ret_func);
	RUN_TESTS(aggregate_ret_kfunc);

	if (testmod_has_arena_tagged_member())
		RUN_TESTS(aggregate_ret_kfunc_arena);
	else
		test__skip();
}
