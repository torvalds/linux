/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2007, Keir Fraser
 */

#ifndef __XEN_PUBLIC_HVM_PARAMS_H__
#define __XEN_PUBLIC_HVM_PARAMS_H__

#include "hvm_op.h"

/*
 * Parameter space for HVMOP_{set,get}_param.
 */

/*
 * How should CPU0 event-channel notifications be delivered?
 * val[63:56] == 0: val[55:0] is a delivery GSI (Global System Interrupt).
 * val[63:56] == 1: val[55:0] is a delivery PCI INTx line, as follows:
 *                  Domain = val[47:32], Bus  = val[31:16],
 *                  DevFn  = val[15: 8], IntX = val[ 1: 0]
 * val[63:56] == 2: val[7:0] is a vector number, check for
 *                  XENFEAT_hvm_callback_vector to know if this delivery
 *                  method is available.
 * If val == 0 then CPU0 event-channel notifications are not delivered.
 */
#define HVM_PARAM_CALLBACK_IRQ 0

/*
 * These are not used by Xen. They are here for convenience of HVM-guest
 * xenbus implementations.
 */
#define HVM_PARAM_STORE_PFN    1
#define HVM_PARAM_STORE_EVTCHN 2

#define HVM_PARAM_PAE_ENABLED  4

#define HVM_PARAM_IOREQ_PFN    5

#define HVM_PARAM_BUFIOREQ_PFN 6
#define HVM_PARAM_BUFIOREQ_EVTCHN 26

#if defined(__i386__) || defined(__x86_64__)

/*
 * Viridian enlightenments
 *
 * (See http://download.microsoft.com/download/A/B/4/AB43A34E-BDD0-4FA6-BDEF-79EEF16E880B/Hypervisor%20Top%20Level%20Functional%20Specification%20v4.0.docx)
 *
 * To expose viridian enlightenments to the guest set this parameter
 * to the desired feature mask. The base feature set must be present
 * in any valid feature mask.
 */
#define HVM_PARAM_VIRIDIAN     9

/* Base+Freq viridian feature sets:
 *
 * - Hypercall MSRs (HV_X64_MSR_GUEST_OS_ID and HV_X64_MSR_HYPERCALL)
 * - APIC access MSRs (HV_X64_MSR_EOI, HV_X64_MSR_ICR and HV_X64_MSR_TPR)
 * - Virtual Processor index MSR (HV_X64_MSR_VP_INDEX)
 * - Timer frequency MSRs (HV_X64_MSR_TSC_FREQUENCY and
 *   HV_X64_MSR_APIC_FREQUENCY)
 */
#define _HVMPV_base_freq 0
#define HVMPV_base_freq  (1 << _HVMPV_base_freq)

/* Feature set modifications */

/* Disable timer frequency MSRs (HV_X64_MSR_TSC_FREQUENCY and
 * HV_X64_MSR_APIC_FREQUENCY).
 * This modification restores the viridian feature set to the
 * original 'base' set exposed in releases prior to Xen 4.4.
 */
#define _HVMPV_no_freq 1
#define HVMPV_no_freq  (1 << _HVMPV_no_freq)

/* Enable Partition Time Reference Counter (HV_X64_MSR_TIME_REF_COUNT) */
#define _HVMPV_time_ref_count 2
#define HVMPV_time_ref_count  (1 << _HVMPV_time_ref_count)

/* Enable Reference TSC Page (HV_X64_MSR_REFERENCE_TSC) */
#define _HVMPV_reference_tsc 3
#define HVMPV_reference_tsc  (1 << _HVMPV_reference_tsc)

#define HVMPV_feature_mask \
	(HVMPV_base_freq | \
	 HVMPV_no_freq | \
	 HVMPV_time_ref_count | \
	 HVMPV_reference_tsc)

#endif

/*
 * Set mode for virtual timers (currently x86 only):
 *  delay_for_missed_ticks (default):
 *   Do not advance a vcpu's time beyond the correct delivery time for
 *   interrupts that have been missed due to preemption. Deliver missed
 *   interrupts when the vcpu is rescheduled and advance the vcpu's virtual
 *   time stepwise for each one.
 *  no_delay_for_missed_ticks:
 *   As above, missed interrupts are delivered, but guest time always tracks
 *   wallclock (i.e., real) time while doing so.
 *  no_missed_ticks_pending:
 *   No missed interrupts are held pending. Instead, to ensure ticks are
 *   delivered at some non-zero rate, if we detect missed ticks then the
 *   internal tick alarm is not disabled if the VCPU is preempted during the
 *   next tick period.
 *  one_missed_tick_pending:
 *   Missed interrupts are collapsed together and delivered as one 'late tick'.
 *   Guest time always tracks wallclock (i.e., real) time.
 */
#define HVM_PARAM_TIMER_MODE   10
#define HVMPTM_delay_for_missed_ticks    0
#define HVMPTM_no_delay_for_missed_ticks 1
#define HVMPTM_no_missed_ticks_pending   2
#define HVMPTM_one_missed_tick_pending   3

/* Boolean: Enable virtual HPET (high-precision event timer)? (x86-only) */
#define HVM_PARAM_HPET_ENABLED 11

/* Identity-map page directory used by Intel EPT when CR0.PG=0. */
#define HVM_PARAM_IDENT_PT     12

/* Device Model domain, defaults to 0. */
#define HVM_PARAM_DM_DOMAIN    13

/* ACPI S state: currently support S0 and S3 on x86. */
#define HVM_PARAM_ACPI_S_STATE 14

/* TSS used on Intel when CR0.PE=0. */
#define HVM_PARAM_VM86_TSS     15

/* Boolean: Enable aligning all periodic vpts to reduce interrupts */
#define HVM_PARAM_VPT_ALIGN    16

/* Console debug shared memory ring and event channel */
#define HVM_PARAM_CONSOLE_PFN    17
#define HVM_PARAM_CONSOLE_EVTCHN 18

/*
 * Select location of ACPI PM1a and TMR control blocks. Currently two locations
 * are supported, specified by version 0 or 1 in this parameter:
 *   - 0: default, use the old addresses
 *        PM1A_EVT == 0x1f40; PM1A_CNT == 0x1f44; PM_TMR == 0x1f48
 *   - 1: use the new default qemu addresses
 *        PM1A_EVT == 0xb000; PM1A_CNT == 0xb004; PM_TMR == 0xb008
 * You can find these address definitions in <hvm/ioreq.h>
 */
#define HVM_PARAM_ACPI_IOPORTS_LOCATION 19

/* Deprecated */
#define HVM_PARAM_MEMORY_EVENT_CR0          20
#define HVM_PARAM_MEMORY_EVENT_CR3          21
#define HVM_PARAM_MEMORY_EVENT_CR4          22
#define HVM_PARAM_MEMORY_EVENT_INT3         23
#define HVM_PARAM_MEMORY_EVENT_SINGLE_STEP  25
#define HVM_PARAM_MEMORY_EVENT_MSR          30

/* Boolean: Enable nestedhvm (hvm only) */
#define HVM_PARAM_NESTEDHVM    24

/* Params for the mem event rings */
#define HVM_PARAM_PAGING_RING_PFN   27
#define HVM_PARAM_MONITOR_RING_PFN  28
#define HVM_PARAM_SHARING_RING_PFN  29

/* SHUTDOWN_* action in case of a triple fault */
#define HVM_PARAM_TRIPLE_FAULT_REASON 31

#define HVM_PARAM_IOREQ_SERVER_PFN 32
#define HVM_PARAM_NR_IOREQ_SERVER_PAGES 33

/* Location of the VM Generation ID in guest physical address space. */
#define HVM_PARAM_VM_GENERATION_ID_ADDR 34

/* Boolean: Enable altp2m */
#define HVM_PARAM_ALTP2M       35

#define HVM_NR_PARAMS          36

#endif /* __XEN_PUBLIC_HVM_PARAMS_H__ */
