/*	$NetBSD: sysarch.h,v 1.5 2003/09/11 09:40:12 kleink Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996-1997 Mark Brinicombe.
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
 *	This product includes software developed by Mark Brinicombe.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD$ */

#ifndef _ARM_SYSARCH_H_
#define _ARM_SYSARCH_H_

#include <machine/armreg.h>

/*
 * The ARM_TP_ADDRESS points to a special purpose page, which is used as local
 * store for the ARM per-thread data and Restartable Atomic Sequences support.
 * Put it just above the "high" vectors' page.
 * The cpu_switch() code assumes ARM_RAS_START is ARM_TP_ADDRESS + 4, and
 * ARM_RAS_END is ARM_TP_ADDRESS + 8, so if that ever changes, be sure to
 * update the cpu_switch() (and cpu_throw()) code as well.
 * In addition, code in arm/include/atomic.h and arm/arm/exception.S
 * assumes that ARM_RAS_END is at ARM_RAS_START+4, so be sure to update those
 * if ARM_RAS_END moves in relation to ARM_RAS_START (look for occurrences
 * of ldr/str rm,[rn, #4]).
 */

/* ARM_TP_ADDRESS is needed for processors that don't support
 * the exclusive-access opcodes introduced with ARMv6K. */
#if __ARM_ARCH <= 5
#define ARM_TP_ADDRESS		(ARM_VECTORS_HIGH + 0x1000)
#define ARM_RAS_START		(ARM_TP_ADDRESS + 4)
#define ARM_RAS_END		(ARM_TP_ADDRESS + 8)
#endif

#ifndef LOCORE
#ifndef __ASSEMBLER__

#include <sys/cdefs.h>

/*
 * Pickup definition of uintptr_t
 */
#include <sys/stdint.h>

/*
 * Architecture specific syscalls (arm)
 */

#define ARM_SYNC_ICACHE		0
#define ARM_DRAIN_WRITEBUF	1
#define ARM_SET_TP		2
#define ARM_GET_TP		3
#define ARM_GET_VFPSTATE	4

struct arm_sync_icache_args {
	uintptr_t	addr;		/* Virtual start address */
	size_t		len;		/* Region size */
};

struct arm_get_vfpstate_args {
	size_t		mc_vfp_size;
	void 		*mc_vfp;
};

#ifndef _KERNEL
__BEGIN_DECLS
int	arm_sync_icache (u_int addr, int len);
int	arm_drain_writebuf (void);
int	sysarch(int, void *);
__END_DECLS
#endif

#endif /* __ASSEMBLER__ */
#endif /* LOCORE */

#endif /* !_ARM_SYSARCH_H_ */
