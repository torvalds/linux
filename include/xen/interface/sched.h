/******************************************************************************
 * sched.h
 *
 * Scheduler state interactions
 *
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_SCHED_H__
#define __XEN_PUBLIC_SCHED_H__

#include <xen/interface/event_channel.h>

/*
 * The prototype for this hypercall is:
 *  long sched_op_new(int cmd, void *arg)
 * @cmd == SCHEDOP_??? (scheduler operation).
 * @arg == Operation-specific extra argument(s), as described below.
 *
 * **NOTE**:
 * Versions of Xen prior to 3.0.2 provide only the following legacy version
 * of this hypercall, supporting only the commands yield, block and shutdown:
 *  long sched_op(int cmd, unsigned long arg)
 * @cmd == SCHEDOP_??? (scheduler operation).
 * @arg == 0               (SCHEDOP_yield and SCHEDOP_block)
 *      == SHUTDOWN_* code (SCHEDOP_shutdown)
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
 */
#define SCHEDOP_shutdown    2
struct sched_shutdown {
    unsigned int reason; /* SHUTDOWN_* */
};
DEFINE_GUEST_HANDLE_STRUCT(sched_shutdown);

/*
 * Poll a set of event-channel ports. Return when one or more are pending. An
 * optional timeout may be specified.
 * @arg == pointer to sched_poll structure.
 */
#define SCHEDOP_poll        3
struct sched_poll {
    GUEST_HANDLE(evtchn_port_t) ports;
    unsigned int nr_ports;
    uint64_t timeout;
};
DEFINE_GUEST_HANDLE_STRUCT(sched_poll);

/*
 * Declare a shutdown for another domain. The main use of this function is
 * in interpreting shutdown requests and reasons for fully-virtualized
 * domains.  A para-virtualized domain may use SCHEDOP_shutdown directly.
 * @arg == pointer to sched_remote_shutdown structure.
 */
#define SCHEDOP_remote_shutdown        4
struct sched_remote_shutdown {
    domid_t domain_id;         /* Remote domain ID */
    unsigned int reason;       /* SHUTDOWN_xxx reason */
};

/*
 * Latch a shutdown code, so that when the domain later shuts down it
 * reports this code to the control tools.
 * @arg == as for SCHEDOP_shutdown.
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
struct sched_watchdog {
    uint32_t id;                /* watchdog ID */
    uint32_t timeout;           /* timeout */
};

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

#endif /* __XEN_PUBLIC_SCHED_H__ */
