/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993 Christopher G. Demetriou
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_CPUTYPES_H_
#define	_MACHINE_CPUTYPES_H_

#include <x86/cputypes.h>

/*
 * Classes of processor.
 */
#define	CPUCLASS_286		0
#define	CPUCLASS_386		1
#define	CPUCLASS_486		2
#define	CPUCLASS_586		3
#define	CPUCLASS_686		4

/*
 * Kinds of processor.
 */
#define	CPU_286			0	/* Intel 80286 */
#define	CPU_386SX		1	/* Intel 80386SX */
#define	CPU_386			2	/* Intel 80386DX */
#define	CPU_486SX		3	/* Intel 80486SX */
#define	CPU_486			4	/* Intel 80486DX */
#define	CPU_586			5	/* Intel Pentium */
#define	CPU_486DLC		6	/* Cyrix 486DLC */
#define	CPU_686			7	/* Pentium Pro */
#define	CPU_M1SC		8	/* Cyrix M1sc (aka 5x86) */
#define	CPU_M1			9	/* Cyrix M1 (aka 6x86) */
#define	CPU_BLUE		10	/* IBM BlueLighting CPU */
#define	CPU_M2			11	/* Cyrix M2 (enhanced 6x86 with MMX) */
#define	CPU_NX586		12	/* NexGen (now AMD) 586 */
#define	CPU_CY486DX		13	/* Cyrix 486S/DX/DX2/DX4 */
#define	CPU_PII			14	/* Intel Pentium II */
#define	CPU_PIII		15	/* Intel Pentium III */
#define	CPU_P4			16	/* Intel Pentium 4 */
#define	CPU_GEODE1100		17	/* NS Geode SC1100 */

#ifndef LOCORE
extern int	cpu;
extern int	cpu_class;
#endif

#endif /* !_MACHINE_CPUTYPES_H_ */
