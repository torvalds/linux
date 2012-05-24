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
 * val[63:56] == 2: val[7:0] is a vector number.
 * If val == 0 then CPU0 event-channel notifications are not delivered.
 */
#define HVM_PARAM_CALLBACK_IRQ 0

#define HVM_PARAM_STORE_PFN    1
#define HVM_PARAM_STORE_EVTCHN 2

#define HVM_PARAM_PAE_ENABLED  4

#define HVM_PARAM_IOREQ_PFN    5

#define HVM_PARAM_BUFIOREQ_PFN 6

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

#define HVM_NR_PARAMS          19

#endif /* __XEN_PUBLIC_HVM_PARAMS_H__ */
