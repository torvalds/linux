/*	$OpenBSD: ipcp.c,v 1.17 2024/08/10 05:32:28 jsg Exp $	*/

/*
 * ipcp.c - PPP IP Control Protocol.
 *
 * Copyright (c) 1984-2000 Carnegie Mellon University. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any legal
 *    details, please contact
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * TODO:
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netdb.h>

#include "pppd.h"
#include "fsm.h"
#include "ipcp.h"
#include "pathnames.h"

/* global vars */
ipcp_options ipcp_wantoptions[NUM_PPP];	/* Options that we want to request */
ipcp_options ipcp_gotoptions[NUM_PPP];	/* Options that peer ack'd */
ipcp_options ipcp_allowoptions[NUM_PPP];	/* Options we allow peer to request */
ipcp_options ipcp_hisoptions[NUM_PPP];	/* Options that we ack'd */

/* local vars */
static int cis_received[NUM_PPP];	/* # Conf-Reqs received */
static int default_route_set[NUM_PPP];	/* Have set up a default route */
static int proxy_arp_set[NUM_PPP];	/* Have created proxy arp entry */

/*
 * Callbacks for fsm code.  (CI = Configuration Information)
 */
static void ipcp_resetci(fsm *);	/* Reset our CI */
static int  ipcp_cilen(fsm *);	        /* Return length of our CI */
static void ipcp_addci(fsm *, u_char *, int *);	/* Add our CI */
static int  ipcp_ackci(fsm *, u_char *, int);	/* Peer ack'd our CI */
static int  ipcp_nakci(fsm *, u_char *, int);	/* Peer nak'd our CI */
static int  ipcp_rejci(fsm *, u_char *, int);	/* Peer rej'd our CI */
static int  ipcp_reqci(fsm *, u_char *, int *, int); /* Rcv CI */
static void ipcp_up(fsm *);		/* We're UP */
static void ipcp_down(fsm *);		/* We're DOWN */
static void ipcp_script(fsm *, char *); /* Run an up/down script */
static void ipcp_finished(fsm *);	/* Don't need lower layer */

fsm ipcp_fsm[NUM_PPP];		/* IPCP fsm structure */

static fsm_callbacks ipcp_callbacks = { /* IPCP callback routines */
    ipcp_resetci,		/* Reset our Configuration Information */
    ipcp_cilen,			/* Length of our Configuration Information */
    ipcp_addci,			/* Add our Configuration Information */
    ipcp_ackci,			/* ACK our Configuration Information */
    ipcp_nakci,			/* NAK our Configuration Information */
    ipcp_rejci,			/* Reject our Configuration Information */
    ipcp_reqci,			/* Request peer's Configuration Information */
    ipcp_up,			/* Called when fsm reaches OPENED state */
    ipcp_down,			/* Called when fsm leaves OPENED state */
    NULL,			/* Called when we want the lower layer up */
    ipcp_finished,		/* Called when we want the lower layer down */
    NULL,			/* Called when Protocol-Reject received */
    NULL,			/* Retransmission is necessary */
    NULL,			/* Called to handle protocol-specific codes */
    "IPCP"			/* String name of protocol */
};

/*
 * Protocol entry points from main code.
 */
static void ipcp_init(int);
static void ipcp_open(int);
static void ipcp_close(int, char *);
static void ipcp_lowerup(int);
static void ipcp_lowerdown(int);
static void ipcp_input(int, u_char *, int);
static void ipcp_protrej(int);
static int  ipcp_printpkt(u_char *, int, void (*)(void *, char *, ...), void *);
static void ip_check_options(void);
static int  ip_demand_conf(int);
static int  ip_active_pkt(u_char *, int);

struct protent ipcp_protent = {
    PPP_IPCP,
    ipcp_init,
    ipcp_input,
    ipcp_protrej,
    ipcp_lowerup,
    ipcp_lowerdown,
    ipcp_open,
    ipcp_close,
    ipcp_printpkt,
    NULL,
    1,
    "IPCP",
    ip_check_options,
    ip_demand_conf,
    ip_active_pkt
};

static void ipcp_clear_addrs(int);

/*
 * Lengths of configuration options.
 */
#define CILEN_VOID	2
#define CILEN_COMPRESS	4	/* min length for compression protocol opt. */
#define CILEN_VJ	6	/* length for RFC1332 Van-Jacobson opt. */
#define CILEN_ADDR	6	/* new-style single address option */
#define CILEN_ADDRS	10	/* old-style dual address option */


#define CODENAME(x)	((x) == CONFACK ? "ACK" : \
			 (x) == CONFNAK ? "NAK" : "REJ")


/*
 * Make a string representation of a network IP address.
 */
char *
ip_ntoa(u_int32_t ipaddr)
{
    static char b[64];

    ipaddr = ntohl(ipaddr);

    snprintf(b, sizeof b, "%d.%d.%d.%d",
	    (u_char)(ipaddr >> 24),
	    (u_char)(ipaddr >> 16),
	    (u_char)(ipaddr >> 8),
	    (u_char)(ipaddr));
    return b;
}


/*
 * ipcp_init - Initialize IPCP.
 */
static void
ipcp_init(int unit)
{
    fsm *f = &ipcp_fsm[unit];
    ipcp_options *wo = &ipcp_wantoptions[unit];
    ipcp_options *ao = &ipcp_allowoptions[unit];

    f->unit = unit;
    f->protocol = PPP_IPCP;
    f->callbacks = &ipcp_callbacks;
    fsm_init(&ipcp_fsm[unit]);

    memset(wo, 0, sizeof(*wo));
    memset(ao, 0, sizeof(*ao));

    wo->neg_addr = 1;
    wo->neg_vj = 1;
    wo->vj_protocol = IPCP_VJ_COMP;
    wo->maxslotindex = MAX_STATES - 1; /* really max index */
    wo->cflag = 1;

    /* max slots and slot-id compression are currently hardwired in */
    /* ppp_if.c to 16 and 1, this needs to be changed (among other */
    /* things) gmc */

    ao->neg_addr = 1;
    ao->neg_vj = 1;
    ao->maxslotindex = MAX_STATES - 1;
    ao->cflag = 1;

    /*
     * XXX These control whether the user may use the proxyarp
     * and defaultroute options.
     */
    ao->proxy_arp = 1;
    ao->default_route = 1;
}


/*
 * ipcp_open - IPCP is allowed to come up.
 */
static void
ipcp_open(int unit)
{
    fsm_open(&ipcp_fsm[unit]);
}


/*
 * ipcp_close - Take IPCP down.
 */
static void
ipcp_close(int unit, char *reason)
{
    fsm_close(&ipcp_fsm[unit], reason);
}


/*
 * ipcp_lowerup - The lower layer is up.
 */
static void
ipcp_lowerup(int unit)
{
    fsm_lowerup(&ipcp_fsm[unit]);
}


/*
 * ipcp_lowerdown - The lower layer is down.
 */
static void
ipcp_lowerdown(int unit)
{
    fsm_lowerdown(&ipcp_fsm[unit]);
}


/*
 * ipcp_input - Input IPCP packet.
 */
static void
ipcp_input(int unit, u_char *p, int len)
{
    fsm_input(&ipcp_fsm[unit], p, len);
}


/*
 * ipcp_protrej - A Protocol-Reject was received for IPCP.
 *
 * Pretend the lower layer went down, so we shut up.
 */
static void
ipcp_protrej(int unit)
{
    fsm_lowerdown(&ipcp_fsm[unit]);
}


/*
 * ipcp_resetci - Reset our CI.
 */
static void
ipcp_resetci(fsm *f)
{
    ipcp_options *wo = &ipcp_wantoptions[f->unit];

    wo->req_addr = wo->neg_addr && ipcp_allowoptions[f->unit].neg_addr;
    if (wo->ouraddr == 0)
	wo->accept_local = 1;
    if (wo->hisaddr == 0)
	wo->accept_remote = 1;
    ipcp_gotoptions[f->unit] = *wo;
    cis_received[f->unit] = 0;
}


/*
 * ipcp_cilen - Return length of our CI.
 */
static int
ipcp_cilen(fsm *f)
{
    ipcp_options *go = &ipcp_gotoptions[f->unit];
    ipcp_options *wo = &ipcp_wantoptions[f->unit];
    ipcp_options *ho = &ipcp_hisoptions[f->unit];

#define LENCIVJ(neg, old)	(neg ? (old? CILEN_COMPRESS : CILEN_VJ) : 0)
#define LENCIADDR(neg, old)	(neg ? (old? CILEN_ADDRS : CILEN_ADDR) : 0)

    /*
     * First see if we want to change our options to the old
     * forms because we have received old forms from the peer.
     */
    if (wo->neg_addr && !go->neg_addr && !go->old_addrs) {
	/* use the old style of address negotiation */
	go->neg_addr = 1;
	go->old_addrs = 1;
    }
    if (wo->neg_vj && !go->neg_vj && !go->old_vj) {
	/* try an older style of VJ negotiation */
	if (cis_received[f->unit] == 0) {
	    /* keep trying the new style until we see some CI from the peer */
	    go->neg_vj = 1;
	} else {
	    /* use the old style only if the peer did */
	    if (ho->neg_vj && ho->old_vj) {
		go->neg_vj = 1;
		go->old_vj = 1;
		go->vj_protocol = ho->vj_protocol;
	    }
	}
    }

    return (LENCIADDR(go->neg_addr, go->old_addrs) +
	    LENCIVJ(go->neg_vj, go->old_vj));
}


/*
 * ipcp_addci - Add our desired CIs to a packet.
 */
static void
ipcp_addci(fsm *f, u_char *ucp, int *lenp)
{
    ipcp_options *go = &ipcp_gotoptions[f->unit];
    int len = *lenp;

#define ADDCIVJ(opt, neg, val, old, maxslotindex, cflag) \
    if (neg) { \
	int vjlen = old? CILEN_COMPRESS : CILEN_VJ; \
	if (len >= vjlen) { \
	    PUTCHAR(opt, ucp); \
	    PUTCHAR(vjlen, ucp); \
	    PUTSHORT(val, ucp); \
	    if (!old) { \
		PUTCHAR(maxslotindex, ucp); \
		PUTCHAR(cflag, ucp); \
	    } \
	    len -= vjlen; \
	} else \
	    neg = 0; \
    }

#define ADDCIADDR(opt, neg, old, val1, val2) \
    if (neg) { \
	int addrlen = (old? CILEN_ADDRS: CILEN_ADDR); \
	if (len >= addrlen) { \
	    u_int32_t l; \
	    PUTCHAR(opt, ucp); \
	    PUTCHAR(addrlen, ucp); \
	    l = ntohl(val1); \
	    PUTLONG(l, ucp); \
	    if (old) { \
		l = ntohl(val2); \
		PUTLONG(l, ucp); \
	    } \
	    len -= addrlen; \
	} else \
	    neg = 0; \
    }

    ADDCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), go->neg_addr,
	      go->old_addrs, go->ouraddr, go->hisaddr);

    ADDCIVJ(CI_COMPRESSTYPE, go->neg_vj, go->vj_protocol, go->old_vj,
	    go->maxslotindex, go->cflag);

    *lenp -= len;
}


/*
 * ipcp_ackci - Ack our CIs.
 *
 * Returns:
 *	0 - Ack was bad.
 *	1 - Ack was good.
 */
static int
ipcp_ackci(fsm *f, u_char *p, int len)
{
    ipcp_options *go = &ipcp_gotoptions[f->unit];
    u_short cilen, citype, cishort;
    u_int32_t cilong;
    u_char cimaxslotindex, cicflag;

    /*
     * CIs must be in exactly the same order that we sent...
     * Check packet length and CI length at each step.
     * If we find any deviations, then this packet is bad.
     */

#define ACKCIVJ(opt, neg, val, old, maxslotindex, cflag) \
    if (neg) { \
	int vjlen = old? CILEN_COMPRESS : CILEN_VJ; \
	if ((len -= vjlen) < 0) \
	    goto bad; \
	GETCHAR(citype, p); \
	GETCHAR(cilen, p); \
	if (cilen != vjlen || \
	    citype != opt)  \
	    goto bad; \
	GETSHORT(cishort, p); \
	if (cishort != val) \
	    goto bad; \
	if (!old) { \
	    GETCHAR(cimaxslotindex, p); \
	    if (cimaxslotindex != maxslotindex) \
		goto bad; \
	    GETCHAR(cicflag, p); \
	    if (cicflag != cflag) \
		goto bad; \
	} \
    }

#define ACKCIADDR(opt, neg, old, val1, val2) \
    if (neg) { \
	int addrlen = (old? CILEN_ADDRS: CILEN_ADDR); \
	u_int32_t l; \
	if ((len -= addrlen) < 0) \
	    goto bad; \
	GETCHAR(citype, p); \
	GETCHAR(cilen, p); \
	if (cilen != addrlen || \
	    citype != opt) \
	    goto bad; \
	GETLONG(l, p); \
	cilong = htonl(l); \
	if (val1 != cilong) \
	    goto bad; \
	if (old) { \
	    GETLONG(l, p); \
	    cilong = htonl(l); \
	    if (val2 != cilong) \
		goto bad; \
	} \
    }

    ACKCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), go->neg_addr,
	      go->old_addrs, go->ouraddr, go->hisaddr);

    ACKCIVJ(CI_COMPRESSTYPE, go->neg_vj, go->vj_protocol, go->old_vj,
	    go->maxslotindex, go->cflag);

    /*
     * If there are any remaining CIs, then this packet is bad.
     */
    if (len != 0)
	goto bad;
    return (1);

bad:
    IPCPDEBUG((LOG_INFO, "ipcp_ackci: received bad Ack!"));
    return (0);
}

/*
 * ipcp_nakci - Peer has sent a NAK for some of our CIs.
 * This should not modify any state if the Nak is bad
 * or if IPCP is in the OPENED state.
 *
 * Returns:
 *	0 - Nak was bad.
 *	1 - Nak was good.
 */
static int
ipcp_nakci(fsm *f, u_char *p, int len)
{
    ipcp_options *go = &ipcp_gotoptions[f->unit];
    u_char cimaxslotindex, cicflag;
    u_char citype, cilen, *next;
    u_short cishort;
    u_int32_t ciaddr1, ciaddr2, l;
    ipcp_options no;		/* options we've seen Naks for */
    ipcp_options try;		/* options to request next time */

    BZERO(&no, sizeof(no));
    try = *go;

    /*
     * Any Nak'd CIs must be in exactly the same order that we sent.
     * Check packet length and CI length at each step.
     * If we find any deviations, then this packet is bad.
     */
#define NAKCIADDR(opt, neg, old, code) \
    if (go->neg && \
	len >= (cilen = (old? CILEN_ADDRS: CILEN_ADDR)) && \
	p[1] == cilen && \
	p[0] == opt) { \
	len -= cilen; \
	INCPTR(2, p); \
	GETLONG(l, p); \
	ciaddr1 = htonl(l); \
	if (old) { \
	    GETLONG(l, p); \
	    ciaddr2 = htonl(l); \
	    no.old_addrs = 1; \
	} else \
	    ciaddr2 = 0; \
	no.neg = 1; \
	code \
    }

#define NAKCIVJ(opt, neg, code) \
    if (go->neg && \
	((cilen = p[1]) == CILEN_COMPRESS || cilen == CILEN_VJ) && \
	len >= cilen && \
	p[0] == opt) { \
	len -= cilen; \
	INCPTR(2, p); \
	GETSHORT(cishort, p); \
	no.neg = 1; \
        code \
    }

    /*
     * Accept the peer's idea of {our,his} address, if different
     * from our idea, only if the accept_{local,remote} flag is set.
     */
    NAKCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), neg_addr, go->old_addrs,
	      if (go->accept_local && ciaddr1) { /* Do we know our address? */
		  try.ouraddr = ciaddr1;
		  IPCPDEBUG((LOG_INFO, "local IP address %s",
			     ip_ntoa(ciaddr1)));
	      }
	      if (go->accept_remote && ciaddr2) { /* Does he know his? */
		  try.hisaddr = ciaddr2;
		  IPCPDEBUG((LOG_INFO, "remote IP address %s",
			     ip_ntoa(ciaddr2)));
	      }
	      );

    /*
     * Accept the peer's value of maxslotindex provided that it
     * is less than what we asked for.  Turn off slot-ID compression
     * if the peer wants.  Send old-style compress-type option if
     * the peer wants.
     */
    NAKCIVJ(CI_COMPRESSTYPE, neg_vj,
	    if (cilen == CILEN_VJ) {
		GETCHAR(cimaxslotindex, p);
		GETCHAR(cicflag, p);
		if (cishort == IPCP_VJ_COMP) {
		    try.old_vj = 0;
		    if (cimaxslotindex < go->maxslotindex)
			try.maxslotindex = cimaxslotindex;
		    if (!cicflag)
			try.cflag = 0;
		} else {
		    try.neg_vj = 0;
		}
	    } else {
		if (cishort == IPCP_VJ_COMP || cishort == IPCP_VJ_COMP_OLD) {
		    try.old_vj = 1;
		    try.vj_protocol = cishort;
		} else {
		    try.neg_vj = 0;
		}
	    }
	    );

    /*
     * There may be remaining CIs, if the peer is requesting negotiation
     * on an option that we didn't include in our request packet.
     * If they want to negotiate about IP addresses, we comply.
     * If they want us to ask for compression, we refuse.
     */
    while (len > CILEN_VOID) {
	GETCHAR(citype, p);
	GETCHAR(cilen, p);
	if( (len -= cilen) < 0 )
	    goto bad;
	next = p + cilen - 2;

	switch (citype) {
	case CI_COMPRESSTYPE:
	    if (go->neg_vj || no.neg_vj ||
		(cilen != CILEN_VJ && cilen != CILEN_COMPRESS))
		goto bad;
	    no.neg_vj = 1;
	    break;
	case CI_ADDRS:
	    if ((go->neg_addr && go->old_addrs) || no.old_addrs
		|| cilen != CILEN_ADDRS)
		goto bad;
	    try.neg_addr = 1;
	    try.old_addrs = 1;
	    GETLONG(l, p);
	    ciaddr1 = htonl(l);
	    if (ciaddr1 && go->accept_local)
		try.ouraddr = ciaddr1;
	    GETLONG(l, p);
	    ciaddr2 = htonl(l);
	    if (ciaddr2 && go->accept_remote)
		try.hisaddr = ciaddr2;
	    no.old_addrs = 1;
	    break;
	case CI_ADDR:
	    if (go->neg_addr || no.neg_addr || cilen != CILEN_ADDR)
		goto bad;
	    try.old_addrs = 0;
	    GETLONG(l, p);
	    ciaddr1 = htonl(l);
	    if (ciaddr1 && go->accept_local)
		try.ouraddr = ciaddr1;
	    if (try.ouraddr != 0)
		try.neg_addr = 1;
	    no.neg_addr = 1;
	    break;
	}
	p = next;
    }

    /* If there is still anything left, this packet is bad. */
    if (len != 0)
	goto bad;

    /*
     * OK, the Nak is good.  Now we can update state.
     */
    if (f->state != OPENED)
	*go = try;

    return 1;

bad:
    IPCPDEBUG((LOG_INFO, "ipcp_nakci: received bad Nak!"));
    return 0;
}


/*
 * ipcp_rejci - Reject some of our CIs.
 */
static int
ipcp_rejci(fsm *f, u_char *p, int len)
{
    ipcp_options *go = &ipcp_gotoptions[f->unit];
    u_char cimaxslotindex, ciflag, cilen;
    u_short cishort;
    u_int32_t cilong;
    ipcp_options try;		/* options to request next time */

    try = *go;
    /*
     * Any Rejected CIs must be in exactly the same order that we sent.
     * Check packet length and CI length at each step.
     * If we find any deviations, then this packet is bad.
     */
#define REJCIADDR(opt, neg, old, val1, val2) \
    if (go->neg && \
	len >= (cilen = old? CILEN_ADDRS: CILEN_ADDR) && \
	p[1] == cilen && \
	p[0] == opt) { \
	u_int32_t l; \
	len -= cilen; \
	INCPTR(2, p); \
	GETLONG(l, p); \
	cilong = htonl(l); \
	/* Check rejected value. */ \
	if (cilong != val1) \
	    goto bad; \
	if (old) { \
	    GETLONG(l, p); \
	    cilong = htonl(l); \
	    /* Check rejected value. */ \
	    if (cilong != val2) \
		goto bad; \
	} \
	try.neg = 0; \
    }

#define REJCIVJ(opt, neg, val, old, maxslot, cflag) \
    if (go->neg && \
	p[1] == (old? CILEN_COMPRESS : CILEN_VJ) && \
	len >= p[1] && \
	p[0] == opt) { \
	len -= p[1]; \
	INCPTR(2, p); \
	GETSHORT(cishort, p); \
	/* Check rejected value. */  \
	if (cishort != val) \
	    goto bad; \
	if (!old) { \
	   GETCHAR(cimaxslotindex, p); \
	   if (cimaxslotindex != maxslot) \
	     goto bad; \
	   GETCHAR(ciflag, p); \
	   if (ciflag != cflag) \
	     goto bad; \
        } \
	try.neg = 0; \
     }

    REJCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), neg_addr,
	      go->old_addrs, go->ouraddr, go->hisaddr);

    REJCIVJ(CI_COMPRESSTYPE, neg_vj, go->vj_protocol, go->old_vj,
	    go->maxslotindex, go->cflag);

    /*
     * If there are any remaining CIs, then this packet is bad.
     */
    if (len != 0)
	goto bad;
    /*
     * Now we can update state.
     */
    if (f->state != OPENED)
	*go = try;
    return 1;

bad:
    IPCPDEBUG((LOG_INFO, "ipcp_rejci: received bad Reject!"));
    return 0;
}


/*
 * ipcp_reqci - Check the peer's requested CIs and send appropriate response.
 *
 * Returns: CONFACK, CONFNAK or CONFREJ and input packet modified
 * appropriately.  If reject_if_disagree is non-zero, doesn't return
 * CONFNAK; returns CONFREJ if it can't return CONFACK.
 */
static int
ipcp_reqci(fsm *f, u_char *inp, int *len, int reject_if_disagree)
{
    ipcp_options *wo = &ipcp_wantoptions[f->unit];
    ipcp_options *ho = &ipcp_hisoptions[f->unit];
    ipcp_options *ao = &ipcp_allowoptions[f->unit];
    ipcp_options *go = &ipcp_gotoptions[f->unit];
    u_char *cip, *next;		/* Pointer to current and next CIs */
    u_short cilen, citype;	/* Parsed len, type */
    u_short cishort;		/* Parsed short value */
    u_int32_t tl, ciaddr1, ciaddr2;/* Parsed address values */
    int rc = CONFACK;		/* Final packet return code */
    int orc;			/* Individual option return code */
    u_char *p;			/* Pointer to next char to parse */
    u_char *ucp = inp;		/* Pointer to current output char */
    int l = *len;		/* Length left */
    u_char maxslotindex, cflag;
    int d;

    cis_received[f->unit] = 1;

    /*
     * Reset all his options.
     */
    BZERO(ho, sizeof(*ho));

    /*
     * Process all his options.
     */
    next = inp;
    while (l) {
	orc = CONFACK;			/* Assume success */
	cip = p = next;			/* Remember beginning of CI */
	if (l < 2 ||			/* Not enough data for CI header or */
	    p[1] < 2 ||			/*  CI length too small or */
	    p[1] > l) {			/*  CI length too big? */
	    IPCPDEBUG((LOG_INFO, "ipcp_reqci: bad CI length!"));
	    orc = CONFREJ;		/* Reject bad CI */
	    cilen = l;			/* Reject till end of packet */
	    l = 0;			/* Don't loop again */
	    goto endswitch;
	}
	GETCHAR(citype, p);		/* Parse CI type */
	GETCHAR(cilen, p);		/* Parse CI length */
	l -= cilen;			/* Adjust remaining length */
	next += cilen;			/* Step to next CI */

	switch (citype) {		/* Check CI type */
	case CI_ADDRS:
	    IPCPDEBUG((LOG_INFO, "ipcp: received ADDRS "));
	    if (!ao->neg_addr ||
		cilen != CILEN_ADDRS) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }

	    /*
	     * If he has no address, or if we both have his address but
	     * disagree about it, then NAK it with our idea.
	     * In particular, if we don't know his address, but he does,
	     * then accept it.
	     */
	    GETLONG(tl, p);		/* Parse source address (his) */
	    ciaddr1 = htonl(tl);
	    IPCPDEBUG((LOG_INFO, "(%s:", ip_ntoa(ciaddr1)));
	    if (ciaddr1 != wo->hisaddr
		&& (ciaddr1 == 0 || !wo->accept_remote)) {
		orc = CONFNAK;
		if (!reject_if_disagree) {
		    DECPTR(sizeof(u_int32_t), p);
		    tl = ntohl(wo->hisaddr);
		    PUTLONG(tl, p);
		}
	    } else if (ciaddr1 == 0 && wo->hisaddr == 0) {
		/*
		 * If neither we nor he knows his address, reject the option.
		 */
		orc = CONFREJ;
		wo->req_addr = 0;	/* don't NAK with 0.0.0.0 later */
		break;
	    }

	    /*
	     * If he doesn't know our address, or if we both have our address
	     * but disagree about it, then NAK it with our idea.
	     */
	    GETLONG(tl, p);		/* Parse destination address (ours) */
	    ciaddr2 = htonl(tl);
	    IPCPDEBUG((LOG_INFO, "%s)", ip_ntoa(ciaddr2)));
	    if (ciaddr2 != wo->ouraddr) {
		if (ciaddr2 == 0 || !wo->accept_local) {
		    orc = CONFNAK;
		    if (!reject_if_disagree) {
			DECPTR(sizeof(u_int32_t), p);
			tl = ntohl(wo->ouraddr);
			PUTLONG(tl, p);
		    }
		} else {
		    go->ouraddr = ciaddr2;	/* accept peer's idea */
		}
	    }

	    ho->neg_addr = 1;
	    ho->old_addrs = 1;
	    ho->hisaddr = ciaddr1;
	    ho->ouraddr = ciaddr2;
	    break;

	case CI_ADDR:
	    IPCPDEBUG((LOG_INFO, "ipcp: received ADDR "));

	    if (!ao->neg_addr ||
		cilen != CILEN_ADDR) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }

	    /*
	     * If he has no address, or if we both have his address but
	     * disagree about it, then NAK it with our idea.
	     * In particular, if we don't know his address, but he does,
	     * then accept it.
	     */
	    GETLONG(tl, p);	/* Parse source address (his) */
	    ciaddr1 = htonl(tl);
	    IPCPDEBUG((LOG_INFO, "(%s)", ip_ntoa(ciaddr1)));
	    if (ciaddr1 != wo->hisaddr
		&& (ciaddr1 == 0 || !wo->accept_remote)) {
		orc = CONFNAK;
		if (!reject_if_disagree) {
		    DECPTR(sizeof(u_int32_t), p);
		    tl = ntohl(wo->hisaddr);
		    PUTLONG(tl, p);
		}
	    } else if (ciaddr1 == 0 && wo->hisaddr == 0) {
		/*
		 * Don't ACK an address of 0.0.0.0 - reject it instead.
		 */
		orc = CONFREJ;
		wo->req_addr = 0;	/* don't NAK with 0.0.0.0 later */
		break;
	    }

	    ho->neg_addr = 1;
	    ho->hisaddr = ciaddr1;
	    break;

	case CI_MS_DNS1:
	case CI_MS_DNS2:
	    /* Microsoft primary or secondary DNS request */
	    d = citype == CI_MS_DNS2;
	    IPCPDEBUG((LOG_INFO, "ipcp: received DNS%d Request ", d+1));

	    /* If we do not have a DNS address then we cannot send it */
	    if (ao->dnsaddr[d] == 0 ||
		cilen != CILEN_ADDR) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }
	    GETLONG(tl, p);
	    if (htonl(tl) != ao->dnsaddr[d]) {
                DECPTR(sizeof(u_int32_t), p);
		tl = ntohl(ao->dnsaddr[d]);
		PUTLONG(tl, p);
		orc = CONFNAK;
            }
            break;

	case CI_MS_WINS1:
	case CI_MS_WINS2:
	    /* Microsoft primary or secondary WINS request */
	    d = citype == CI_MS_WINS2;
	    IPCPDEBUG((LOG_INFO, "ipcp: received WINS%d Request ", d+1));

	    /* If we do not have a DNS address then we cannot send it */
	    if (ao->winsaddr[d] == 0 ||
		cilen != CILEN_ADDR) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }
	    GETLONG(tl, p);
	    if (htonl(tl) != ao->winsaddr[d]) {
		DECPTR(sizeof(u_int32_t), p);
		tl = ntohl(ao->winsaddr[d]);
		PUTLONG(tl, p);
		orc = CONFNAK;
	    }
	    break;

	case CI_COMPRESSTYPE:
	    IPCPDEBUG((LOG_INFO, "ipcp: received COMPRESSTYPE "));
	    if (!ao->neg_vj ||
		(cilen != CILEN_VJ && cilen != CILEN_COMPRESS)) {
		orc = CONFREJ;
		break;
	    }
	    GETSHORT(cishort, p);
	    IPCPDEBUG((LOG_INFO, "(%d)", cishort));

	    if (!(cishort == IPCP_VJ_COMP ||
		  (cishort == IPCP_VJ_COMP_OLD && cilen == CILEN_COMPRESS))) {
		orc = CONFREJ;
		break;
	    }

	    ho->neg_vj = 1;
	    ho->vj_protocol = cishort;
	    if (cilen == CILEN_VJ) {
		GETCHAR(maxslotindex, p);
		if (maxslotindex > ao->maxslotindex) {
		    orc = CONFNAK;
		    if (!reject_if_disagree){
			DECPTR(1, p);
			PUTCHAR(ao->maxslotindex, p);
		    }
		}
		GETCHAR(cflag, p);
		if (cflag && !ao->cflag) {
		    orc = CONFNAK;
		    if (!reject_if_disagree){
			DECPTR(1, p);
			PUTCHAR(wo->cflag, p);
		    }
		}
		ho->maxslotindex = maxslotindex;
		ho->cflag = cflag;
	    } else {
		ho->old_vj = 1;
		ho->maxslotindex = MAX_STATES - 1;
		ho->cflag = 1;
	    }
	    break;

	default:
	    orc = CONFREJ;
	    break;
	}

endswitch:
	IPCPDEBUG((LOG_INFO, " (%s)\n", CODENAME(orc)));

	if (orc == CONFACK &&		/* Good CI */
	    rc != CONFACK)		/*  but prior CI wasnt? */
	    continue;			/* Don't send this one */

	if (orc == CONFNAK) {		/* Nak this CI? */
	    if (reject_if_disagree)	/* Getting fed up with sending NAKs? */
		orc = CONFREJ;		/* Get tough if so */
	    else {
		if (rc == CONFREJ)	/* Rejecting prior CI? */
		    continue;		/* Don't send this one */
		if (rc == CONFACK) {	/* Ack'd all prior CIs? */
		    rc = CONFNAK;	/* Not anymore... */
		    ucp = inp;		/* Backup */
		}
	    }
	}

	if (orc == CONFREJ &&		/* Reject this CI */
	    rc != CONFREJ) {		/*  but no prior ones? */
	    rc = CONFREJ;
	    ucp = inp;			/* Backup */
	}

	/* Need to move CI? */
	if (ucp != cip)
	    BCOPY(cip, ucp, cilen);	/* Move it */

	/* Update output pointer */
	INCPTR(cilen, ucp);
    }

    /*
     * If we aren't rejecting this packet, and we want to negotiate
     * their address, and they didn't send their address, then we
     * send a NAK with a CI_ADDR option appended.  We assume the
     * input buffer is long enough that we can append the extra
     * option safely.
     */
    if (rc != CONFREJ && !ho->neg_addr &&
	wo->req_addr && !reject_if_disagree) {
	if (rc == CONFACK) {
	    rc = CONFNAK;
	    ucp = inp;			/* reset pointer */
	    wo->req_addr = 0;		/* don't ask again */
	}
	PUTCHAR(CI_ADDR, ucp);
	PUTCHAR(CILEN_ADDR, ucp);
	tl = ntohl(wo->hisaddr);
	PUTLONG(tl, ucp);
    }

    *len = ucp - inp;			/* Compute output length */
    IPCPDEBUG((LOG_INFO, "ipcp: returning Configure-%s", CODENAME(rc)));
    return (rc);			/* Return final code */
}


/*
 * ip_check_options - check that any IP-related options are OK,
 * and assign appropriate defaults.
 */
static void
ip_check_options(void)
{
    struct hostent *hp;
    u_int32_t local;
    ipcp_options *wo = &ipcp_wantoptions[0];

    /*
     * Default our local IP address based on our hostname.
     * If local IP address already given, don't bother.
     */
    if (wo->ouraddr == 0 && !disable_defaultip) {
	/*
	 * Look up our hostname (possibly with domain name appended)
	 * and take the first IP address as our local IP address.
	 * If there isn't an IP address for our hostname, too bad.
	 */
	wo->accept_local = 1;	/* don't insist on this default value */
	if ((hp = gethostbyname(hostname)) != NULL) {
	    local = *(u_int32_t *)hp->h_addr;
	    if (local != 0 && !bad_ip_adrs(local))
		wo->ouraddr = local;
	}
    }

    if (demand && wo->hisaddr == 0) {
	option_error("remote IP address required for demand-dialling\n");
	exit(1);
    }
#if 0
    if (demand && wo->accept_remote) {
	option_error("ipcp-accept-remote is incompatible with demand\n");
	exit(1);
    }
#endif
}


/*
 * ip_demand_conf - configure the interface as though
 * IPCP were up, for use with dial-on-demand.
 */
static int
ip_demand_conf(int u)
{
    ipcp_options *wo = &ipcp_wantoptions[u];

    if (!sifaddr(u, wo->ouraddr, wo->hisaddr, GetMask(wo->ouraddr)))
	return 0;
    if (!sifup(u))
	return 0;
    if (!sifnpmode(u, PPP_IP, NPMODE_QUEUE))
	return 0;
    if (wo->default_route)
	if (sifdefaultroute(u, wo->ouraddr, wo->hisaddr))
	    default_route_set[u] = 1;
    if (wo->proxy_arp)
	if (sifproxyarp(u, wo->hisaddr))
	    proxy_arp_set[u] = 1;

    syslog(LOG_NOTICE, "local  IP address %s", ip_ntoa(wo->ouraddr));
    syslog(LOG_NOTICE, "remote IP address %s", ip_ntoa(wo->hisaddr));

    return 1;
}


/*
 * ipcp_up - IPCP has come UP.
 *
 * Configure the IP network interface appropriately and bring it up.
 */
static void
ipcp_up(fsm *f)
{
    u_int32_t mask;
    ipcp_options *ho = &ipcp_hisoptions[f->unit];
    ipcp_options *go = &ipcp_gotoptions[f->unit];
    ipcp_options *wo = &ipcp_wantoptions[f->unit];

    np_up(f->unit, PPP_IP);
    IPCPDEBUG((LOG_INFO, "ipcp: up"));

    /*
     * We must have a non-zero IP address for both ends of the link.
     */
    if (!ho->neg_addr)
	ho->hisaddr = wo->hisaddr;

    if (ho->hisaddr == 0) {
	syslog(LOG_ERR, "Could not determine remote IP address");
	ipcp_close(f->unit, "Could not determine remote IP address");
	return;
    }
    if (go->ouraddr == 0) {
	syslog(LOG_ERR, "Could not determine local IP address");
	ipcp_close(f->unit, "Could not determine local IP address");
	return;
    }
    script_setenv("IPLOCAL", ip_ntoa(go->ouraddr));
    script_setenv("IPREMOTE", ip_ntoa(ho->hisaddr));

    /*
     * Check that the peer is allowed to use the IP address it wants.
     */
    if (!auth_ip_addr(f->unit, ho->hisaddr)) {
	syslog(LOG_ERR, "Peer is not authorized to use remote address %s",
	       ip_ntoa(ho->hisaddr));
	ipcp_close(f->unit, "Unauthorized remote IP address");
	return;
    }

    /* set tcp compression */
    sifvjcomp(f->unit, ho->neg_vj, ho->cflag, ho->maxslotindex);

    /*
     * If we are doing dial-on-demand, the interface is already
     * configured, so we put out any saved-up packets, then set the
     * interface to pass IP packets.
     */
    if (demand) {
	if (go->ouraddr != wo->ouraddr || ho->hisaddr != wo->hisaddr) {
	    if (go->ouraddr != wo->ouraddr)
		syslog(LOG_WARNING, "Local IP address changed to %s",
		       ip_ntoa(go->ouraddr));
	    if (ho->hisaddr != wo->hisaddr)
		syslog(LOG_WARNING, "Remote IP address changed to %s",
		       ip_ntoa(ho->hisaddr));
	    ipcp_clear_addrs(f->unit);

	    /* Set the interface to the new addresses */
	    mask = GetMask(go->ouraddr);
	    if (!sifaddr(f->unit, go->ouraddr, ho->hisaddr, mask)) {
		IPCPDEBUG((LOG_WARNING, "sifaddr failed"));
		ipcp_close(f->unit, "Interface configuration failed");
		return;
	    }

	    /* assign a default route through the interface if required */
	    if (ipcp_wantoptions[f->unit].default_route) 
		if (sifdefaultroute(f->unit, go->ouraddr, ho->hisaddr))
		    default_route_set[f->unit] = 1;

	    /* Make a proxy ARP entry if requested. */
	    if (ipcp_wantoptions[f->unit].proxy_arp)
		if (sifproxyarp(f->unit, ho->hisaddr))
		    proxy_arp_set[f->unit] = 1;

	}
	demand_rexmit(PPP_IP);
	sifnpmode(f->unit, PPP_IP, NPMODE_PASS);

    } else {
	/*
	 * Set IP addresses and (if specified) netmask.
	 */
	mask = GetMask(go->ouraddr);

#if !(defined(SVR4) && (defined(SNI) || defined(__USLC__)))
	if (!sifaddr(f->unit, go->ouraddr, ho->hisaddr, mask)) {
	    IPCPDEBUG((LOG_WARNING, "sifaddr failed"));
	    ipcp_close(f->unit, "Interface configuration failed");
	    return;
	}
#endif

	/* bring the interface up for IP */
	if (!sifup(f->unit)) {
	    IPCPDEBUG((LOG_WARNING, "sifup failed"));
	    ipcp_close(f->unit, "Interface configuration failed");
	    return;
	}

#if (defined(SVR4) && (defined(SNI) || defined(__USLC__)))
	if (!sifaddr(f->unit, go->ouraddr, ho->hisaddr, mask)) {
	    IPCPDEBUG((LOG_WARNING, "sifaddr failed"));
	    ipcp_close(f->unit, "Interface configuration failed");
	    return;
	}
#endif
	sifnpmode(f->unit, PPP_IP, NPMODE_PASS);

	/* assign a default route through the interface if required */
	if (ipcp_wantoptions[f->unit].default_route)
	    if (sifdefaultroute(f->unit, go->ouraddr, ho->hisaddr))
		default_route_set[f->unit] = 1;

	/* Make a proxy ARP entry if requested. */
	if (ipcp_wantoptions[f->unit].proxy_arp)
	    if (sifproxyarp(f->unit, ho->hisaddr))
		proxy_arp_set[f->unit] = 1;

	syslog(LOG_NOTICE, "local  IP address %s", ip_ntoa(go->ouraddr));
	syslog(LOG_NOTICE, "remote IP address %s", ip_ntoa(ho->hisaddr));
    }

    /*
     * Execute the ip-up script, like this:
     *	/etc/ppp/ip-up interface tty speed local-IP remote-IP
     */
    ipcp_script(f, _PATH_IPUP);

}


/*
 * ipcp_down - IPCP has gone DOWN.
 *
 * Take the IP network interface down, clear its addresses
 * and delete routes through it.
 */
static void
ipcp_down(fsm *f)
{
    IPCPDEBUG((LOG_INFO, "ipcp: down"));
    np_down(f->unit, PPP_IP);
    sifvjcomp(f->unit, 0, 0, 0);

    /*
     * If we are doing dial-on-demand, set the interface
     * to queue up outgoing packets (for now).
     */
    if (demand) {
	sifnpmode(f->unit, PPP_IP, NPMODE_QUEUE);
    } else {
	sifdown(f->unit);
	ipcp_clear_addrs(f->unit);
    }

    /* Execute the ip-down script */
    ipcp_script(f, _PATH_IPDOWN);
}


/*
 * ipcp_clear_addrs() - clear the interface addresses, routes,
 * proxy arp entries, etc.
 */
static void
ipcp_clear_addrs(int unit)
{
    u_int32_t ouraddr, hisaddr;

    ouraddr = ipcp_gotoptions[unit].ouraddr;
    hisaddr = ipcp_hisoptions[unit].hisaddr;
    if (proxy_arp_set[unit]) {
	cifproxyarp(unit, hisaddr);
	proxy_arp_set[unit] = 0;
    }
    if (default_route_set[unit]) {
	cifdefaultroute(unit, ouraddr, hisaddr);
	default_route_set[unit] = 0;
    }
    cifaddr(unit, ouraddr, hisaddr);
}


/*
 * ipcp_finished - possibly shut down the lower layers.
 */
static void
ipcp_finished(fsm *f)
{
    np_finished(f->unit, PPP_IP);
}


/*
 * ipcp_script - Execute a script with arguments
 * interface-name tty-name speed local-IP remote-IP.
 */
static void
ipcp_script(fsm *f, char *script)
{
    char strspeed[32], strlocal[32], strremote[32];
    char *argv[8];

    snprintf(strspeed, sizeof strspeed, "%d", baud_rate);
    strlcpy(strlocal, ip_ntoa(ipcp_gotoptions[f->unit].ouraddr),
	sizeof strlocal);
    strlcpy(strremote, ip_ntoa(ipcp_hisoptions[f->unit].hisaddr),
	sizeof strremote);

    argv[0] = script;
    argv[1] = ifname;
    argv[2] = devnam;
    argv[3] = strspeed;
    argv[4] = strlocal;
    argv[5] = strremote;
    argv[6] = ipparam;
    argv[7] = NULL;
    run_program(script, argv, 0);
}

/*
 * ipcp_printpkt - print the contents of an IPCP packet.
 */
static char *ipcp_codenames[] = {
    "ConfReq", "ConfAck", "ConfNak", "ConfRej",
    "TermReq", "TermAck", "CodeRej"
};

static int
ipcp_printpkt(u_char *p, int plen, void (*printer)(void *, char *, ...), void *arg)
{
    int code, id, len, olen;
    u_char *pstart, *optend;
    u_short cishort;
    u_int32_t cilong;

    if (plen < HEADERLEN)
	return 0;
    pstart = p;
    GETCHAR(code, p);
    GETCHAR(id, p);
    GETSHORT(len, p);
    if (len < HEADERLEN || len > plen)
	return 0;

    if (code >= 1 && code <= sizeof(ipcp_codenames) / sizeof(char *))
	printer(arg, " %s", ipcp_codenames[code-1]);
    else
	printer(arg, " code=0x%x", code);
    printer(arg, " id=0x%x", id);
    len -= HEADERLEN;
    switch (code) {
    case CONFREQ:
    case CONFACK:
    case CONFNAK:
    case CONFREJ:
	/* print option list */
	while (len >= 2) {
	    GETCHAR(code, p);
	    GETCHAR(olen, p);
	    p -= 2;
	    if (olen < 2 || olen > len) {
		break;
	    }
	    printer(arg, " <");
	    len -= olen;
	    optend = p + olen;
	    switch (code) {
	    case CI_ADDRS:
		if (olen == CILEN_ADDRS) {
		    p += 2;
		    GETLONG(cilong, p);
		    printer(arg, "addrs %I", htonl(cilong));
		    GETLONG(cilong, p);
		    printer(arg, " %I", htonl(cilong));
		}
		break;
	    case CI_COMPRESSTYPE:
		if (olen >= CILEN_COMPRESS) {
		    p += 2;
		    GETSHORT(cishort, p);
		    printer(arg, "compress ");
		    switch (cishort) {
		    case IPCP_VJ_COMP:
			printer(arg, "VJ");
			break;
		    case IPCP_VJ_COMP_OLD:
			printer(arg, "old-VJ");
			break;
		    default:
			printer(arg, "0x%x", cishort);
		    }
		}
		break;
	    case CI_ADDR:
		if (olen == CILEN_ADDR) {
		    p += 2;
		    GETLONG(cilong, p);
		    printer(arg, "addr %I", htonl(cilong));
		}
		break;
	    case CI_MS_DNS1:
	    case CI_MS_DNS2:
		p += 2;
		GETLONG(cilong, p);
		printer(arg, "ms-dns %I", htonl(cilong));
		break;
	    case CI_MS_WINS1:
	    case CI_MS_WINS2:
		p += 2;
		GETLONG(cilong, p);
		printer(arg, "ms-wins %I", htonl(cilong));
		break;
	    }
	    while (p < optend) {
		GETCHAR(code, p);
		printer(arg, " %.2x", code);
	    }
	    printer(arg, ">");
	}
	break;

    case TERMACK:
    case TERMREQ:
	if (len > 0 && *p >= ' ' && *p < 0x7f) {
	    printer(arg, " ");
	    print_string(p, len, printer, arg);
	    p += len;
	    len = 0;
	}
        break;
    }

    /* print the rest of the bytes in the packet */
    for (; len > 0; --len) {
	GETCHAR(code, p);
	printer(arg, " %.2x", code);
    }

    return p - pstart;
}

/*
 * ip_active_pkt - see if this IP packet is worth bringing the link up for.
 * We don't bring the link up for IP fragments or for TCP FIN packets
 * with no data.
 */
#define IP_HDRLEN	20	/* bytes */
#define IP_OFFMASK	0x1fff
#define IPPROTO_TCP	6
#define TCP_HDRLEN	20
#define TH_FIN		0x01

/*
 * We use these macros because the IP header may be at an odd address,
 * and some compilers might use word loads to get th_off or ip_hl.
 */

#define net_short(x)	(((x)[0] << 8) + (x)[1])
#define get_iphl(x)	(((unsigned char *)(x))[0] & 0xF)
#define get_ipoff(x)	net_short((unsigned char *)(x) + 6)
#define get_ipproto(x)	(((unsigned char *)(x))[9])
#define get_tcpoff(x)	(((unsigned char *)(x))[12] >> 4)
#define get_tcpflags(x)	(((unsigned char *)(x))[13])

static int
ip_active_pkt(u_char *pkt, int len)
{
    u_char *tcp;
    int hlen;

    len -= PPP_HDRLEN;
    pkt += PPP_HDRLEN;
    if (len < IP_HDRLEN)
	return 0;
    if ((get_ipoff(pkt) & IP_OFFMASK) != 0)
	return 0;
    if (get_ipproto(pkt) != IPPROTO_TCP)
	return 1;
    hlen = get_iphl(pkt) * 4;
    if (len < hlen + TCP_HDRLEN)
	return 0;
    tcp = pkt + hlen;
    if ((get_tcpflags(tcp) & TH_FIN) != 0 && len == hlen + get_tcpoff(tcp) * 4)
	return 0;
    return 1;
}
