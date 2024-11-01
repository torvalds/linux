{
	"subtraction bounds (map value) variant 1",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 9),
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_JMP_IMM(BPF_JGT, BPF_REG_1, 0xff, 7),
	BPF_LDX_MEM(BPF_B, BPF_REG_3, BPF_REG_0, 1),
	BPF_JMP_IMM(BPF_JGT, BPF_REG_3, 0xff, 5),
	BPF_ALU64_REG(BPF_SUB, BPF_REG_1, BPF_REG_3),
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_1, 56),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "R0 max value is outside of the allowed memory range",
	.result = REJECT,
},
{
	"subtraction bounds (map value) variant 2",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_JMP_IMM(BPF_JGT, BPF_REG_1, 0xff, 6),
	BPF_LDX_MEM(BPF_B, BPF_REG_3, BPF_REG_0, 1),
	BPF_JMP_IMM(BPF_JGT, BPF_REG_3, 0xff, 4),
	BPF_ALU64_REG(BPF_SUB, BPF_REG_1, BPF_REG_3),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "R0 min value is negative, either use unsigned index or do a if (index >=0) check.",
	.errstr_unpriv = "R1 has unknown scalar with mixed signed bounds",
	.result = REJECT,
},
{
	"check subtraction on pointers for unpriv",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_LD_MAP_FD(BPF_REG_ARG1, 0),
	BPF_MOV64_REG(BPF_REG_ARG2, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_ARG2, -8),
	BPF_ST_MEM(BPF_DW, BPF_REG_ARG2, 0, 9),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_MOV64_REG(BPF_REG_9, BPF_REG_FP),
	BPF_ALU64_REG(BPF_SUB, BPF_REG_9, BPF_REG_0),
	BPF_LD_MAP_FD(BPF_REG_ARG1, 0),
	BPF_MOV64_REG(BPF_REG_ARG2, BPF_REG_FP),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_ARG2, -8),
	BPF_ST_MEM(BPF_DW, BPF_REG_ARG2, 0, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_STX_MEM(BPF_DW, BPF_REG_0, BPF_REG_9, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 1, 9 },
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R9 pointer -= pointer prohibited",
},
{
	"bounds check based on zero-extended MOV",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	/* r2 = 0x0000'0000'ffff'ffff */
	BPF_MOV32_IMM(BPF_REG_2, 0xffffffff),
	/* r2 = 0 */
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_2, 32),
	/* no-op */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_2),
	/* access at offset 0 */
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT
},
{
	"bounds check based on sign-extended MOV. test1",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	/* r2 = 0xffff'ffff'ffff'ffff */
	BPF_MOV64_IMM(BPF_REG_2, 0xffffffff),
	/* r2 = 0xffff'ffff */
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_2, 32),
	/* r0 = <oob pointer> */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_2),
	/* access to OOB pointer */
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "map_value pointer and 4294967295",
	.result = REJECT
},
{
	"bounds check based on sign-extended MOV. test2",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	/* r2 = 0xffff'ffff'ffff'ffff */
	BPF_MOV64_IMM(BPF_REG_2, 0xffffffff),
	/* r2 = 0xfff'ffff */
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_2, 36),
	/* r0 = <oob pointer> */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_2),
	/* access to OOB pointer */
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "R0 min value is outside of the allowed memory range",
	.result = REJECT
},
{
	"bounds check based on reg_off + var_off + insn_off. test1",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
		    offsetof(struct __sk_buff, mark)),
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	BPF_ALU64_IMM(BPF_AND, BPF_REG_6, 1),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, (1 << 29) - 1),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_6),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, (1 << 29) - 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 3),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 4 },
	.errstr = "value_size=8 off=1073741825",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"bounds check based on reg_off + var_off + insn_off. test2",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
		    offsetof(struct __sk_buff, mark)),
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	BPF_ALU64_IMM(BPF_AND, BPF_REG_6, 1),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, (1 << 30) - 1),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_6),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, (1 << 29) - 1),
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 3),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 4 },
	.errstr = "value 1073741823",
	.result = REJECT,
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"bounds check after truncation of non-boundary-crossing range",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 9),
	/* r1 = [0x00, 0xff] */
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_2, 1),
	/* r2 = 0x10'0000'0000 */
	BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 36),
	/* r1 = [0x10'0000'0000, 0x10'0000'00ff] */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_2),
	/* r1 = [0x10'7fff'ffff, 0x10'8000'00fe] */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x7fffffff),
	/* r1 = [0x00, 0xff] */
	BPF_ALU32_IMM(BPF_SUB, BPF_REG_1, 0x7fffffff),
	/* r1 = 0 */
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_1, 8),
	/* no-op */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* access at offset 0 */
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT
},
{
	"bounds check after truncation of boundary-crossing range (1)",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),
	/* r1 = [0x00, 0xff] */
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0xffffff80 >> 1),
	/* r1 = [0xffff'ff80, 0x1'0000'007f] */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0xffffff80 >> 1),
	/* r1 = [0xffff'ff80, 0xffff'ffff] or
	 *      [0x0000'0000, 0x0000'007f]
	 */
	BPF_ALU32_IMM(BPF_ADD, BPF_REG_1, 0),
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 0xffffff80 >> 1),
	/* r1 = [0x00, 0xff] or
	 *      [0xffff'ffff'0000'0080, 0xffff'ffff'ffff'ffff]
	 */
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 0xffffff80 >> 1),
	/* error on OOB pointer computation */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	/* not actually fully unbounded, but the bound is very high */
	.errstr = "value -4294967168 makes map_value pointer be out of bounds",
	.result = REJECT,
},
{
	"bounds check after truncation of boundary-crossing range (2)",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),
	/* r1 = [0x00, 0xff] */
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0xffffff80 >> 1),
	/* r1 = [0xffff'ff80, 0x1'0000'007f] */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0xffffff80 >> 1),
	/* r1 = [0xffff'ff80, 0xffff'ffff] or
	 *      [0x0000'0000, 0x0000'007f]
	 * difference to previous test: truncation via MOV32
	 * instead of ALU32.
	 */
	BPF_MOV32_REG(BPF_REG_1, BPF_REG_1),
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 0xffffff80 >> 1),
	/* r1 = [0x00, 0xff] or
	 *      [0xffff'ffff'0000'0080, 0xffff'ffff'ffff'ffff]
	 */
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 0xffffff80 >> 1),
	/* error on OOB pointer computation */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "value -4294967168 makes map_value pointer be out of bounds",
	.result = REJECT,
},
{
	"bounds check after wrapping 32-bit addition",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),
	/* r1 = 0x7fff'ffff */
	BPF_MOV64_IMM(BPF_REG_1, 0x7fffffff),
	/* r1 = 0xffff'fffe */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0x7fffffff),
	/* r1 = 0 */
	BPF_ALU32_IMM(BPF_ADD, BPF_REG_1, 2),
	/* no-op */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* access at offset 0 */
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT
},
{
	"bounds check after shift with oversized count operand",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 6),
	BPF_MOV64_IMM(BPF_REG_2, 32),
	BPF_MOV64_IMM(BPF_REG_1, 1),
	/* r1 = (u32)1 << (u32)32 = ? */
	BPF_ALU32_REG(BPF_LSH, BPF_REG_1, BPF_REG_2),
	/* r1 = [0x0000, 0xffff] */
	BPF_ALU64_IMM(BPF_AND, BPF_REG_1, 0xffff),
	/* computes unknown pointer, potentially OOB */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* potentially OOB access */
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "R0 max value is outside of the allowed memory range",
	.result = REJECT
},
{
	"bounds check after right shift of maybe-negative number",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 6),
	/* r1 = [0x00, 0xff] */
	BPF_LDX_MEM(BPF_B, BPF_REG_1, BPF_REG_0, 0),
	/* r1 = [-0x01, 0xfe] */
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 1),
	/* r1 = 0 or 0xff'ffff'ffff'ffff */
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_1, 8),
	/* r1 = 0 or 0xffff'ffff'ffff */
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_1, 8),
	/* computes unknown pointer, potentially OOB */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* potentially OOB access */
	BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "R0 unbounded memory access",
	.result = REJECT
},
{
	"bounds check after 32-bit right shift with 64-bit input",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 6),
	/* r1 = 2 */
	BPF_MOV64_IMM(BPF_REG_1, 2),
	/* r1 = 1<<32 */
	BPF_ALU64_IMM(BPF_LSH, BPF_REG_1, 31),
	/* r1 = 0 (NOT 2!) */
	BPF_ALU32_IMM(BPF_RSH, BPF_REG_1, 31),
	/* r1 = 0xffff'fffe (NOT 0!) */
	BPF_ALU32_IMM(BPF_SUB, BPF_REG_1, 2),
	/* error on computing OOB pointer */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	/* exit */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "math between map_value pointer and 4294967294 is not allowed",
	.result = REJECT,
},
{
	"bounds check map access with off+size signed 32bit overflow. test1",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 0x7ffffffe),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_JMP_A(0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "map_value pointer and 2147483646",
	.result = REJECT
},
{
	"bounds check map access with off+size signed 32bit overflow. test2",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 0x1fffffff),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 0x1fffffff),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 0x1fffffff),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
	BPF_JMP_A(0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "pointer offset 1073741822",
	.errstr_unpriv = "R0 pointer arithmetic of map value goes out of range",
	.result = REJECT
},
{
	"bounds check map access with off+size signed 32bit overflow. test3",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 0x1fffffff),
	BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 0x1fffffff),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 2),
	BPF_JMP_A(0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "pointer offset -1073741822",
	.errstr_unpriv = "R0 pointer arithmetic of map value goes out of range",
	.result = REJECT
},
{
	"bounds check map access with off+size signed 32bit overflow. test4",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_1, 1000000),
	BPF_ALU64_IMM(BPF_MUL, BPF_REG_1, 1000000),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 2),
	BPF_JMP_A(0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr = "map_value pointer and 1000000000000",
	.result = REJECT
},
{
	"bounds check mixed 32bit and 64bit arithmetic. test1",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_1, -1),
	BPF_ALU64_IMM(BPF_LSH, BPF_REG_1, 32),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 1),
	/* r1 = 0xffffFFFF00000001 */
	BPF_JMP32_IMM(BPF_JGT, BPF_REG_1, 1, 3),
	/* check ALU64 op keeps 32bit bounds */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 1),
	BPF_JMP32_IMM(BPF_JGT, BPF_REG_1, 2, 1),
	BPF_JMP_A(1),
	/* invalid ldx if bounds are lost above */
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, -1),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 invalid mem access 'scalar'",
	.result_unpriv = REJECT,
	.result = ACCEPT
},
{
	"bounds check mixed 32bit and 64bit arithmetic. test2",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_1, -1),
	BPF_ALU64_IMM(BPF_LSH, BPF_REG_1, 32),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 1),
	/* r1 = 0xffffFFFF00000001 */
	BPF_MOV64_IMM(BPF_REG_2, 3),
	/* r1 = 0x2 */
	BPF_ALU32_IMM(BPF_ADD, BPF_REG_1, 1),
	/* check ALU32 op zero extends 64bit bounds */
	BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_2, 1),
	BPF_JMP_A(1),
	/* invalid ldx if bounds are lost above */
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, -1),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 invalid mem access 'scalar'",
	.result_unpriv = REJECT,
	.result = ACCEPT
},
{
	"assigning 32bit bounds to 64bit for wA = 0, wB = wA",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_8, BPF_REG_1,
		    offsetof(struct __sk_buff, data_end)),
	BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_1,
		    offsetof(struct __sk_buff, data)),
	BPF_MOV32_IMM(BPF_REG_9, 0),
	BPF_MOV32_REG(BPF_REG_2, BPF_REG_9),
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_7),
	BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_2),
	BPF_MOV64_REG(BPF_REG_3, BPF_REG_6),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_3, 8),
	BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_8, 1),
	BPF_LDX_MEM(BPF_W, BPF_REG_5, BPF_REG_6, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	.result = ACCEPT,
	.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
},
{
	"bounds check for reg = 0, reg xor 1",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_1, 0),
	BPF_ALU64_IMM(BPF_XOR, BPF_REG_1, 1),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 min value is outside of the allowed memory range",
	.result_unpriv = REJECT,
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT,
},
{
	"bounds check for reg32 = 0, reg32 xor 1",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_MOV32_IMM(BPF_REG_1, 0),
	BPF_ALU32_IMM(BPF_XOR, BPF_REG_1, 1),
	BPF_JMP32_IMM(BPF_JNE, BPF_REG_1, 0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 min value is outside of the allowed memory range",
	.result_unpriv = REJECT,
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT,
},
{
	"bounds check for reg = 2, reg xor 3",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_MOV64_IMM(BPF_REG_1, 2),
	BPF_ALU64_IMM(BPF_XOR, BPF_REG_1, 3),
	BPF_JMP_IMM(BPF_JGT, BPF_REG_1, 0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 min value is outside of the allowed memory range",
	.result_unpriv = REJECT,
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT,
},
{
	"bounds check for reg = any, reg xor 3",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0),
	BPF_ALU64_IMM(BPF_XOR, BPF_REG_1, 3),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.result = REJECT,
	.errstr = "invalid access to map value",
	.errstr_unpriv = "invalid access to map value",
},
{
	"bounds check for reg32 = any, reg32 xor 3",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0),
	BPF_ALU32_IMM(BPF_XOR, BPF_REG_1, 3),
	BPF_JMP32_IMM(BPF_JNE, BPF_REG_1, 0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.result = REJECT,
	.errstr = "invalid access to map value",
	.errstr_unpriv = "invalid access to map value",
},
{
	"bounds check for reg > 0, reg xor 3",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0),
	BPF_JMP_IMM(BPF_JLE, BPF_REG_1, 0, 3),
	BPF_ALU64_IMM(BPF_XOR, BPF_REG_1, 3),
	BPF_JMP_IMM(BPF_JGE, BPF_REG_1, 0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 min value is outside of the allowed memory range",
	.result_unpriv = REJECT,
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT,
},
{
	"bounds check for reg32 > 0, reg32 xor 3",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
	BPF_EXIT_INSN(),
	BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0),
	BPF_JMP32_IMM(BPF_JLE, BPF_REG_1, 0, 3),
	BPF_ALU32_IMM(BPF_XOR, BPF_REG_1, 3),
	BPF_JMP32_IMM(BPF_JGE, BPF_REG_1, 0, 1),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 min value is outside of the allowed memory range",
	.result_unpriv = REJECT,
	.fixup_map_hash_8b = { 3 },
	.result = ACCEPT,
},
{
	"bounds checks after 32-bit truncation. test 1",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_0, 0),
	/* This used to reduce the max bound to 0x7fffffff */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
	BPF_JMP_IMM(BPF_JGT, BPF_REG_1, 0x7fffffff, 1),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr_unpriv = "R0 leaks addr",
	.result_unpriv = REJECT,
	.result = ACCEPT,
},
{
	"bounds checks after 32-bit truncation. test 2",
	.insns = {
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_0, 0),
	BPF_JMP_IMM(BPF_JSLT, BPF_REG_1, 1, 1),
	BPF_JMP32_IMM(BPF_JSLT, BPF_REG_1, 0, 1),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_8b = { 3 },
	.errstr_unpriv = "R0 leaks addr",
	.result_unpriv = REJECT,
	.result = ACCEPT,
},
