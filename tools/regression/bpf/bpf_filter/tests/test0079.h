/*-
 * Test 0079:	An empty filter program.
 *
 * $FreeBSD$
 */

/* BPF program */
static struct bpf_insn	pc[] = {
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
static u_int	expect =	(u_int)-1;

/* Expected signal */
static int	expect_signal =	0;
