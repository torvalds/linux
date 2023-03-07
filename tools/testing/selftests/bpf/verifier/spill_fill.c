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
	"check valid spill/fill, ptr to mem",
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
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 6),
	/* spill R6(mem) into the stack */
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
	/* fill it back in R7 */
	BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, -8),
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
	.result = ACCEPT,
	.result_unpriv = ACCEPT,
},
{
	"check with invalid reg offset 0",
	.insns = {
	/* reserve 8 byte ringbuf memory */
	BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_MOV64_IMM(BPF_REG_2, 8),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),
	/* store a pointer to the reserved memory in R6 */
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
	/* add invalid offset to memory or NULL */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
	/* check whether the reservation was successful */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),
	/* should not be able to access *(R7) = 0 */
	BPF_ST_MEM(BPF_W, BPF_REG_6, 0, 0),
	/* submit the reserved ringbuf memory */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_MOV64_IMM(BPF_REG_2, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_ringbuf = { 1 },
	.result = REJECT,
	.errstr = "R0 pointer arithmetic on ringbuf_mem_or_null prohibited",
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
	.errstr = "R0 invalid mem access 'scalar'",
	.result = REJECT,
	.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
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
{
	"Spill and refill a u32 const scalar.  Offset to skb->data",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
		    offsetof(struct __sk_buff, data)),
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
		    offsetof(struct __sk_buff, data_end)),
	/* r4 = 20 */
	BPF_MOV32_IMM(BPF_REG_4, 20),
	/* *(u32 *)(r10 -8) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -8),
	/* r4 = *(u32 *)(r10 -8) */
	BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_10, -8),
	/* r0 = r2 */
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=20 */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_4),
	/* if (r0 > r3) R0=pkt,off=20 R2=pkt R3=pkt_end R4=20 */
	BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
	/* r0 = *(u32 *)r2 R0=pkt,off=20,r=20 R2=pkt,r=20 R3=pkt_end R4=20 */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_2, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"Spill a u32 const, refill from another half of the uninit u32 from the stack",
	.insns = {
	/* r4 = 20 */
	BPF_MOV32_IMM(BPF_REG_4, 20),
	/* *(u32 *)(r10 -8) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -8),
	/* r4 = *(u32 *)(r10 -4) fp-8=????rrrr*/
	BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_10, -4),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result_unpriv = REJECT,
	.errstr_unpriv = "invalid read from stack off -4+0 size 4",
	/* in privileged mode reads from uninitialized stack locations are permitted */
	.result = ACCEPT,
},
{
	"Spill a u32 const scalar.  Refill as u16.  Offset to skb->data",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
		    offsetof(struct __sk_buff, data)),
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
		    offsetof(struct __sk_buff, data_end)),
	/* r4 = 20 */
	BPF_MOV32_IMM(BPF_REG_4, 20),
	/* *(u32 *)(r10 -8) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -8),
	/* r4 = *(u16 *)(r10 -8) */
	BPF_LDX_MEM(BPF_H, BPF_REG_4, BPF_REG_10, -8),
	/* r0 = r2 */
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=65535 */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_4),
	/* if (r0 > r3) R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=umax=65535 */
	BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
	/* r0 = *(u32 *)r2 R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=20 */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_2, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "invalid access to packet",
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"Spill u32 const scalars.  Refill as u64.  Offset to skb->data",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
		    offsetof(struct __sk_buff, data)),
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
		    offsetof(struct __sk_buff, data_end)),
	/* r6 = 0 */
	BPF_MOV32_IMM(BPF_REG_6, 0),
	/* r7 = 20 */
	BPF_MOV32_IMM(BPF_REG_7, 20),
	/* *(u32 *)(r10 -4) = r6 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_6, -4),
	/* *(u32 *)(r10 -8) = r7 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, -8),
	/* r4 = *(u64 *)(r10 -8) */
	BPF_LDX_MEM(BPF_H, BPF_REG_4, BPF_REG_10, -8),
	/* r0 = r2 */
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=65535 */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_4),
	/* if (r0 > r3) R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=umax=65535 */
	BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
	/* r0 = *(u32 *)r2 R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=20 */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_2, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "invalid access to packet",
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"Spill a u32 const scalar.  Refill as u16 from fp-6.  Offset to skb->data",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
		    offsetof(struct __sk_buff, data)),
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
		    offsetof(struct __sk_buff, data_end)),
	/* r4 = 20 */
	BPF_MOV32_IMM(BPF_REG_4, 20),
	/* *(u32 *)(r10 -8) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -8),
	/* r4 = *(u16 *)(r10 -6) */
	BPF_LDX_MEM(BPF_H, BPF_REG_4, BPF_REG_10, -6),
	/* r0 = r2 */
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=65535 */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_4),
	/* if (r0 > r3) R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=umax=65535 */
	BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
	/* r0 = *(u32 *)r2 R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=20 */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_2, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "invalid access to packet",
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"Spill and refill a u32 const scalar at non 8byte aligned stack addr.  Offset to skb->data",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
		    offsetof(struct __sk_buff, data)),
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
		    offsetof(struct __sk_buff, data_end)),
	/* r4 = 20 */
	BPF_MOV32_IMM(BPF_REG_4, 20),
	/* *(u32 *)(r10 -8) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -8),
	/* *(u32 *)(r10 -4) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -4),
	/* r4 = *(u32 *)(r10 -4),  */
	BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_10, -4),
	/* r0 = r2 */
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=U32_MAX */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_4),
	/* if (r0 > r3) R0=pkt,umax=U32_MAX R2=pkt R3=pkt_end R4= */
	BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
	/* r0 = *(u32 *)r2 R0=pkt,umax=U32_MAX R2=pkt R3=pkt_end R4= */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_2, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "invalid access to packet",
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"Spill and refill a umax=40 bounded scalar.  Offset to skb->data",
	.insns = {
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
		    offsetof(struct __sk_buff, data)),
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
		    offsetof(struct __sk_buff, data_end)),
	BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_1,
		    offsetof(struct __sk_buff, tstamp)),
	BPF_JMP_IMM(BPF_JLE, BPF_REG_4, 40, 2),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	/* *(u32 *)(r10 -8) = r4 R4=umax=40 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -8),
	/* r4 = (*u32 *)(r10 - 8) */
	BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_10, -8),
	/* r2 += r4 R2=pkt R4=umax=40 */
	BPF_ALU64_REG(BPF_ADD, BPF_REG_2, BPF_REG_4),
	/* r0 = r2 R2=pkt,umax=40 R4=umax=40 */
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
	/* r2 += 20 R0=pkt,umax=40 R2=pkt,umax=40 */
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 20),
	/* if (r2 > r3) R0=pkt,umax=40 R2=pkt,off=20,umax=40 */
	BPF_JMP_REG(BPF_JGT, BPF_REG_2, BPF_REG_3, 1),
	/* r0 = *(u32 *)r0 R0=pkt,r=20,umax=40 R2=pkt,off=20,r=20,umax=40 */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
{
	"Spill a u32 scalar at fp-4 and then at fp-8",
	.insns = {
	/* r4 = 4321 */
	BPF_MOV32_IMM(BPF_REG_4, 4321),
	/* *(u32 *)(r10 -4) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -4),
	/* *(u32 *)(r10 -8) = r4 */
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_4, -8),
	/* r4 = *(u64 *)(r10 -8) */
	BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.prog_type = BPF_PROG_TYPE_SCHED_CLS,
},
