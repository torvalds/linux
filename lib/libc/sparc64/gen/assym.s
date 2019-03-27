/*
 * Offsets into into structures used from asm.  Must be kept in sync with
 * appropriate headers.
 *
 * $FreeBSD$
 */

#define	_JB_FP		0x0
#define	_JB_PC		0x8
#define	_JB_SP		0x10
#define	_JB_SIGMASK	0x18
#define	_JB_SIGFLAG	0x28

#define	SIG_BLOCK	1
#define	SIG_SETMASK	3

#define	FSR_RD_MASK	0xc0000000
#define	FSR_RD_RD_Z	0x40000000
