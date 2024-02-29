// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include "test_global_func1.skel.h"
#include "test_global_func2.skel.h"
#include "test_global_func3.skel.h"
#include "test_global_func4.skel.h"
#include "test_global_func5.skel.h"
#include "test_global_func6.skel.h"
#include "test_global_func7.skel.h"
#include "test_global_func8.skel.h"
#include "test_global_func9.skel.h"
#include "test_global_func10.skel.h"
#include "test_global_func11.skel.h"
#include "test_global_func12.skel.h"
#include "test_global_func13.skel.h"
#include "test_global_func14.skel.h"
#include "test_global_func15.skel.h"
#include "test_global_func16.skel.h"
#include "test_global_func17.skel.h"
#include "test_global_func_ctx_args.skel.h"

#include "bpf/libbpf_internal.h"
#include "btf_helpers.h"

static void check_ctx_arg_type(const struct btf *btf, const struct btf_param *p)
{
	const struct btf_type *t;
	const char *s;

	t = btf__type_by_id(btf, p->type);
	if (!ASSERT_EQ(btf_kind(t), BTF_KIND_PTR, "ptr_t"))
		return;

	s = btf_type_raw_dump(btf, t->type);
	if (!ASSERT_HAS_SUBSTR(s, "STRUCT 'bpf_perf_event_data' size=0 vlen=0",
			       "ctx_struct_t"))
		return;
}

static void subtest_ctx_arg_rewrite(void)
{
	struct test_global_func_ctx_args *skel = NULL;
	struct bpf_prog_info info;
	char func_info_buf[1024] __attribute__((aligned(8)));
	struct bpf_func_info_min *rec;
	struct btf *btf = NULL;
	__u32 info_len = sizeof(info);
	int err, fd, i;
	struct btf *kern_btf = NULL;

	kern_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(kern_btf, "kern_btf_load"))
		return;

	/* simple detection of kernel native arg:ctx tag support */
	if (btf__find_by_name_kind(kern_btf, "bpf_subprog_arg_info", BTF_KIND_STRUCT) > 0) {
		test__skip();
		btf__free(kern_btf);
		return;
	}
	btf__free(kern_btf);

	skel = test_global_func_ctx_args__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bpf_program__set_autoload(skel->progs.arg_tag_ctx_perf, true);

	err = test_global_func_ctx_args__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto out;

	memset(&info, 0, sizeof(info));
	info.func_info = ptr_to_u64(&func_info_buf);
	info.nr_func_info = 3;
	info.func_info_rec_size = sizeof(struct bpf_func_info_min);

	fd = bpf_program__fd(skel->progs.arg_tag_ctx_perf);
	err = bpf_prog_get_info_by_fd(fd, &info, &info_len);
	if (!ASSERT_OK(err, "prog_info"))
		goto out;

	if (!ASSERT_EQ(info.nr_func_info, 3, "nr_func_info"))
		goto out;

	btf = btf__load_from_kernel_by_id(info.btf_id);
	if (!ASSERT_OK_PTR(btf, "obj_kern_btf"))
		goto out;

	rec = (struct bpf_func_info_min *)func_info_buf;
	for (i = 0; i < info.nr_func_info; i++, rec = (void *)rec + info.func_info_rec_size) {
		const struct btf_type *fn_t, *proto_t;
		const char *name;

		if (rec->insn_off == 0)
			continue; /* main prog, skip */

		fn_t = btf__type_by_id(btf, rec->type_id);
		if (!ASSERT_OK_PTR(fn_t, "fn_type"))
			goto out;
		if (!ASSERT_EQ(btf_kind(fn_t), BTF_KIND_FUNC, "fn_type_kind"))
			goto out;
		proto_t = btf__type_by_id(btf, fn_t->type);
		if (!ASSERT_OK_PTR(proto_t, "proto_type"))
			goto out;

		name = btf__name_by_offset(btf, fn_t->name_off);
		if (strcmp(name, "subprog_ctx_tag") == 0) {
			/* int subprog_ctx_tag(void *ctx __arg_ctx) */
			if (!ASSERT_EQ(btf_vlen(proto_t), 1, "arg_cnt"))
				goto out;

			/* arg 0 is PTR -> STRUCT bpf_perf_event_data */
			check_ctx_arg_type(btf, &btf_params(proto_t)[0]);
		} else if (strcmp(name, "subprog_multi_ctx_tags") == 0) {
			/* int subprog_multi_ctx_tags(void *ctx1 __arg_ctx,
			 *			      struct my_struct *mem,
			 *			      void *ctx2 __arg_ctx)
			 */
			if (!ASSERT_EQ(btf_vlen(proto_t), 3, "arg_cnt"))
				goto out;

			/* arg 0 is PTR -> STRUCT bpf_perf_event_data */
			check_ctx_arg_type(btf, &btf_params(proto_t)[0]);
			/* arg 2 is PTR -> STRUCT bpf_perf_event_data */
			check_ctx_arg_type(btf, &btf_params(proto_t)[2]);
		} else {
			ASSERT_FAIL("unexpected subprog %s", name);
			goto out;
		}
	}

out:
	btf__free(btf);
	test_global_func_ctx_args__destroy(skel);
}

void test_test_global_funcs(void)
{
	RUN_TESTS(test_global_func1);
	RUN_TESTS(test_global_func2);
	RUN_TESTS(test_global_func3);
	RUN_TESTS(test_global_func4);
	RUN_TESTS(test_global_func5);
	RUN_TESTS(test_global_func6);
	RUN_TESTS(test_global_func7);
	RUN_TESTS(test_global_func8);
	RUN_TESTS(test_global_func9);
	RUN_TESTS(test_global_func10);
	RUN_TESTS(test_global_func11);
	RUN_TESTS(test_global_func12);
	RUN_TESTS(test_global_func13);
	RUN_TESTS(test_global_func14);
	RUN_TESTS(test_global_func15);
	RUN_TESTS(test_global_func16);
	RUN_TESTS(test_global_func17);
	RUN_TESTS(test_global_func_ctx_args);

	if (test__start_subtest("ctx_arg_rewrite"))
		subtest_ctx_arg_rewrite();
}
