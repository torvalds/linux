/* $Id: ptrace.h,v 1.25 1997/03/04 16:27:25 jj Exp $ */
#ifndef _SPARC_PTRACE_H
#define _SPARC_PTRACE_H

#include <asm/psr.h>

/* This struct defines the way the registers are stored on the 
 * stack during a system call and basically all traps.
 */

#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long psr;
	unsigned long pc;
	unsigned long npc;
	unsigned long y;
	unsigned long u_regs[16]; /* globals and ins */
};

#define UREG_G0        0
#define UREG_G1        1
#define UREG_G2        2
#define UREG_G3        3
#define UREG_G4        4
#define UREG_G5        5
#define UREG_G6        6
#define UREG_G7        7
#define UREG_I0        8
#define UREG_I1        9
#define UREG_I2        10
#define UREG_I3        11
#define UREG_I4        12
#define UREG_I5        13
#define UREG_I6        14
#define UREG_I7        15
#define UREG_WIM       UREG_G0
#define UREG_FADDR     UREG_G0
#define UREG_FP        UREG_I6
#define UREG_RETPC     UREG_I7

/* A register window */
struct reg_window {
	unsigned long locals[8];
	unsigned long ins[8];
};

/* A Sparc stack frame */
struct sparc_stackf {
	unsigned long locals[8];
        unsigned long ins[6];
	struct sparc_stackf *fp;
	unsigned long callers_pc;
	char *structptr;
	unsigned long xargs[6];
	unsigned long xxargs[1];
};	

#define TRACEREG_SZ   sizeof(struct pt_regs)
#define STACKFRAME_SZ sizeof(struct sparc_stackf)

#ifdef __KERNEL__

#define __ARCH_SYS_PTRACE	1

#define user_mode(regs) (!((regs)->psr & PSR_PS))
#define instruction_pointer(regs) ((regs)->pc)
unsigned long profile_pc(struct pt_regs *);
extern void show_regs(struct pt_regs *);
#endif

#else /* __ASSEMBLY__ */
/* For assembly code. */
#define TRACEREG_SZ       0x50
#define STACKFRAME_SZ     0x60
#endif

/*
 * The asm-offsets.h is a generated file, so we cannot include it.
 * It may be OK for glibc headers, but it's utterly pointless for C code.
 * The assembly code using those offsets has to include it explicitly.
 */
/* #include <asm/asm-offsets.h> */

/* These are for pt_regs. */
#define PT_PSR    0x0
#define PT_PC     0x4
#define PT_NPC    0x8
#define PT_Y      0xc
#define PT_G0     0x10
#define PT_WIM    PT_G0
#define PT_G1     0x14
#define PT_G2     0x18
#define PT_G3     0x1c
#define PT_G4     0x20
#define PT_G5     0x24
#define PT_G6     0x28
#define PT_G7     0x2c
#define PT_I0     0x30
#define PT_I1     0x34
#define PT_I2     0x38
#define PT_I3     0x3c
#define PT_I4     0x40
#define PT_I5     0x44
#define PT_I6     0x48
#define PT_FP     PT_I6
#define PT_I7     0x4c

/* Reg_window offsets */
#define RW_L0     0x00
#define RW_L1     0x04
#define RW_L2     0x08
#define RW_L3     0x0c
#define RW_L4     0x10
#define RW_L5     0x14
#define RW_L6     0x18
#define RW_L7     0x1c
#define RW_I0     0x20
#define RW_I1     0x24
#define RW_I2     0x28
#define RW_I3     0x2c
#define RW_I4     0x30
#define RW_I5     0x34
#define RW_I6     0x38
#define RW_I7     0x3c

/* Stack_frame offsets */
#define SF_L0     0x00
#define SF_L1     0x04
#define SF_L2     0x08
#define SF_L3     0x0c
#define SF_L4     0x10
#define SF_L5     0x14
#define SF_L6     0x18
#define SF_L7     0x1c
#define SF_I0     0x20
#define SF_I1     0x24
#define SF_I2     0x28
#define SF_I3     0x2c
#define SF_I4     0x30
#define SF_I5     0x34
#define SF_FP     0x38
#define SF_PC     0x3c
#define SF_RETP   0x40
#define SF_XARG0  0x44
#define SF_XARG1  0x48
#define SF_XARG2  0x4c
#define SF_XARG3  0x50
#define SF_XARG4  0x54
#define SF_XARG5  0x58
#define SF_XXARG  0x5c

/* Stuff for the ptrace system call */
#define PTRACE_SUNATTACH	  10
#define PTRACE_SUNDETACH	  11
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13
#define PTRACE_GETFPREGS          14
#define PTRACE_SETFPREGS          15
#define PTRACE_READDATA           16
#define PTRACE_WRITEDATA          17
#define PTRACE_READTEXT           18
#define PTRACE_WRITETEXT          19
#define PTRACE_GETFPAREGS         20
#define PTRACE_SETFPAREGS         21

#define PTRACE_GETUCODE           29  /* stupid bsd-ism */


#endif /* !(_SPARC_PTRACE_H) */
