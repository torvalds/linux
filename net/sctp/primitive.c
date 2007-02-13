/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These functions implement the SCTP primitive functions from Section 10.
 *
 * Note that the descriptions from the specification are USER level
 * functions--this file is the functions which populate the struct proto
 * for SCTP which is the BOTTOM of the sockets interface.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Narasimha Budihal     <narasimha@refcode.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *    Kevin Gao             <kevin.gao@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/list.h> /* For struct list_head */
#include <linux/socket.h>
#include <linux/ip.h>
#include <linux/time.h> /* For struct timeval */
#include <net/sock.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

#define DECLARE_PRIMITIVE(name) \
/* This is called in the code as sctp_primitive_ ## name.  */ \
int sctp_primitive_ ## name(struct sctp_association *asoc, \
			    void *arg) { \
	int error = 0; \
	sctp_event_t event_type; sctp_subtype_t subtype; \
	sctp_state_t state; \
	struct sctp_endpoint *ep; \
	\
	event_type = SCTP_EVENT_T_PRIMITIVE; \
	subtype = SCTP_ST_PRIMITIVE(SCTP_PRIMITIVE_ ## name); \
	state = asoc ? asoc->state : SCTP_STATE_CLOSED; \
	ep = asoc ? asoc->ep : NULL; \
	\
	error = sctp_do_sm(event_type, subtype, state, ep, asoc, \
			   arg, GFP_KERNEL); \
	return error; \
}

/* 10.1 ULP-to-SCTP
 * B) Associate
 *
 * Format: ASSOCIATE(local SCTP instance name, destination transport addr,
 *         outbound stream count)
 * -> association id [,destination transport addr list] [,outbound stream
 *    count]
 *
 * This primitive allows the upper layer to initiate an association to a
 * specific peer endpoint.
 *
 * This version assumes that asoc is fully populated with the initial
 * parameters.  We then return a traditional kernel indicator of
 * success or failure.
 */

/* This is called in the code as sctp_primitive_ASSOCIATE.  */

DECLARE_PRIMITIVE(ASSOCIATE)

/* 10.1 ULP-to-SCTP
 * C) Shutdown
 *
 * Format: SHUTDOWN(association id)
 * -> result
 *
 * Gracefully closes an association. Any locally queued user data
 * will be delivered to the peer. The association will be terminated only
 * after the peer acknowledges all the SCTP packets sent.  A success code
 * will be returned on successful termination of the association. If
 * attempting to terminate the association results in a failure, an error
 * code shall be returned.
 */

DECLARE_PRIMITIVE(SHUTDOWN);

/* 10.1 ULP-to-SCTP
 * C) Abort
 *
 * Format: Abort(association id [, cause code])
 * -> result
 *
 * Ungracefully closes an association. Any locally queued user data
 * will be discarded and an ABORT chunk is sent to the peer. A success
 * code will be returned on successful abortion of the association. If
 * attempting to abort the association results in a failure, an error
 * code shall be returned.
 */

DECLARE_PRIMITIVE(ABORT);

/* 10.1 ULP-to-SCTP
 * E) Send
 *
 * Format: SEND(association id, buffer address, byte count [,context]
 *         [,stream id] [,life time] [,destination transport address]
 *         [,unorder flag] [,no-bundle flag] [,payload protocol-id] )
 * -> result
 *
 * This is the main method to send user data via SCTP.
 *
 * Mandatory attributes:
 *
 *  o association id - local handle to the SCTP association
 *
 *  o buffer address - the location where the user message to be
 *    transmitted is stored;
 *
 *  o byte count - The size of the user data in number of bytes;
 *
 * Optional attributes:
 *
 *  o context - an optional 32 bit integer that will be carried in the
 *    sending failure notification to the ULP if the transportation of
 *    this User Message fails.
 *
 *  o stream id - to indicate which stream to send the data on. If not
 *    specified, stream 0 will be used.
 *
 *  o life time - specifies the life time of the user data. The user data
 *    will not be sent by SCTP after the life time expires. This
 *    parameter can be used to avoid efforts to transmit stale
 *    user messages. SCTP notifies the ULP if the data cannot be
 *    initiated to transport (i.e. sent to the destination via SCTP's
 *    send primitive) within the life time variable. However, the
 *    user data will be transmitted if SCTP has attempted to transmit a
 *    chunk before the life time expired.
 *
 *  o destination transport address - specified as one of the destination
 *    transport addresses of the peer endpoint to which this packet
 *    should be sent. Whenever possible, SCTP should use this destination
 *    transport address for sending the packets, instead of the current
 *    primary path.
 *
 *  o unorder flag - this flag, if present, indicates that the user
 *    would like the data delivered in an unordered fashion to the peer
 *    (i.e., the U flag is set to 1 on all DATA chunks carrying this
 *    message).
 *
 *  o no-bundle flag - instructs SCTP not to bundle this user data with
 *    other outbound DATA chunks. SCTP MAY still bundle even when
 *    this flag is present, when faced with network congestion.
 *
 *  o payload protocol-id - A 32 bit unsigned integer that is to be
 *    passed to the peer indicating the type of payload protocol data
 *    being transmitted. This value is passed as opaque data by SCTP.
 */

DECLARE_PRIMITIVE(SEND);

/* 10.1 ULP-to-SCTP
 * J) Request Heartbeat
 *
 * Format: REQUESTHEARTBEAT(association id, destination transport address)
 *
 * -> result
 *
 * Instructs the local endpoint to perform a HeartBeat on the specified
 * destination transport address of the given association. The returned
 * result should indicate whether the transmission of the HEARTBEAT
 * chunk to the destination address is successful.
 *
 * Mandatory attributes:
 *
 * o association id - local handle to the SCTP association
 *
 * o destination transport address - the transport address of the
 *   association on which a heartbeat should be issued.
 */

DECLARE_PRIMITIVE(REQUESTHEARTBEAT);

/* ADDIP
* 3.1.1 Address Configuration Change Chunk (ASCONF)
*
* This chunk is used to communicate to the remote endpoint one of the
* configuration change requests that MUST be acknowledged.  The
* information carried in the ASCONF Chunk uses the form of a
* Type-Length-Value (TLV), as described in "3.2.1 Optional/
* Variable-length Parameter Format" in RFC2960 [5], forall variable
* parameters.
*/

DECLARE_PRIMITIVE(ASCONF);
