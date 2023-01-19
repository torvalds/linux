{
	"ringbuf: invalid reservation offset 1",
	.insns = {
	/* reserve 8 byte ringbuf memory */
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_MOV64_IMM(BPF_REG_2, 8),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),
	/* store a pointer to the reserved memory in R6 */
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
	/* check whether the reservation was successful */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 7),
	/* spill R6(mem) into the stack */
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
	/* fill it back in R7 */
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, -8),
	/* should be able to access *(R7) = 0 */
	BPF_ST_MEM(BPF_DW, BPF_REG_7, 0, 0),
	/* submit the reserved ringbuf memory */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
	/* add invalid offset to reserved ringbuf memory */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0xcafe),
	BPF_MOV64_IMM(BPF_REG_2, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_ringbuf = { 1 },
	.result = REJECT,
	.errstr = "R1 must have zero offset when passed to release func",
},
{
	"ringbuf: invalid reservation offset 2",
	.insns = {
	/* reserve 8 byte ringbuf memory */
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_MOV64_IMM(BPF_REG_2, 8),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),
	/* store a pointer to the reserved memory in R6 */
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
	/* check whether the reservation was successful */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 7),
	/* spill R6(mem) into the stack */
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
	/* fill it back in R7 */
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, -8),
	/* add invalid offset to reserved ringbuf memory */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, 0xcafe),
	/* should be able to access *(R7) = 0 */
	BPF_ST_MEM(BPF_DW, BPF_REG_7, 0, 0),
	/* submit the reserved ringbuf memory */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
	BPF_MOV64_IMM(BPF_REG_2, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_ringbuf = { 1 },
	.result = REJECT,
	.errstr = "R7 min value is outside of the allowed memory range",
},
{
	"ringbuf: check passing rb mem to helpers",
	.insns = {
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
	/* reserve 8 byte ringbuf memory */
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_MOV64_IMM(BPF_REG_2, 8),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
	/* check whether the reservation was successful */
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	/* pass allocated ring buffer memory to fib lookup */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_0),
	BPF_MOV64_IMM(BPF_REG_3, 8),
	BPF_MOV64_IMM(BPF_REG_4, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_fib_lookup),
	/* submit the ringbuf memory */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
	BPF_MOV64_IMM(BPF_REG_2, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_ringbuf = { 2 },
	.prog_type = BPF_PROG_TYPE_XDP,
	.result = ACCEPT,
},
