/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#ifndef _VMX_CONTROLS_H_
#define	_VMX_CONTROLS_H_

/* Pin-Based VM-Execution Controls */
#define	PINBASED_EXTINT_EXITING		(1 << 0)
#define	PINBASED_NMI_EXITING		(1 << 3)
#define	PINBASED_VIRTUAL_NMI		(1 << 5)
#define	PINBASED_PREMPTION_TIMER	(1 << 6)
#define	PINBASED_POSTED_INTERRUPT	(1 << 7)

/* Primary Processor-Based VM-Execution Controls */
#define	PROCBASED_INT_WINDOW_EXITING	(1 << 2)
#define	PROCBASED_TSC_OFFSET		(1 << 3)
#define	PROCBASED_HLT_EXITING		(1 << 7)
#define	PROCBASED_INVLPG_EXITING	(1 << 9)
#define	PROCBASED_MWAIT_EXITING		(1 << 10)
#define	PROCBASED_RDPMC_EXITING		(1 << 11)
#define	PROCBASED_RDTSC_EXITING		(1 << 12)
#define	PROCBASED_CR3_LOAD_EXITING	(1 << 15)
#define	PROCBASED_CR3_STORE_EXITING	(1 << 16)
#define	PROCBASED_CR8_LOAD_EXITING	(1 << 19)
#define	PROCBASED_CR8_STORE_EXITING	(1 << 20)
#define	PROCBASED_USE_TPR_SHADOW	(1 << 21)
#define	PROCBASED_NMI_WINDOW_EXITING	(1 << 22)
#define PROCBASED_MOV_DR_EXITING	(1 << 23)
#define	PROCBASED_IO_EXITING		(1 << 24)
#define	PROCBASED_IO_BITMAPS		(1 << 25)
#define	PROCBASED_MTF			(1 << 27)
#define	PROCBASED_MSR_BITMAPS		(1 << 28)
#define	PROCBASED_MONITOR_EXITING	(1 << 29)
#define	PROCBASED_PAUSE_EXITING		(1 << 30)
#define	PROCBASED_SECONDARY_CONTROLS	(1U << 31)

/* Secondary Processor-Based VM-Execution Controls */
#define	PROCBASED2_VIRTUALIZE_APIC_ACCESSES	(1 << 0)
#define	PROCBASED2_ENABLE_EPT			(1 << 1)
#define	PROCBASED2_DESC_TABLE_EXITING		(1 << 2)
#define	PROCBASED2_ENABLE_RDTSCP		(1 << 3)
#define	PROCBASED2_VIRTUALIZE_X2APIC_MODE	(1 << 4)
#define	PROCBASED2_ENABLE_VPID			(1 << 5)
#define	PROCBASED2_WBINVD_EXITING		(1 << 6)
#define	PROCBASED2_UNRESTRICTED_GUEST		(1 << 7)
#define	PROCBASED2_APIC_REGISTER_VIRTUALIZATION	(1 << 8)
#define	PROCBASED2_VIRTUAL_INTERRUPT_DELIVERY	(1 << 9)
#define	PROCBASED2_PAUSE_LOOP_EXITING		(1 << 10)
#define	PROCBASED2_ENABLE_INVPCID		(1 << 12)

/* VM Exit Controls */
#define	VM_EXIT_SAVE_DEBUG_CONTROLS	(1 << 2)
#define	VM_EXIT_HOST_LMA		(1 << 9)
#define	VM_EXIT_LOAD_PERF_GLOBAL_CTRL	(1 << 12)
#define	VM_EXIT_ACKNOWLEDGE_INTERRUPT	(1 << 15)
#define	VM_EXIT_SAVE_PAT		(1 << 18)
#define	VM_EXIT_LOAD_PAT		(1 << 19)
#define	VM_EXIT_SAVE_EFER		(1 << 20)
#define	VM_EXIT_LOAD_EFER		(1 << 21)
#define	VM_EXIT_SAVE_PREEMPTION_TIMER	(1 << 22)

/* VM Entry Controls */
#define	VM_ENTRY_LOAD_DEBUG_CONTROLS	(1 << 2)
#define	VM_ENTRY_GUEST_LMA		(1 << 9)
#define	VM_ENTRY_INTO_SMM		(1 << 10)
#define	VM_ENTRY_DEACTIVATE_DUAL_MONITOR (1 << 11)
#define	VM_ENTRY_LOAD_PERF_GLOBAL_CTRL	(1 << 13)
#define	VM_ENTRY_LOAD_PAT		(1 << 14)
#define	VM_ENTRY_LOAD_EFER		(1 << 15)

#endif
