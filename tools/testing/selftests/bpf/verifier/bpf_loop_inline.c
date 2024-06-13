#define BTF_TYPES \
	.btf_strings = "\0int\0i\0ctx\0callback\0main\0", \
	.btf_types = { \
	/* 1: int   */ BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4), \
	/* 2: int*  */ BTF_PTR_ENC(1), \
	/* 3: void* */ BTF_PTR_ENC(0), \
	/* 4: int __(void*) */ BTF_FUNC_PROTO_ENC(1, 1), \
		BTF_FUNC_PROTO_ARG_ENC(7, 3), \
	/* 5: int __(int, int*) */ BTF_FUNC_PROTO_ENC(1, 2), \
		BTF_FUNC_PROTO_ARG_ENC(5, 1), \
		BTF_FUNC_PROTO_ARG_ENC(7, 2), \
	/* 6: main      */ BTF_FUNC_ENC(20, 4), \
	/* 7: callback  */ BTF_FUNC_ENC(11, 5), \
	BTF_END_RAW \
	}

#define MAIN_TYPE	6
#define CALLBACK_TYPE	7

/* can't use BPF_CALL_REL, jit_subprogs adjusts IMM & OFF
 * fields for pseudo calls
 */
#define PSEUDO_CALL_INSN() \
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_CALL, \
		     INSN_OFF_MASK, INSN_IMM_MASK)

/* can't use BPF_FUNC_loop constant,
 * do_mix_fixups adjusts the IMM field
 */
#define HELPER_CALL_INSN() \
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, INSN_OFF_MASK, INSN_IMM_MASK)

{
	"inline simple bpf_loop call",
	.insns = {
	/* main */
	/* force verifier state branching to verify logic on first and
	 * subsequent bpf_loop insn processing steps
	 */
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 777, 2),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1),
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 2),

	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 6),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	/* callback */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.expected_insns = { PSEUDO_CALL_INSN() },
	.unexpected_insns = { HELPER_CALL_INSN() },
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.result = ACCEPT,
	.runs = 0,
	.func_info = { { 0, MAIN_TYPE }, { 12, CALLBACK_TYPE } },
	.func_info_cnt = 2,
	BTF_TYPES
},
{
	"don't inline bpf_loop call, flags non-zero",
	.insns = {
	/* main */
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
	BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
	BPF_ALU64_REG(BPF_MOV, BPF_REG_7, BPF_REG_0),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 0, 9),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 7),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 1),
	BPF_JMP_IMM(BPF_JA, 0, 0, -10),
	/* callback */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.expected_insns = { HELPER_CALL_INSN() },
	.unexpected_insns = { PSEUDO_CALL_INSN() },
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.result = ACCEPT,
	.runs = 0,
	.func_info = { { 0, MAIN_TYPE }, { 16, CALLBACK_TYPE } },
	.func_info_cnt = 2,
	BTF_TYPES
},
{
	"don't inline bpf_loop call, callback non-constant",
	.insns = {
	/* main */
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 777, 4), /* pick a random callback */

	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 10),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_JMP_IMM(BPF_JA, 0, 0, 3),

	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 8),
	BPF_RAW_INSN(0, 0, 0, 0, 0),

	BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	/* callback */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	/* callback #2 */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.expected_insns = { HELPER_CALL_INSN() },
	.unexpected_insns = { PSEUDO_CALL_INSN() },
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.result = ACCEPT,
	.runs = 0,
	.func_info = {
		{ 0, MAIN_TYPE },
		{ 14, CALLBACK_TYPE },
		{ 16, CALLBACK_TYPE }
	},
	.func_info_cnt = 3,
	BTF_TYPES
},
{
	"bpf_loop_inline and a dead func",
	.insns = {
	/* main */

	/* A reference to callback #1 to make verifier count it as a func.
	 * This reference is overwritten below and callback #1 is dead.
	 */
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 9),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 8),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	/* callback */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	/* callback #2 */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.expected_insns = { PSEUDO_CALL_INSN() },
	.unexpected_insns = { HELPER_CALL_INSN() },
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.result = ACCEPT,
	.runs = 0,
	.func_info = {
		{ 0, MAIN_TYPE },
		{ 10, CALLBACK_TYPE },
		{ 12, CALLBACK_TYPE }
	},
	.func_info_cnt = 3,
	BTF_TYPES
},
{
	"bpf_loop_inline stack locations for loop vars",
	.insns = {
	/* main */
	BPF_ST_MEM(BPF_W, BPF_REG_10, -12, 0x77),
	/* bpf_loop call #1 */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 22),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop),
	/* bpf_loop call #2 */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 2),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 16),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop),
	/* call func and exit */
	BPF_CALL_REL(2),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	/* func */
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -32, 0x55),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 2),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, 6),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	/* callback */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.expected_insns = {
	BPF_ST_MEM(BPF_W, BPF_REG_10, -12, 0x77),
	SKIP_INSNS(),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -40),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_7, -32),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_8, -24),
	SKIP_INSNS(),
	/* offsets are the same as in the first call */
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -40),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_7, -32),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_8, -24),
	SKIP_INSNS(),
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -32, 0x55),
	SKIP_INSNS(),
	/* offsets differ from main because of different offset
	 * in BPF_ST_MEM instruction
	 */
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -56),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_7, -48),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_8, -40),
	},
	.unexpected_insns = { HELPER_CALL_INSN() },
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.result = ACCEPT,
	.func_info = {
		{ 0, MAIN_TYPE },
		{ 16, MAIN_TYPE },
		{ 25, CALLBACK_TYPE },
	},
	.func_info_cnt = 3,
	BTF_TYPES
},
{
	"inline bpf_loop call in a big program",
	.insns = {},
	.fill_helper = bpf_fill_big_prog_with_loop_1,
	.expected_insns = { PSEUDO_CALL_INSN() },
	.unexpected_insns = { HELPER_CALL_INSN() },
	.result = ACCEPT,
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info = { { 0, MAIN_TYPE }, { 16, CALLBACK_TYPE } },
	.func_info_cnt = 2,
	BTF_TYPES
},

#undef HELPER_CALL_INSN
#undef PSEUDO_CALL_INSN
#undef CALLBACK_TYPE
#undef MAIN_TYPE
#undef BTF_TYPES
