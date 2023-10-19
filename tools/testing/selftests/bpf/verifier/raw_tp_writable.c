{
	"raw_tracepoint_writable: reject variable offset",
	.insns = {
		/* r6 is our tp buffer */
		BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, 0),

		BPF_LD_MAP_FD(BPF_REG_1, 0),
		/* move the key (== 0) to r10-8 */
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_0, 0),
		/* lookup in the map */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			     BPF_FUNC_map_lookup_elem),

		/* exit clean if null */
		BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
		BPF_EXIT_INSN(),

		/* shift the buffer pointer to a variable location */
		BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_0, 0),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_0),
		/* clobber whatever's there */
		BPF_MOV64_IMM(BPF_REG_7, 4242),
		BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_7, 0),

		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 1, },
	.prog_type = BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE,
	.errstr = "R6 invalid variable buffer offset: off=0, var_off=(0x0; 0xffffffff)",
	.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
},
