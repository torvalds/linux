/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2004 Juli Mallett.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_HWFUNC_H_
#define	_MACHINE_HWFUNC_H_

#include <sys/_cpuset.h>

struct timecounter;

/*
 * Hooks downward into platform functionality.
 */
void platform_reset(void);
void platform_start(__register_t, __register_t,  __register_t, __register_t);

/* For clocks and ticks and such */
void platform_initclocks(void);
uint64_t platform_get_frequency(void);
unsigned platform_get_timecount(struct timecounter *);

/* For hardware specific CPU initialization */
void platform_cpu_init(void);

#ifdef SMP

/*
 * Spin up the AP so that it starts executing MP bootstrap entry point: mpentry
 *
 * Returns 0 on sucess and non-zero on failure.
 */
int platform_start_ap(int processor_id);

/*
 * Platform-specific initialization that needs to be done when an AP starts
 * running. This function is called from the MP bootstrap code in mpboot.S
 */
void platform_init_ap(int processor_id);

/*
 * Return a plaform-specific interrrupt number that is used to deliver IPIs.
 *
 * This hardware interrupt is used to deliver IPIs exclusively and must
 * not be used for any other interrupt source.
 */
int platform_ipi_hardintr_num(void);
int platform_ipi_softintr_num(void);

#ifdef PLATFORM_INIT_SECONDARY
/*
 * Set up IPIs for this CPU.
 */
void platform_init_secondary(int cpuid);
#endif

/*
 * Trigger a IPI interrupt on 'cpuid'.
 */
void platform_ipi_send(int cpuid);

/*
 * Quiesce the IPI interrupt source on the current cpu.
 */
void platform_ipi_clear(void);

/*
 * Return the processor id.
 *
 * Note that this function is called in early boot when stack is not available.
 */
extern int platform_processor_id(void);

/*
 * Return the cpumask of available processors.
 */
extern void platform_cpu_mask(cpuset_t *mask);

/*
 * Return the topology of processors on this platform
 */
struct cpu_group *platform_smp_topo(void);

#endif	/* SMP */

#endif /* !_MACHINE_HWFUNC_H_ */
