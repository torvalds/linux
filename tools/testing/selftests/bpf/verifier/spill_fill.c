{
	"check valid spill/fill",
	.insns = {
	/* spill R1(ctx) into stack */
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8),
	/* fill it back into R2 */
	BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_10, -8),
	/* should be able to access R0 = *(R2 + 8) */
	/* BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_2, 8), */
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "R0 leaks addr",
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.retval = POINTER_VALUE,
},
{
	"check valid spill/fill, skb mark",
	.insns = {
	BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_0,
		    offsetof(struct __sk_buff, mark)),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.result_unpriv = ACCEPT,
},
{
	"check corrupted spill/fill",
	.insns = {
	/* spill R1(ctx) into stack */
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8),
	/* mess up with R1 pointer on stack */
	BPF_ST_MEM(BPF_B, BPF_REG_10, -7, 0x23),
	/* fill back into R0 is fine for priv.
	 * R0 now becomes SCALAR_VALUE.
	 */
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
	/* Load from R0 should fail. */
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 8),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "attempt to corrupt spilled",
	.errstr = "R0 invalid mem access 'inv",
	.result = REJECT,
},
{
	"check corrupted spill/fill, LSB",
	.insns = {
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8),
	BPF_ST_MEM(BPF_H, BPF_REG_10, -8, 0xcafe),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "attempt to corrupt spilled",
	.result_unpriv = REJECT,
	.result = ACCEPT,
	.retval = POINTER_VALUE,
},
{
	"check corrupted spill/fill, MSB",
	.insns = {
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8),
	BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0x12345678),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
	BPF_EXIT_INSN(),
	},
	.errstr_unpriv = "attempt to corrupt spilled",
	.result_unpriv = REJECT,
	.result = ACCEPT,
	.retval = POINTER_VALUE,
},
