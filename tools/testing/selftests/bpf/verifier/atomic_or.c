{
	"BPF_ATOMIC OR without fetch",
	.insns = {
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* atomic_or(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_OR, BPF_REG_10, BPF_REG_1, -8),
		/* if (val != 0x111) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0x111, 2),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* r1 should not be clobbered, no BPF_FETCH flag */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x011, 1),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC OR with fetch",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 123),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* old = atomic_fetch_or(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 0x110) exit(3); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x111) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x111, 2),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (for fear of x86 JIT bug) */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 123, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC OR with fetch 32bit",
	.insns = {
		/* r0 = (s64) -1 */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 1),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0x110),
		/* old = atomic_fetch_or(&val, 0x011); */
		BPF_MOV32_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_W, BPF_OR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 0x110) exit(3); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x111) exit(2); */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x111, 2),
		BPF_MOV32_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (for fear of x86 JIT bug)
		 * It should be -1 so add 1 to get exit code.
		 */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_W atomic_fetch_or should zero top 32 bits",
	.insns = {
		/* r1 = U64_MAX; */
		BPF_MOV64_IMM(BPF_REG_1, 0),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 1),
		/* u64 val = r1; */
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8),
		/* r1 = (u32)atomic_fetch_or((u32 *)&val, 2); */
		BPF_MOV32_IMM(BPF_REG_1, 2),
		BPF_ATOMIC_OP(BPF_W, BPF_OR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* r2 = 0x00000000FFFFFFFF; */
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 32),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_2, 1),
		/* if (r2 != r1) exit(1); */
		BPF_JMP_REG(BPF_JEQ, BPF_REG_2, BPF_REG_1, 2),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
