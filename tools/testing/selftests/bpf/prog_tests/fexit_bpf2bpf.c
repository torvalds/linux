// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include <bpf/btf.h>

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
	struct bpf_program **prog = NULL;
	struct bpf_link **link = NULL;
	__u32 duration = 0, retval;
	int err, tgt_fd, i;

	err = bpf_prog_load(target_obj_file, BPF_PROG_TYPE_UNSPEC,
			    &tgt_obj, &tgt_fd);
	if (CHECK(err, "tgt_prog_load", "file %s err %d errno %d\n",
		  target_obj_file, err, errno))
		return;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts,
			    .attach_prog_fd = tgt_fd,
			   );

	link = calloc(sizeof(struct bpf_link *), prog_cnt);
	prog = calloc(sizeof(struct bpf_program *), prog_cnt);
	if (CHECK(!link || !prog, "alloc_memory", "failed to alloc memory"))
		goto close_prog;

	obj = bpf_object__open_file(obj_file, &opts);
	if (CHECK(IS_ERR_OR_NULL(obj), "obj_open",
		  "failed to open %s: %ld\n", obj_file,
		  PTR_ERR(obj)))
		goto close_prog;

	err = bpf_object__load(obj);
	if (CHECK(err, "obj_load", "err %d\n", err))
		goto close_prog;

	for (i = 0; i < prog_cnt; i++) {
		prog[i] = bpf_object__find_program_by_title(obj, prog_name[i]);
		if (CHECK(!prog[i], "find_prog", "prog %s not found\n", prog_name[i]))
			goto close_prog;
		link[i] = bpf_program__attach_trace(prog[i]);
		if (CHECK(IS_ERR(link[i]), "attach_trace", "failed to link\n"))
			goto close_prog;
	}

	if (cb) {
		err = cb(obj);
		if (err)
			goto close_prog;
	}

	if (!run_prog)
		goto close_prog;

	err = bpf_prog_test_run(tgt_fd, 1, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	if (check_data_map(obj, prog_cnt, false))
		goto close_prog;

close_prog:
	for (i = 0; i < prog_cnt; i++)
		if (!IS_ERR_OR_NULL(link[i]))
			bpf_link__destroy(link[i]);
	if (!IS_ERR_OR_NULL(obj))
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
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf_simple.o",
				  "./test_pkt_md_access.o",
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
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf.o",
				  "./test_pkt_access.o",
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
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf.o",
				  "./test_pkt_access.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true, NULL);
}

static void test_func_replace_verify(void)
{
	const char *prog_name[] = {
		"freplace/do_bind",
	};
	test_fexit_bpf2bpf_common("./freplace_connect4.o",
				  "./connect4_prog.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, false, NULL);
}

static int test_second_attach(struct bpf_object *obj)
{
	const char *prog_name = "freplace/get_constant";
	const char *tgt_name = prog_name + 9; /* cut off freplace/ */
	const char *tgt_obj_file = "./test_pkt_access.o";
	struct bpf_program *prog = NULL;
	struct bpf_object *tgt_obj;
	__u32 duration = 0, retval;
	struct bpf_link *link;
	int err = 0, tgt_fd;

	prog = bpf_object__find_program_by_title(obj, prog_name);
	if (CHECK(!prog, "find_prog", "prog %s not found\n", prog_name))
		return -ENOENT;

	err = bpf_prog_load(tgt_obj_file, BPF_PROG_TYPE_UNSPEC,
			    &tgt_obj, &tgt_fd);
	if (CHECK(err, "second_prog_load", "file %s err %d errno %d\n",
		  tgt_obj_file, err, errno))
		return err;

	link = bpf_program__attach_freplace(prog, tgt_fd, tgt_name);
	if (CHECK(IS_ERR(link), "second_link", "failed to attach second link prog_fd %d tgt_fd %d\n", bpf_program__fd(prog), tgt_fd))
		goto out;

	err = bpf_prog_test_run(tgt_fd, 1, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval, "ipv6",
		  "err %d errno %d retval %d duration %d\n",
		  err, errno, retval, duration))
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
	test_fexit_bpf2bpf_common("./freplace_get_constant.o",
				  "./test_pkt_access.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true, test_second_attach);
}

static void test_fmod_ret_freplace(void)
{
	struct bpf_object *freplace_obj = NULL, *pkt_obj, *fmod_obj = NULL;
	const char *freplace_name = "./freplace_get_constant.o";
	const char *fmod_ret_name = "./fmod_ret_freplace.o";
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	const char *tgt_name = "./test_pkt_access.o";
	struct bpf_link *freplace_link = NULL;
	struct bpf_program *prog;
	__u32 duration = 0;
	int err, pkt_fd;

	err = bpf_prog_load(tgt_name, BPF_PROG_TYPE_UNSPEC,
			    &pkt_obj, &pkt_fd);
	/* the target prog should load fine */
	if (CHECK(err, "tgt_prog_load", "file %s err %d errno %d\n",
		  tgt_name, err, errno))
		return;
	opts.attach_prog_fd = pkt_fd;

	freplace_obj = bpf_object__open_file(freplace_name, &opts);
	if (CHECK(IS_ERR_OR_NULL(freplace_obj), "freplace_obj_open",
		  "failed to open %s: %ld\n", freplace_name,
		  PTR_ERR(freplace_obj)))
		goto out;

	err = bpf_object__load(freplace_obj);
	if (CHECK(err, "freplace_obj_load", "err %d\n", err))
		goto out;

	prog = bpf_program__next(NULL, freplace_obj);
	freplace_link = bpf_program__attach_trace(prog);
	if (CHECK(IS_ERR(freplace_link), "freplace_attach_trace", "failed to link\n"))
		goto out;

	opts.attach_prog_fd = bpf_program__fd(prog);
	fmod_obj = bpf_object__open_file(fmod_ret_name, &opts);
	if (CHECK(IS_ERR_OR_NULL(fmod_obj), "fmod_obj_open",
		  "failed to open %s: %ld\n", fmod_ret_name,
		  PTR_ERR(fmod_obj)))
		goto out;

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
	test_fexit_bpf2bpf_common("./freplace_cls_redirect.o",
				  "./test_cls_redirect.o",
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
	int err, pkt_fd;
	__u32 duration = 0;

	err = bpf_prog_load(target_obj_file, BPF_PROG_TYPE_UNSPEC,
			    &pkt_obj, &pkt_fd);
	/* the target prog should load fine */
	if (CHECK(err, "tgt_prog_load", "file %s err %d errno %d\n",
		  target_obj_file, err, errno))
		return;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts,
			    .attach_prog_fd = pkt_fd,
			   );

	obj = bpf_object__open_file(obj_file, &opts);
	if (CHECK(IS_ERR_OR_NULL(obj), "obj_open",
		  "failed to open %s: %ld\n", obj_file,
		  PTR_ERR(obj)))
		goto close_prog;

	/* It should fail to load the program */
	err = bpf_object__load(obj);
	if (CHECK(!err, "bpf_obj_load should fail", "err %d\n", err))
		goto close_prog;

close_prog:
	if (!IS_ERR_OR_NULL(obj))
		bpf_object__close(obj);
	bpf_object__close(pkt_obj);
}

static void test_func_replace_return_code(void)
{
	/* test invalid return code in the replaced program */
	test_obj_load_failure_common("./freplace_connect_v4_prog.o",
				     "./connect4_prog.o");
}

static void test_func_map_prog_compatibility(void)
{
	/* test with spin lock map value in the replaced program */
	test_obj_load_failure_common("./freplace_attach_probe.o",
				     "./test_attach_probe.o");
}

void test_fexit_bpf2bpf(void)
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
}
