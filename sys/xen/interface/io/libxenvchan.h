/**
 * @file
 * @section AUTHORS
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *
 *  Authors:
 *       Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *       Daniel De Graaf <dgdegra@tycho.nsa.gov>
 *
 * @section LICENSE
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
 * @section DESCRIPTION
 *
 *  Originally borrowed from the Qubes OS Project, http://www.qubes-os.org,
 *  this code has been substantially rewritten to use the gntdev and gntalloc
 *  devices instead of raw MFNs and map_foreign_range.
 *
 *  This is a library for inter-domain communication.  A standard Xen ring
 *  buffer is used, with a datagram-based interface built on top.  The grant
 *  reference and event channels are shared in XenStore under a user-specified
 *  path.
 *
 *  The ring.h macros define an asymmetric interface to a shared data structure
 *  that assumes all rings reside in a single contiguous memory space. This is
 *  not suitable for vchan because the interface to the ring is symmetric except
 *  for the setup. Unlike the producer-consumer rings defined in ring.h, the
 *  size of the rings used in vchan are determined at execution time instead of
 *  compile time, so the macros in ring.h cannot be used to access the rings.
 */

#include <stdint.h>
#include <sys/types.h>

struct ring_shared {
	uint32_t cons, prod;
};

#define VCHAN_NOTIFY_WRITE 0x1
#define VCHAN_NOTIFY_READ 0x2

/**
 * vchan_interface: primary shared data structure
 */
struct vchan_interface {
	/**
	 * Standard consumer/producer interface, one pair per buffer
	 * left is client write, server read
	 * right is client read, server write
	 */
	struct ring_shared left, right;
	/**
	 * size of the rings, which determines their location
	 * 10   - at offset 1024 in ring's page
	 * 11   - at offset 2048 in ring's page
	 * 12+  - uses 2^(N-12) grants to describe the multi-page ring
	 * These should remain constant once the page is shared.
	 * Only one of the two orders can be 10 (or 11).
	 */
	uint16_t left_order, right_order;
	/**
	 * Shutdown detection:
	 *  0: client (or server) has exited
	 *  1: client (or server) is connected
	 *  2: client has not yet connected
	 */
	uint8_t cli_live, srv_live;
	/**
	 * Notification bits:
	 *  VCHAN_NOTIFY_WRITE: send notify when data is written
	 *  VCHAN_NOTIFY_READ: send notify when data is read (consumed)
	 * cli_notify is used for the client to inform the server of its action
	 */
	uint8_t cli_notify, srv_notify;
	/**
	 * Grant list: ordering is left, right. Must not extend into actual ring
	 * or grow beyond the end of the initial shared page.
	 * These should remain constant once the page is shared, to allow
	 * for possible remapping by a client that restarts.
	 */
	uint32_t grants[0];
};

