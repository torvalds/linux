// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <linux/nbd.h>
#include "bpf_util.h"

/* NOTE: conflict with other tests. */
void serial_test_raw_tp_writable_test_run(void)
{
	__u32 duration = 0;
	char error[4096];

	const struct bpf_insn trace_program[] = {
		BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, 0),
		BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_6, 0),
		BPF_MOV64_IMM(BPF_REG_0, 42),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	LIBBPF_OPTS(bpf_prog_load_opts, trace_opts,
		.log_level = 2,
		.log_buf = error,
		.log_size = sizeof(error),
	);

	int bpf_fd = bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE, NULL, "GPL v2",
				   trace_program, ARRAY_SIZE(trace_program),
				   &trace_opts);
	if (CHECK(bpf_fd < 0, "bpf_raw_tracepoint_writable loaded",
		  "failed: %d errno %d\n", bpf_fd, errno))
		return;

	const struct bpf_insn skb_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	LIBBPF_OPTS(bpf_prog_load_opts, skb_opts,
		.log_buf = error,
		.log_size = sizeof(error),
	);

	int filter_fd = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, NULL, "GPL v2",
				      skb_program, ARRAY_SIZE(skb_program),
				      &skb_opts);
	if (CHECK(filter_fd < 0, "test_program_loaded", "failed: %d errno %d\n",
		  filter_fd, errno))
		goto out_bpffd;

	int tp_fd = bpf_raw_tracepoint_open("bpf_test_finish", bpf_fd);
	if (CHECK(tp_fd < 0, "bpf_raw_tracepoint_writable opened",
		  "failed: %d errno %d\n", tp_fd, errno))
		goto out_filterfd;

	char test_skb[128] = {
		0,
	};

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = test_skb,
		.data_size_in = sizeof(test_skb),
		.repeat = 1,
	);
	int err = bpf_prog_test_run_opts(filter_fd, &topts);
	CHECK(err != 42, "test_run",
	      "tracepoint did not modify return value\n");
	CHECK(topts.retval != 0, "test_run_ret",
	      "socket_filter did not return 0\n");

	close(tp_fd);

	err = bpf_prog_test_run_opts(filter_fd, &topts);
	CHECK(err != 0, "test_run_notrace",
	      "test_run failed with %d errno %d\n", err, errno);
	CHECK(topts.retval != 0, "test_run_ret_notrace",
	      "socket_filter did not return 0\n");

out_filterfd:
	close(filter_fd);
out_bpffd:
	close(bpf_fd);
}
