/*	$OpenBSD: cpu.h,v 1.15 2022/10/21 22:42:36 gkoehler Exp $	*/
/*	$NetBSD: cpu.h,v 1.1 1996/09/30 16:34:21 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

/* 
 * CTL_MACHDEP definitions.
 */
#define CPU_ALLOWAPERTURE	1	/* allow mmap of /dev/xf86 */
#define CPU_ALTIVEC		2	/* altivec is present */
#define CPU_LIDACTION		3	/* action caused by lid close */
#define CPU_PWRACTION		4	/* action caused by power button */
#define CPU_MAXID		5	/* number of valid machdep ids */

#define	CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "allowaperture", CTLTYPE_INT }, \
	{ "altivec", CTLTYPE_INT }, \
	{ "lidaction", CTLTYPE_INT }, \
	{ "pwraction", CTLTYPE_INT }, \
}

#ifdef _KERNEL

#include <powerpc/cpu.h>

/* Frequency scaling */
#define FREQ_FULL	0
#define FREQ_HALF	1
#define FREQ_QUARTER	2	/* Supported only on IBM 970MP */

extern u_int32_t	ppc_curfreq;
extern u_int32_t	ppc_maxfreq;
extern int		ppc_altivec;

extern void (*ppc64_slew_voltage)(u_int);

extern u_int32_t	ticks_per_sec;
extern u_int32_t 	ns_per_tick;

extern int		lid_action;
extern int		pwr_action;

#endif /* _KERNEL */
#endif /* _MACHINE_CPU_H_ */
