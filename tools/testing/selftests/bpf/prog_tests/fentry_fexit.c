// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>

void test_fentry_fexit(void)
{
	struct bpf_prog_load_attr attr_fentry = {
		.file = "./fentry_test.o",
	};
	struct bpf_prog_load_attr attr_fexit = {
		.file = "./fexit_test.o",
	};

	struct bpf_object *obj_fentry = NULL, *obj_fexit = NULL, *pkt_obj;
	struct bpf_map *data_map_fentry, *data_map_fexit;
	char fentry_name[] = "fentry/bpf_fentry_testX";
	char fexit_name[] = "fexit/bpf_fentry_testX";
	int err, pkt_fd, kfree_skb_fd, i;
	struct bpf_link *link[12] = {};
	struct bpf_program *prog[12];
	__u32 duration, retval;
	const int zero = 0;
	u64 result[12];

	err = bpf_prog_load("./test_pkt_access.o", BPF_PROG_TYPE_SCHED_CLS,
			    &pkt_obj, &pkt_fd);
	if (CHECK(err, "prog_load sched cls", "err %d errno %d\n", err, errno))
		return;
	err = bpf_prog_load_xattr(&attr_fentry, &obj_fentry, &kfree_skb_fd);
	if (CHECK(err, "prog_load fail", "err %d errno %d\n", err, errno))
		goto close_prog;
	err = bpf_prog_load_xattr(&attr_fexit, &obj_fexit, &kfree_skb_fd);
	if (CHECK(err, "prog_load fail", "err %d errno %d\n", err, errno))
		goto close_prog;

	for (i = 0; i < 6; i++) {
		fentry_name[sizeof(fentry_name) - 2] = '1' + i;
		prog[i] = bpf_object__find_program_by_title(obj_fentry, fentry_name);
		if (CHECK(!prog[i], "find_prog", "prog %s not found\n", fentry_name))
			goto close_prog;
		link[i] = bpf_program__attach_trace(prog[i]);
		if (CHECK(IS_ERR(link[i]), "attach_trace", "failed to link\n"))
			goto close_prog;
	}
	data_map_fentry = bpf_object__find_map_by_name(obj_fentry, "fentry_t.bss");
	if (CHECK(!data_map_fentry, "find_data_map", "data map not found\n"))
		goto close_prog;

	for (i = 6; i < 12; i++) {
		fexit_name[sizeof(fexit_name) - 2] = '1' + i - 6;
		prog[i] = bpf_object__find_program_by_title(obj_fexit, fexit_name);
		if (CHECK(!prog[i], "find_prog", "prog %s not found\n", fexit_name))
			goto close_prog;
		link[i] = bpf_program__attach_trace(prog[i]);
		if (CHECK(IS_ERR(link[i]), "attach_trace", "failed to link\n"))
			goto close_prog;
	}
	data_map_fexit = bpf_object__find_map_by_name(obj_fexit, "fexit_te.bss");
	if (CHECK(!data_map_fexit, "find_data_map", "data map not found\n"))
		goto close_prog;

	err = bpf_prog_test_run(pkt_fd, 1, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	err = bpf_map_lookup_elem(bpf_map__fd(data_map_fentry), &zero, &result);
	if (CHECK(err, "get_result",
		  "failed to get output data: %d\n", err))
		goto close_prog;

	err = bpf_map_lookup_elem(bpf_map__fd(data_map_fexit), &zero, result + 6);
	if (CHECK(err, "get_result",
		  "failed to get output data: %d\n", err))
		goto close_prog;

	for (i = 0; i < 12; i++)
		if (CHECK(result[i] != 1, "result", "bpf_fentry_test%d failed err %ld\n",
			  i % 6 + 1, result[i]))
			goto close_prog;

close_prog:
	for (i = 0; i < 12; i++)
		if (!IS_ERR_OR_NULL(link[i]))
			bpf_link__destroy(link[i]);
	bpf_object__close(obj_fentry);
	bpf_object__close(obj_fexit);
	bpf_object__close(pkt_obj);
}
