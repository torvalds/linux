/*	$OpenBSD: fsm.c,v 1.11 2024/02/26 08:25:51 yasuoka Exp $ */

/**@file
 * This file was adapted from NetBSD:/usr/src/usr.sbin/pppd/pppd/fsm.c
 */
/*
 * fsm.c is simple and it can be use without modifications.  So keep the
 * original as much as possible.  (2005/04 yasuoka)
 *
 * XXX: I think the same message for initial configure-request is a bad idea
 * XXX: on resending configure-request.(yasuoka)
 */
/*	$NetBSD: fsm.c,v 1.13 2000/09/23 22:39:35 christos Exp $	*/

/*
 * fsm.c - {Link, IP} Control Protocol Finite State Machine.
 *
 * Copyright (c) 1989 Carnegie Mellon University.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Carnegie Mellon University.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * TODO:
 * Randomize fsm id on link/init.
 * Deal with variable outgoing MTU.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdlib.h>

/* npppd related headers below */
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <time.h>
#include <event.h>
#include "debugutil.h"
#include "npppd.h"
#include "fsm.h"

#ifdef	FSM_DEBUG
#define	FSMDEBUG(x)	fsm_log x
#define	FSM_ASSERT(x)	ASSERT(x)
#else
#define	FSMDEBUG(x)
#define	FSM_ASSERT(x)
#endif

#define	HEADERLEN	4

#ifdef RCSID
static const char rcsid[] = RCSID;
#endif

static void fsm_timeout(void *);
static void fsm_rconfreq(fsm *, u_char, u_char *, int len);
static void fsm_rconfack(fsm *, int, u_char *, int);
static void fsm_rconfnakrej(fsm *, int, int, u_char *, int);
static void fsm_rtermreq(fsm *, int, u_char *, int);
static void fsm_rtermack(fsm *);
static void fsm_rcoderej(fsm *, u_char *, int);
static void fsm_sconfreq(fsm *, int);

#define PROTO_NAME(f)	((f)->callbacks->proto_name)

void
fsm_evtimer_timeout(int fd, short evtype, void *ctx)
{
	struct evtimer_wrap *wrap;

	wrap = ctx;
	wrap->func(wrap->ctx);
}


/*
 * fsm_init - Initialize fsm.
 *
 * Initialize fsm state.
 */
void
fsm_init(fsm *f)
{
    f->state = INITIAL;
    f->flags = 0;
    f->id = 0;				/* XXX Start with random id? */
    f->timeouttime = DEFTIMEOUT;
    f->maxconfreqtransmits = DEFMAXCONFREQS;
    f->maxtermtransmits = DEFMAXTERMREQS;
    f->maxnakloops = DEFMAXNAKLOOPS;
    f->term_reason_len = 0;
    memset(&f->timerctx, 0, sizeof(f->timerctx));
    f->timerctx.ctx = f;
}


/*
 * fsm_lowerup - The lower layer is up.
 */
void
fsm_lowerup(fsm *f)
{
    switch( f->state ){
    case INITIAL:
	f->state = CLOSED;
	break;

    case STARTING:
	if( f->flags & OPT_SILENT )
	    f->state = STOPPED;
	else {
	    /* Send an initial configure-request */
	    fsm_sconfreq(f, 0);
	    f->state = REQSENT;
	}
	break;

    default:
	FSMDEBUG((f, LOG_DEBUG, "Up event in state %d!", f->state));
    }
}


/*
 * fsm_lowerdown - The lower layer is down.
 *
 * Cancel all timeouts and inform upper layers.
 */
void
fsm_lowerdown(fsm *f)
{
    switch( f->state ){
    case CLOSED:
	f->state = INITIAL;
	break;

    case STOPPED:
	f->state = STARTING;
	if( f->callbacks->starting )
	    (*f->callbacks->starting)(f);
	break;

    case CLOSING:
	f->state = INITIAL;
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	break;

    case STOPPING:
    case REQSENT:
    case ACKRCVD:
    case ACKSENT:
	f->state = STARTING;
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	break;

    case OPENED:
	if( f->callbacks->down )
	    (*f->callbacks->down)(f);
	f->state = STARTING;
	break;

    default:
	FSMDEBUG((f, LOG_DEBUG, "Down event in state %d!", f->state));
    }
}


/*
 * fsm_open - Link is allowed to come up.
 */
void
fsm_open(fsm *f)
{
    switch( f->state ){
    case INITIAL:
	f->state = STARTING;
	if( f->callbacks->starting )
	    (*f->callbacks->starting)(f);
	break;

    case CLOSED:
	if( f->flags & OPT_SILENT )
	    f->state = STOPPED;
	else {
	    /* Send an initial configure-request */
	    fsm_sconfreq(f, 0);
	    f->state = REQSENT;
	}
	break;

    case CLOSING:
	f->state = STOPPING;
	/* fall through */
    case STOPPED:
    case OPENED:
	if( f->flags & OPT_RESTART ){
	    fsm_lowerdown(f);
	    fsm_lowerup(f);
	}
	break;
    }
}


/*
 * fsm_close - Start closing connection.
 *
 * Cancel timeouts and either initiate close or possibly go directly to
 * the CLOSED state.
 */
void
fsm_close(fsm *f, const char *reason)
{
    f->term_reason = (char *)reason;
    f->term_reason_len = (reason == NULL? 0: strlen(reason));
    switch( f->state ){
    case STARTING:
	f->state = INITIAL;
	break;
    case STOPPED:
	f->state = CLOSED;
	break;
    case STOPPING:
	f->state = CLOSING;
	break;

    case REQSENT:
    case ACKRCVD:
    case ACKSENT:
    case OPENED:
	if( f->state != OPENED )
	    UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	else if( f->callbacks->down )
	    (*f->callbacks->down)(f);	/* Inform upper layers we're down */

	/* Init restart counter, send Terminate-Request */
	f->retransmits = f->maxtermtransmits;
	fsm_sdata(f, TERMREQ, f->reqid = ++f->id,
		  (u_char *) f->term_reason, f->term_reason_len);
	TIMEOUT(fsm_timeout, f, f->timeouttime);
	--f->retransmits;

	f->state = CLOSING;
	break;
    }
}


/*
 * fsm_timeout - Timeout expired.
 */
static void
fsm_timeout(void *arg)
{
    fsm *f = (fsm *) arg;

    switch (f->state) {
    case CLOSING:
    case STOPPING:
	if( f->retransmits <= 0 ){
	    /*
	     * We've waited for an ack long enough.  Peer probably heard us.
	     */
	    f->state = (f->state == CLOSING)? CLOSED: STOPPED;
	    if( f->callbacks->finished )
		(*f->callbacks->finished)(f);
	} else {
	    /* Send Terminate-Request */
	    fsm_sdata(f, TERMREQ, f->reqid = ++f->id,
		      (u_char *) f->term_reason, f->term_reason_len);
	    TIMEOUT(fsm_timeout, f, f->timeouttime);
	    --f->retransmits;
	}
	break;

    case REQSENT:
    case ACKRCVD:
    case ACKSENT:
	if (f->retransmits <= 0) {
	    fsm_log(f, LOG_WARNING, "timeout sending Config-Requests\n");
	    f->state = STOPPED;
	    if( (f->flags & OPT_PASSIVE) == 0 && f->callbacks->finished )
		(*f->callbacks->finished)(f);

	} else {
	    /* Retransmit the configure-request */
	    if (f->callbacks->retransmit)
		(*f->callbacks->retransmit)(f);
	    fsm_sconfreq(f, 1);		/* Re-send Configure-Request */
	    if( f->state == ACKRCVD )
		f->state = REQSENT;
	}
	break;

    default:
	FSMDEBUG((f, LOG_DEBUG, "Timeout event in state %d!", f->state));
    }
}


/*
 * fsm_input - Input packet.
 */
void
fsm_input(fsm *f, u_char *inpacket, int l)
{
    u_char *inp;
    u_char code, id;
    int len;

    /*
     * Parse header (code, id and length).
     * If packet too short, drop it.
     */
    inp = inpacket;
    if (l < HEADERLEN) {
	FSMDEBUG((f, LOG_DEBUG, "fsm_input(): Rcvd short header."));
	return;
    }
    GETCHAR(code, inp);
    GETCHAR(id, inp);
    GETSHORT(len, inp);
    if (len < HEADERLEN) {
	FSMDEBUG((f, LOG_DEBUG, "fsm_input(): Rcvd illegal length."));
	return;
    }
    if (len > l) {
	FSMDEBUG((f, LOG_DEBUG, "fsm_input(): Rcvd short packet."));
	return;
    }
    len -= HEADERLEN;		/* subtract header length */

    if( f->state == INITIAL || f->state == STARTING ){
	FSMDEBUG((f, LOG_DEBUG, "fsm_input(): Rcvd packet in state %d.",
	    f->state));
	return;
    }

    /*
     * Action depends on code.
     */
    switch (code) {
    case CONFREQ:
	fsm_rconfreq(f, id, inp, len);
	break;

    case CONFACK:
	fsm_rconfack(f, id, inp, len);
	break;

    case CONFNAK:
    case CONFREJ:
	fsm_rconfnakrej(f, code, id, inp, len);
	break;

    case TERMREQ:
	fsm_rtermreq(f, id, inp, len);
	break;

    case TERMACK:
	fsm_rtermack(f);
	break;

    case CODEREJ:
	fsm_rcoderej(f, inp, len);
	break;

    default:
	if( !f->callbacks->extcode
	   || !(*f->callbacks->extcode)(f, code, id, inp, len) )
	    fsm_sdata(f, CODEREJ, ++f->id, inpacket, len + HEADERLEN);
	break;
    }
}


/*
 * fsm_rconfreq - Receive Configure-Request.
 */
static void
fsm_rconfreq(fsm *f, u_char id, u_char *inp, int len)
{
    int code, reject_if_disagree;

    switch( f->state ){
    case CLOSED:
	/* Go away, we're closed */
	fsm_sdata(f, TERMACK, id, NULL, 0);
	return;
    case CLOSING:
    case STOPPING:
	return;

    case OPENED:
	/* Go down and restart negotiation */
	if( f->callbacks->down )
	    (*f->callbacks->down)(f);	/* Inform upper layers */
	fsm_sconfreq(f, 0);		/* Send initial Configure-Request */
	break;

    case STOPPED:
	/* Negotiation started by our peer */
	fsm_sconfreq(f, 0);		/* Send initial Configure-Request */
	f->state = REQSENT;
	break;
    }

    /*
     * Pass the requested configuration options
     * to protocol-specific code for checking.
     */
    if (f->callbacks->reqci){		/* Check CI */
	reject_if_disagree = (f->nakloops >= f->maxnakloops);
	code = (*f->callbacks->reqci)(f, inp, &len, reject_if_disagree);
    } else if (len)
	code = CONFREJ;			/* Reject all CI */
    else
	code = CONFACK;

    /* send the Ack, Nak or Rej to the peer */
    fsm_sdata(f, code, id, inp, len);

    if (code == CONFACK) {
	if (f->state == ACKRCVD) {
	    UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	    f->state = OPENED;
	    if (f->callbacks->up)
		(*f->callbacks->up)(f);	/* Inform upper layers */
	} else
	    f->state = ACKSENT;
	f->nakloops = 0;

    } else {
	/* we sent CONFNAK or CONFREJ */
	if (f->state != ACKRCVD)
	    f->state = REQSENT;
	if( code == CONFNAK )
	    ++f->nakloops;
    }
}


/*
 * fsm_rconfack - Receive Configure-Ack.
 */
static void
fsm_rconfack(fsm *f, int id, u_char *inp, int len)
{
    if (id != f->reqid || f->seen_ack)		/* Expected id? */
	return;					/* Nope, toss... */
    if( !(f->callbacks->ackci? (*f->callbacks->ackci)(f, inp, len):
	  (len == 0)) ){
	/* Ack is bad - ignore it */
	fsm_log(f, LOG_ERR, "Received bad configure-ack: %p(%d)", inp, len);
	return;
    }
    f->seen_ack = 1;

    switch (f->state) {
    case CLOSED:
    case STOPPED:
	fsm_sdata(f, TERMACK, id, NULL, 0);
	break;

    case REQSENT:
	f->state = ACKRCVD;
	f->retransmits = f->maxconfreqtransmits;
	break;

    case ACKRCVD:
	/* Huh? an extra valid Ack? oh well... */
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	fsm_sconfreq(f, 0);
	f->state = REQSENT;
	break;

    case ACKSENT:
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	f->state = OPENED;
	f->retransmits = f->maxconfreqtransmits;
	if (f->callbacks->up)
	    (*f->callbacks->up)(f);	/* Inform upper layers */
	break;

    case OPENED:
	/* Go down and restart negotiation */
	if (f->callbacks->down)
	    (*f->callbacks->down)(f);	/* Inform upper layers */
	fsm_sconfreq(f, 0);		/* Send initial Configure-Request */
	f->state = REQSENT;
	break;
    }
}


/*
 * fsm_rconfnakrej - Receive Configure-Nak or Configure-Reject.
 */
static void
fsm_rconfnakrej(fsm *f, int code, int id, u_char *inp, int len)
{
    int (*proc)(fsm *, u_char *, int);
    int ret;

    if (id != f->reqid || f->seen_ack)	/* Expected id? */
	return;				/* Nope, toss... */
    proc = (code == CONFNAK)? f->callbacks->nakci: f->callbacks->rejci;
    if (!proc || !(ret = proc(f, inp, len))) {
	/* Nak/reject is bad - ignore it */
	fsm_log(f, LOG_INFO, "Received bad configure-%s: %p(%d)",
	    (code == CONFNAK)? "nak" : "rej", inp, len);
	return;
    }
    f->seen_ack = 1;

    switch (f->state) {
    case CLOSED:
    case STOPPED:
	fsm_sdata(f, TERMACK, id, NULL, 0);
	break;

    case REQSENT:
    case ACKSENT:
	/* They didn't agree to what we wanted - try another request */
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	if (ret < 0)
	    f->state = STOPPED;		/* kludge for stopping CCP */
	else
	    fsm_sconfreq(f, 0);		/* Send Configure-Request */
	break;

    case ACKRCVD:
	/* Got a Nak/reject when we had already had an Ack?? oh well... */
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	fsm_sconfreq(f, 0);
	f->state = REQSENT;
	break;

    case OPENED:
	/* Go down and restart negotiation */
	if (f->callbacks->down)
	    (*f->callbacks->down)(f);	/* Inform upper layers */
	fsm_sconfreq(f, 0);		/* Send initial Configure-Request */
	f->state = REQSENT;
	break;
    }
}


/*
 * fsm_rtermreq - Receive Terminate-Req.
 */
static void
fsm_rtermreq(fsm *f, int id, u_char *p, int len)
{
    switch (f->state) {
    case ACKRCVD:
    case ACKSENT:
	f->state = REQSENT;		/* Start over but keep trying */
	break;

    case OPENED:
	fsm_log(f, LOG_INFO, "terminated by peer");
	if (f->callbacks->down)
	    (*f->callbacks->down)(f);	/* Inform upper layers */
	f->retransmits = 0;
	f->state = STOPPING;
	TIMEOUT(fsm_timeout, f, f->timeouttime);
	break;
    }

    fsm_sdata(f, TERMACK, id, NULL, 0);
}


/*
 * fsm_rtermack - Receive Terminate-Ack.
 */
static void
fsm_rtermack(fsm *f)
{
    switch (f->state) {
    case CLOSING:
	UNTIMEOUT(fsm_timeout, f);
	f->state = CLOSED;
	if( f->callbacks->finished )
	    (*f->callbacks->finished)(f);
	break;
    case STOPPING:
	UNTIMEOUT(fsm_timeout, f);
	f->state = STOPPED;
	if( f->callbacks->finished )
	    (*f->callbacks->finished)(f);
	break;

    case ACKRCVD:
	f->state = REQSENT;
	break;

    case OPENED:
	if (f->callbacks->down)
	    (*f->callbacks->down)(f);	/* Inform upper layers */
	fsm_sconfreq(f, 0);
	f->state = REQSENT;
	break;
    }
}


/*
 * fsm_rcoderej - Receive an Code-Reject.
 */
static void
fsm_rcoderej(fsm *f, u_char *inp, int len)
{
    u_char code, id;

    if (len < HEADERLEN) {
	FSMDEBUG((f, LOG_DEBUG,
	    "fsm_rcoderej: Rcvd short Code-Reject packet!"));
	return;
    }
    GETCHAR(code, inp);
    GETCHAR(id, inp);
    fsm_log(f, LOG_INFO,
	"%s: Rcvd Code-Reject for code %d, id %d", PROTO_NAME(f), code, id);

    if( f->state == ACKRCVD )
	f->state = REQSENT;
}


/*
 * fsm_protreject - Peer doesn't speak this protocol.
 *
 * Treat this as a catastrophic error (RXJ-).
 */
void
fsm_protreject(fsm *f)
{
    switch( f->state ){
    case CLOSING:
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	/* fall through */
    case CLOSED:
	f->state = CLOSED;
	if( f->callbacks->finished )
	    (*f->callbacks->finished)(f);
	break;

    case STOPPING:
    case REQSENT:
    case ACKRCVD:
    case ACKSENT:
	UNTIMEOUT(fsm_timeout, f);	/* Cancel timeout */
	/* fall through */
    case STOPPED:
	f->state = STOPPED;
	if( f->callbacks->finished )
	    (*f->callbacks->finished)(f);
	break;

    case OPENED:
	if( f->callbacks->down )
	    (*f->callbacks->down)(f);

	/* Init restart counter, send Terminate-Request */
	f->retransmits = f->maxtermtransmits;
	fsm_sdata(f, TERMREQ, f->reqid = ++f->id,
		  (u_char *) f->term_reason, f->term_reason_len);
	TIMEOUT(fsm_timeout, f, f->timeouttime);
	--f->retransmits;

	f->state = STOPPING;
	break;

    default:
	FSMDEBUG((f, LOG_DEBUG,
	    "Protocol-reject event in state %d!", f->state));
    }
}


/*
 * fsm_sconfreq - Send a Configure-Request.
 */
static void
fsm_sconfreq(fsm *f, int retransmit)
{
    u_char *outp;
    int cilen;

    if( f->state != REQSENT && f->state != ACKRCVD && f->state != ACKSENT ){
	/* Not currently negotiating - reset options */
	if( f->callbacks->resetci )
	    (*f->callbacks->resetci)(f);
	f->nakloops = 0;
    }

    if( !retransmit ){
	/* New request - reset retransmission counter, use new ID */
	f->retransmits = f->maxconfreqtransmits;
	f->reqid = ++f->id;
    }

    f->seen_ack = 0;

    /*
     * Make up the request packet
     */
    outp = f->ppp->outpacket_buf + PPP_HDRLEN + HEADERLEN;
    if( f->callbacks->cilen && f->callbacks->addci ){
	cilen = (*f->callbacks->cilen)(f);
	if( cilen > f->ppp->mru - HEADERLEN )
	    cilen = f->ppp->mru - HEADERLEN;
	if (f->callbacks->addci)
	    (*f->callbacks->addci)(f, outp, &cilen);
    } else
	cilen = 0;

    /* send the request to our peer */
    fsm_sdata(f, CONFREQ, f->reqid, outp, cilen);

    /* start the retransmit timer */
    --f->retransmits;
    TIMEOUT(fsm_timeout, f, f->timeouttime);
}


/*
 * fsm_sdata - Send some data.
 *
 * Used for all packets sent to our peer by this module.
 */
void
fsm_sdata(fsm *f, u_char code, u_char id, u_char *data, int datalen)
{
    ppp_output(f->ppp, f->protocol, code, id, data, datalen);
}


void
fsm_log(fsm *f, uint32_t prio, const char *fmt, ...)
{
    char logbuf[BUFSIZ];
    va_list ap;

    FSM_ASSERT(f != NULL);
    FSM_ASSERT(f->callbacks != NULL);

    va_start(ap, fmt);
    snprintf(logbuf, sizeof(logbuf), "ppp id=%u layer=%s %s", f->ppp->id,
	PROTO_NAME(f), fmt);
    vlog_printf(prio, logbuf, ap);
    va_end(ap);
}
