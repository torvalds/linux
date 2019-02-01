{
	"jset32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	/* reg, high bits shouldn't be tested */
	BPF_JMP32_IMM(BPF_JSET, BPF_REG_7, -2, 1),
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	BPF_EXIT_INSN(),

	BPF_JMP32_IMM(BPF_JSET, BPF_REG_7, 1, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { 1ULL << 63, }
		},
		{ .retval = 2,
		  .data64 = { 1, }
		},
		{ .retval = 2,
		  .data64 = { 1ULL << 63 | 1, }
		},
	},
},
{
	"jset32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_LD_IMM64(BPF_REG_8, 0x8000000000000000),
	BPF_JMP32_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	BPF_EXIT_INSN(),

	BPF_LD_IMM64(BPF_REG_8, 0x8000000000000001),
	BPF_JMP32_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { 1ULL << 63, }
		},
		{ .retval = 2,
		  .data64 = { 1, }
		},
		{ .retval = 2,
		  .data64 = { 1ULL << 63 | 1, }
		},
	},
},
{
	"jset32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_JMP32_IMM(BPF_JSET, BPF_REG_7, 0x10, 1),
	BPF_EXIT_INSN(),
	BPF_JMP32_IMM(BPF_JGE, BPF_REG_7, 0x10, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"jeq32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JEQ, BPF_REG_7, -1, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 2,
	.retvals = {
		{ .retval = 0,
		  .data64 = { -2, }
		},
		{ .retval = 2,
		  .data64 = { -1, }
		},
	},
},
{
	"jeq32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_LD_IMM64(BPF_REG_8, 0x7000000000000001),
	BPF_JMP32_REG(BPF_JEQ, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { 2, }
		},
		{ .retval = 2,
		  .data64 = { 1, }
		},
		{ .retval = 2,
		  .data64 = { 1ULL << 63 | 1, }
		},
	},
},
{
	"jeq32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_JMP32_IMM(BPF_JEQ, BPF_REG_7, 0x10, 1),
	BPF_EXIT_INSN(),
	BPF_JMP32_IMM(BPF_JSGE, BPF_REG_7, 0xf, 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"jne32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JNE, BPF_REG_7, -1, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 2,
	.retvals = {
		{ .retval = 2,
		  .data64 = { 1, }
		},
		{ .retval = 0,
		  .data64 = { -1, }
		},
	},
},
{
	"jne32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_LD_IMM64(BPF_REG_8, 0x8000000000000001),
	BPF_JMP32_REG(BPF_JNE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { 1, }
		},
		{ .retval = 2,
		  .data64 = { 2, }
		},
		{ .retval = 2,
		  .data64 = { 1ULL << 63 | 2, }
		},
	},
},
{
	"jne32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_JMP32_IMM(BPF_JNE, BPF_REG_7, 0x10, 1),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0x10, 1),
	BPF_EXIT_INSN(),
	BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"jge32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JGE, BPF_REG_7, UINT_MAX - 1, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { UINT_MAX, }
		},
		{ .retval = 2,
		  .data64 = { UINT_MAX - 1, }
		},
		{ .retval = 0,
		  .data64 = { 0, }
		},
	},
},
{
	"jge32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, UINT_MAX | 1ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JGE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { UINT_MAX, }
		},
		{ .retval = 0,
		  .data64 = { INT_MAX, }
		},
		{ .retval = 0,
		  .data64 = { (UINT_MAX - 1) | 2ULL << 32, }
		},
	},
},
{
	"jge32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
	BPF_JMP32_REG(BPF_JGE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP32_IMM(BPF_JGE, BPF_REG_7, 0x7ffffff0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
{
	"jgt32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JGT, BPF_REG_7, UINT_MAX - 1, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { UINT_MAX, }
		},
		{ .retval = 0,
		  .data64 = { UINT_MAX - 1, }
		},
		{ .retval = 0,
		  .data64 = { 0, }
		},
	},
},
{
	"jgt32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, (UINT_MAX - 1) | 1ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JGT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { UINT_MAX, }
		},
		{ .retval = 0,
		  .data64 = { UINT_MAX - 1, }
		},
		{ .retval = 0,
		  .data64 = { (UINT_MAX - 1) | 2ULL << 32, }
		},
	},
},
{
	"jgt32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
	BPF_JMP32_REG(BPF_JGT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 0x7ffffff0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
{
	"jle32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JLE, BPF_REG_7, INT_MAX, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { INT_MAX - 1, }
		},
		{ .retval = 0,
		  .data64 = { UINT_MAX, }
		},
		{ .retval = 2,
		  .data64 = { INT_MAX, }
		},
	},
},
{
	"jle32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, (INT_MAX - 1) | 2ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JLE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { INT_MAX | 1ULL << 32, }
		},
		{ .retval = 2,
		  .data64 = { INT_MAX - 2, }
		},
		{ .retval = 0,
		  .data64 = { UINT_MAX, }
		},
	},
},
{
	"jle32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
	BPF_JMP32_REG(BPF_JLE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP32_IMM(BPF_JLE, BPF_REG_7, 0x7ffffff0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
{
	"jlt32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JLT, BPF_REG_7, INT_MAX, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { INT_MAX, }
		},
		{ .retval = 0,
		  .data64 = { UINT_MAX, }
		},
		{ .retval = 2,
		  .data64 = { INT_MAX - 1, }
		},
	},
},
{
	"jlt32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, INT_MAX | 2ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JLT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { INT_MAX | 1ULL << 32, }
		},
		{ .retval = 0,
		  .data64 = { UINT_MAX, }
		},
		{ .retval = 2,
		  .data64 = { (INT_MAX - 1) | 3ULL << 32, }
		},
	},
},
{
	"jlt32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
	BPF_JMP32_REG(BPF_JLT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JSLT, BPF_REG_7, 0x7ffffff0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
{
	"jsge32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JSGE, BPF_REG_7, -1, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { 0, }
		},
		{ .retval = 2,
		  .data64 = { -1, }
		},
		{ .retval = 0,
		  .data64 = { -2, }
		},
	},
},
{
	"jsge32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, (__u32)-1 | 2ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JSGE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { -1, }
		},
		{ .retval = 2,
		  .data64 = { 0x7fffffff | 1ULL << 32, }
		},
		{ .retval = 0,
		  .data64 = { -2, }
		},
	},
},
{
	"jsge32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
	BPF_JMP32_REG(BPF_JSGE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JSGE, BPF_REG_7, 0x7ffffff0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
{
	"jsgt32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JSGT, BPF_REG_7, -1, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { (__u32)-2, }
		},
		{ .retval = 0,
		  .data64 = { -1, }
		},
		{ .retval = 2,
		  .data64 = { 1, }
		},
	},
},
{
	"jsgt32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffffe | 1ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JSGT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 0,
		  .data64 = { 0x7ffffffe, }
		},
		{ .retval = 0,
		  .data64 = { 0x1ffffffffULL, }
		},
		{ .retval = 2,
		  .data64 = { 0x7fffffff, }
		},
	},
},
{
	"jsgt32: min/max deduction",
	.insns = {
	BPF_RAND_SEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, (__u32)(-2) | 1ULL << 32),
	BPF_JMP32_REG(BPF_JSGT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JSGT, BPF_REG_7, -2, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
{
	"jsle32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JSLE, BPF_REG_7, -1, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { (__u32)-2, }
		},
		{ .retval = 2,
		  .data64 = { -1, }
		},
		{ .retval = 0,
		  .data64 = { 1, }
		},
	},
},
{
	"jsle32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffffe | 1ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JSLE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { 0x7ffffffe, }
		},
		{ .retval = 2,
		  .data64 = { (__u32)-1, }
		},
		{ .retval = 0,
		  .data64 = { 0x7fffffff | 2ULL << 32, }
		},
	},
},
{
	"jsle32: min/max deduction",
	.insns = {
	BPF_RAND_UEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
	BPF_JMP32_REG(BPF_JSLE, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP_IMM(BPF_JSLE, BPF_REG_7, 0x7ffffff0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
{
	"jslt32: BPF_K",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_IMM(BPF_JSLT, BPF_REG_7, -1, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { (__u32)-2, }
		},
		{ .retval = 0,
		  .data64 = { -1, }
		},
		{ .retval = 0,
		  .data64 = { 1, }
		},
	},
},
{
	"jslt32: BPF_X",
	.insns = {
	BPF_DIRECT_PKT_R2,
	BPF_LD_IMM64(BPF_REG_8, 0x7fffffff | 1ULL << 32),
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
	BPF_JMP32_REG(BPF_JSLT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.runs = 3,
	.retvals = {
		{ .retval = 2,
		  .data64 = { 0x7ffffffe, }
		},
		{ .retval = 2,
		  .data64 = { 0xffffffff, }
		},
		{ .retval = 0,
		  .data64 = { 0x7fffffff | 2ULL << 32, }
		},
	},
},
{
	"jslt32: min/max deduction",
	.insns = {
	BPF_RAND_SEXT_R7,
	BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
	BPF_LD_IMM64(BPF_REG_8, (__u32)(-1) | 1ULL << 32),
	BPF_JMP32_REG(BPF_JSLT, BPF_REG_7, BPF_REG_8, 1),
	BPF_EXIT_INSN(),
	BPF_JMP32_IMM(BPF_JSLT, BPF_REG_7, -1, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 2,
},
