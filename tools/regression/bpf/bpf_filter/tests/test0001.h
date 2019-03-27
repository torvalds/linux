/*-
 * Test 0001:	Catch illegal instruction.
 *
 * $FreeBSD$
 */

/* BPF program */
static struct	bpf_insn pc[] = {
	BPF_STMT(0x55, 0),
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
static int	invalid =	1;

/* Expected return value */
static u_int	expect =	0;

/* Expected signal */
static int	expect_signal =	SIGABRT;
