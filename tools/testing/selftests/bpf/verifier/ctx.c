{
	"context stores via ST",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_ST_MEM(BPF_DW, BPF_REG_1, offsetof(struct __sk_buff, mark), 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "BPF_ST stores into R1 ctx is not allowed",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"context stores via XADD",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_RAW_INSN(BPF_STX | BPF_XADD | BPF_W, BPF_REG_1,
		     BPF_REG_0, offsetof(struct __sk_buff, mark), 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "BPF_XADD stores into R1 ctx is not allowed",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
