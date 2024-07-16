// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>

/* that's kernel internal BPF_MAX_TRAMP_PROGS define */
#define CNT 38

void serial_test_fexit_stress(void)
{
	int fexit_fd[CNT] = {};
	int link_fd[CNT] = {};
	int err, i;

	const struct bpf_insn trace_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	LIBBPF_OPTS(bpf_prog_load_opts, trace_opts,
		.expected_attach_type = BPF_TRACE_FEXIT,
	);

	LIBBPF_OPTS(bpf_test_run_opts, topts);

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
		link_fd[i] = bpf_link_create(fexit_fd[i], 0, BPF_TRACE_FEXIT, NULL);
		if (!ASSERT_GE(link_fd[i], 0, "fexit attach"))
			goto out;
	}

	err = bpf_prog_test_run_opts(fexit_fd[0], &topts);
	ASSERT_OK(err, "bpf_prog_test_run_opts");

out:
	for (i = 0; i < CNT; i++) {
		if (link_fd[i])
			close(link_fd[i]);
		if (fexit_fd[i])
			close(fexit_fd[i]);
	}
}
