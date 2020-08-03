// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include <network_helpers.h>

static void test_fexit_bpf2bpf_common(const char *obj_file,
				      const char *target_obj_file,
				      int prog_cnt,
				      const char **prog_name,
				      bool run_prog)
{
	struct bpf_object *obj = NULL, *pkt_obj;
	int err, pkt_fd, i;
	struct bpf_link **link = NULL;
	struct bpf_program **prog = NULL;
	__u32 duration = 0, retval;
	struct bpf_map *data_map;
	const int zero = 0;
	u64 *result = NULL;

	err = bpf_prog_load(target_obj_file, BPF_PROG_TYPE_UNSPEC,
			    &pkt_obj, &pkt_fd);
	if (CHECK(err, "tgt_prog_load", "file %s err %d errno %d\n",
		  target_obj_file, err, errno))
		return;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts,
			    .attach_prog_fd = pkt_fd,
			   );

	link = calloc(sizeof(struct bpf_link *), prog_cnt);
	prog = calloc(sizeof(struct bpf_program *), prog_cnt);
	result = malloc((prog_cnt + 32 /* spare */) * sizeof(u64));
	if (CHECK(!link || !prog || !result, "alloc_memory",
		  "failed to alloc memory"))
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

	if (!run_prog)
		goto close_prog;

	data_map = bpf_object__find_map_by_name(obj, "fexit_bp.bss");
	if (CHECK(!data_map, "find_data_map", "data map not found\n"))
		goto close_prog;

	err = bpf_prog_test_run(pkt_fd, 1, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	err = bpf_map_lookup_elem(bpf_map__fd(data_map), &zero, result);
	if (CHECK(err, "get_result",
		  "failed to get output data: %d\n", err))
		goto close_prog;

	for (i = 0; i < prog_cnt; i++)
		if (CHECK(result[i] != 1, "result", "fexit_bpf2bpf failed err %ld\n",
			  result[i]))
			goto close_prog;

close_prog:
	for (i = 0; i < prog_cnt; i++)
		if (!IS_ERR_OR_NULL(link[i]))
			bpf_link__destroy(link[i]);
	if (!IS_ERR_OR_NULL(obj))
		bpf_object__close(obj);
	bpf_object__close(pkt_obj);
	free(link);
	free(prog);
	free(result);
}

static void test_target_no_callees(void)
{
	const char *prog_name[] = {
		"fexit/test_pkt_md_access",
	};
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf_simple.o",
				  "./test_pkt_md_access.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true);
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
				  prog_name, true);
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
	};
	test_fexit_bpf2bpf_common("./fexit_bpf2bpf.o",
				  "./test_pkt_access.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, true);
}

static void test_func_replace_verify(void)
{
	const char *prog_name[] = {
		"freplace/do_bind",
	};
	test_fexit_bpf2bpf_common("./freplace_connect4.o",
				  "./connect4_prog.o",
				  ARRAY_SIZE(prog_name),
				  prog_name, false);
}

void test_fexit_bpf2bpf(void)
{
	test_target_no_callees();
	test_target_yes_callees();
	test_func_replace();
	test_func_replace_verify();
}
