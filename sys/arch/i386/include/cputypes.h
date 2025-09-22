/*	$OpenBSD: cputypes.h,v 1.15 2022/08/22 08:53:55 jsg Exp $	*/
/*	$NetBSD: cputypes.h,v 1.10 1997/10/18 04:51:03 mikel Exp $	*/

/*
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
 */

/*
 *	Classes of Processor
 */

#define	CPUCLASS_486	1
#define	CPUCLASS_586	2
#define	CPUCLASS_686	3

/*
 * CPU vendors
 */

#define CPUVENDOR_UNKNOWN	-1
#define CPUVENDOR_INTEL		0
#define CPUVENDOR_CYRIX		1
#define CPUVENDOR_AMD		3
#define CPUVENDOR_IDT		4
#define CPUVENDOR_TRANSMETA	6
#define CPUVENDOR_NS		7
#define CPUVENDOR_VIA		8
#define CPUVENDOR_SIS		9

/*
 * Some other defines, dealing with values returned by cpuid.
 */

#define CPU_MAXMODEL	15	/* Models within family range 0-15 */
#define CPU_DEFMODEL	16	/* Value for unknown model -> default  */
#define CPU_MINFAMILY	 4	/* Lowest that cpuid can return (486) */
#define CPU_MAXFAMILY  0xf	/* Highest we know (686) */
