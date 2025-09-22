/*	$OpenBSD: param.h,v 1.25 2023/12/14 13:26:49 claudio Exp $	*/

/*
 * Copyright (c) 1994,1995 Mark Brinicombe.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the RiscBSD team.
 * 4. The name "RiscBSD" nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RISCBSD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL RISCBSD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_ARM_PARAM_H_
#define	_ARM_PARAM_H_

#define	MACHINE_ARCH	"arm"
#define	_MACHINE_ARCH	arm
#define	MID_MACHINE	MID_ARM6

#define	PAGE_SHIFT	12
#define	PAGE_SIZE	(1 << PAGE_SHIFT)
#define	PAGE_MASK	(PAGE_SIZE - 1)

#ifdef _KERNEL

#define	NPTEPG		(PAGE_SIZE/(sizeof (pt_entry_t)))

#define	NBPG		PAGE_SIZE
#define	PGSHIFT		PAGE_SHIFT
#define	PGOFSET		PAGE_MASK

#define	UPAGES		2			/* pages of u-area */
#define	USPACE		(UPAGES * PAGE_SIZE)	/* total size of u-area */
#define	USPACE_ALIGN	0			/* u-area alignment 0-none */

#define	NMBCLUSTERS	(32 * 1024)		/* max cluster allocation */

/* Constants used to divide the USPACE area */
/*
 * The USPACE area contains :
 * 1. the user structure for the process
 * 2. the fp context for FP emulation
 * 3. the kernel (svc) stack
 * 4. the undefined instruction stack
 *
 * The layout of the area looks like this
 *
 * | user area | FP context | undefined stack | kernel stack |
 *
 * The size of the user area is known.
 * The size of the FP context is variable depending of the FP emulator
 * in use and whether there is hardware FP support. However we can put
 * an upper limit on it.
 * The undefined stack needs to be at least 512 bytes. This is a requirement
 * of the FP emulators
 * The kernel stack should be at least 4K in size.
 *
 * The stack top addresses are used to set the stack pointers. The stack bottom
 * addresses are the addresses monitored by the diagnostic code for stack
 * overflows.
 */

#define	FPCONTEXTSIZE			(0x100)
#define	USPACE_SVC_STACK_TOP		(USPACE)
#define	USPACE_SVC_STACK_BOTTOM		(USPACE_SVC_STACK_TOP - 0x1000)
#define	USPACE_UNDEF_STACK_TOP		(USPACE_SVC_STACK_BOTTOM - 0x10)
#define	USPACE_UNDEF_STACK_BOTTOM	(sizeof(struct user) + FPCONTEXTSIZE + 10)

#ifndef _LOCORE
void	delay (unsigned);
#define	DELAY(x)	delay(x)
#endif

#if !defined(_LOCORE)
#include <machine/cpu.h>
#endif

/* ARM-specific macro to align a stack pointer (downwards). */
#define	STACKALIGNBYTES		(8 - 1)
#define	STACKALIGN(p)		((u_long)(p) &~ STACKALIGNBYTES)

#endif /* _KERNEL */

#endif /* _ARM_PARAM_H_ */
