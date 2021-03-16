{
	"stack out of bounds",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, 8, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid write to stack",
	.result = REJECT,
},
{
	"uninitialized stack1",
	.insns = {
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 2 },
	.errstr = "invalid indirect read from stack",
	.result = REJECT,
},
{
	"uninitialized stack2",
	.insns = {
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_2, -8),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid read from stack",
	.result = REJECT,
},
{
	"invalid fp arithmetic",
	/* If this gets ever changed, make sure JITs can deal with it. */
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 8),
	BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "R1 subtraction from stack pointer",
	.result = REJECT,
},
{
	"non-invalid fp arithmetic",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"misaligned read from stack",
	.insns = {
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_2, -4),
	BPF_EXIT_INSN(),
	},
	.errstr = "misaligned stack access",
	.result = REJECT,
},
