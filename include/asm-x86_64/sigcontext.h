#ifndef _ASM_X86_64_SIGCONTEXT_H
#define _ASM_X86_64_SIGCONTEXT_H

#include <asm/types.h>
#include <linux/compiler.h>

/* FXSAVE frame */
/* Note: reserved1/2 may someday contain valuable data. Always save/restore
   them when you change signal frames. */
struct _fpstate {
	__u16	cwd;
	__u16	swd;
	__u16	twd;	/* Note this is not the same as the 32bit/x87/FSAVE twd */
	__u16	fop;
	__u64	rip;
	__u64	rdp; 
	__u32	mxcsr;
	__u32	mxcsr_mask;
	__u32	st_space[32];	/* 8*16 bytes for each FP-reg */
	__u32	xmm_space[64];	/* 16*16 bytes for each XMM-reg  */
	__u32	reserved2[24];
};

struct sigcontext { 
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long rdi;
	unsigned long rsi;
	unsigned long rbp;
	unsigned long rbx;
	unsigned long rdx;
	unsigned long rax;
	unsigned long rcx;
	unsigned long rsp;
	unsigned long rip;
	unsigned long eflags;		/* RFLAGS */
	unsigned short cs;
	unsigned short gs;
	unsigned short fs;
	unsigned short __pad0; 
	unsigned long err;
	unsigned long trapno;
	unsigned long oldmask;
	unsigned long cr2;
	struct _fpstate __user *fpstate;	/* zero when no FPU context */
	unsigned long reserved1[8];
};

#endif
