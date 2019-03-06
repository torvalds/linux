/* Just make sure that JITs used udiv/umod as otherwise we get
 * an exception from INT_MIN/-1 overflow similarly as with div
 * by zero.
 */
{
	"DIV32 overflow, check 1",
	.insns = {
	BPF_MOV32_IMM(BPF_REG_1, -1),
	BPF_MOV32_IMM(BPF_REG_0, INT_MIN),
	BPF_ALU32_REG(BPF_DIV, BPF_REG_0, BPF_REG_1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = 0,
},
{
	"DIV32 overflow, check 2",
	.insns = {
	BPF_MOV32_IMM(BPF_REG_0, INT_MIN),
	BPF_ALU32_IMM(BPF_DIV, BPF_REG_0, -1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = 0,
},
{
	"DIV64 overflow, check 1",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_1, -1),
	BPF_LD_IMM64(BPF_REG_0, LLONG_MIN),
	BPF_ALU64_REG(BPF_DIV, BPF_REG_0, BPF_REG_1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = 0,
},
{
	"DIV64 overflow, check 2",
	.insns = {
	BPF_LD_IMM64(BPF_REG_0, LLONG_MIN),
	BPF_ALU64_IMM(BPF_DIV, BPF_REG_0, -1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = 0,
},
{
	"MOD32 overflow, check 1",
	.insns = {
	BPF_MOV32_IMM(BPF_REG_1, -1),
	BPF_MOV32_IMM(BPF_REG_0, INT_MIN),
	BPF_ALU32_REG(BPF_MOD, BPF_REG_0, BPF_REG_1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = INT_MIN,
},
{
	"MOD32 overflow, check 2",
	.insns = {
	BPF_MOV32_IMM(BPF_REG_0, INT_MIN),
	BPF_ALU32_IMM(BPF_MOD, BPF_REG_0, -1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = INT_MIN,
},
{
	"MOD64 overflow, check 1",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_1, -1),
	BPF_LD_IMM64(BPF_REG_2, LLONG_MIN),
	BPF_MOV64_REG(BPF_REG_3, BPF_REG_2),
	BPF_ALU64_REG(BPF_MOD, BPF_REG_2, BPF_REG_1),
	BPF_MOV32_IMM(BPF_REG_0, 0),
	BPF_JMP_REG(BPF_JNE, BPF_REG_3, BPF_REG_2, 1),
	BPF_MOV32_IMM(BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = 1,
},
{
	"MOD64 overflow, check 2",
	.insns = {
	BPF_LD_IMM64(BPF_REG_2, LLONG_MIN),
	BPF_MOV64_REG(BPF_REG_3, BPF_REG_2),
	BPF_ALU64_IMM(BPF_MOD, BPF_REG_2, -1),
	BPF_MOV32_IMM(BPF_REG_0, 0),
	BPF_JMP_REG(BPF_JNE, BPF_REG_3, BPF_REG_2, 1),
	BPF_MOV32_IMM(BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.retval = 1,
},
