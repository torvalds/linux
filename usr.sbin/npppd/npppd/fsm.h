/*	$OpenBSD: fsm.h,v 1.7 2024/02/26 08:25:51 yasuoka Exp $ */
/*	$NetBSD: fsm.h,v 1.10 2000/09/23 22:39:35 christos Exp $	*/

/*
 * fsm.h - {Link, IP} Control Protocol Finite State Machine definitions.
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
 *
 * Id: fsm.h,v 1.8 1999/11/15 01:51:50 paulus Exp
 */
#ifndef	FSM_H
#define	FSM_H 1

/*
 * Packet header = Code, id, length.
 */
#define HEADERLEN	4


/*
 *  CP (LCP, IPCP, etc.) codes.
 */
#define CONFREQ		1	/* Configuration Request */
#define CONFACK		2	/* Configuration Ack */
#define CONFNAK		3	/* Configuration Nak */
#define CONFREJ		4	/* Configuration Reject */
#define TERMREQ		5	/* Termination Request */
#define TERMACK		6	/* Termination Ack */
#define CODEREJ		7	/* Code Reject */
#define RESETREQ	14	/* Reset Request */
#define RESETACK	15	/* Reset Ack */

struct evtimer_wrap {
	void *ctx;
	void (*func)(void *);
	struct event ev;
};
/*
 * Each FSM is described by an fsm structure and fsm callbacks.
 */
typedef struct fsm {
    //int unit;			/* Interface unit number */
    npppd_ppp *ppp;		/* npppd's ppp */
    struct evtimer_wrap timerctx;/* context for event(3) */
    int protocol;		/* Data Link Layer Protocol field value */
    int state;			/* State */
    int flags;			/* Contains option bits */
    u_char id;			/* Current id */
    u_char reqid;		/* Current request id */
    u_char seen_ack;		/* Have received valid Ack/Nak/Rej to Req */
    int timeouttime;		/* Timeout time in milliseconds */
    int maxconfreqtransmits;	/* Maximum Configure-Request transmissions */
    int retransmits;		/* Number of retransmissions left */
    int maxtermtransmits;	/* Maximum Terminate-Request transmissions */
    int nakloops;		/* Number of nak loops since last ack */
    int maxnakloops;		/* Maximum number of nak loops tolerated */
    struct fsm_callbacks *callbacks;	/* Callback routines */
    const char *term_reason;	/* Reason for closing protocol */
    int term_reason_len;	/* Length of term_reason */
} fsm;


typedef struct fsm_callbacks {
    void (*resetci)(fsm *);	/* Reset our Configuration Information */
    int  (*cilen)(fsm *);	/* Length of our Configuration Information */
    void (*addci) 		/* Add our Configuration Information */
		(fsm *, u_char *, int *);
    int  (*ackci)		/* ACK our Configuration Information */
		(fsm *, u_char *, int);
    int  (*nakci)		/* NAK our Configuration Information */
		(fsm *, u_char *, int);
    int  (*rejci)		/* Reject our Configuration Information */
		(fsm *, u_char *, int);
    int  (*reqci)		/* Request peer's Configuration Information */
		(fsm *, u_char *, int *, int);
    void (*up)(fsm *);		/* Called when fsm reaches OPENED state */
    void (*down)(fsm *);	/* Called when fsm leaves OPENED state */
		
    void (*starting)(fsm *);	/* Called when we want the lower layer */
    void (*finished)(fsm *);	/* Called when we don't want the lower layer */
    void (*protreject)(int);	/* Called when Protocol-Reject received */
    void (*retransmit)(fsm *);	/* Retransmission is necessary */
    int  (*extcode)		/* Called when unknown code received */
		(fsm *, int, int, u_char *, int);
    char *proto_name;		/* String name for protocol (for messages) */
} fsm_callbacks;


/*
 * Link states.
 */
#define INITIAL		0	/* Down, hasn't been opened */
#define STARTING	1	/* Down, been opened */
#define CLOSED		2	/* Up, hasn't been opened */
#define STOPPED		3	/* Open, waiting for down event */
#define CLOSING		4	/* Terminating the connection, not open */
#define STOPPING	5	/* Terminating, but open */
#define REQSENT		6	/* We've sent a Config Request */
#define ACKRCVD		7	/* We've received a Config Ack */
#define ACKSENT		8	/* We've sent a Config Ack */
#define OPENED		9	/* Connection available */


/*
 * Flags - indicate options controlling FSM operation
 */
#define OPT_PASSIVE	1	/* Don't die if we don't get a response */
#define OPT_RESTART	2	/* Treat 2nd OPEN as DOWN, UP */
#define OPT_SILENT	4	/* Wait for peer to speak first */


/*
 * Timeouts.
 */
#define DEFTIMEOUT	3	/* Timeout time in seconds */
#define DEFMAXTERMREQS	2	/* Maximum Terminate-Request transmissions */
#define DEFMAXCONFREQS	10	/* Maximum Configure-Request transmissions */
#define DEFMAXNAKLOOPS	5	/* Maximum number of nak loops */

/** define TIMEOUT to use event(3)'s timer. */
#define	TIMEOUT(fn, f, t)						\
	{								\
		struct timeval tv0;					\
									\
		tv0.tv_usec = 0;					\
		tv0.tv_sec = (t);					\
		if (!evtimer_initialized(&(f)->timerctx.ev))		\
			evtimer_set(&(f)->timerctx.ev, fsm_evtimer_timeout,\
			    &(f)->timerctx);				\
		(f)->timerctx.func = fn;				\
		evtimer_del(&(f)->timerctx.ev);				\
		evtimer_add(&(f)->timerctx.ev, &tv0);			\
	}

#define	UNTIMEOUT(fn, f)	evtimer_del(&(f)->timerctx.ev)

/*
 * Prototypes
 */
void fsm_evtimer_timeout(int, short, void *);
void fsm_init(fsm *);
void fsm_lowerup(fsm *);
void fsm_lowerdown(fsm *);
void fsm_open(fsm *);
void fsm_close(fsm *, const char *);
void fsm_input(fsm *, u_char *, int);
void fsm_protreject(fsm *);
void fsm_sdata(fsm *, u_char, u_char, u_char *, int);
void fsm_log(fsm *, uint32_t, const char *, ...) __attribute__((__format__ (__printf__, 3, 4)));


#endif
