{
	"bpf_exit with invalid return code. test1",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "R0 has value (0x0; 0xffffffff)",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
},
{
	"bpf_exit with invalid return code. test2",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
	BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
},
{
	"bpf_exit with invalid return code. test3",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
	BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 3),
	BPF_EXIT_INSN(),
	},
	.errstr = "R0 has value (0x0; 0x3)",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
},
{
	"bpf_exit with invalid return code. test4",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
},
{
	"bpf_exit with invalid return code. test5",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.errstr = "R0 has value (0x2; 0x0)",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
},
{
	"bpf_exit with invalid return code. test6",
	.insns = {
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
	BPF_EXIT_INSN(),
	},
	.errstr = "R0 is not a known value (ctx)",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
},
{
	"bpf_exit with invalid return code. test7",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1, 4),
	BPF_ALU64_REG(BPF_MUL, BPF_REG_0, BPF_REG_2),
	BPF_EXIT_INSN(),
	},
	.errstr = "R0 has unknown scalar value",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
},
