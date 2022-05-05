// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>

/* that's kernel internal BPF_MAX_TRAMP_PROGS define */
#define CNT 38

void test_fexit_stress(void)
{
	char test_skb[128] = {};
	int fexit_fd[CNT] = {};
	int link_fd[CNT] = {};
	char error[4096];
	int err, i, filter_fd;

	const struct bpf_insn trace_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	LIBBPF_OPTS(bpf_prog_load_opts, trace_opts,
		.expected_attach_type = BPF_TRACE_FEXIT,
		.log_buf = error,
		.log_size = sizeof(error),
	);

	const struct bpf_insn skb_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	LIBBPF_OPTS(bpf_prog_load_opts, skb_opts,
		.log_buf = error,
		.log_size = sizeof(error),
	);

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = test_skb,
		.data_size_in = sizeof(test_skb),
		.repeat = 1,
	);

	err = libbpf_find_vmlinux_btf_id("bpf_fentry_test1",
					 trace_opts.expected_attach_type);
	if (!ASSERT_GT(err, 0, "find_vmlinux_btf_id"))
		goto out;
	trace_opts.attach_btf_id = err;

	for (i = 0; i < CNT; i++) {
		fexit_fd[i] = bpf_prog_load(BPF_PROG_TYPE_TRACING, NULL, "GPL",
					    trace_program,
					    sizeof(trace_program) / sizeof(struct bpf_insn),
					    &trace_opts);
		if (!ASSERT_GE(fexit_fd[i], 0, "fexit load"))
			goto out;
		link_fd[i] = bpf_raw_tracepoint_open(NULL, fexit_fd[i]);
		if (!ASSERT_GE(link_fd[i], 0, "fexit attach"))
			goto out;
	}

	filter_fd = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, NULL, "GPL",
				  skb_program, sizeof(skb_program) / sizeof(struct bpf_insn),
				  &skb_opts);
	if (!ASSERT_GE(filter_fd, 0, "test_program_loaded"))
		goto out;

	err = bpf_prog_test_run_opts(filter_fd, &topts);
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
