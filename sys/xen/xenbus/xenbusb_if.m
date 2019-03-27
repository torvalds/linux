#-
# Copyright (c) 2010 Spectra Logic Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions, and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce at minimum a disclaimer
#    substantially similar to the "NO WARRANTY" disclaimer below
#    ("Disclaimer") and any redistribution must be conditioned upon
#    including a substantially similar Disclaimer requirement for further
#    binary redistribution.
#
# NO WARRANTY
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGES.
#
# $FreeBSD$
#

#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>

#include <xen/xenstore/xenstorevar.h>
#include <xen/xenbus/xenbusb.h>

INTERFACE xenbusb;

/**
 * \brief Enumerate all devices of the given type on this bus.
 *
 * \param _dev  NewBus device_t for this XenBus (front/back) bus instance.
 * \param _type String indicating the device sub-tree (e.g. "vfb", "vif")
 *              to enumerate. 
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * Devices that are found should be entered into the NewBus hierarchy via
 * xenbusb_add_device().  xenbusb_add_device() ignores duplicate detects
 * and ignores duplicate devices, so it can be called unconditionally
 * for any device found in the XenStore.
 */
METHOD int enumerate_type {
	device_t _dev;
	const char *_type;
};

/**
 * \brief Determine and store the XenStore path for the other end of
 *        a split device whose local end is represented by ivars.
 *
 * If successful, the xd_otherend_path field of the child's instance
 * variables must be updated.
 *
 * \param _dev    NewBus device_t for this XenBus (front/back) bus instance.
 * \param _ivars  Instance variables from the XenBus child device for
 *                which to perform this function.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
METHOD int get_otherend_node {
	device_t _dev;
	struct xenbus_device_ivars *_ivars;
}

/**
 * \brief Handle a XenStore change detected in the peer tree of a child
 *        device of the bus.
 *
 * \param _bus       NewBus device_t for this XenBus (front/back) bus instance.
 * \param _child     NewBus device_t for the child device whose peer XenStore
 *                   tree has changed.
 * \param _state     The current state of the peer.
 */
METHOD void otherend_changed {
	device_t _bus;
	device_t _child;
	enum xenbus_state _state;
} DEFAULT xenbusb_otherend_changed;

/**
 * \brief Handle a XenStore change detected in the local tree of a child
 *        device of the bus.
 *
 * \param _bus    NewBus device_t for this XenBus (front/back) bus instance.
 * \param _child  NewBus device_t for the child device whose peer XenStore
 *                tree has changed.
 * \param _path   The tree relative sub-path to the modified node.  The empty
 *                string indicates the root of the tree was destroyed.
 */
METHOD void localend_changed {
	device_t _bus;
	device_t _child;
	const char * _path;
} DEFAULT xenbusb_localend_changed;
