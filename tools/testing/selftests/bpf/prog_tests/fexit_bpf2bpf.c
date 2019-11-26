// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>

#define PROG_CNT 3

void test_fexit_bpf2bpf(void)
{
	const char *prog_name[PROG_CNT] = {
		"fexit/test_pkt_access",
		"fexit/test_pkt_access_subprog1",
		"fexit/test_pkt_access_subprog2",
	};
	struct bpf_object *obj = NULL, *pkt_obj;
	int err, pkt_fd, i;
	struct bpf_link *link[PROG_CNT] = {};
	struct bpf_program *prog[PROG_CNT];
	__u32 duration, retval;
	struct bpf_map *data_map;
	const int zero = 0;
	u64 result[PROG_CNT];

	err = bpf_prog_load("./test_pkt_access.o", BPF_PROG_TYPE_UNSPEC,
			    &pkt_obj, &pkt_fd);
	if (CHECK(err, "prog_load sched cls", "err %d errno %d\n", err, errno))
		return;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts,
			    .attach_prog_fd = pkt_fd,
			   );

	obj = bpf_object__open_file("./fexit_bpf2bpf.o", &opts);
	if (CHECK(IS_ERR_OR_NULL(obj), "obj_open",
		  "failed to open fexit_bpf2bpf: %ld\n",
		  PTR_ERR(obj)))
		goto close_prog;

	err = bpf_object__load(obj);
	if (CHECK(err, "obj_load", "err %d\n", err))
		goto close_prog;

	for (i = 0; i < PROG_CNT; i++) {
		prog[i] = bpf_object__find_program_by_title(obj, prog_name[i]);
		if (CHECK(!prog[i], "find_prog", "prog %s not found\n", prog_name[i]))
			goto close_prog;
		link[i] = bpf_program__attach_trace(prog[i]);
		if (CHECK(IS_ERR(link[i]), "attach_trace", "failed to link\n"))
			goto close_prog;
	}
	data_map = bpf_object__find_map_by_name(obj, "fexit_bp.bss");
	if (CHECK(!data_map, "find_data_map", "data map not found\n"))
		goto close_prog;

	err = bpf_prog_test_run(pkt_fd, 1, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	err = bpf_map_lookup_elem(bpf_map__fd(data_map), &zero, &result);
	if (CHECK(err, "get_result",
		  "failed to get output data: %d\n", err))
		goto close_prog;

	for (i = 0; i < PROG_CNT; i++)
		if (CHECK(result[i] != 1, "result", "fexit_bpf2bpf failed err %ld\n",
			  result[i]))
			goto close_prog;

close_prog:
	for (i = 0; i < PROG_CNT; i++)
		if (!IS_ERR_OR_NULL(link[i]))
			bpf_link__destroy(link[i]);
	if (!IS_ERR_OR_NULL(obj))
		bpf_object__close(obj);
	bpf_object__close(pkt_obj);
}
