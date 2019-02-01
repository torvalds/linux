{
	"XDP, using ifindex from netdev",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
		    offsetof(struct xdp_md, ingress_ifindex)),
	BPF_JMP_IMM(BPF_JLT, BPF_REG_2, 1, 1),
	BPF_MOV64_IMM(BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.prog_type = BPF_PROG_TYPE_XDP,
	.retval = 1,
},
