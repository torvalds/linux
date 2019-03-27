/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Core definitions and data structures shareable across OS platforms.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Spectra Logic Corporation
 * Copyright (C) 2008 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#ifndef _XEN_XENBUS_XENBUSB_H
#define _XEN_XENBUS_XENBUSB_H

/**
 * \file xenbusb.h
 *
 * Datastructures and function declarations for use in implementing
 * bus attachements (e.g. frontend and backend device buses) for XenBus.
 */

/**
 * Enumeration of state flag values for the xbs_flags field of
 * the xenbusb_softc structure.
 */
typedef enum {
	/** */
	XBS_ATTACH_CH_ACTIVE = 0x01
} xenbusb_softc_flag;

/**
 * \brief Container for all state needed to manage a Xenbus Bus
 *	  attachment.
 */
struct xenbusb_softc {
	/**
	 * XenStore watch used to monitor the subtree of the
	 * XenStore where devices for this bus attachment arrive	
	 * and depart.
	 */
	struct xs_watch	        xbs_device_watch;

	/** Mutex used to protect fields of the xenbusb_softc. */
	struct mtx		xbs_lock;

	/** State flags. */
	xenbusb_softc_flag	xbs_flags;

	/**
	 * A dedicated task for processing child arrival and
	 * departure events.
	 */
	struct task		xbs_probe_children;

	/**
	 * Config Hook used to block boot processing until
	 * XenBus devices complete their connection processing
	 * with other VMs.
	 */
	struct intr_config_hook xbs_attach_ch;

	/**
	 * The number of children for this bus that are still
	 * in the connecting (to other VMs) state.  This variable
	 * is used to determine when to release xbs_attach_ch.
	 */
	u_int			xbs_connecting_children;

	/** The NewBus device_t for this bus attachment. */
	device_t		xbs_dev;

	/**
	 * The VM relative path to the XenStore subtree this
	 * bus attachment manages.
	 */
	const char	       *xbs_node;

	/**
	 * The number of path components (strings separated by the '/'
	 * character) that make up the device ID on this bus.
	 */
	u_int			xbs_id_components;	
};

/**
 * Enumeration of state flag values for the xbs_flags field of
 * the xenbusb_softc structure.
 */
typedef enum {

	/**
	 * This device is contributing to the xbs_connecting_children
	 * count of its parent bus.
	 */
	XDF_CONNECTING = 0x01
} xenbus_dev_flag;

/** Instance variables for devices on a XenBus bus. */
struct xenbus_device_ivars {
	/**
	 * XenStore watch used to monitor the subtree of the
	 * XenStore where information about the otherend of
	 * the split Xen device this device instance represents.
	 */
	struct xs_watch		xd_otherend_watch;

	/**
	 * XenStore watch used to monitor the XenStore sub-tree
	 * associated with this device.  This watch will fire
	 * for modifications that we make from our domain as
	 * well as for those made by the control domain.
	 */
	struct xs_watch		xd_local_watch;

	/** Sleepable lock used to protect instance data. */
	struct sx		xd_lock;

	/** State flags. */
	xenbus_dev_flag		xd_flags;

	/** The NewBus device_t for this XenBus device instance. */
	device_t		xd_dev;

	/**
	 * The VM relative path to the XenStore subtree representing
	 * this VMs half of this device.
	 */
	char		       *xd_node;

	/** The length of xd_node.  */
	int			xd_node_len;

	/** XenBus device type ("vbd", "vif", etc.). */
	char		       *xd_type;

	/**
	 * Cached version of <xd_node>/state node in the XenStore.
	 */
	enum xenbus_state	xd_state;

	/** The VM identifier of the other end of this split device. */
	int			xd_otherend_id;

	/**
	 * The path to the subtree of the XenStore where information
	 * about the otherend of this split device instance.
	 */
	char		       *xd_otherend_path;

	/** The length of xd_otherend_path.  */
	int			xd_otherend_path_len;
};

/**
 * \brief Identify instances of this device type in the system.
 *
 * \param driver  The driver performing this identify action.
 * \param parent  The NewBus parent device for any devices this method adds.
 */
void xenbusb_identify(driver_t *driver __unused, device_t parent);

/**
 * \brief Perform common XenBus bus attach processing.
 *
 * \param dev            The NewBus device representing this XenBus bus.
 * \param bus_node       The XenStore path to the XenStore subtree for
 *                       this XenBus bus.
 * \param id_components  The number of '/' separated path components that
 *                       make up a unique device ID on this XenBus bus.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * Intiailizes the softc for this bus, installs an interrupt driven
 * configuration hook to block boot processing until XenBus devices fully
 * configure, performs an initial probe/attach of the bus, and registers
 * a XenStore watch so we are notified when the bus topology changes.
 */
int xenbusb_attach(device_t dev, char *bus_node, u_int id_components);

/**
 * \brief Perform common XenBus bus resume handling.
 *
 * \param dev  The NewBus device representing this XenBus bus.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xenbusb_resume(device_t dev);

/**
 * \brief Pretty-prints information about a child of a XenBus bus.
 *
 * \param dev    The NewBus device representing this XenBus bus.
 * \param child	 The NewBus device representing a child of dev%'s XenBus bus.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xenbusb_print_child(device_t dev, device_t child);

/**
 * \brief Common XenBus child instance variable read access method.
 *
 * \param dev     The NewBus device representing this XenBus bus.
 * \param child	  The NewBus device representing a child of dev%'s XenBus bus.
 * \param index	  The index of the instance variable to access.
 * \param result  The value of the instance variable accessed.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xenbusb_read_ivar(device_t dev, device_t child, int index,
		      uintptr_t *result);

/**
 * \brief Common XenBus child instance variable write access method.
 *
 * \param dev    The NewBus device representing this XenBus bus.
 * \param child	 The NewBus device representing a child of dev%'s XenBus bus.
 * \param index	 The index of the instance variable to access.
 * \param value  The new value to set in the instance variable accessed.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xenbusb_write_ivar(device_t dev, device_t child, int index,
		       uintptr_t value);

/**
 * \brief Common XenBus method implementing responses to peer state changes.
 * 
 * \param bus       The XenBus bus parent of child.
 * \param child     The XenBus child whose peer stat has changed.
 * \param state     The current state of the peer.
 */
void xenbusb_otherend_changed(device_t bus, device_t child,
			      enum xenbus_state state);

/**
 * \brief Common XenBus method implementing responses to local XenStore changes.
 * 
 * \param bus    The XenBus bus parent of child.
 * \param child  The XenBus child whose peer stat has changed.
 * \param path   The tree relative sub-path to the modified node.  The empty
 *               string indicates the root of the tree was destroyed.
 */
void xenbusb_localend_changed(device_t bus, device_t child, const char *path);

/**
 * \brief Attempt to add a XenBus device instance to this XenBus bus.
 *
 * \param dev   The NewBus device representing this XenBus bus.
 * \param type  The device type being added (e.g. "vbd", "vif").
 * \param id	The device ID for this device.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.  Failure indicates that either the
 *          path to this device no longer exists or insufficient
 *          information exists in the XenStore to create a new
 *          device.
 *
 * If successful, this routine will add a device_t with instance
 * variable storage to the NewBus device topology.  Probe/Attach
 * processing is not performed by this routine, but must be scheduled
 * via the xbs_probe_children task.  This separation of responsibilities
 * is required to avoid hanging up the XenStore event delivery thread
 * with our probe/attach work in the event a device is added via
 * a callback from the XenStore.
 */
int xenbusb_add_device(device_t dev, const char *type, const char *id);

#include "xenbusb_if.h"

#endif /* _XEN_XENBUS_XENBUSB_H */
