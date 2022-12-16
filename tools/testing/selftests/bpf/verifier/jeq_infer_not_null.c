{
	/* This is equivalent to the following program:
	 *
	 *   r6 = skb->sk;
	 *   r7 = sk_fullsock(r6);
	 *   r0 = sk_fullsock(r6);
	 *   if (r0 == 0) return 0;    (a)
	 *   if (r0 != r7) return 0;   (b)
	 *   *r7->type;                (c)
	 *   return 0;
	 *
	 * It is safe to dereference r7 at point (c), because of (a) and (b).
	 * The test verifies that relation r0 == r7 is propagated from (b) to (c).
	 */
	"jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL -> PTR_TO_SOCKET for JNE false branch",
	.insns = {
	/* r6 = skb->sk; */
	BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, offsetof(struct __sk_buff, sk)),
	/* if (r6 == 0) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 8),
	/* r7 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
	/* r0 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	/* if (r0 == null) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
	/* if (r0 == r7) r0 = *(r7->type); */
	BPF_JMP_REG(BPF_JNE, BPF_REG_0, BPF_REG_7, 1), /* Use ! JNE ! */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_7, offsetof(struct bpf_sock, type)),
	/* return 0 */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_CGROUP_SKB,
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R7 pointer comparison",
},
{
	/* Same as above, but verify that another branch of JNE still
	 * prohibits access to PTR_MAYBE_NULL.
	 */
	"jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL unchanged for JNE true branch",
	.insns = {
	/* r6 = skb->sk */
	BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, offsetof(struct __sk_buff, sk)),
	/* if (r6 == 0) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 9),
	/* r7 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
	/* r0 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	/* if (r0 == null) return 0; */
	BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 3),
	/* if (r0 == r7) return 0; */
	BPF_JMP_REG(BPF_JNE, BPF_REG_0, BPF_REG_7, 1), /* Use ! JNE ! */
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	/* r0 = *(r7->type); */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_7, offsetof(struct bpf_sock, type)),
	/* return 0 */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_CGROUP_SKB,
	.result = REJECT,
	.errstr = "R7 invalid mem access 'sock_or_null'",
	.result_unpriv = REJECT,
	.errstr_unpriv = "R7 pointer comparison",
},
{
	/* Same as a first test, but not null should be inferred for JEQ branch */
	"jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL -> PTR_TO_SOCKET for JEQ true branch",
	.insns = {
	/* r6 = skb->sk; */
	BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, offsetof(struct __sk_buff, sk)),
	/* if (r6 == null) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 9),
	/* r7 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
	/* r0 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	/* if (r0 == null) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
	/* if (r0 != r7) return 0; */
	BPF_JMP_REG(BPF_JEQ, BPF_REG_0, BPF_REG_7, 1), /* Use ! JEQ ! */
	BPF_JMP_IMM(BPF_JA, 0, 0, 1),
	/* r0 = *(r7->type); */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_7, offsetof(struct bpf_sock, type)),
	/* return 0; */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_CGROUP_SKB,
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R7 pointer comparison",
},
{
	/* Same as above, but verify that another branch of JNE still
	 * prohibits access to PTR_MAYBE_NULL.
	 */
	"jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL unchanged for JEQ false branch",
	.insns = {
	/* r6 = skb->sk; */
	BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1, offsetof(struct __sk_buff, sk)),
	/* if (r6 == null) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 8),
	/* r7 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
	/* r0 = sk_fullsock(skb); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_EMIT_CALL(BPF_FUNC_sk_fullsock),
	/* if (r0 == null) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
	/* if (r0 != r7) r0 = *(r7->type); */
	BPF_JMP_REG(BPF_JEQ, BPF_REG_0, BPF_REG_7, 1), /* Use ! JEQ ! */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_7, offsetof(struct bpf_sock, type)),
	/* return 0; */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_CGROUP_SKB,
	.result = REJECT,
	.errstr = "R7 invalid mem access 'sock_or_null'",
	.result_unpriv = REJECT,
	.errstr_unpriv = "R7 pointer comparison",
},
{
	/* Maps are treated in a different branch of `mark_ptr_not_null_reg`,
	 * so separate test for maps case.
	 */
	"jne/jeq infer not null, PTR_TO_MAP_VALUE_OR_NULL -> PTR_TO_MAP_VALUE",
	.insns = {
	/* r9 = &some stack to use as key */
	BPF_ST_MEM(BPF_W, BPF_REG_10, -8, 0),
	BPF_MOV64_REG(BPF_REG_9, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_9, -8),
	/* r8 = process local map */
	BPF_LD_MAP_FD(BPF_REG_8, 0),
	/* r6 = map_lookup_elem(r8, r9); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_8),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_9),
	BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
	/* r7 = map_lookup_elem(r8, r9); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_8),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_9),
	BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
	/* if (r6 == 0) return 0; */
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 2),
	/* if (r6 != r7) return 0; */
	BPF_JMP_REG(BPF_JNE, BPF_REG_6, BPF_REG_7, 1),
	/* read *r7; */
	BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_7, offsetof(struct bpf_xdp_sock, queue_id)),
	/* return 0; */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.fixup_map_xskmap = { 3 },
	.prog_type = BPF_PROG_TYPE_XDP,
	.result = ACCEPT,
},
