/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * These are the definitions needed for the sctp_ulpq type.  The
 * sctp_ulpq is the interface between the Upper Layer Protocol, or ULP,
 * and the core SCTP state machine.  This is the component which handles
 * reassembly and ordering.
 *
 * The SCTP reference implementation  is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * the SCTP reference implementation  is distributed in the hope that it
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
 * email addresses:
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *   Jon Grimm             <jgrimm@us.ibm.com>
 *   La Monte H.P. Yarroll <piggy@acm.org>
 *   Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#ifndef __sctp_ulpqueue_h__
#define __sctp_ulpqueue_h__

/* A structure to carry information to the ULP (e.g. Sockets API) */
struct sctp_ulpq {
	char malloced;
	char pd_mode;
	struct sctp_association *asoc;
	struct sk_buff_head reasm;
	struct sk_buff_head lobby;
};

/* Prototypes. */
struct sctp_ulpq *sctp_ulpq_init(struct sctp_ulpq *,
				 struct sctp_association *);
void sctp_ulpq_free(struct sctp_ulpq *);

/* Add a new DATA chunk for processing. */
int sctp_ulpq_tail_data(struct sctp_ulpq *, struct sctp_chunk *, gfp_t);

/* Add a new event for propagation to the ULP. */
int sctp_ulpq_tail_event(struct sctp_ulpq *, struct sctp_ulpevent *ev);

/* Renege previously received chunks.  */
void sctp_ulpq_renege(struct sctp_ulpq *, struct sctp_chunk *, gfp_t);

/* Perform partial delivery. */
void sctp_ulpq_partial_delivery(struct sctp_ulpq *, struct sctp_chunk *, gfp_t);

/* Abort the partial delivery. */
void sctp_ulpq_abort_pd(struct sctp_ulpq *, gfp_t);

/* Clear the partial data delivery condition on this socket. */
int sctp_clear_pd(struct sock *sk);

/* Skip over an SSN. */
void sctp_ulpq_skip(struct sctp_ulpq *ulpq, __u16 sid, __u16 ssn);

#endif /* __sctp_ulpqueue_h__ */







