{
	"BPF_ST_MEM stack imm non-zero",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 42),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, -42),
	/* if value is tracked correctly R0 is zero */
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	/* Use prog type that requires return value in range [0, 1] */
	.prog_type = BPF_PROG_TYPE_SK_LOOKUP,
	.expected_attach_type = BPF_SK_LOOKUP,
	.runs = -1,
},
{
	"BPF_ST_MEM stack imm zero",
	.insns = {
	/* mark stack 0000 0000 */
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	/* read and sum a few bytes */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_10, -8),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_10, -4),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_10, -1),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* if value is tracked correctly R0 is zero */
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	/* Use prog type that requires return value in range [0, 1] */
	.prog_type = BPF_PROG_TYPE_SK_LOOKUP,
	.expected_attach_type = BPF_SK_LOOKUP,
	.runs = -1,
},
