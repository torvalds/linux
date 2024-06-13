// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include <bpf/btf.h>
#include "bind4_prog.skel.h"

typedef int (*test_cb)(struct bpf_object *obj);

static int check_data_map(struct bpf_object *obj, int prog_cnt, bool reset)
{
	struct bpf_map *data_map = NULL, *map;
	__u64 *result = NULL;
	const int zero = 0;
	__u32 duration = 0;
	int ret = -1, i;

	result = malloc((prog_cnt + 32 /* spare */) * sizeof(__u64));
	if (CHECK(!result, "alloc_memory", "failed to alloc memory"))
		return -ENOMEM;

	bpf_object__for_each_map(map, obj)
		if (bpf_map__is_internal(map)) {
			data_map = map;
			break;
		}
	if (CHECK(!data_map, "find_data_map", "data map not found\n"))
		goto out;

	ret = bpf_map_lookup_elem(bpf_map__fd(data_map), &zero, result);
	if (CHECK(ret, "get_result",
		  "failed to get output data: %d\n", ret))
		goto out;

	for (i = 0; i < prog_cnt; i++) {
		if (CHECK(result[i] != 1, "result",
			  "fexit_bpf2bpf result[%d] failed err %llu\n",
			  i, result[i]))
			goto out;
		result[i] = 0;
	}
	if (reset) {
		ret = bpf_map_update_elem(bpf_map__fd(data_map), &zero, result, 0);
		if (CHECK(ret, "reset_result", "failed to reset result\n"))
			goto out;
	}

	ret = 0;
out:
	free(result);
	return ret;
}

static void test_fexit_bpf2bpf_common(const char *obj_file,
				      const char *target_obj_file,
				      int prog_cnt,
				      const char **prog_name,
				      bool run_prog,
				      test_cb cb)
{
	struct bpf_object *obj = NULL, *tgt_obj;
	__u32 tgt_prog_id, info_len;
	struct bpf_prog_info prog_info = {};
	struct bpf_program **prog = NULL, *p;
	struct bpf_link **link = NULL;
	int err, tgt_fd, i;
	struct btf *btf;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v6,
		.data_size_in = sizeof(pkt_v6),
		.repeat = 1,
	);

	err = bpf_prog_test_load(target_obj_file, BPF_PROG_TYPE_UNSPEC,
			    &tgt_obj, &tgt_fd);
	if (!ASSERT_OK(err, "tgt_prog_load"))
		return;

	info_len = sizeof(prog_info);
	err = bpf_obj_get_info_by_fd(tgt_fd, &prog_info, &info_len);
	if (!ASSERT_OK(err, "tgt_fd_get_info"))
		goto close_prog;

	tgt_prog_id = prog_info.id;
	btf = bpf_object__btf(tgt_obj);

	link = calloc(sizeof(struct bpf_link *), prog_cnt);
	if (!ASSERT_OK_PTR(link, "link_ptr"))
		goto close_prog;

	prog = calloc(sizeof(struct bpf_program *), prog_cnt);
	if (!ASSERT_OK_PTR(prog, "prog_ptr"))
		goto close_prog;

	obj = bpf_object__open_file(obj_file, NULL);
	if (!ASSERT_OK_PTR(obj, "obj_open"))
		goto close_prog;

	bpf_object__for_each_program(p, obj) {
		err = bpf_program__set_attach_target(p, tgt_fd, NULL);
		ASSERT_OK(err, "set_attach_target");
	}

	err = bpf_object__load(obj);
	if (!ASSERT_OK(err, "obj_load"))
		goto close_prog;

	for (i = 0; i < prog_cnt; i++) {
		struct bpf_link_info link_info;
		struct bpf_program *pos;
		const char *pos_sec_name;
		char *tgt_name;
		__s32 btf_id;

		tgt_name = strstr(prog_name[i], "/");
		if (!ASSERT_OK_PTR(tgt_name, "tgt_name"))
			goto close_prog;
		btf_id = btf__find_by_name_kind(btf, tgt_name + 1, BTF_KIND_FUNC);

		prog[i] = NULL;
		bpf_object__for_each_program(pos, obj) {
			pos_sec_name = bpf_program__section_name(pos);
			if (pos_sec_name && !strcmp(pos_sec_name, prog_name[i])) {
				prog[i] = pos;
				break;
			}
		}
		if (!ASSERT_OK_PTR(prog[i], prog_name[i]))
			goto close_prog;

		link[i] = bpf_program__attach_trace(prog[i]);
		if (!ASSERT_OK_PTR(link[i], "attach_trace"))
			goto close_prog;

		info_len = sizeof(link_info);
		memset(&link_info, 0, sizeof(link_info));
		err = bpf_obj_get_info_by_fd(bpf_link__fd(link[i]),
					     &link_info, &info_len);
		ASSERT_OK(err, "link_fd_get_info");
		ASSERT_EQ(link_info.tracing.attach_type,
			  bpf_program__expected_attach_type(prog[i]),
			  "link_attach_type");
		ASSERT_EQ(link_info.tracing.target_obj_id, tgt_prog_id, "link_tgt_obj_id");
		ASSERT_EQ(link_info.tracing.target_btf_id, btf_id, "link_tgt_btf_id");
	}

	if (cb) {
		err = cb(obj);
		if (err)
			goto close_prog;
	}

	if (!run_prog)
		goto close_prog;

	err = bpf_prog_test_run_opts(tgt_fd, &topts);
	ASSERT_OK(err, "prog_run");
	ASSERT_EQ(topts.retval, 0, "prog_run_ret");

	if (check_data_map(obj, prog_cnt, false))
		goto close_prog;

close_prog:
	for (i = 0; i < prog_cnt; i++)
		bpf_link__destroy(link[i]);
	bpf_object__close(obj);
	bpf_object__close(tgt_obj);
	free(link);
	free(prog);
}

static void test_target_no_callees(void)
{
	const char *prog_name[] = {
		"fexit/test_pkt_md_access",
	};
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf_simple.bpf.o",
				  "./test_pkt_md_access.bpf.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true, NULL);
}

static void test_target_yes_callees(void)
{
	const char *prog_name[] = {
		"fexit/test_pkt_access",
		"fexit/test_pkt_access_subprog1",
		"fexit/test_pkt_access_subprog2",
		"fexit/test_pkt_access_subprog3",
	};
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf.bpf.o",
				  "./test_pkt_access.bpf.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true, NULL);
}

static void test_func_replace(void)
{
	const char *prog_name[] = {
		"fexit/test_pkt_access",
		"fexit/test_pkt_access_subprog1",
		"fexit/test_pkt_access_subprog2",
		"fexit/test_pkt_access_subprog3",
		"freplace/get_skb_len",
		"freplace/get_skb_ifindex",
		"freplace/get_constant",
		"freplace/test_pkt_write_access_subprog",
	};
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf.bpf.o",
				  "./test_pkt_access.bpf.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true, NULL);
}

static void test_func_replace_verify(void)
{
	const char *prog_name[] = {
		"freplace/do_bind",
	};
	test_fexit_bpf2bpf_common("./freplace_connect4.bpf.o",
				  "./connect4_prog.bpf.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, false, NULL);
}

static int test_second_attach(struct bpf_object *obj)
{
	const char *prog_name = "security_new_get_constant";
	const char *tgt_name = "get_constant";
	const char *tgt_obj_file = "./test_pkt_access.bpf.o";
	struct bpf_program *prog = NULL;
	struct bpf_object *tgt_obj;
	struct bpf_link *link;
	int err = 0, tgt_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v6,
		.data_size_in = sizeof(pkt_v6),
		.repeat = 1,
	);

	prog = bpf_object__find_program_by_name(obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "find_prog"))
		return -ENOENT;

	err = bpf_prog_test_load(tgt_obj_file, BPF_PROG_TYPE_UNSPEC,
			    &tgt_obj, &tgt_fd);
	if (!ASSERT_OK(err, "second_prog_load"))
		return err;

	link = bpf_program__attach_freplace(prog, tgt_fd, tgt_name);
	if (!ASSERT_OK_PTR(link, "second_link"))
		goto out;

	err = bpf_prog_test_run_opts(tgt_fd, &topts);
	if (!ASSERT_OK(err, "ipv6 test_run"))
		goto out;
	if (!ASSERT_OK(topts.retval, "ipv6 retval"))
		goto out;

	err = check_data_map(obj, 1, true);
	if (err)
		goto out;

out:
	bpf_link__destroy(link);
	bpf_object__close(tgt_obj);
	return err;
}

static void test_func_replace_multi(void)
{
	const char *prog_name[] = {
		"freplace/get_constant",
	};
	test_fexit_bpf2bpf_common("./freplace_get_constant.bpf.o",
				  "./test_pkt_access.bpf.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true, test_second_attach);
}

static void test_fmod_ret_freplace(void)
{
	struct bpf_object *freplace_obj = NULL, *pkt_obj, *fmod_obj = NULL;
	const char *freplace_name = "./freplace_get_constant.bpf.o";
	const char *fmod_ret_name = "./fmod_ret_freplace.bpf.o";
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	const char *tgt_name = "./test_pkt_access.bpf.o";
	struct bpf_link *freplace_link = NULL;
	struct bpf_program *prog;
	__u32 duration = 0;
	int err, pkt_fd, attach_prog_fd;

	err = bpf_prog_test_load(tgt_name, BPF_PROG_TYPE_UNSPEC,
			    &pkt_obj, &pkt_fd);
	/* the target prog should load fine */
	if (CHECK(err, "tgt_prog_load", "file %s err %d errno %d\n",
		  tgt_name, err, errno))
		return;

	freplace_obj = bpf_object__open_file(freplace_name, NULL);
	if (!ASSERT_OK_PTR(freplace_obj, "freplace_obj_open"))
		goto out;

	prog = bpf_object__next_program(freplace_obj, NULL);
	err = bpf_program__set_attach_target(prog, pkt_fd, NULL);
	ASSERT_OK(err, "freplace__set_attach_target");

	err = bpf_object__load(freplace_obj);
	if (CHECK(err, "freplace_obj_load", "err %d\n", err))
		goto out;

	freplace_link = bpf_program__attach_trace(prog);
	if (!ASSERT_OK_PTR(freplace_link, "freplace_attach_trace"))
		goto out;

	fmod_obj = bpf_object__open_file(fmod_ret_name, NULL);
	if (!ASSERT_OK_PTR(fmod_obj, "fmod_obj_open"))
		goto out;

	attach_prog_fd = bpf_program__fd(prog);
	prog = bpf_object__next_program(fmod_obj, NULL);
	err = bpf_program__set_attach_target(prog, attach_prog_fd, NULL);
	ASSERT_OK(err, "fmod_ret_set_attach_target");

	err = bpf_object__load(fmod_obj);
	if (CHECK(!err, "fmod_obj_load", "loading fmod_ret should fail\n"))
		goto out;

out:
	bpf_link__destroy(freplace_link);
	bpf_object__close(freplace_obj);
	bpf_object__close(fmod_obj);
	bpf_object__close(pkt_obj);
}


static void test_func_sockmap_update(void)
{
	const char *prog_name[] = {
		"freplace/cls_redirect",
	};
	test_fexit_bpf2bpf_common("./freplace_cls_redirect.bpf.o",
				  "./test_cls_redirect.bpf.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, false, NULL);
}

static void test_obj_load_failure_common(const char *obj_file,
					 const char *target_obj_file)
{
	/*
	 * standalone test that asserts failure to load freplace prog
	 * because of invalid return code.
	 */
	struct bpf_object *obj = NULL, *pkt_obj;
	struct bpf_program *prog;
	int err, pkt_fd;
	__u32 duration = 0;

	err = bpf_prog_test_load(target_obj_file, BPF_PROG_TYPE_UNSPEC,
			    &pkt_obj, &pkt_fd);
	/* the target prog should load fine */
	if (CHECK(err, "tgt_prog_load", "file %s err %d errno %d\n",
		  target_obj_file, err, errno))
		return;

	obj = bpf_object__open_file(obj_file, NULL);
	if (!ASSERT_OK_PTR(obj, "obj_open"))
		goto close_prog;

	prog = bpf_object__next_program(obj, NULL);
	err = bpf_program__set_attach_target(prog, pkt_fd, NULL);
	ASSERT_OK(err, "set_attach_target");

	/* It should fail to load the program */
	err = bpf_object__load(obj);
	if (CHECK(!err, "bpf_obj_load should fail", "err %d\n", err))
		goto close_prog;

close_prog:
	bpf_object__close(obj);
	bpf_object__close(pkt_obj);
}

static void test_func_replace_return_code(void)
{
	/* test invalid return code in the replaced program */
	test_obj_load_failure_common("./freplace_connect_v4_prog.bpf.o",
				     "./connect4_prog.bpf.o");
}

static void test_func_map_prog_compatibility(void)
{
	/* test with spin lock map value in the replaced program */
	test_obj_load_failure_common("./freplace_attach_probe.bpf.o",
				     "./test_attach_probe.bpf.o");
}

static void test_func_replace_global_func(void)
{
	const char *prog_name[] = {
		"freplace/test_pkt_access",
	};

	test_fexit_bpf2bpf_common("./freplace_global_func.bpf.o",
				  "./test_pkt_access.bpf.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, false, NULL);
}

static int find_prog_btf_id(const char *name, __u32 attach_prog_fd)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	struct btf *btf;
	int ret;

	ret = bpf_obj_get_info_by_fd(attach_prog_fd, &info, &info_len);
	if (ret)
		return ret;

	if (!info.btf_id)
		return -EINVAL;

	btf = btf__load_from_kernel_by_id(info.btf_id);
	ret = libbpf_get_error(btf);
	if (ret)
		return ret;

	ret = btf__find_by_name_kind(btf, name, BTF_KIND_FUNC);
	btf__free(btf);
	return ret;
}

static int load_fentry(int attach_prog_fd, int attach_btf_id)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		    .expected_attach_type = BPF_TRACE_FENTRY,
		    .attach_prog_fd = attach_prog_fd,
		    .attach_btf_id = attach_btf_id,
	);
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	return bpf_prog_load(BPF_PROG_TYPE_TRACING,
			     "bind4_fentry",
			     "GPL",
			     insns,
			     ARRAY_SIZE(insns),
			     &opts);
}

static void test_fentry_to_cgroup_bpf(void)
{
	struct bind4_prog *skel = NULL;
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int cgroup_fd = -1;
	int fentry_fd = -1;
	int btf_id;

	cgroup_fd = test__join_cgroup("/fentry_to_cgroup_bpf");
	if (!ASSERT_GE(cgroup_fd, 0, "cgroup_fd"))
		return;

	skel = bind4_prog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		goto cleanup;

	skel->links.bind_v4_prog = bpf_program__attach_cgroup(skel->progs.bind_v4_prog, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.bind_v4_prog, "bpf_program__attach_cgroup"))
		goto cleanup;

	btf_id = find_prog_btf_id("bind_v4_prog", bpf_program__fd(skel->progs.bind_v4_prog));
	if (!ASSERT_GE(btf_id, 0, "find_prog_btf_id"))
		goto cleanup;

	fentry_fd = load_fentry(bpf_program__fd(skel->progs.bind_v4_prog), btf_id);
	if (!ASSERT_GE(fentry_fd, 0, "load_fentry"))
		goto cleanup;

	/* Make sure bpf_obj_get_info_by_fd works correctly when attaching
	 * to another BPF program.
	 */

	ASSERT_OK(bpf_obj_get_info_by_fd(fentry_fd, &info, &info_len),
		  "bpf_obj_get_info_by_fd");

	ASSERT_EQ(info.btf_id, 0, "info.btf_id");
	ASSERT_EQ(info.attach_btf_id, btf_id, "info.attach_btf_id");
	ASSERT_GT(info.attach_btf_obj_id, 0, "info.attach_btf_obj_id");

cleanup:
	if (cgroup_fd >= 0)
		close(cgroup_fd);
	if (fentry_fd >= 0)
		close(fentry_fd);
	bind4_prog__destroy(skel);
}

/* NOTE: affect other tests, must run in serial mode */
void serial_test_fexit_bpf2bpf(void)
{
	if (test__start_subtest("target_no_callees"))
		test_target_no_callees();
	if (test__start_subtest("target_yes_callees"))
		test_target_yes_callees();
	if (test__start_subtest("func_replace"))
		test_func_replace();
	if (test__start_subtest("func_replace_verify"))
		test_func_replace_verify();
	if (test__start_subtest("func_sockmap_update"))
		test_func_sockmap_update();
	if (test__start_subtest("func_replace_return_code"))
		test_func_replace_return_code();
	if (test__start_subtest("func_map_prog_compatibility"))
		test_func_map_prog_compatibility();
	if (test__start_subtest("func_replace_multi"))
		test_func_replace_multi();
	if (test__start_subtest("fmod_ret_freplace"))
		test_fmod_ret_freplace();
	if (test__start_subtest("func_replace_global_func"))
		test_func_replace_global_func();
	if (test__start_subtest("fentry_to_cgroup_bpf"))
		test_fentry_to_cgroup_bpf();
}
