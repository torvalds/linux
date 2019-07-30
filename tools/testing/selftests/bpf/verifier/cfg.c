{
	"unreachable",
	.insns = {
	BPF_EXIT_INSN(),
	BPF_EXIT_INSN(),
	},
	.errstr = "unreachable",
	.result = REJECT,
},
{
	"unreachable2",
	.insns = {
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	BPF_JMP_IMM(BPF_JA, 0, 0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "unreachable",
	.result = REJECT,
},
{
	"out of range jump",
	.insns = {
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "jump out of range",
	.result = REJECT,
},
{
	"out of range jump2",
	.insns = {
	BPF_JMP_IMM(BPF_JA, 0, 0, -2),
	BPF_EXIT_INSN(),
	},
	.errstr = "jump out of range",
	.result = REJECT,
},
{
	"loop (back-edge)",
	.insns = {
	BPF_JMP_IMM(BPF_JA, 0, 0, -1),
	BPF_EXIT_INSN(),
	},
	.errstr = "back-edge",
	.result = REJECT,
},
{
	"loop2 (back-edge)",
	.insns = {
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_0),
	BPF_MOV64_REG(BPF_REG_3, BPF_REG_0),
	BPF_JMP_IMM(BPF_JA, 0, 0, -4),
	BPF_EXIT_INSN(),
	},
	.errstr = "back-edge",
	.result = REJECT,
},
{
	"conditional loop",
	.insns = {
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_0),
	BPF_MOV64_REG(BPF_REG_3, BPF_REG_0),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, -3),
	BPF_EXIT_INSN(),
	},
	.errstr = "back-edge",
	.result = REJECT,
},
