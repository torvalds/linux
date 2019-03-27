/******************************************************************************
 * hypervisor.h
  * 
 * Linux-specific hypervisor handling.
 * 
 * Copyright (c) 2002, K A Fraser
 *
 * $FreeBSD$
 */

#ifndef __XEN_HYPERVISOR_H__
#define __XEN_HYPERVISOR_H__

#include <sys/cdefs.h>
#include <sys/systm.h>
#include <xen/interface/xen.h>
#include <xen/interface/platform.h>
#include <xen/interface/event_channel.h>
#include <xen/interface/physdev.h>
#include <xen/interface/sched.h>
#include <xen/interface/callback.h>
#include <xen/interface/memory.h>
#include <machine/xen/hypercall.h>

extern uint64_t get_system_time(int ticks);

static inline int 
HYPERVISOR_console_write(const char *str, int count)
{
    return HYPERVISOR_console_io(CONSOLEIO_write, count, str); 
}

static inline int
HYPERVISOR_yield(void)
{
        int rc = HYPERVISOR_sched_op(SCHEDOP_yield, NULL);

#if CONFIG_XEN_COMPAT <= 0x030002
	if (rc == -ENOXENSYS)
		rc = HYPERVISOR_sched_op_compat(SCHEDOP_yield, 0);
#endif
        return (rc);
}

static inline int
HYPERVISOR_block(
        void)
{
        int rc = HYPERVISOR_sched_op(SCHEDOP_block, NULL);

#if CONFIG_XEN_COMPAT <= 0x030002
	if (rc == -ENOXENSYS)
		rc = HYPERVISOR_sched_op_compat(SCHEDOP_block, 0);
#endif
        return (rc);
}


static inline void 
HYPERVISOR_shutdown(unsigned int reason)
{
	struct sched_shutdown sched_shutdown = {
		.reason = reason
	};

	HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
#if CONFIG_XEN_COMPAT <= 0x030002
	HYPERVISOR_sched_op_compat(SCHEDOP_shutdown, reason);
#endif
}

static inline void
HYPERVISOR_crash(void) 
{
        HYPERVISOR_shutdown(SHUTDOWN_crash); 
	/* NEVER REACHED */
        for (;;) ; /* eliminate noreturn error */ 
}

/* Transfer control to hypervisor until an event is detected on one */
/* of the specified ports or the specified number of ticks elapse */
static inline int
HYPERVISOR_poll(
	evtchn_port_t *ports, unsigned int nr_ports, int ticks)
{
	int rc;
	struct sched_poll sched_poll = {
		.nr_ports = nr_ports,
		.timeout = get_system_time(ticks)
	};
	set_xen_guest_handle(sched_poll.ports, ports);

	rc = HYPERVISOR_sched_op(SCHEDOP_poll, &sched_poll);
#if CONFIG_XEN_COMPAT <= 0x030002
	if (rc == -ENOXENSYS)
		rc = HYPERVISOR_sched_op_compat(SCHEDOP_yield, 0);
#endif	
	return (rc);
}

#endif /* __XEN_HYPERVISOR_H__ */
