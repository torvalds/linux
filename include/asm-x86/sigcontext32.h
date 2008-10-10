#ifndef ASM_X86__SIGCONTEXT32_H
#define ASM_X86__SIGCONTEXT32_H

/* signal context for 32bit programs. */

#define X86_FXSR_MAGIC		0x0000

struct _fpreg {
	unsigned short significand[4];
	unsigned short exponent;
};

struct _fpxreg {
	unsigned short significand[4];
	unsigned short exponent;
	unsigned short padding[3];
};

struct _xmmreg {
	__u32	element[4];
};

/* FSAVE frame with extensions */
struct _fpstate_ia32 {
	/* Regular FPU environment */
	__u32 	cw;
	__u32	sw;
	__u32	tag;	/* not compatible to 64bit twd */
	__u32	ipoff;
	__u32	cssel;
	__u32	dataoff;
	__u32	datasel;
	struct _fpreg	_st[8];
	unsigned short	status;
	unsigned short	magic;		/* 0xffff = regular FPU data only */

	/* FXSR FPU environment */
	__u32	_fxsr_env[6];
	__u32	mxcsr;
	__u32	reserved;
	struct _fpxreg	_fxsr_st[8];
	struct _xmmreg	_xmm[8];	/* It's actually 16 */
	__u32	padding[56];
};

struct sigcontext_ia32 {
       unsigned short gs, __gsh;
       unsigned short fs, __fsh;
       unsigned short es, __esh;
       unsigned short ds, __dsh;
       unsigned int di;
       unsigned int si;
       unsigned int bp;
       unsigned int sp;
       unsigned int bx;
       unsigned int dx;
       unsigned int cx;
       unsigned int ax;
       unsigned int trapno;
       unsigned int err;
       unsigned int ip;
       unsigned short cs, __csh;
       unsigned int flags;
       unsigned int sp_at_signal;
       unsigned short ss, __ssh;
       unsigned int fpstate;		/* really (struct _fpstate_ia32 *) */
       unsigned int oldmask;
       unsigned int cr2;
};

#endif /* ASM_X86__SIGCONTEXT32_H */
