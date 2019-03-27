/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)reg.h	5.5 (Berkeley) 1/18/91
 * $FreeBSD$
 */

#ifndef _MACHINE_REG_H_
#define	_MACHINE_REG_H_

#include <machine/_types.h>

#ifdef __i386__
/*
 * Indices for registers in `struct trapframe' and `struct regs'.
 *
 * This interface is deprecated.  In the kernel, it is only used in FPU
 * emulators to convert from register numbers encoded in instructions to
 * register values.  Everything else just accesses the relevant struct
 * members.  In userland, debuggers tend to abuse this interface since
 * they don't understand that `struct regs' is a struct.  I hope they have
 * stopped accessing the registers in the trap frame via PT_{READ,WRITE}_U
 * and we can stop supporting the user area soon.
 */
#define	tFS	(0)
#define	tES	(1)
#define	tDS	(2)
#define	tEDI	(3)
#define	tESI	(4)
#define	tEBP	(5)
#define	tISP	(6)
#define	tEBX	(7)
#define	tEDX	(8)
#define	tECX	(9)
#define	tEAX	(10)
#define	tERR	(12)
#define	tEIP	(13)
#define	tCS	(14)
#define	tEFLAGS	(15)
#define	tESP	(16)
#define	tSS	(17)

/*
 * Indices for registers in `struct regs' only.
 *
 * Some registers live in the pcb and are only in an "array" with the
 * other registers in application interfaces that copy all the registers
 * to or from a `struct regs'.
 */
#define	tGS	(18)
#endif /* __i386__ */

/* Rename the structs below depending on the machine architecture. */
#ifdef	__i386__
#define	__reg32		reg
#define	__fpreg32	fpreg
#define	__dbreg32	dbreg
#else
#define	__reg32		reg32
#define	__reg64		reg
#define	__fpreg32	fpreg32
#define	__fpreg64	fpreg
#define	__dbreg32	dbreg32
#define	__dbreg64	dbreg
#define	__HAVE_REG32
#endif

/*
 * Register set accessible via /proc/$pid/regs and PT_{SET,GET}REGS.
 */
struct __reg32 {
	__uint32_t	r_fs;
	__uint32_t	r_es;
	__uint32_t	r_ds;
	__uint32_t	r_edi;
	__uint32_t	r_esi;
	__uint32_t	r_ebp;
	__uint32_t	r_isp;
	__uint32_t	r_ebx;
	__uint32_t	r_edx;
	__uint32_t	r_ecx;
	__uint32_t	r_eax;
	__uint32_t	r_trapno;
	__uint32_t	r_err;
	__uint32_t	r_eip;
	__uint32_t	r_cs;
	__uint32_t	r_eflags;
	__uint32_t	r_esp;
	__uint32_t	r_ss;
	__uint32_t	r_gs;
};

struct __reg64 {
	__int64_t	r_r15;
	__int64_t	r_r14;
	__int64_t	r_r13;
	__int64_t	r_r12;
	__int64_t	r_r11;
	__int64_t	r_r10;
	__int64_t	r_r9;
	__int64_t	r_r8;
	__int64_t	r_rdi;
	__int64_t	r_rsi;
	__int64_t	r_rbp;
	__int64_t	r_rbx;
	__int64_t	r_rdx;
	__int64_t	r_rcx;
	__int64_t	r_rax;
	__uint32_t	r_trapno;
	__uint16_t	r_fs;
	__uint16_t	r_gs;
	__uint32_t	r_err;
	__uint16_t	r_es;
	__uint16_t	r_ds;
	__int64_t	r_rip;
	__int64_t	r_cs;
	__int64_t	r_rflags;
	__int64_t	r_rsp;
	__int64_t	r_ss;
};

/*
 * Register set accessible via /proc/$pid/fpregs.
 *
 * XXX should get struct from fpu.h.  Here we give a slightly
 * simplified struct.  This may be too much detail.  Perhaps
 * an array of unsigned longs is best.
 */
struct __fpreg32 {
	__uint32_t	fpr_env[7];
	__uint8_t	fpr_acc[8][10];
	__uint32_t	fpr_ex_sw;
	__uint8_t	fpr_pad[64];
};

struct __fpreg64 {
	__uint64_t	fpr_env[4];
	__uint8_t	fpr_acc[8][16];
	__uint8_t	fpr_xacc[16][16];
	__uint64_t	fpr_spare[12];
};

/*
 * Register set accessible via PT_GETXMMREGS (i386).
 */
struct xmmreg {
	/*
	 * XXX should get struct from npx.h.  Here we give a slightly
	 * simplified struct.  This may be too much detail.  Perhaps
	 * an array of unsigned longs is best.
	 */
	__uint32_t	xmm_env[8];
	__uint8_t	xmm_acc[8][16];
	__uint8_t	xmm_reg[8][16];
	__uint8_t	xmm_pad[224];
};

/*
 * Register set accessible via /proc/$pid/dbregs.
 */
struct __dbreg32 {
	__uint32_t	dr[8];	/* debug registers */
				/* Index 0-3: debug address registers */
				/* Index 4-5: reserved */
				/* Index 6: debug status */
				/* Index 7: debug control */
};

struct __dbreg64 {
	__uint64_t	dr[16];	/* debug registers */
				/* Index 0-3: debug address registers */
				/* Index 4-5: reserved */
				/* Index 6: debug status */
				/* Index 7: debug control */
				/* Index 8-15: reserved */
};

#define	DBREG_DR6_RESERVED1	0xffff0ff0
#define	DBREG_DR6_BMASK		0x000f
#define	DBREG_DR6_B(i)		(1 << (i))
#define	DBREG_DR6_BD		0x2000
#define	DBREG_DR6_BS		0x4000
#define	DBREG_DR6_BT		0x8000

#define	DBREG_DR7_RESERVED1	0x0400
#define	DBREG_DR7_LOCAL_ENABLE	0x01
#define	DBREG_DR7_GLOBAL_ENABLE	0x02
#define	DBREG_DR7_LEN_1		0x00	/* 1 byte length          */
#define	DBREG_DR7_LEN_2		0x01
#define	DBREG_DR7_LEN_4		0x03
#define	DBREG_DR7_LEN_8		0x02
#define	DBREG_DR7_EXEC		0x00	/* break on execute       */
#define	DBREG_DR7_WRONLY	0x01	/* break on write         */
#define	DBREG_DR7_RDWR		0x03	/* break on read or write */
#define	DBREG_DR7_MASK(i)	\
	((__u_register_t)(0xf) << ((i) * 4 + 16) | 0x3 << (i) * 2)
#define	DBREG_DR7_SET(i, len, access, enable)				\
	((__u_register_t)((len) << 2 | (access)) << ((i) * 4 + 16) | 	\
	(enable) << (i) * 2)
#define	DBREG_DR7_GD		0x2000
#define	DBREG_DR7_ENABLED(d, i)	(((d) & 0x3 << (i) * 2) != 0)
#define	DBREG_DR7_ACCESS(d, i)	((d) >> ((i) * 4 + 16) & 0x3)
#define	DBREG_DR7_LEN(d, i)	((d) >> ((i) * 4 + 18) & 0x3)

#define	DBREG_DRX(d,x)	((d)->dr[(x)])	/* reference dr0 - dr7 by
					   register number */

#undef __reg32
#undef __reg64
#undef __fpreg32
#undef __fpreg64
#undef __dbreg32
#undef __dbreg64

#ifdef _KERNEL
struct thread;

/*
 * XXX these interfaces are MI, so they should be declared in a MI place.
 */
int	fill_regs(struct thread *, struct reg *);
int	fill_frame_regs(struct trapframe *, struct reg *);
int	set_regs(struct thread *, struct reg *);
int	fill_fpregs(struct thread *, struct fpreg *);
int	set_fpregs(struct thread *, struct fpreg *);
int	fill_dbregs(struct thread *, struct dbreg *);
int	set_dbregs(struct thread *, struct dbreg *);
#ifdef COMPAT_FREEBSD32
int	fill_regs32(struct thread *, struct reg32 *);
int	set_regs32(struct thread *, struct reg32 *);
int	fill_fpregs32(struct thread *, struct fpreg32 *);
int	set_fpregs32(struct thread *, struct fpreg32 *);
int	fill_dbregs32(struct thread *, struct dbreg32 *);
int	set_dbregs32(struct thread *, struct dbreg32 *);
#endif
#endif

#endif /* !_MACHINE_REG_H_ */
