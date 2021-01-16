{
	"atomic compare-and-exchange smoketest - 64bit",
	.insns = {
		/* val = 3; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		/* old = atomic_cmpxchg(&val, 2, 4); */
		BPF_MOV64_IMM(BPF_REG_1, 4),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 3) exit(2); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* if (val != 3) exit(3); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* old = atomic_cmpxchg(&val, 3, 4); */
		BPF_MOV64_IMM(BPF_REG_1, 4),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 3) exit(4); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 4),
		BPF_EXIT_INSN(),
		/* if (val != 4) exit(5); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 4, 2),
		BPF_MOV64_IMM(BPF_REG_0, 5),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"atomic compare-and-exchange smoketest - 32bit",
	.insns = {
		/* val = 3; */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 3),
		/* old = atomic_cmpxchg(&val, 2, 4); */
		BPF_MOV32_IMM(BPF_REG_1, 4),
		BPF_MOV32_IMM(BPF_REG_0, 2),
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 3) exit(2); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV32_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* if (val != 3) exit(3); */
		BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* old = atomic_cmpxchg(&val, 3, 4); */
		BPF_MOV32_IMM(BPF_REG_1, 4),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 3) exit(4); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV32_IMM(BPF_REG_0, 4),
		BPF_EXIT_INSN(),
		/* if (val != 4) exit(5); */
		BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 4, 2),
		BPF_MOV32_IMM(BPF_REG_0, 5),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"Can't use cmpxchg on uninit src reg",
	.insns = {
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_2, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "!read_ok",
},
{
	"Can't use cmpxchg on uninit memory",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_MOV64_IMM(BPF_REG_2, 4),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_2, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "invalid read from stack",
},
