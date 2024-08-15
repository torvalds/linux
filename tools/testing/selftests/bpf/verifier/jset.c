{
	"jset: functional",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),

	/* reg, bit 63 or bit 0 set, taken */
	BPF_LD_IMM64(BPF_REG_8, 0x8000000000000001),
	BPF_JMP_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),

	/* reg, bit 62, not taken */
	BPF_LD_IMM64(BPF_REG_8, 0x4000000000000000),
	BPF_JMP_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	BPF_EXIT_INSN(),

	/* imm, any bit set, taken */
	BPF_JMP_IMM(BPF_JSET, BPF_REG_7, -1, 1),
	BPF_EXIT_INSN(),

	/* imm, bit 31 set, taken */
	BPF_JMP_IMM(BPF_JSET, BPF_REG_7, 0x80000000, 1),
	BPF_EXIT_INSN(),

	/* all good - return r0 == 2 */
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 7,
	.retvals = {
		{ .retval = 2,
		  .data64 = { (1ULL << 63) | (1U << 31) | (1U << 0), }
		},
		{ .retval = 2,
		  .data64 = { (1ULL << 63) | (1U << 31), }
		},
		{ .retval = 2,
		  .data64 = { (1ULL << 31) | (1U << 0), }
		},
		{ .retval = 2,
		  .data64 = { (__u32)-1, }
		},
		{ .retval = 2,
		  .data64 = { ~0x4000000000000000ULL, }
		},
		{ .retval = 0,
		  .data64 = { 0, }
		},
		{ .retval = 0,
		  .data64 = { ~0ULL, }
		},
	},
	.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
},
{
	"jset: sign-extend",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),

	BPF_JMP_IMM(BPF_JSET, BPF_REG_7, 0x80000000, 1),
	BPF_EXIT_INSN(),

	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = 2,
	.data = { 1, 0, 0, 0, 0, 0, 0, 1, },
	.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
},
{
	"jset: known const compare",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 1),
	BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
	.errstr_unpriv = "R9 !read_ok",
	.result_unpriv = REJECT,
	.retval = 1,
	.result = ACCEPT,
},
{
	"jset: known const compare bad",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
	.errstr_unpriv = "!read_ok",
	.result_unpriv = REJECT,
	.errstr = "!read_ok",
	.result = REJECT,
},
{
	"jset: unknown const compare taken",
	.insns = {
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
	BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
	.errstr_unpriv = "!read_ok",
	.result_unpriv = REJECT,
	.errstr = "!read_ok",
	.result = REJECT,
},
{
	"jset: unknown const compare not taken",
	.insns = {
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
	BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
	.errstr_unpriv = "!read_ok",
	.result_unpriv = REJECT,
	.errstr = "!read_ok",
	.result = REJECT,
},
{
	"jset: half-known const compare",
	.insns = {
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
	BPF_ALU64_IMM(BPF_OR, BPF_REG_0, 2),
	BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 3, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
	.errstr_unpriv = "R9 !read_ok",
	.result_unpriv = REJECT,
	.result = ACCEPT,
},
{
	"jset: range",
	.insns = {
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_ALU64_IMM(BPF_AND, BPF_REG_1, 0xff),
	BPF_JMP_IMM(BPF_JSET, BPF_REG_1, 0xf0, 3),
	BPF_JMP_IMM(BPF_JLT, BPF_REG_1, 0x10, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JSET, BPF_REG_1, 0x10, 1),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JGE, BPF_REG_1, 0x10, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
	.errstr_unpriv = "R9 !read_ok",
	.result_unpriv = REJECT,
	.result = ACCEPT,
},
