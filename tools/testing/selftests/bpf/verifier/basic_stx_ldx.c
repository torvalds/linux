{
	"invalid src register in STX",
	.insns = {
	BPF_STX_MEM(BPF_B, BPF_REG_10, -1, -1),
	BPF_EXIT_INSN(),
	},
	.errstr = "R15 is invalid",
	.result = REJECT,
},
{
	"invalid dst register in STX",
	.insns = {
	BPF_STX_MEM(BPF_B, 14, BPF_REG_10, -1),
	BPF_EXIT_INSN(),
	},
	.errstr = "R14 is invalid",
	.result = REJECT,
},
{
	"invalid dst register in ST",
	.insns = {
	BPF_ST_MEM(BPF_B, 14, -1, -1),
	BPF_EXIT_INSN(),
	},
	.errstr = "R14 is invalid",
	.result = REJECT,
},
{
	"invalid src register in LDX",
	.insns = {
	BPF_LDX_MEM(BPF_B, BPF_REG_0, 12, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "R12 is invalid",
	.result = REJECT,
},
{
	"invalid dst register in LDX",
	.insns = {
	BPF_LDX_MEM(BPF_B, 11, BPF_REG_1, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "R11 is invalid",
	.result = REJECT,
},
