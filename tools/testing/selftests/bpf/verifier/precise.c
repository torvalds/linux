{
	"precise: test 1",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 1),
	BPF_LD_MAP_FD(BPF_REG_6, 0),
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_ST_MEM(BPF_DW, BPF_REG_FP, -8, 0),
	BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),

	BPF_MOV64_REG(BPF_REG_9, BPF_REG_0),

	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),

	BPF_MOV64_REG(BPF_REG_8, BPF_REG_0),

	BPF_ALU64_REG(BPF_SUB, BPF_REG_9, BPF_REG_8), /* map_value_ptr -= map_value_ptr */
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_9),
	BPF_JMP_IMM(BPF_JLT, BPF_REG_2, 8, 1),
	BPF_EXIT_INSN(),

	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1), /* R2=scalar(umin=1, umax=8) */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_EMIT_CALL(BPF_FUNC_probe_read_kernel),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.fixup_map_array_48b = { 1 },
	.result = VERBOSE_ACCEPT,
	.errstr =
	"mark_precise: frame0: last_idx 26 first_idx 20\
	mark_precise: frame0: regs=r2 stack= before 25\
	mark_precise: frame0: regs=r2 stack= before 24\
	mark_precise: frame0: regs=r2 stack= before 23\
	mark_precise: frame0: regs=r2 stack= before 22\
	mark_precise: frame0: regs=r2 stack= before 20\
	mark_precise: frame0: parent state regs=r2 stack=:\
	mark_precise: frame0: last_idx 19 first_idx 10\
	mark_precise: frame0: regs=r2,r9 stack= before 19\
	mark_precise: frame0: regs=r9 stack= before 18\
	mark_precise: frame0: regs=r8,r9 stack= before 17\
	mark_precise: frame0: regs=r0,r9 stack= before 15\
	mark_precise: frame0: regs=r0,r9 stack= before 14\
	mark_precise: frame0: regs=r9 stack= before 13\
	mark_precise: frame0: regs=r9 stack= before 12\
	mark_precise: frame0: regs=r9 stack= before 11\
	mark_precise: frame0: regs=r9 stack= before 10\
	mark_precise: frame0: parent state regs= stack=:",
},
{
	"precise: test 2",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 1),
	BPF_LD_MAP_FD(BPF_REG_6, 0),
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_ST_MEM(BPF_DW, BPF_REG_FP, -8, 0),
	BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),

	BPF_MOV64_REG(BPF_REG_9, BPF_REG_0),

	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),

	BPF_MOV64_REG(BPF_REG_8, BPF_REG_0),

	BPF_ALU64_REG(BPF_SUB, BPF_REG_9, BPF_REG_8), /* map_value_ptr -= map_value_ptr */
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_9),
	BPF_JMP_IMM(BPF_JLT, BPF_REG_2, 8, 1),
	BPF_EXIT_INSN(),

	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1), /* R2=scalar(umin=1, umax=8) */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_EMIT_CALL(BPF_FUNC_probe_read_kernel),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.fixup_map_array_48b = { 1 },
	.result = VERBOSE_ACCEPT,
	.flags = BPF_F_TEST_STATE_FREQ,
	.errstr =
	"26: (85) call bpf_probe_read_kernel#113\
	mark_precise: frame0: last_idx 26 first_idx 22\
	mark_precise: frame0: regs=r2 stack= before 25\
	mark_precise: frame0: regs=r2 stack= before 24\
	mark_precise: frame0: regs=r2 stack= before 23\
	mark_precise: frame0: regs=r2 stack= before 22\
	mark_precise: frame0: parent state regs=r2 stack=:\
	mark_precise: frame0: last_idx 20 first_idx 20\
	mark_precise: frame0: regs=r2,r9 stack= before 20\
	mark_precise: frame0: parent state regs=r2,r9 stack=:\
	mark_precise: frame0: last_idx 19 first_idx 17\
	mark_precise: frame0: regs=r2,r9 stack= before 19\
	mark_precise: frame0: regs=r9 stack= before 18\
	mark_precise: frame0: regs=r8,r9 stack= before 17\
	mark_precise: frame0: parent state regs= stack=:",
},
{
	"precise: cross frame pruning",
	.insns = {
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
	BPF_MOV64_IMM(BPF_REG_8, 0),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_MOV64_IMM(BPF_REG_8, 1),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
	BPF_MOV64_IMM(BPF_REG_9, 0),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_MOV64_IMM(BPF_REG_9, 1),
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_8, 1, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_2, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_XDP,
	.flags = BPF_F_TEST_STATE_FREQ,
	.errstr = "!read_ok",
	.result = REJECT,
},
{
	"precise: ST insn causing spi > allocated_stack",
	.insns = {
	BPF_MOV64_REG(BPF_REG_3, BPF_REG_10),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_3, 123, 0),
	BPF_ST_MEM(BPF_DW, BPF_REG_3, -8, 0),
	BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
	BPF_MOV64_IMM(BPF_REG_0, -1),
	BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_XDP,
	.flags = BPF_F_TEST_STATE_FREQ,
	.errstr = "mark_precise: frame0: last_idx 5 first_idx 5\
	mark_precise: frame0: parent state regs=r4 stack=:\
	mark_precise: frame0: last_idx 4 first_idx 2\
	mark_precise: frame0: regs=r4 stack= before 4\
	mark_precise: frame0: regs=r4 stack= before 3\
	mark_precise: frame0: regs= stack=-8 before 2\
	mark_precise: frame0: falling back to forcing all scalars precise\
	force_precise: frame0: forcing r0 to be precise\
	mark_precise: frame0: last_idx 5 first_idx 5\
	mark_precise: frame0: parent state regs= stack=:",
	.result = VERBOSE_ACCEPT,
	.retval = -1,
},
{
	"precise: STX insn causing spi > allocated_stack",
	.insns = {
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
	BPF_MOV64_REG(BPF_REG_3, BPF_REG_10),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_3, 123, 0),
	BPF_STX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, -8),
	BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
	BPF_MOV64_IMM(BPF_REG_0, -1),
	BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_XDP,
	.flags = BPF_F_TEST_STATE_FREQ,
	.errstr = "mark_precise: frame0: last_idx 6 first_idx 6\
	mark_precise: frame0: parent state regs=r4 stack=:\
	mark_precise: frame0: last_idx 5 first_idx 3\
	mark_precise: frame0: regs=r4 stack= before 5\
	mark_precise: frame0: regs=r4 stack= before 4\
	mark_precise: frame0: regs= stack=-8 before 3\
	mark_precise: frame0: falling back to forcing all scalars precise\
	force_precise: frame0: forcing r0 to be precise\
	force_precise: frame0: forcing r0 to be precise\
	force_precise: frame0: forcing r0 to be precise\
	force_precise: frame0: forcing r0 to be precise\
	mark_precise: frame0: last_idx 6 first_idx 6\
	mark_precise: frame0: parent state regs= stack=:",
	.result = VERBOSE_ACCEPT,
	.retval = -1,
},
{
	"precise: mark_chain_precision for ARG_CONST_ALLOC_SIZE_OR_ZERO",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_1, offsetof(struct xdp_md, ingress_ifindex)),
	BPF_LD_MAP_FD(BPF_REG_6, 0),
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_MOV64_IMM(BPF_REG_2, 1),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_4, 0, 1),
	BPF_MOV64_IMM(BPF_REG_2, 0x1000),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
	BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_0, 42),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_ringbuf = { 1 },
	.prog_type = BPF_PROG_TYPE_XDP,
	.flags = BPF_F_TEST_STATE_FREQ | F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	.errstr = "invalid access to memory, mem_size=1 off=42 size=8",
	.result = REJECT,
},
{
	"precise: program doesn't prematurely prune branches",
	.insns = {
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_6, 0x400),
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_7, 0),
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_8, 0),
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_9, 0x80000000),
		BPF_ALU64_IMM(BPF_MOD, BPF_REG_6, 0x401),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0),
		BPF_JMP_REG(BPF_JLE, BPF_REG_6, BPF_REG_9, 2),
		BPF_ALU64_IMM(BPF_MOD, BPF_REG_6, 1),
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_9, 0),
		BPF_JMP_REG(BPF_JLE, BPF_REG_6, BPF_REG_9, 1),
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_6, 0),
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -4),
		BPF_LD_MAP_FD(BPF_REG_4, 0),
		BPF_ALU64_REG(BPF_MOV, BPF_REG_1, BPF_REG_4),
		BPF_ALU64_REG(BPF_MOV, BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
		BPF_EXIT_INSN(),
		BPF_ALU64_IMM(BPF_RSH, BPF_REG_6, 10),
		BPF_ALU64_IMM(BPF_MUL, BPF_REG_6, 8192),
		BPF_ALU64_REG(BPF_MOV, BPF_REG_1, BPF_REG_0),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_6),
		BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0),
		BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_3, 0),
		BPF_EXIT_INSN(),
	},
	.fixup_map_array_48b = { 13 },
	.prog_type = BPF_PROG_TYPE_XDP,
	.result = REJECT,
	.errstr = "register with unbounded min value is not allowed",
},
