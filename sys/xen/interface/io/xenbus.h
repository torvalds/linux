/*****************************************************************************
 * xenbus.h
 *
 * Xenbus protocol details.
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
 * Copyright (C) 2005 XenSource Ltd.
 */

#ifndef _XEN_PUBLIC_IO_XENBUS_H
#define _XEN_PUBLIC_IO_XENBUS_H

/*
 * The state of either end of the Xenbus, i.e. the current communication
 * status of initialisation across the bus.  States here imply nothing about
 * the state of the connection between the driver and the kernel's device
 * layers.
 */
enum xenbus_state {
    XenbusStateUnknown       = 0,

    XenbusStateInitialising  = 1,

    /*
     * InitWait: Finished early initialisation but waiting for information
     * from the peer or hotplug scripts.
     */
    XenbusStateInitWait      = 2,

    /*
     * Initialised: Waiting for a connection from the peer.
     */
    XenbusStateInitialised   = 3,

    XenbusStateConnected     = 4,

    /*
     * Closing: The device is being closed due to an error or an unplug event.
     */
    XenbusStateClosing       = 5,

    XenbusStateClosed        = 6,

    /*
     * Reconfiguring: The device is being reconfigured.
     */
    XenbusStateReconfiguring = 7,

    XenbusStateReconfigured  = 8
};
typedef enum xenbus_state XenbusState;

#endif /* _XEN_PUBLIC_IO_XENBUS_H */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
