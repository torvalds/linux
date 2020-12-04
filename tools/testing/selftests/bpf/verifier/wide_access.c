#define BPF_SOCK_ADDR_STORE(field, off, res, err, flgs)	\
{ \
	"wide store to bpf_sock_addr." #field "[" #off "]", \
	.insns = { \
	BPF_MOV64_IMM(BPF_REG_0, 1), \
	BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, \
		    offsetof(struct bpf_sock_addr, field[off])), \
	BPF_EXIT_INSN(), \
	}, \
	.result = res, \
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK_ADDR, \
	.expected_attach_type = BPF_CGROUP_UDP6_SENDMSG, \
	.errstr = err, \
	.flags = flgs, \
}

/* user_ip6[0] is u64 aligned */
BPF_SOCK_ADDR_STORE(user_ip6, 0, ACCEPT,
		    NULL, 0),
BPF_SOCK_ADDR_STORE(user_ip6, 1, REJECT,
		    "invalid bpf_context access off=12 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),
BPF_SOCK_ADDR_STORE(user_ip6, 2, ACCEPT,
		    NULL, 0),
BPF_SOCK_ADDR_STORE(user_ip6, 3, REJECT,
		    "invalid bpf_context access off=20 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),

/* msg_src_ip6[0] is _not_ u64 aligned */
BPF_SOCK_ADDR_STORE(msg_src_ip6, 0, REJECT,
		    "invalid bpf_context access off=44 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),
BPF_SOCK_ADDR_STORE(msg_src_ip6, 1, ACCEPT,
		    NULL, 0),
BPF_SOCK_ADDR_STORE(msg_src_ip6, 2, REJECT,
		    "invalid bpf_context access off=52 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),
BPF_SOCK_ADDR_STORE(msg_src_ip6, 3, REJECT,
		    "invalid bpf_context access off=56 size=8", 0),

#undef BPF_SOCK_ADDR_STORE

#define BPF_SOCK_ADDR_LOAD(field, off, res, err, flgs)	\
{ \
	"wide load from bpf_sock_addr." #field "[" #off "]", \
	.insns = { \
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, \
		    offsetof(struct bpf_sock_addr, field[off])), \
	BPF_MOV64_IMM(BPF_REG_0, 1), \
	BPF_EXIT_INSN(), \
	}, \
	.result = res, \
	.prog_type = BPF_PROG_TYPE_CGROUP_SOCK_ADDR, \
	.expected_attach_type = BPF_CGROUP_UDP6_SENDMSG, \
	.errstr = err, \
	.flags = flgs, \
}

/* user_ip6[0] is u64 aligned */
BPF_SOCK_ADDR_LOAD(user_ip6, 0, ACCEPT,
		   NULL, 0),
BPF_SOCK_ADDR_LOAD(user_ip6, 1, REJECT,
		   "invalid bpf_context access off=12 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),
BPF_SOCK_ADDR_LOAD(user_ip6, 2, ACCEPT,
		   NULL, 0),
BPF_SOCK_ADDR_LOAD(user_ip6, 3, REJECT,
		   "invalid bpf_context access off=20 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),

/* msg_src_ip6[0] is _not_ u64 aligned */
BPF_SOCK_ADDR_LOAD(msg_src_ip6, 0, REJECT,
		   "invalid bpf_context access off=44 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),
BPF_SOCK_ADDR_LOAD(msg_src_ip6, 1, ACCEPT,
		   NULL, 0),
BPF_SOCK_ADDR_LOAD(msg_src_ip6, 2, REJECT,
		   "invalid bpf_context access off=52 size=8",
		    F_NEEDS_EFFICIENT_UNALIGNED_ACCESS),
BPF_SOCK_ADDR_LOAD(msg_src_ip6, 3, REJECT,
		   "invalid bpf_context access off=56 size=8", 0),

#undef BPF_SOCK_ADDR_LOAD
