/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_DTRACE_DECLARE_H_
#define _NETINET_SCTP_DTRACE_DECLARE_H_

#include <sys/kernel.h>
#include <sys/sdt.h>

/* Declare the SCTP provider */
SDT_PROVIDER_DECLARE(sctp);

/* The probes we have so far: */

/* One to track a net's cwnd */
/* initial */
SDT_PROBE_DECLARE(sctp, cwnd, net, init);
/* update at a ack -- increase */
SDT_PROBE_DECLARE(sctp, cwnd, net, ack);
/* update at a fast retransmit -- decrease */
SDT_PROBE_DECLARE(sctp, cwnd, net, fr);
/* update at a time-out -- decrease */
SDT_PROBE_DECLARE(sctp, cwnd, net, to);
/* update at a burst-limit -- decrease */
SDT_PROBE_DECLARE(sctp, cwnd, net, bl);
/* update at a ECN -- decrease */
SDT_PROBE_DECLARE(sctp, cwnd, net, ecn);
/* update at a Packet-Drop -- decrease */
SDT_PROBE_DECLARE(sctp, cwnd, net, pd);
/* Rttvar probe declaration */
SDT_PROBE_DECLARE(sctp, cwnd, net, rttvar);
SDT_PROBE_DECLARE(sctp, cwnd, net, rttstep);

/* One to track an associations rwnd */
SDT_PROBE_DECLARE(sctp, rwnd, assoc, val);

/* One to track a net's flight size */
SDT_PROBE_DECLARE(sctp, flightsize, net, val);

/* One to track an associations flight size */
SDT_PROBE_DECLARE(sctp, flightsize, assoc, val);






#endif
