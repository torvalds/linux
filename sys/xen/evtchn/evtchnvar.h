/******************************************************************************
 * evtchn.h
 * 
 * Data structures and definitions private to the FreeBSD implementation
 * of the Xen event channel API.
 * 
 * Copyright (c) 2004, K A Fraser
 * Copyright (c) 2012, Spectra Logic Corporation
 *
 * This file may be distributed separately from the Linux kernel, or
 * incorporated into other software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef __XEN_EVTCHN_EVTCHNVAR_H__
#define __XEN_EVTCHN_EVTCHNVAR_H__

#include <xen/hypervisor.h>
#include <xen/interface/event_channel.h>

enum evtchn_type {
	EVTCHN_TYPE_UNBOUND,
	EVTCHN_TYPE_PIRQ,
	EVTCHN_TYPE_VIRQ,
	EVTCHN_TYPE_IPI,
	EVTCHN_TYPE_PORT,
	EVTCHN_TYPE_COUNT
};

/** Submit a port notification for delivery to a userland evtchn consumer */
void evtchn_device_upcall(evtchn_port_t port);

/**
 * Disable signal delivery for an event channel port, returning its
 * previous mask state.
 *
 * \param port  The event channel port to query and mask.
 *
 * \returns  1 if event delivery was previously disabled.  Otherwise 0.
 */
static inline int
evtchn_test_and_set_mask(evtchn_port_t port)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	return synch_test_and_set_bit(port, s->evtchn_mask);
}

/**
 * Clear any pending event for the given event channel port.
 *
 * \param port  The event channel port to clear.
 */
static inline void 
evtchn_clear_port(evtchn_port_t port)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	synch_clear_bit(port, &s->evtchn_pending[0]);
}

/**
 * Disable signal delivery for an event channel port.
 *
 * \param port  The event channel port to mask.
 */
static inline void
evtchn_mask_port(evtchn_port_t port)
{
	shared_info_t *s = HYPERVISOR_shared_info;

	synch_set_bit(port, &s->evtchn_mask[0]);
}

/**
 * Enable signal delivery for an event channel port.
 *
 * \param port  The event channel port to enable.
 */
static inline void
evtchn_unmask_port(evtchn_port_t port)
{
	evtchn_unmask_t op = { .port = port };

	HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &op);
}

#endif /* __XEN_EVTCHN_EVTCHNVAR_H__ */
