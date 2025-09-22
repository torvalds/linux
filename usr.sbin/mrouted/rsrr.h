/*	$NetBSD: rsrr.h,v 1.2 1995/10/09 03:51:57 thorpej Exp $	*/

/*
 * Copyright (c) 1993, 1998-2001.
 * The University of Southern California/Information Sciences Institute.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define RSRR_SERV_PATH "/tmp/.rsrr_svr"
/* Note this needs to be 14 chars for 4.3 BSD compatibility */
#define RSRR_CLI_PATH "/tmp/.rsrr_cli"

#define RSRR_MAX_LEN 2048
#define RSRR_HEADER_LEN (sizeof(struct rsrr_header))
#define RSRR_RQ_LEN (RSRR_HEADER_LEN + sizeof(struct rsrr_rq))
#define RSRR_RR_LEN (RSRR_HEADER_LEN + sizeof(struct rsrr_rr))
#define RSRR_VIF_LEN (sizeof(struct rsrr_vif))

/* Current maximum number of vifs. */
#define RSRR_MAX_VIFS 32

/* Maximum acceptable version */
#define RSRR_MAX_VERSION 1

/* RSRR message types */
#define RSRR_ALL_TYPES     0
#define RSRR_INITIAL_QUERY 1
#define RSRR_INITIAL_REPLY 2
#define RSRR_ROUTE_QUERY   3
#define RSRR_ROUTE_REPLY   4

/* RSRR Initial Reply (Vif) Status bits
 * Each definition represents the position of the bit from right to left.
 *
 * Right-most bit is the disabled bit, set if the vif is administratively
 * disabled.
 */
#define RSRR_DISABLED_BIT 0
/* All other bits are zeroes */

/* RSRR Route Query/Reply flag bits
 * Each definition represents the position of the bit from right to left.
 *
 * Right-most bit is the Route Change Notification bit, set if the
 * reservation protocol wishes to receive notification of
 * a route change for the source-destination pair listed in the query.
 * Notification is in the form of an unsolicited Route Reply.
 */
#define RSRR_NOTIFICATION_BIT 0
/* Next bit indicates an error returning the Route Reply. */
#define RSRR_ERROR_BIT 1
/* All other bits are zeroes */

/* Definition of an RSRR message header.
 * An Initial Query uses only the header, and an Initial Reply uses
 * the header and a list of vifs.
 */
struct rsrr_header {
    u_char version;			/* RSRR Version, currently 1 */
    u_char type;			/* type of message, as defined above */
    u_char flags;			/* flags; defined by type */
    u_char num;				/* number; defined by type */
};

/* Definition of a vif as seen by the reservation protocol.
 *
 * Routing gives the reservation protocol a list of vifs in the
 * Initial Reply.
 *
 * We explicitly list the ID because we can't assume that all routing
 * protocols will use the same numbering scheme.
 *
 * The status is a bitmask of status flags, as defined above.  It is the
 * responsibility of the reservation protocol to perform any status checks
 * if it uses the MULTICAST_VIF socket option.
 *
 * The threshold indicates the ttl an outgoing packet needs in order to
 * be forwarded. The reservation protocol must perform this check itself if
 * it uses the MULTICAST_VIF socket option.
 *
 * The local address is the address of the physical interface over which
 * packets are sent.
 */
struct rsrr_vif {
    u_char id;				/* vif id */
    u_char threshold;			/* vif threshold ttl */
    u_short status;			/* vif status bitmask */
    struct in_addr local_addr;		/* vif local address */
};

/* Definition of an RSRR Route Query.
 *
 * The query asks routing for the forwarding entry for a particular
 * source and destination.  The query ID uniquely identifies the query
 * for the reservation protocol.  Thus, the combination of the client's
 * address and the query ID forms a unique identifier for routing.
 * Flags are defined above.
 */
struct rsrr_rq {
    struct in_addr dest_addr;		/* destination */
    struct in_addr source_addr;		/* source */
    u_long query_id;			/* query ID */
};

/* Definition of an RSRR Route Reply.
 *
 * Routing uses the reply to give the reservation protocol the
 * forwarding entry for a source-destination pair.  Routing copies the
 * query ID from the query and fills in the incoming vif and a bitmask
 * of the outgoing vifs.
 * Flags are defined above.
 */
struct rsrr_rr {
    struct in_addr dest_addr;		/* destination */
    struct in_addr source_addr;		/* source */
    u_long query_id;			/* query ID */
    u_short in_vif;			/* incoming vif */
    u_short reserved;			/* reserved */
    u_long out_vif_bm;			/* outgoing vif bitmask */
};
