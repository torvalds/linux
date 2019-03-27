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

#ifndef _NETINET_SCTP_DTRACE_DEFINE_H_
#define _NETINET_SCTP_DTRACE_DEFINE_H_

#include <sys/kernel.h>
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(sctp);

/********************************************************/
/* Cwnd probe - tracks changes in the congestion window on a netp */
/********************************************************/
/* Initial */
SDT_PROBE_DEFINE5(sctp, cwnd, net, init,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The old value of the cwnd */
    "int");			/* The new value of the cwnd */

/* ACK-INCREASE */
SDT_PROBE_DEFINE5(sctp, cwnd, net, ack,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The old value of the cwnd */
    "int");			/* The new value of the cwnd */

/* ACK-INCREASE */
SDT_PROBE_DEFINE5(sctp, cwnd, net, rttvar,
    "uint64_t",			/* The Vtag << 32 | localport << 16 |
				 * remoteport */
    "uint64_t",			/* obw | nbw */
    "uint64_t",			/* bwrtt | newrtt */
    "uint64_t",			/* flight */
    "uint64_t");		/* (cwnd << 32) | point << 16 | retval(0/1) */

SDT_PROBE_DEFINE5(sctp, cwnd, net, rttstep,
    "uint64_t",			/* The Vtag << 32 | localport << 16 |
				 * remoteport */
    "uint64_t",			/* obw | nbw */
    "uint64_t",			/* bwrtt | newrtt */
    "uint64_t",			/* flight */
    "uint64_t");		/* (cwnd << 32) | point << 16 | retval(0/1) */

/* FastRetransmit-DECREASE */
SDT_PROBE_DEFINE5(sctp, cwnd, net, fr,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The old value of the cwnd */
    "int");			/* The new value of the cwnd */

/* TimeOut-DECREASE */
SDT_PROBE_DEFINE5(sctp, cwnd, net, to,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The old value of the cwnd */
    "int");			/* The new value of the cwnd */

/* BurstLimit-DECREASE */
SDT_PROBE_DEFINE5(sctp, cwnd, net, bl,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The old value of the cwnd */
    "int");			/* The new value of the cwnd */

/* ECN-DECREASE */
SDT_PROBE_DEFINE5(sctp, cwnd, net, ecn,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The old value of the cwnd */
    "int");			/* The new value of the cwnd */

/* PacketDrop-DECREASE */
SDT_PROBE_DEFINE5(sctp, cwnd, net, pd,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The old value of the cwnd */
    "int");			/* The new value of the cwnd */

/********************************************************/
/* Rwnd probe - tracks changes in the receiver window for an assoc */
/********************************************************/
SDT_PROBE_DEFINE4(sctp, rwnd, assoc, val,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "int",			/* The up/down amount */
    "int");			/* The new value of the cwnd */

/********************************************************/
/* flight probe - tracks changes in the flight size on a net or assoc */
/********************************************************/
SDT_PROBE_DEFINE5(sctp, flightsize, net, val,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "uintptr_t",		/* The pointer to the struct sctp_nets *
				 * changing */
    "int",			/* The up/down amount */
    "int");			/* The new value of the cwnd */

/********************************************************/
/* The total flight version */
/********************************************************/
SDT_PROBE_DEFINE4(sctp, flightsize, assoc, val,
    "uint32_t",			/* The Vtag for this end */
    "uint32_t",			/* The port number of the local side << 16 |
				 * port number of remote in network byte
				 * order. */
    "int",			/* The up/down amount */
    "int");			/* The new value of the cwnd */

#endif
