/*	$OpenBSD: i82489var.h,v 1.21 2024/11/07 17:24:42 bluhm Exp $	*/
/*	$NetBSD: i82489var.h,v 1.1 2003/02/26 21:26:10 fvdl Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Frank van der Linden.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_I82489VAR_H_
#define _MACHINE_I82489VAR_H_

#include "vmm.h"

/*
 * Software definitions belonging to Local APIC driver.
 */

#ifdef _KERNEL
extern volatile u_int32_t local_apic[];
extern volatile u_int32_t lapic_tpr;
#endif

extern u_int32_t (*lapic_readreg)(int);
extern void (*lapic_writereg)(int, u_int32_t);
u_int32_t lapic_cpu_number(void);

/*
 * "spurious interrupt vector"; vector used by interrupt which was
 * aborted because the CPU masked it after it happened but before it
 * was delivered.. "Oh, sorry, i caught you at a bad time".
 * Low-order 4 bits must be all ones.
 */
extern void Xintrspurious(void);
#define LAPIC_SPURIOUS_VECTOR		0xef

/*
 * Vector used for inter-processor interrupts.
 */
extern void Xintr_lapic_ipi(void);
extern void Xrecurse_lapic_ipi(void);
extern void Xresume_lapic_ipi(void);
#define LAPIC_IPI_VECTOR			0xe0

/*
 * We take 0xf0-0xfe for fast IPI handlers.
 */
#define LAPIC_IPI_OFFSET			0xf0
#define LAPIC_IPI_INVLTLB			(LAPIC_IPI_OFFSET + 0)
#define LAPIC_IPI_INVLPG			(LAPIC_IPI_OFFSET + 1)
#define LAPIC_IPI_INVLRANGE			(LAPIC_IPI_OFFSET + 2)
#define LAPIC_IPI_WBINVD			(LAPIC_IPI_OFFSET + 3)
#define LAPIC_IPI_INVEPT			(LAPIC_IPI_OFFSET + 4)

extern void Xipi_invltlb(void);
extern void Xipi_invltlb_pcid(void);
extern void Xipi_invlpg(void);
extern void Xipi_invlpg_pcid(void);
extern void Xipi_invlrange(void);
extern void Xipi_invlrange_pcid(void);
extern void Xipi_wbinvd(void);
#if NVMM > 0
extern void Xipi_invept(void);
#endif /* NVMM > 0 */

/*
 * Vector used for local apic timer interrupts.
 */

extern void Xintr_lapic_ltimer(void);
extern void Xresume_lapic_ltimer(void);
extern void Xrecurse_lapic_ltimer(void);
#define LAPIC_TIMER_VECTOR		0xc0

/*
 * Vector used for Xen HVM Event Channel Interrupts.
 */
extern void Xintr_xen_upcall(void);
extern void Xresume_xen_upcall(void);
extern void Xrecurse_xen_upcall(void);
#define LAPIC_XEN_VECTOR		0x70

/*
 * Vector used for Hyper-V Interrupts.
 */
extern void Xintr_hyperv_upcall(void);
extern void Xresume_hyperv_upcall(void);
extern void Xrecurse_hyperv_upcall(void);
#define LAPIC_HYPERV_VECTOR		0x71

struct cpu_info;

extern void lapic_boot_init(paddr_t);
extern void lapic_set_lvt(void);
extern void lapic_enable(void);
extern void lapic_disable(void);
extern void lapic_calibrate_timer(struct cpu_info *ci);
extern void lapic_startclock(void);
extern void lapic_initclocks(void);

#endif
