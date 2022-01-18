// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <linux/nbd.h>

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

	struct bpf_load_program_attr load_attr = {
		.prog_type = BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE,
		.license = "GPL v2",
		.insns = trace_program,
		.insns_cnt = sizeof(trace_program) / sizeof(struct bpf_insn),
		.log_level = 2,
	};

	int bpf_fd = bpf_load_program_xattr(&load_attr, error, sizeof(error));
	if (CHECK(bpf_fd < 0, "bpf_raw_tracepoint_writable loaded",
		  "failed: %d errno %d\n", bpf_fd, errno))
		return;

	const struct bpf_insn skb_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	struct bpf_load_program_attr skb_load_attr = {
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.license = "GPL v2",
		.insns = skb_program,
		.insns_cnt = sizeof(skb_program) / sizeof(struct bpf_insn),
	};

	int filter_fd =
		bpf_load_program_xattr(&skb_load_attr, error, sizeof(error));
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

	__u32 prog_ret;
	int err = bpf_prog_test_run(filter_fd, 1, test_skb, sizeof(test_skb), 0,
				    0, &prog_ret, 0);
	CHECK(err != 42, "test_run",
	      "tracepoint did not modify return value\n");
	CHECK(prog_ret != 0, "test_run_ret",
	      "socket_filter did not return 0\n");

	close(tp_fd);

	err = bpf_prog_test_run(filter_fd, 1, test_skb, sizeof(test_skb), 0, 0,
				&prog_ret, 0);
	CHECK(err != 0, "test_run_notrace",
	      "test_run failed with %d errno %d\n", err, errno);
	CHECK(prog_ret != 0, "test_run_ret_notrace",
	      "socket_filter did not return 0\n");

out_filterfd:
	close(filter_fd);
out_bpffd:
	close(bpf_fd);
}
