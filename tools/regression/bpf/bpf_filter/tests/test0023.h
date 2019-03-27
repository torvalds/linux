/*-
 * Test 0023:	BPF_JMP+BPF_JSET+BPF_X
 *
 * $FreeBSD$
 */

/* BPF program */
static struct bpf_insn	pc[] = {
	BPF_STMT(BPF_LD+BPF_IMM, 0x01234567),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x80000000),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0, 9, 0),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x40000000),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0, 7, 0),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x20000000),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0, 5, 0),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x10000000),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0, 3, 0),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x1),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0x1, 2, 1),
	BPF_STMT(BPF_LD+BPF_IMM, 0xdeadc0de),
	BPF_STMT(BPF_RET+BPF_A, 0),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x08000000),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0, 5, 0),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x04000000),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0, 3, 0),
	BPF_STMT(BPF_LDX+BPF_IMM, 0x02000000),
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_X, 0, 1, 0),
	BPF_STMT(BPF_LD+BPF_IMM, 0xc0decafe),
	BPF_STMT(BPF_RET+BPF_A, 0),
};

/* Packet */
static u_char	pkt[] = {
	0x00,
};

/* Packet length seen on wire */
static u_int	wirelen =	sizeof(pkt);

/* Packet length passed on buffer */
static u_int	buflen =	sizeof(pkt);

/* Invalid instruction */
static int	invalid =	0;

/* Expected return value */
static u_int	expect =	0xc0decafe;

/* Expected signal */
static int	expect_signal =	0;
