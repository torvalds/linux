{
	"pointer/scalar confusion in state equality check (way 1)",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_JMP_A(1),
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_10),
	BPF_JMP_A(0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT,
	.retval = POINTER_VALUE,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R0 leaks addr as return value"
},
{
	"pointer/scalar confusion in state equality check (way 2)",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_10),
	BPF_JMP_A(1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT,
	.retval = POINTER_VALUE,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R0 leaks addr as return value"
},
{
	"liveness pruning and write screening",
	.insns = {
	/* Get an unknown value */
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1, 0),
	/* branch conditions teach us nothing about R2 */
	BPF_JMP_IMM(BPF_JGE, BPF_REG_2, 0, 1),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_JMP_IMM(BPF_JGE, BPF_REG_2, 0, 1),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "R0 !read_ok",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_LWT_IN,
},
{
	"varlen_map_value_access pruning",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),
	BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0),
	BPF_MOV32_IMM(BPF_REG_2, MAX_ENTRIES),
	BPF_JMP_REG(BPF_JSGT, BPF_REG_2, BPF_REG_1, 1),
	BPF_MOV32_IMM(BPF_REG_1, 0),
	BPF_ALU32_IMM(BPF_LSH, BPF_REG_1, 2),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	BPF_JMP_IMM(BPF_JA, 0, 0, 0),
	BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, offsetof(struct test_val, foo)),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_48b = { 3 },
	.errstr_unpriv = "R0 leaks addr",
	.errstr = "R0 unbounded memory access",
	.result_unpriv = REJECT,
	.result = REJECT,
	.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
},
{
	"search pruning: all branches should be verified (nop operation)",
	.insns = {
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
		BPF_LD_MAP_FD(BPF_REG_1, 0),
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 11),
		BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_3, 0xbeef, 2),
		BPF_MOV64_IMM(BPF_REG_4, 0),
		BPF_JMP_A(1),
		BPF_MOV64_IMM(BPF_REG_4, 1),
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_4, -16),
		BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
		BPF_LDX_MEM(BPF_DW, BPF_REG_5, BPF_REG_10, -16),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_5, 0, 2),
		BPF_MOV64_IMM(BPF_REG_6, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_6, 0, 0xdead),
		BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "R6 invalid mem access 'scalar'",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
},
{
	"search pruning: all branches should be verified (invalid stack access)",
	.insns = {
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
		BPF_LD_MAP_FD(BPF_REG_1, 0),
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),
		BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0),
		BPF_MOV64_IMM(BPF_REG_4, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_3, 0xbeef, 2),
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_4, -16),
		BPF_JMP_A(1),
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_4, -24),
		BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
		BPF_LDX_MEM(BPF_DW, BPF_REG_5, BPF_REG_10, -16),
		BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "invalid read from stack off -16+0 size 8",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
},
{
	"precision tracking for u32 spill/fill",
	.insns = {
		BPF_MOV64_REG(BPF_REG_7, BPF_REG_1),
		BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),
		BPF_MOV32_IMM(BPF_REG_6, 32),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_MOV32_IMM(BPF_REG_6, 4),
		/* Additional insns to introduce a pruning point. */
		BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),
		BPF_MOV64_IMM(BPF_REG_3, 0),
		BPF_MOV64_IMM(BPF_REG_3, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_MOV64_IMM(BPF_REG_3, 0),
		/* u32 spill/fill */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_6, -8),
		BPF_LDX_MEM(BPF_W, BPF_REG_8, BPF_REG_10, -8),
		/* out-of-bound map value access for r6=32 */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -16, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
		BPF_LD_MAP_FD(BPF_REG_1, 0),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_8),
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_0, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 15 },
	.result = REJECT,
	.errstr = "R0 min value is outside of the allowed memory range",
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
},
{
	"precision tracking for u32 spills, u64 fill",
	.insns = {
		BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
		BPF_MOV32_IMM(BPF_REG_7, 0xffffffff),
		/* Additional insns to introduce a pruning point. */
		BPF_MOV64_IMM(BPF_REG_3, 1),
		BPF_MOV64_IMM(BPF_REG_3, 1),
		BPF_MOV64_IMM(BPF_REG_3, 1),
		BPF_MOV64_IMM(BPF_REG_3, 1),
		BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_MOV64_IMM(BPF_REG_3, 1),
		BPF_ALU32_IMM(BPF_DIV, BPF_REG_3, 0),
		/* u32 spills, u64 fill */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_6, -4),
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, -8),
		BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_10, -8),
		/* if r8 != X goto pc+1  r8 known in fallthrough branch */
		BPF_JMP_IMM(BPF_JNE, BPF_REG_8, 0xffffffff, 1),
		BPF_MOV64_IMM(BPF_REG_3, 1),
		/* if r8 == X goto pc+1  condition always true on first
		 * traversal, so starts backtracking to mark r8 as requiring
		 * precision. r7 marked as needing precision. r6 not marked
		 * since it's not tracked.
		 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_8, 0xffffffff, 1),
		/* fails if r8 correctly marked unknown after fill. */
		BPF_ALU32_IMM(BPF_DIV, BPF_REG_3, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "div by zero",
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
},
{
	"allocated_stack",
	.insns = {
		BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
		BPF_ALU64_REG(BPF_MOV, BPF_REG_7, BPF_REG_0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
		BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_10, -8),
		BPF_STX_MEM(BPF_B, BPF_REG_10, BPF_REG_7, -9),
		BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_10, -9),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.result_unpriv = ACCEPT,
	.insn_processed = 15,
},
/* The test performs a conditional 64-bit write to a stack location
 * fp[-8], this is followed by an unconditional 8-bit write to fp[-8],
 * then data is read from fp[-8]. This sequence is unsafe.
 *
 * The test would be mistakenly marked as safe w/o dst register parent
 * preservation in verifier.c:copy_register_state() function.
 *
 * Note the usage of BPF_F_TEST_STATE_FREQ to force creation of the
 * checkpoint state after conditional 64-bit assignment.
 */
{
	"write tracking and register parent chain bug",
	.insns = {
	/* r6 = ktime_get_ns() */
	BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
	/* r0 = ktime_get_ns() */
	BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
	/* if r0 > r6 goto +1 */
	BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_6, 1),
	/* *(u64 *)(r10 - 8) = 0xdeadbeef */
	BPF_ST_MEM(BPF_DW, BPF_REG_FP, -8, 0xdeadbeef),
	/* r1 = 42 */
	BPF_MOV64_IMM(BPF_REG_1, 42),
	/* *(u8 *)(r10 - 8) = r1 */
	BPF_STX_MEM(BPF_B, BPF_REG_FP, BPF_REG_1, -8),
	/* r2 = *(u64 *)(r10 - 8) */
	BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_FP, -8),
	/* exit(0) */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.flags = BPF_F_TEST_STATE_FREQ,
	.errstr = "invalid read from stack off -8+1 size 8",
	.result = REJECT,
},
