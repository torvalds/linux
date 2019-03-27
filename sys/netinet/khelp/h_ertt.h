/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010
 * 	Swinburne University of Technology, Melbourne, Australia
 * Copyright (c) 2010 Lawrence Stewart <lstewart@freebsd.org>
 * All rights reserved.
 *
 * This software was developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, by David Hayes, made
 * possible in part by a grant from the Cisco University Research Program Fund
 * at Community Foundation Silicon Valley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * The ERTT (Enhanced Round Trip Time) Khelp module calculates an estimate of
 * the instantaneous TCP RTT which, for example, is used by delay-based
 * congestion control schemes. When the module is loaded, ERTT data is
 * calculated for each active TCP connection and encapsulated within a
 * "struct ertt".
 *
 * This software was first released in 2010 by David Hayes and Lawrence Stewart
 * whilst working on the NewTCP research project at Swinburne University of
 * Technology's Centre for Advanced Internet Architectures, Melbourne,
 * Australia, which was made possible in part by a grant from the Cisco
 * University Research Program Fund at Community Foundation Silicon Valley.
 * Testing and development was further assisted by a grant from the FreeBSD
 * Foundation. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#ifndef	_NETINET_KHELP_H_ERTT_
#define	_NETINET_KHELP_H_ERTT_

struct txseginfo;

/* Structure used as the ertt data block. */
struct ertt {
	/* Information about transmitted segments to aid in RTT calculation. */
	TAILQ_HEAD(txseginfo_head, txseginfo) txsegi_q;
	/* Bytes TX so far in marked RTT. */
	long		bytes_tx_in_rtt;
	/* Final version of above. */
	long		bytes_tx_in_marked_rtt;
	/* cwnd for marked RTT. */
	unsigned long	marked_snd_cwnd;
	/* Per-packet measured RTT. */
	int		rtt;
	/* Maximum RTT measured. */
	int		maxrtt;
	/* Minimum RTT measured. */
	int		minrtt;
	/* Guess if the receiver is using delayed ack. */
	int		dlyack_rx;
	/* Keep track of inconsistencies in packet timestamps. */
	int		timestamp_errors;
	/* RTT for a marked packet. */
	int		markedpkt_rtt;
	/* Flags to signal conditions between hook function calls. */
	uint32_t	flags;
};

/* Flags for struct ertt. */
#define	ERTT_NEW_MEASUREMENT		0x01
#define	ERTT_MEASUREMENT_IN_PROGRESS	0x02
#define	ERTT_TSO_DISABLED		0x04

#endif /* _NETINET_KHELP_H_ERTT_ */
