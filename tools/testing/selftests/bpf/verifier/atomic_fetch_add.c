{
	"BPF_ATOMIC_FETCH_ADD smoketest - 64bit",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		/* Write 3 to stack */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		/* Put a 1 in R1, add it to the 3 on the stack, and load the value back into R1 */
		BPF_MOV64_IMM(BPF_REG_1, 1),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* Check the value we loaded back was 3 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* Load value from stack */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),
		/* Check value loaded from stack was 4 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 4, 1),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC_FETCH_ADD smoketest - 32bit",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		/* Write 3 to stack */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 3),
		/* Put a 1 in R1, add it to the 3 on the stack, and load the value back into R1 */
		BPF_MOV32_IMM(BPF_REG_1, 1),
		BPF_ATOMIC_OP(BPF_W, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -4),
		/* Check the value we loaded back was 3 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* Load value from stack */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -4),
		/* Check value loaded from stack was 4 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 4, 1),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"Can't use ATM_FETCH_ADD on frame pointer",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_10, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr_unpriv = "R10 leaks addr into mem",
	.errstr = "frame pointer is read only",
},
{
	"Can't use ATM_FETCH_ADD on uninit src reg",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_2, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	/* It happens that the address leak check is first, but it would also be
	 * complain about the fact that we're trying to modify R10.
	 */
	.errstr = "!read_ok",
},
{
	"Can't use ATM_FETCH_ADD on uninit dst reg",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_2, BPF_REG_0, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	/* It happens that the address leak check is first, but it would also be
	 * complain about the fact that we're trying to modify R10.
	 */
	.errstr = "!read_ok",
},
{
	"Can't use ATM_FETCH_ADD on kernel memory",
	.insns = {
		/* This is an fentry prog, context is array of the args of the
		 * kernel function being called. Load first arg into R2.
		 */
		BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 0),
		/* First arg of bpf_fentry_test7 is a pointer to a struct.
		 * Attempt to modify that struct. Verifier shouldn't let us
		 * because it's kernel memory.
		 */
		BPF_MOV64_IMM(BPF_REG_3, 1),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_2, BPF_REG_3, 0),
		/* Done */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACING,
	.expected_attach_type = BPF_TRACE_FENTRY,
	.kfunc = "bpf_fentry_test7",
	.result = REJECT,
	.errstr = "only read is supported",
},
