/*
 * Buggy verifier accepted the program below while not patching BPF_PSEUDO_FUNC
 * load instruction to contain a real address. Which resulted in a function call
 * to a bogus address.
 */
{
	"BPF_PSEUDO_FUNC reference to the main program",
	.insns = {
	/* r6 = bpf_map_lookup_elem(&timer_map, &(int){0}); */
	BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
	BPF_LD_MAP_FD(BPF_REG_1, 0),
	BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 10),
	BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
	/* bpf_timer_init(r6, &timer_map, 0); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_LD_MAP_FD(BPF_REG_2, 0),
	BPF_MOV64_IMM(BPF_REG_3, 0),
	BPF_EMIT_CALL(BPF_FUNC_timer_init),
	/* bpf_timer_set_callback(r6, <insn #0>); */
	BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_2, BPF_PSEUDO_FUNC, 0, -15),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_EMIT_CALL(BPF_FUNC_timer_set_callback),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.fixup_map_timer = { 3, 9 },
	.result = REJECT,
	.errstr = "callback function cannot be the main program",
	.func_info = { { 0, 4 /* main_prog */ } },
	.func_info_cnt = 1,
	.btf_strings = "\0int\0ctx\0main_prog",
	.btf_types = {
	/* 1: int            */ BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),
	/* 2: void*          */ BTF_PTR_ENC(0),
	/* 3: int __(void *) */ BTF_FUNC_PROTO_ENC(1, 1),
				BTF_FUNC_PROTO_ARG_ENC(5, 2),
	/* 4: main_prog      */ BTF_FUNC_ENC(9, 3),
	BTF_END_RAW
	}
},
