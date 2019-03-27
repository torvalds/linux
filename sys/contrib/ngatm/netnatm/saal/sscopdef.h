/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/saal/sscopdef.h,v 1.4 2004/07/08 08:22:17 brandt Exp $
 *
 * Definitions of SSCOP constants and parameter blocks. This is seen by
 * the outside world.
 */
#ifndef _NETNATM_SAAL_SSCOPDEF_H_
#define _NETNATM_SAAL_SSCOPDEF_H_

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/stdint.h>
#else
#include <stdint.h>
#endif

/*
 * AA-interface signals
 */
enum sscop_aasig {
	SSCOP_ESTABLISH_request,	/* <- UU, BR */
	SSCOP_ESTABLISH_indication,	/* -> UU */
	SSCOP_ESTABLISH_response,	/* <- UU, BR */
	SSCOP_ESTABLISH_confirm,	/* -> UU */

	SSCOP_RELEASE_request,		/* <- UU */
	SSCOP_RELEASE_indication,	/* -> UU, SRC */
	SSCOP_RELEASE_confirm,		/* -> */

	SSCOP_DATA_request,		/* <- MU */
	SSCOP_DATA_indication,		/* -> MU, SN */

	SSCOP_UDATA_request,		/* <- MU */
	SSCOP_UDATA_indication,		/* -> MU */

	SSCOP_RECOVER_indication,	/* -> */
	SSCOP_RECOVER_response,		/* <- */

	SSCOP_RESYNC_request,		/* <- UU */
	SSCOP_RESYNC_indication,	/* -> UU */
	SSCOP_RESYNC_response,		/* <- */
	SSCOP_RESYNC_confirm,		/* -> */

	SSCOP_RETRIEVE_request,		/* <- RN */
	SSCOP_RETRIEVE_indication,	/* -> MU */
	SSCOP_RETRIEVE_COMPL_indication,/* -> */
};

enum sscop_maasig {
	SSCOP_MDATA_request,		/* <- MU */
	SSCOP_MDATA_indication,		/* -> MU */
	SSCOP_MERROR_indication,	/* -> CODE, CNT */
};

/*
 * Values for retrieval. Numbers in SSCOP are 24bit, so
 * we can use the large values
 */
enum {
	SSCOP_MAXSEQNO		= 0xffffff,

	SSCOP_RETRIEVE_UNKNOWN	= SSCOP_MAXSEQNO + 1,
	SSCOP_RETRIEVE_TOTAL	= SSCOP_MAXSEQNO + 2,
};

/*
 * SSCOP states
 */
enum sscop_state {
	SSCOP_IDLE,		/* initial state */
	SSCOP_OUT_PEND,		/* outgoing connection pending */
	SSCOP_IN_PEND,		/* incoming connection pending */
	SSCOP_OUT_DIS_PEND,	/* outgoing disconnect pending */
	SSCOP_OUT_RESYNC_PEND,	/* outgoing resynchronisation pending */
	SSCOP_IN_RESYNC_PEND,	/* incoming resynchronisation pending */
	SSCOP_OUT_REC_PEND,	/* outgoing recovery pending */
	SSCOP_REC_PEND,		/* recovery response pending */
	SSCOP_IN_REC_PEND,	/* incoming recovery pending */
	SSCOP_READY,		/* data transfer ready */
};
#define SSCOP_NSTATES 10

struct sscop_param {
	uint32_t	timer_cc;	/* timer_cc in msec */
	uint32_t	timer_poll;	/* timer_poll im msec */
	uint32_t	timer_keep_alive;/* timer_keep_alive in msec */
	uint32_t	timer_no_response;/*timer_no_response in msec */
	uint32_t	timer_idle;	/* timer_idle in msec */
	uint32_t	maxk;		/* maximum user data in bytes */
	uint32_t	maxj;		/* maximum u-u info in bytes */
	uint32_t	maxcc;		/* max. retransmissions for control packets */
	uint32_t	maxpd;		/* max. vt(pd) before sending poll */
	uint32_t	maxstat;	/* max. number of elements in stat list */
	uint32_t	mr;		/* initial window */
	uint32_t	flags;		/* flags */
};
enum {
	SSCOP_ROBUST 	= 0x0001,	/* atmf/97-0216 robustness */
	SSCOP_POLLREX	= 0x0002,	/* send POLL after retransmit */
};

enum {
	SSCOP_SET_TCC		= 0x0001,
	SSCOP_SET_TPOLL		= 0x0002,
	SSCOP_SET_TKA		= 0x0004,
	SSCOP_SET_TNR		= 0x0008,
	SSCOP_SET_TIDLE		= 0x0010,
	SSCOP_SET_MAXK		= 0x0020,
	SSCOP_SET_MAXJ		= 0x0040,
	SSCOP_SET_MAXCC		= 0x0080,
	SSCOP_SET_MAXPD		= 0x0100,
	SSCOP_SET_MAXSTAT	= 0x0200,
	SSCOP_SET_MR		= 0x0400,
	SSCOP_SET_ROBUST	= 0x0800,
	SSCOP_SET_POLLREX	= 0x1000,

	SSCOP_SET_ALLMASK	= 0x1fff,
};

enum {
	SSCOP_DBG_USIG	= 0x0001,
	SSCOP_DBG_TIMER	= 0x0002,
	SSCOP_DBG_BUG	= 0x0004,
	SSCOP_DBG_INSIG	= 0x0008,
	SSCOP_DBG_STATE	= 0x0010,
	SSCOP_DBG_PDU	= 0x0020,
	SSCOP_DBG_ERR	= 0x0040,
	SSCOP_DBG_EXEC	= 0x0080,
	SSCOP_DBG_FLOW	= 0x0100,
};

#endif
