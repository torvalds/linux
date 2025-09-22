/*	$OpenBSD: cbcp.c,v 1.10 2024/08/09 05:16:13 deraadt Exp $	*/

/*
 * cbcp - Call Back Configuration Protocol.
 *
 * Copyright (c) 1995 Pedro Roque Marques.  All rights reserved.
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
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <syslog.h>

#include "pppd.h"
#include "cbcp.h"
#include "fsm.h"
#include "lcp.h"
#include "ipcp.h"

/*
 * Protocol entry points.
 */
static void cbcp_init(int unit);
static void cbcp_open(int unit);
static void cbcp_lowerup(int unit);
static void cbcp_input(int unit, u_char *pkt, int len);
static void cbcp_protrej(int unit);
static int  cbcp_printpkt(u_char *pkt, int len,
    void (*printer)(void *, char *, ...), void *arg);

struct protent cbcp_protent = {
    PPP_CBCP,
    cbcp_init,
    cbcp_input,
    cbcp_protrej,
    cbcp_lowerup,
    NULL,
    cbcp_open,
    NULL,
    cbcp_printpkt,
    NULL,
    0,
    "CBCP",
    NULL,
    NULL,
    NULL
};

cbcp_state cbcp[NUM_PPP];	

/* internal prototypes */

static void cbcp_recvreq(cbcp_state *us, char *pckt, int len);
static void cbcp_resp(cbcp_state *us);
static void cbcp_up(cbcp_state *us);
static void cbcp_recvack(cbcp_state *us, char *pckt, int len);
static void cbcp_send(cbcp_state *us, u_char code, u_char *buf, int len);

/* init state */
static void
cbcp_init(int iface)
{
    cbcp_state *us;

    us = &cbcp[iface];
    memset(us, 0, sizeof(cbcp_state));
    us->us_unit = iface;
    us->us_type |= (1 << CB_CONF_NO);
}

/* lower layer is up */
static void
cbcp_lowerup(int iface)
{
    cbcp_state *us = &cbcp[iface];

    syslog(LOG_DEBUG, "cbcp_lowerup");
    syslog(LOG_DEBUG, "want: %d", us->us_type);

    if (us->us_type == CB_CONF_USER)
        syslog(LOG_DEBUG, "phone no: %s", us->us_number);
}

static void
cbcp_open(int unit)
{
    syslog(LOG_DEBUG, "cbcp_open");
}

/* process an incoming packet */
static void
cbcp_input(int unit, u_char *inpacket, int pktlen)
{
    u_char *inp;
    u_char code, id;
    u_short len;

    cbcp_state *us = &cbcp[unit];

    inp = inpacket;

    if (pktlen < CBCP_MINLEN) {
        syslog(LOG_ERR, "CBCP packet is too small");
	return;
    }

    GETCHAR(code, inp);
    GETCHAR(id, inp);
    GETSHORT(len, inp);

    if (len < CBCP_MINLEN || len > pktlen) {
        syslog(LOG_ERR, "CBCP packet: invalid length");
        return;
    }
    len -= CBCP_MINLEN;
 
    switch(code) {
    case CBCP_REQ:
        us->us_id = id;
	cbcp_recvreq(us, inp, len);
	break;

    case CBCP_RESP:
	syslog(LOG_DEBUG, "CBCP_RESP received");
	break;

    case CBCP_ACK:
	if (id != us->us_id)
	    syslog(LOG_DEBUG, "id doesn't match: expected %d recv %d",
		   us->us_id, id);

	cbcp_recvack(us, inp, len);
	break;

    default:
	break;
    }
}

/* protocol was rejected by foe */
void cbcp_protrej(int iface)
{
}

char *cbcp_codenames[] = {
    "Request", "Response", "Ack"
};

char *cbcp_optionnames[] = {
    "NoCallback",
    "UserDefined",
    "AdminDefined",
    "List"
};

/* pretty print a packet */
static int
cbcp_printpkt(u_char *p, int plen, void (*printer)(void *, char *, ...),
    void *arg)
{
    int code, opt, id, len, olen, delay;
    u_char *pstart;

    if (plen < HEADERLEN)
	return 0;
    pstart = p;
    GETCHAR(code, p);
    GETCHAR(id, p);
    GETSHORT(len, p);
    if (len < HEADERLEN || len > plen)
	return 0;

    if (code >= 1 && code <= sizeof(cbcp_codenames) / sizeof(char *))
	printer(arg, " %s", cbcp_codenames[code-1]);
    else
	printer(arg, " code=0x%x", code); 

    printer(arg, " id=0x%x", id);
    len -= HEADERLEN;

    switch (code) {
    case CBCP_REQ:
    case CBCP_RESP:
    case CBCP_ACK:
        while(len >= 2) {
	    GETCHAR(opt, p);
	    GETCHAR(olen, p);

	    if (olen < 2 || olen > len) {
	        break;
	    }

	    printer(arg, " <");
	    len -= olen;

	    if (opt >= 1 && opt <= sizeof(cbcp_optionnames) / sizeof(char *))
	    	printer(arg, " %s", cbcp_optionnames[opt-1]);
	    else
	        printer(arg, " option=0x%x", opt); 

	    if (olen > 2) {
	        GETCHAR(delay, p);
		printer(arg, " delay = %d", delay);
	    }

	    if (olen > 3) {
	        int addrt;
		char str[256];

		GETCHAR(addrt, p);
		memcpy(str, p, olen - 4);
		str[olen - 4] = 0;
		printer(arg, " number = %s", str);
	    }
	    printer(arg, ">");
	    break;
	}

    default:
	break;
    }

    for (; len > 0; --len) {
	GETCHAR(code, p);
	printer(arg, " %.2x", code);
    }

    return p - pstart;
}

/* received CBCP request */
static void
cbcp_recvreq(cbcp_state *us, char *pckt, int pcktlen)
{
    u_char type, opt_len, delay, addr_type;
    char address[256];
    int len = pcktlen;

    address[0] = 0;

    while (len > 1) {
        syslog(LOG_DEBUG, "length: %d", len);

	GETCHAR(type, pckt);
	GETCHAR(opt_len, pckt);

	if (len < opt_len)
	    break;
	len -= opt_len;

	if (opt_len > 2)
	    GETCHAR(delay, pckt);

	us->us_allowed |= (1 << type);

	switch(type) {
	case CB_CONF_NO:
	    syslog(LOG_DEBUG, "no callback allowed");
	    break;

	case CB_CONF_USER:
	    syslog(LOG_DEBUG, "user callback allowed");
	    if (opt_len > 4) {
	        GETCHAR(addr_type, pckt);
		memcpy(address, pckt, opt_len - 4);
		address[opt_len - 4] = 0;
		if (address[0])
		    syslog(LOG_DEBUG, "address: %s", address);
	    }
	    break;

	case CB_CONF_ADMIN:
	    syslog(LOG_DEBUG, "user admin defined allowed");
	    break;

	case CB_CONF_LIST:
	    break;
	}
    }

    cbcp_resp(us);
}

static void
cbcp_resp(cbcp_state *us)
{
    u_char cb_type;
    u_char buf[256];
    u_char *bufp = buf;
    int len = 0;

    cb_type = us->us_allowed & us->us_type;
    syslog(LOG_DEBUG, "cbcp_resp cb_type=%d", cb_type);

#if 0
    if (!cb_type)
        lcp_down(us->us_unit);
#endif

    if (cb_type & ( 1 << CB_CONF_USER ) ) {
	syslog(LOG_DEBUG, "cbcp_resp CONF_USER");
	PUTCHAR(CB_CONF_USER, bufp);
	len = 3 + 1 + strlen(us->us_number) + 1;
	PUTCHAR(len , bufp);
	PUTCHAR(5, bufp); /* delay */
	PUTCHAR(1, bufp);
	BCOPY(us->us_number, bufp, strlen(us->us_number) + 1);
	cbcp_send(us, CBCP_RESP, buf, len);
	return;
    }

    if (cb_type & ( 1 << CB_CONF_ADMIN ) ) {
	syslog(LOG_DEBUG, "cbcp_resp CONF_ADMIN");
        PUTCHAR(CB_CONF_ADMIN, bufp);
	len = 3 + 1;
	PUTCHAR(len , bufp);
	PUTCHAR(5, bufp); /* delay */
	PUTCHAR(0, bufp);
	cbcp_send(us, CBCP_RESP, buf, len);
	return;
    }

    if (cb_type & ( 1 << CB_CONF_NO ) ) {
        syslog(LOG_DEBUG, "cbcp_resp CONF_NO");
	PUTCHAR(CB_CONF_NO, bufp);
	len = 3;
	PUTCHAR(len , bufp);
	PUTCHAR(0, bufp);
	cbcp_send(us, CBCP_RESP, buf, len);
	(*ipcp_protent.open)(us->us_unit);
	return;
    }
}

static void
cbcp_send(cbcp_state *us, u_char code, u_char *buf, int len)
{
    u_char *outp;
    int outlen;

    outp = outpacket_buf;

    outlen = 4 + len;
    
    MAKEHEADER(outp, PPP_CBCP);

    PUTCHAR(code, outp);
    PUTCHAR(us->us_id, outp);
    PUTSHORT(outlen, outp);
    
    if (len)
        BCOPY(buf, outp, len);

    output(us->us_unit, outpacket_buf, outlen + PPP_HDRLEN);
}

static void
cbcp_recvack(cbcp_state *us, char *pckt, int len)
{
    u_char type, delay, addr_type;
    int opt_len;
    char address[256];

    if (len > 1) {
        GETCHAR(type, pckt);
	GETCHAR(opt_len, pckt);

	if (opt_len > len)
	    return;

	if (opt_len > 2)
	    GETCHAR(delay, pckt);

	if (opt_len > 4) {
	    GETCHAR(addr_type, pckt);
	    memcpy(address, pckt, opt_len - 4);
	    address[opt_len - 4] = 0;
	    if (address[0])
	        syslog(LOG_DEBUG, "peer will call: %s", address);
	}
    }

    cbcp_up(us);
}

extern volatile sig_atomic_t persist;

/* ok peer will do callback */
static void
cbcp_up(cbcp_state *us)
{
    persist = 0;
    lcp_close(0, "Call me back, please");
}
