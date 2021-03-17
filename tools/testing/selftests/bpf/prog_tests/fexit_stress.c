// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>

/* x86-64 fits 55 JITed and 43 interpreted progs into half page */
#define CNT 40

void test_fexit_stress(void)
{
	char test_skb[128] = {};
	int fexit_fd[CNT] = {};
	int link_fd[CNT] = {};
	__u32 duration = 0;
	char error[4096];
	__u32 prog_ret;
	int err, i, filter_fd;

	const struct bpf_insn trace_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	struct bpf_load_program_attr load_attr = {
		.prog_type = BPF_PROG_TYPE_TRACING,
		.license = "GPL",
		.insns = trace_program,
		.insns_cnt = sizeof(trace_program) / sizeof(struct bpf_insn),
		.expected_attach_type = BPF_TRACE_FEXIT,
	};

	const struct bpf_insn skb_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	struct bpf_load_program_attr skb_load_attr = {
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.license = "GPL",
		.insns = skb_program,
		.insns_cnt = sizeof(skb_program) / sizeof(struct bpf_insn),
	};

	err = libbpf_find_vmlinux_btf_id("bpf_fentry_test1",
					 load_attr.expected_attach_type);
	if (CHECK(err <= 0, "find_vmlinux_btf_id", "failed: %d\n", err))
		goto out;
	load_attr.attach_btf_id = err;

	for (i = 0; i < CNT; i++) {
		fexit_fd[i] = bpf_load_program_xattr(&load_attr, error, sizeof(error));
		if (CHECK(fexit_fd[i] < 0, "fexit loaded",
			  "failed: %d errno %d\n", fexit_fd[i], errno))
			goto out;
		link_fd[i] = bpf_raw_tracepoint_open(NULL, fexit_fd[i]);
		if (CHECK(link_fd[i] < 0, "fexit attach failed",
			  "prog %d failed: %d err %d\n", i, link_fd[i], errno))
			goto out;
	}

	filter_fd = bpf_load_program_xattr(&skb_load_attr, error, sizeof(error));
	if (CHECK(filter_fd < 0, "test_program_loaded", "failed: %d errno %d\n",
		  filter_fd, errno))
		goto out;

	err = bpf_prog_test_run(filter_fd, 1, test_skb, sizeof(test_skb), 0,
				0, &prog_ret, 0);
	close(filter_fd);
	CHECK_FAIL(err);
out:
	for (i = 0; i < CNT; i++) {
		if (link_fd[i])
			close(link_fd[i]);
		if (fexit_fd[i])
			close(fexit_fd[i]);
	}
}
