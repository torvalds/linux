/******************************************************************************
 * xen_intr.h
 * 
 * APIs for managing Xen event channel, virtual IRQ, and physical IRQ
 * notifications.
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
#ifndef _XEN_INTR_H_
#define _XEN_INTR_H_

#include <xen/interface/event_channel.h>

/** Registered Xen interrupt callback handle. */
typedef void * xen_intr_handle_t;

/** If non-zero, the hypervisor has been configured to use a direct vector */
extern int xen_vector_callback_enabled;

void xen_intr_handle_upcall(struct trapframe *trap_frame);

/**
 * Associate an already allocated local event channel port an interrupt
 * handler.
 *
 * \param dev         The device making this bind request.
 * \param local_port  The event channel to bind.
 * \param filter      An interrupt filter handler.  Specify NULL
 *                    to always dispatch to the ithread handler.
 * \param handler     An interrupt ithread handler.  Optional (can
 *                    specify NULL) if all necessary event actions
 *                    are performed by filter.
 * \param arg         Argument to present to both filter and handler.
 * \param irqflags    Interrupt handler flags.  See sys/bus.h.
 * \param handlep     Pointer to an opaque handle used to manage this
 *                    registration.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_intr_bind_local_port(device_t dev, evtchn_port_t local_port,
	driver_filter_t filter, driver_intr_t handler, void *arg,
	enum intr_type irqflags, xen_intr_handle_t *handlep);

/**
 * Allocate a local event channel port, accessible by the specified
 * remote/foreign domain and, if successful, associate the port with
 * the specified interrupt handler.
 *
 * \param dev            The device making this bind request.
 * \param remote_domain  Remote domain grant permission to signal the
 *                       newly allocated local port.
 * \param filter         An interrupt filter handler.  Specify NULL
 *                       to always dispatch to the ithread handler.
 * \param handler        An interrupt ithread handler.  Optional (can
 *                       specify NULL) if all necessary event actions
 *                       are performed by filter.
 * \param arg            Argument to present to both filter and handler.
 * \param irqflags       Interrupt handler flags.  See sys/bus.h.
 * \param handlep        Pointer to an opaque handle used to manage this
 *                       registration.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_intr_alloc_and_bind_local_port(device_t dev,
	u_int remote_domain, driver_filter_t filter, driver_intr_t handler,
	void *arg, enum intr_type irqflags, xen_intr_handle_t *handlep);

/**
 * Associate the specified interrupt handler with the remote event
 * channel port specified by remote_domain and remote_port.
 *
 * \param dev            The device making this bind request.
 * \param remote_domain  The domain peer for this event channel connection.
 * \param remote_port    Remote domain's local port number for this event
 *                       channel port.
 * \param filter         An interrupt filter handler.  Specify NULL
 *                       to always dispatch to the ithread handler.
 * \param handler        An interrupt ithread handler.  Optional (can
 *                       specify NULL) if all necessary event actions
 *                       are performed by filter.
 * \param arg            Argument to present to both filter and handler.
 * \param irqflags       Interrupt handler flags.  See sys/bus.h.
 * \param handlep        Pointer to an opaque handle used to manage this
 *                       registration.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_intr_bind_remote_port(device_t dev, u_int remote_domain,
	evtchn_port_t remote_port, driver_filter_t filter,
	driver_intr_t handler, void *arg, enum intr_type irqflags,
	xen_intr_handle_t *handlep);

/**
 * Associate the specified interrupt handler with the specified Xen
 * virtual interrupt source.
 *
 * \param dev       The device making this bind request.
 * \param virq      The Xen virtual IRQ number for the Xen interrupt
 *                  source being hooked.
 * \param cpu       The cpu on which interrupt events should be delivered. 
 * \param filter    An interrupt filter handler.  Specify NULL
 *                  to always dispatch to the ithread handler.
 * \param handler   An interrupt ithread handler.  Optional (can
 *                  specify NULL) if all necessary event actions
 *                  are performed by filter.
 * \param arg       Argument to present to both filter and handler.
 * \param irqflags  Interrupt handler flags.  See sys/bus.h.
 * \param handlep   Pointer to an opaque handle used to manage this
 *                  registration.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_intr_bind_virq(device_t dev, u_int virq, u_int cpu,
	driver_filter_t filter, driver_intr_t handler,
	void *arg, enum intr_type irqflags, xen_intr_handle_t *handlep);

/**
 * Allocate a local event channel port for servicing interprocessor
 * interupts and, if successful, associate the port with the specified
 * interrupt handler.
 *
 * \param cpu       The cpu receiving the IPI.
 * \param filter    The interrupt filter servicing this IPI.
 * \param irqflags  Interrupt handler flags.  See sys/bus.h.
 * \param handlep   Pointer to an opaque handle used to manage this
 *                  registration.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_intr_alloc_and_bind_ipi(u_int cpu,
	driver_filter_t filter, enum intr_type irqflags,
	xen_intr_handle_t *handlep);

/**
 * Register a physical interrupt vector and setup the interrupt source.
 *
 * \param vector        The global vector to use.
 * \param trig          Default trigger method.
 * \param pol           Default polarity of the interrupt.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_register_pirq(int vector, enum intr_trigger trig,
	enum intr_polarity pol);

/**
 * Unbind an interrupt handler from its interrupt source.
 *
 * \param handlep  A pointer to the opaque handle that was initialized
 *		   at the time the interrupt source was bound.
 *
 * \returns  0 on success, otherwise an errno.
 *
 * \note  The event channel, if any, that was allocated at bind time is
 *        closed upon successful return of this method.
 *
 * \note  It is always safe to call xen_intr_unbind() on a handle that
 *        has been initilized to NULL.
 */
void xen_intr_unbind(xen_intr_handle_t *handle);

/**
 * Add a description to an interrupt handler.
 *
 * \param handle  The opaque handle that was initialized at the time
 *		  the interrupt source was bound.
 *
 * \param fmt     The sprintf compatible format string for the description,
 *                followed by optional sprintf arguments.
 *
 * \returns  0 on success, otherwise an errno.
 */
int
xen_intr_describe(xen_intr_handle_t port_handle, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/**
 * Signal the remote peer of an interrupt source associated with an
 * event channel port.
 *
 * \param handle  The opaque handle that was initialized at the time
 *                the interrupt source was bound.
 *
 * \note  For xen interrupt sources other than event channel ports,
 *        this method takes no action.
 */
void xen_intr_signal(xen_intr_handle_t handle);

/**
 * Get the local event channel port number associated with this interrupt
 * source.
 *
 * \param handle  The opaque handle that was initialized at the time
 *                the interrupt source was bound.
 *
 * \returns  0 if the handle is invalid, otherwise positive port number.
 */
evtchn_port_t xen_intr_port(xen_intr_handle_t handle);

/**
 * Setup MSI vector interrupt(s).
 *
 * \param dev     The device that requests the binding.
 *
 * \param vector  Requested initial vector to bind the MSI interrupt(s) to.
 *
 * \param count   Number of vectors to allocate.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_register_msi(device_t dev, int vector, int count);

/**
 * Teardown a MSI vector interrupt.
 *
 * \param vector  Requested vector to release.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_release_msi(int vector);

/**
 * Bind an event channel port with a handler
 *
 * \param dev       The device making this bind request.
 * \param filter    An interrupt filter handler.  Specify NULL
 *                  to always dispatch to the ithread handler.
 * \param handler   An interrupt ithread handler.  Optional (can
 *                  specify NULL) if all necessary event actions
 *                  are performed by filter.
 * \param arg       Argument to present to both filter and handler.
 * \param irqflags  Interrupt handler flags.  See sys/bus.h.
 * \param handle    Opaque handle used to manage this registration.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_intr_add_handler(const char *name, driver_filter_t filter,
	driver_intr_t handler, void *arg, enum intr_type flags,
	xen_intr_handle_t handle);

/**
 * Get a reference to an event channel port
 *
 * \param port	    Event channel port to which we get a reference.
 * \param handlep   Pointer to an opaque handle used to manage this
 *                  registration.
 *
 * \returns  0 on success, otherwise an errno.
 */
int xen_intr_get_evtchn_from_port(evtchn_port_t port,
	xen_intr_handle_t *handlep);

/**
 * Register the IO-APIC PIRQs when running in legacy PVH Dom0 mode.
 *
 * \param pic	    PIC instance.
 *
 * NB: this should be removed together with the support for legacy PVH mode.
 */
struct pic;
void xenpv_register_pirqs(struct pic *pic);

#endif /* _XEN_INTR_H_ */
