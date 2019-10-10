// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_flow_dissector_load_bytes(void)
{
	struct bpf_flow_keys flow_keys;
	__u32 duration = 0, retval, size;
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

	/* make sure bpf_skb_load_bytes is not allowed from skb-less context
	 */
	fd = bpf_load_program(BPF_PROG_TYPE_FLOW_DISSECTOR, prog,
			      ARRAY_SIZE(prog), "GPL", 0, NULL, 0);
	CHECK(fd < 0,
	      "flow_dissector-bpf_skb_load_bytes-load",
	      "fd %d errno %d\n",
	      fd, errno);

	err = bpf_prog_test_run(fd, 1, &pkt_v4, sizeof(pkt_v4),
				&flow_keys, &size, &retval, &duration);
	CHECK(size != sizeof(flow_keys) || err || retval != 1,
	      "flow_dissector-bpf_skb_load_bytes",
	      "err %d errno %d retval %d duration %d size %u/%zu\n",
	      err, errno, retval, duration, size, sizeof(flow_keys));

	if (fd >= -1)
		close(fd);
}
