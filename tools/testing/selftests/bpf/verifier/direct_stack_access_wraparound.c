{
	"direct stack access with 32-bit wraparound. test1",
	.insns = {
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x7fffffff),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x7fffffff),
	BPF_MOV32_IMM(BPF_REG_0, 0),
	BPF_STX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "fp pointer and 2147483647",
	.result = REJECT
},
{
	"direct stack access with 32-bit wraparound. test2",
	.insns = {
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x3fffffff),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x3fffffff),
	BPF_MOV32_IMM(BPF_REG_0, 0),
	BPF_STX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "fp pointer and 1073741823",
	.result = REJECT
},
{
	"direct stack access with 32-bit wraparound. test3",
	.insns = {
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x1fffffff),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x1fffffff),
	BPF_MOV32_IMM(BPF_REG_0, 0),
	BPF_STX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "fp pointer offset 1073741822",
	.errstr_unpriv = "R1 stack pointer arithmetic goes out of range",
	.result = REJECT
},
