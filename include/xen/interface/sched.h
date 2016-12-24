/******************************************************************************
 * sched.h
 *
 * Scheduler state interactions
 *
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
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_SCHED_H__
#define __XEN_PUBLIC_SCHED_H__

#include <xen/interface/event_channel.h>

/*
 * Guest Scheduler Operations
 *
 * The SCHEDOP interface provides mechanisms for a guest to interact
 * with the scheduler, including yield, blocking and shutting itself
 * down.
 */

/*
 * The prototype for this hypercall is:
 * long HYPERVISOR_sched_op(enum sched_op cmd, void *arg, ...)
 *
 * @cmd == SCHEDOP_??? (scheduler operation).
 * @arg == Operation-specific extra argument(s), as described below.
 * ...  == Additional Operation-specific extra arguments, described below.
 *
 * Versions of Xen prior to 3.0.2 provided only the following legacy version
 * of this hypercall, supporting only the commands yield, block and shutdown:
 *  long sched_op(int cmd, unsigned long arg)
 * @cmd == SCHEDOP_??? (scheduler operation).
 * @arg == 0               (SCHEDOP_yield and SCHEDOP_block)
 *      == SHUTDOWN_* code (SCHEDOP_shutdown)
 *
 * This legacy version is available to new guests as:
 * long HYPERVISOR_sched_op_compat(enum sched_op cmd, unsigned long arg)
 */

/*
 * Voluntarily yield the CPU.
 * @arg == NULL.
 */
#define SCHEDOP_yield       0

/*
 * Block execution of this VCPU until an event is received for processing.
 * If called with event upcalls masked, this operation will atomically
 * reenable event delivery and check for pending events before blocking the
 * VCPU. This avoids a "wakeup waiting" race.
 * @arg == NULL.
 */
#define SCHEDOP_block       1

/*
 * Halt execution of this domain (all VCPUs) and notify the system controller.
 * @arg == pointer to sched_shutdown structure.
 *
 * If the sched_shutdown_t reason is SHUTDOWN_suspend then
 * x86 PV guests must also set RDX (EDX for 32-bit guests) to the MFN
 * of the guest's start info page.  RDX/EDX is the third hypercall
 * argument.
 *
 * In addition, which reason is SHUTDOWN_suspend this hypercall
 * returns 1 if suspend was cancelled or the domain was merely
 * checkpointed, and 0 if it is resuming in a new domain.
 */
#define SCHEDOP_shutdown    2

/*
 * Poll a set of event-channel ports. Return when one or more are pending. An
 * optional timeout may be specified.
 * @arg == pointer to sched_poll structure.
 */
#define SCHEDOP_poll        3

/*
 * Declare a shutdown for another domain. The main use of this function is
 * in interpreting shutdown requests and reasons for fully-virtualized
 * domains.  A para-virtualized domain may use SCHEDOP_shutdown directly.
 * @arg == pointer to sched_remote_shutdown structure.
 */
#define SCHEDOP_remote_shutdown        4

/*
 * Latch a shutdown code, so that when the domain later shuts down it
 * reports this code to the control tools.
 * @arg == sched_shutdown, as for SCHEDOP_shutdown.
 */
#define SCHEDOP_shutdown_code 5

/*
 * Setup, poke and destroy a domain watchdog timer.
 * @arg == pointer to sched_watchdog structure.
 * With id == 0, setup a domain watchdog timer to cause domain shutdown
 *               after timeout, returns watchdog id.
 * With id != 0 and timeout == 0, destroy domain watchdog timer.
 * With id != 0 and timeout != 0, poke watchdog timer and set new timeout.
 */
#define SCHEDOP_watchdog    6

/*
 * Override the current vcpu affinity by pinning it to one physical cpu or
 * undo this override restoring the previous affinity.
 * @arg == pointer to sched_pin_override structure.
 *
 * A negative pcpu value will undo a previous pin override and restore the
 * previous cpu affinity.
 * This call is allowed for the hardware domain only and requires the cpu
 * to be part of the domain's cpupool.
 */
#define SCHEDOP_pin_override 7

struct sched_shutdown {
    unsigned int reason; /* SHUTDOWN_* => shutdown reason */
};
DEFINE_GUEST_HANDLE_STRUCT(sched_shutdown);

struct sched_poll {
    GUEST_HANDLE(evtchn_port_t) ports;
    unsigned int nr_ports;
    uint64_t timeout;
};
DEFINE_GUEST_HANDLE_STRUCT(sched_poll);

struct sched_remote_shutdown {
    domid_t domain_id;         /* Remote domain ID */
    unsigned int reason;       /* SHUTDOWN_* => shutdown reason */
};
DEFINE_GUEST_HANDLE_STRUCT(sched_remote_shutdown);

struct sched_watchdog {
    uint32_t id;                /* watchdog ID */
    uint32_t timeout;           /* timeout */
};
DEFINE_GUEST_HANDLE_STRUCT(sched_watchdog);

struct sched_pin_override {
    int32_t pcpu;
};
DEFINE_GUEST_HANDLE_STRUCT(sched_pin_override);

/*
 * Reason codes for SCHEDOP_shutdown. These may be interpreted by control
 * software to determine the appropriate action. For the most part, Xen does
 * not care about the shutdown code.
 */
#define SHUTDOWN_poweroff   0  /* Domain exited normally. Clean up and kill. */
#define SHUTDOWN_reboot     1  /* Clean up, kill, and then restart.          */
#define SHUTDOWN_suspend    2  /* Clean up, save suspend info, kill.         */
#define SHUTDOWN_crash      3  /* Tell controller we've crashed.             */
#define SHUTDOWN_watchdog   4  /* Restart because watchdog time expired.     */

/*
 * Domain asked to perform 'soft reset' for it. The expected behavior is to
 * reset internal Xen state for the domain returning it to the point where it
 * was created but leaving the domain's memory contents and vCPU contexts
 * intact. This will allow the domain to start over and set up all Xen specific
 * interfaces again.
 */
#define SHUTDOWN_soft_reset 5
#define SHUTDOWN_MAX        5  /* Maximum valid shutdown reason.             */

#endif /* __XEN_PUBLIC_SCHED_H__ */
