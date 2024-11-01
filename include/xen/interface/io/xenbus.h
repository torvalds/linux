/* SPDX-License-Identifier: MIT */
/*****************************************************************************
 * xenbus.h
 *
 * Xenbus protocol details.
 *
 * Copyright (C) 2005 XenSource Ltd.
 */

#ifndef _XEN_PUBLIC_IO_XENBUS_H
#define _XEN_PUBLIC_IO_XENBUS_H

/* The state of either end of the Xenbus, i.e. the current communication
   status of initialisation across the bus.  States here imply nothing about
   the state of the connection between the driver and the kernel's device
   layers.  */
enum xenbus_state
{
	XenbusStateUnknown      = 0,
	XenbusStateInitialising = 1,
	XenbusStateInitWait     = 2,  /* Finished early
					 initialisation, but waiting
					 for information from the peer
					 or hotplug scripts. */
	XenbusStateInitialised  = 3,  /* Initialised and waiting for a
					 connection from the peer. */
	XenbusStateConnected    = 4,
	XenbusStateClosing      = 5,  /* The device is being closed
					 due to an error or an unplug
					 event. */
	XenbusStateClosed       = 6,

	/*
	* Reconfiguring: The device is being reconfigured.
	*/
	XenbusStateReconfiguring = 7,

	XenbusStateReconfigured  = 8
};

#endif /* _XEN_PUBLIC_IO_XENBUS_H */
