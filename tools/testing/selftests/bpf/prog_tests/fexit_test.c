// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>

void test_fexit_test(void)
{
	struct bpf_prog_load_attr attr = {
		.file = "./fexit_test.o",
	};

	char prog_name[] = "fexit/bpf_fentry_testX";
	struct bpf_object *obj = NULL, *pkt_obj;
	int err, pkt_fd, kfree_skb_fd, i;
	struct bpf_link *link[6] = {};
	struct bpf_program *prog[6];
	__u32 duration, retval;
	struct bpf_map *data_map;
	const int zero = 0;
	u64 result[6];

	err = bpf_prog_load("./test_pkt_access.o", BPF_PROG_TYPE_SCHED_CLS,
			    &pkt_obj, &pkt_fd);
	if (CHECK(err, "prog_load sched cls", "err %d errno %d\n", err, errno))
		return;
	err = bpf_prog_load_xattr(&attr, &obj, &kfree_skb_fd);
	if (CHECK(err, "prog_load fail", "err %d errno %d\n", err, errno))
		goto close_prog;

	for (i = 0; i < 6; i++) {
		prog_name[sizeof(prog_name) - 2] = '1' + i;
		prog[i] = bpf_object__find_program_by_title(obj, prog_name);
		if (CHECK(!prog[i], "find_prog", "prog %s not found\n", prog_name))
			goto close_prog;
		link[i] = bpf_program__attach_trace(prog[i]);
		if (CHECK(IS_ERR(link[i]), "attach_trace", "failed to link\n"))
			goto close_prog;
	}
	data_map = bpf_object__find_map_by_name(obj, "fexit_te.bss");
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

	for (i = 0; i < 6; i++)
		if (CHECK(result[i] != 1, "result", "bpf_fentry_test%d failed err %ld\n",
			  i + 1, result[i]))
			goto close_prog;

close_prog:
	for (i = 0; i < 6; i++)
		if (!IS_ERR_OR_NULL(link[i]))
			bpf_link__destroy(link[i]);
	bpf_object__close(obj);
	bpf_object__close(pkt_obj);
}
