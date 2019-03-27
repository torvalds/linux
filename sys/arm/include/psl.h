/*	$NetBSD: psl.h,v 1.6 2003/06/16 20:00:58 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Mark Brinicombe.
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
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * psl.h
 *
 * spl prototypes.
 * Eventually this will become a set of defines.
 *
 * Created      : 21/07/95
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PSL_H_
#define _MACHINE_PSL_H_

/*
 * These are the different SPL states
 *
 * Each state has an interrupt mask associated with it which
 * indicate which interrupts are allowed.
 */

#define _SPL_0		0
#define _SPL_SOFTCLOCK	1
#define _SPL_SOFTNET	2
#define _SPL_BIO	3
#define _SPL_NET	4
#define _SPL_SOFTSERIAL	5
#define _SPL_TTY	6
#define _SPL_VM		7
#define _SPL_AUDIO	8
#define _SPL_CLOCK	9
#define _SPL_STATCLOCK	10
#define _SPL_HIGH	11
#define _SPL_SERIAL	12
#define _SPL_LEVELS	13

#ifdef _KERNEL
#ifndef _LOCORE
extern int current_spl_level;

extern u_int spl_masks[_SPL_LEVELS + 1];
extern u_int spl_smasks[_SPL_LEVELS];
#endif /* _LOCORE */
#endif /* _KERNEL */

#endif /* _ARM_PSL_H_ */
/* End of psl.h */
