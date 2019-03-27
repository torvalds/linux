/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 *
 *	$NetBSD: powerpc.h,v 1.3 2000/06/01 00:49:59 matt Exp $
 * $FreeBSD$
 */

#ifndef	_MACHINE_PLATFORM_H_
#define	_MACHINE_PLATFORM_H_
  
#include <machine/smp.h>
#include <machine/pcpu.h>

struct mem_region {
	uint64_t	mr_start;
	uint64_t	mr_size;
};

/* Documentation for these functions is in platform_if.m */

void	mem_regions(struct mem_region **, int *, struct mem_region **, int *);
vm_offset_t platform_real_maxaddr(void);

u_long	platform_timebase_freq(struct cpuref *);
  
int	platform_smp_first_cpu(struct cpuref *);
int	platform_smp_next_cpu(struct cpuref *);
int	platform_smp_get_bsp(struct cpuref *);
int	platform_smp_start_cpu(struct pcpu *);
void	platform_smp_timebase_sync(u_long tb, int ap);
void	platform_smp_ap_init(void);
void	platform_smp_probe_threads(void);
  
const char *installed_platform(void);
void platform_probe_and_attach(void);

void platform_sleep(void);
  
#endif	/* _MACHINE_PLATFORM_H_ */
