#-
# Copyright (c) 2008 Doug Rabson
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#include <sys/bus.h>

#include <machine/atomic.h>

#include <xen/xen-os.h>
#include <xen/evtchn.h>
#include <xen/xenbus/xenbusvar.h>

INTERFACE xenbus;

/**
 * \brief Callback triggered when the state of the otherend
 *        of a split device changes.
 *
 * \param _dev       NewBus device_t for this XenBus device whose otherend's
 *                   state has changed..
 * \param _newstate  The new state of the otherend device.
 */
METHOD void otherend_changed {
	device_t _dev;
	enum xenbus_state _newstate;
};

/**
 * \brief Callback triggered when the XenStore tree of the local end
 *        of a split device changes.
 *
 * \param _dev   NewBus device_t for this XenBus device whose otherend's
 *               state has changed..
 * \param _path  The tree relative sub-path to the modified node.  The empty
 *               string indicates the root of the tree was destroyed.
 */
METHOD void localend_changed {
	device_t _dev;
	const char * _path;
} DEFAULT xenbus_localend_changed;
