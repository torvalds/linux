// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

void serial_test_flow_dissector_load_bytes(void)
{
	struct bpf_flow_keys flow_keys;
	struct bpf_insn prog[] = {
		// BPF_REG_1 - 1st argument: context
		// BPF_REG_2 - 2nd argument: offset, start at first byte
		BPF_MOV64_IMM(BPF_REG_2, 0),
		// BPF_REG_3 - 3rd argument: destination, reserve byte on stack
		BPF_ALU64_REG(BPF_MOV, BPF_REG_3, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_3, -1),
		// BPF_REG_4 - 4th argument: copy one byte
		BPF_MOV64_IMM(BPF_REG_4, 1),
		// bpf_skb_load_bytes(ctx, sizeof(pkt_v4), ptr, 1)
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			     BPF_FUNC_skb_load_bytes),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
		// if (ret == 0) return BPF_DROP (2)
		BPF_MOV64_IMM(BPF_REG_0, BPF_DROP),
		BPF_EXIT_INSN(),
		// if (ret != 0) return BPF_OK (0)
		BPF_MOV64_IMM(BPF_REG_0, BPF_OK),
		BPF_EXIT_INSN(),
	};
	int fd, err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = &flow_keys,
		.data_size_out = sizeof(flow_keys),
		.repeat = 1,
	);

	/* make sure bpf_skb_load_bytes is not allowed from skb-less context
	 */
	fd = bpf_test_load_program(BPF_PROG_TYPE_FLOW_DISSECTOR, prog,
			      ARRAY_SIZE(prog), "GPL", 0, NULL, 0);
	ASSERT_GE(fd, 0, "bpf_test_load_program good fd");

	err = bpf_prog_test_run_opts(fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.data_size_out, sizeof(flow_keys),
		  "test_run data_size_out");
	ASSERT_EQ(topts.retval, BPF_OK, "test_run retval");

	if (fd >= -1)
		close(fd);
}
