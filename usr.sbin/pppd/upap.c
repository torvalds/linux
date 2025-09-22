/*	$OpenBSD: upap.c,v 1.12 2024/08/09 05:16:13 deraadt Exp $	*/

/*
 * upap.c - User/Password Authentication Protocol.
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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <syslog.h>

#include "pppd.h"
#include "upap.h"

/*
 * Protocol entry points.
 */
static void upap_init(int);
static void upap_lowerup(int);
static void upap_lowerdown(int);
static void upap_input(int, u_char *, int);
static void upap_protrej(int);
static int  upap_printpkt(u_char *, int, void (*)(void *, char *, ...), void *);

struct protent pap_protent = {
    PPP_PAP,
    upap_init,
    upap_input,
    upap_protrej,
    upap_lowerup,
    upap_lowerdown,
    NULL,
    NULL,
    upap_printpkt,
    NULL,
    1,
    "PAP",
    NULL,
    NULL,
    NULL
};

upap_state upap[NUM_PPP];		/* UPAP state; one for each unit */

static void upap_timeout(void *);
static void upap_reqtimeout(void *);
static void upap_rauthreq(upap_state *, u_char *, int, int);
static void upap_rauthack(upap_state *, u_char *, int, int);
static void upap_rauthnak(upap_state *, u_char *, int, int);
static void upap_sauthreq(upap_state *);
static void upap_sresp(upap_state *, int, int, char *, int);


/*
 * upap_init - Initialize a UPAP unit.
 */
static void
upap_init(int unit)
{
    upap_state *u = &upap[unit];

    u->us_unit = unit;
    u->us_user = NULL;
    u->us_userlen = 0;
    u->us_passwd = NULL;
    u->us_passwdlen = 0;
    u->us_clientstate = UPAPCS_INITIAL;
    u->us_serverstate = UPAPSS_INITIAL;
    u->us_id = 0;
    u->us_timeouttime = UPAP_DEFTIMEOUT;
    u->us_maxtransmits = 10;
    u->us_reqtimeout = UPAP_DEFREQTIME;
}


/*
 * upap_authwithpeer - Authenticate us with our peer (start client).
 *
 * Set new state and send authenticate's.
 */
void
upap_authwithpeer(int unit, char *user, char *password)
{
    upap_state *u = &upap[unit];

    /* Save the username and password we're given */
    u->us_user = user;
    u->us_userlen = strlen(user);
    u->us_passwd = password;
    u->us_passwdlen = strlen(password);
    u->us_transmits = 0;

    /* Lower layer up yet? */
    if (u->us_clientstate == UPAPCS_INITIAL ||
	u->us_clientstate == UPAPCS_PENDING) {
	u->us_clientstate = UPAPCS_PENDING;
	return;
    }

    upap_sauthreq(u);			/* Start protocol */
}


/*
 * upap_authpeer - Authenticate our peer (start server).
 *
 * Set new state.
 */
void
upap_authpeer(int unit)
{
    upap_state *u = &upap[unit];

    /* Lower layer up yet? */
    if (u->us_serverstate == UPAPSS_INITIAL ||
	u->us_serverstate == UPAPSS_PENDING) {
	u->us_serverstate = UPAPSS_PENDING;
	return;
    }

    u->us_serverstate = UPAPSS_LISTEN;
    if (u->us_reqtimeout > 0)
	TIMEOUT(upap_reqtimeout, u, u->us_reqtimeout);
}


/*
 * upap_timeout - Retransmission timer for sending auth-reqs expired.
 */
static void
upap_timeout(void *arg)
{
    upap_state *u = (upap_state *) arg;

    if (u->us_clientstate != UPAPCS_AUTHREQ)
	return;

    if (u->us_transmits >= u->us_maxtransmits) {
	/* give up in disgust */
	syslog(LOG_ERR, "No response to PAP authenticate-requests");
	u->us_clientstate = UPAPCS_BADAUTH;
	auth_withpeer_fail(u->us_unit, PPP_PAP);
	return;
    }

    upap_sauthreq(u);		/* Send Authenticate-Request */
}


/*
 * upap_reqtimeout - Give up waiting for the peer to send an auth-req.
 */
static void
upap_reqtimeout(void *arg)
{
    upap_state *u = (upap_state *) arg;

    if (u->us_serverstate != UPAPSS_LISTEN)
	return;			/* huh?? */

    auth_peer_fail(u->us_unit, PPP_PAP);
    u->us_serverstate = UPAPSS_BADAUTH;
}


/*
 * upap_lowerup - The lower layer is up.
 *
 * Start authenticating if pending.
 */
static void
upap_lowerup(int unit)
{
    upap_state *u = &upap[unit];

    if (u->us_clientstate == UPAPCS_INITIAL)
	u->us_clientstate = UPAPCS_CLOSED;
    else if (u->us_clientstate == UPAPCS_PENDING) {
	upap_sauthreq(u);	/* send an auth-request */
    }

    if (u->us_serverstate == UPAPSS_INITIAL)
	u->us_serverstate = UPAPSS_CLOSED;
    else if (u->us_serverstate == UPAPSS_PENDING) {
	u->us_serverstate = UPAPSS_LISTEN;
	if (u->us_reqtimeout > 0)
	    TIMEOUT(upap_reqtimeout, u, u->us_reqtimeout);
    }
}


/*
 * upap_lowerdown - The lower layer is down.
 *
 * Cancel all timeouts.
 */
static void
upap_lowerdown(int unit)
{
    upap_state *u = &upap[unit];

    if (u->us_clientstate == UPAPCS_AUTHREQ)	/* Timeout pending? */
	UNTIMEOUT(upap_timeout, u);	/* Cancel timeout */
    if (u->us_serverstate == UPAPSS_LISTEN && u->us_reqtimeout > 0)
	UNTIMEOUT(upap_reqtimeout, u);

    u->us_clientstate = UPAPCS_INITIAL;
    u->us_serverstate = UPAPSS_INITIAL;
}


/*
 * upap_protrej - Peer doesn't speak this protocol.
 *
 * This shouldn't happen.  In any case, pretend lower layer went down.
 */
static void
upap_protrej(int unit)
{
    upap_state *u = &upap[unit];

    if (u->us_clientstate == UPAPCS_AUTHREQ) {
	syslog(LOG_ERR, "PAP authentication failed due to protocol-reject");
	auth_withpeer_fail(unit, PPP_PAP);
    }
    if (u->us_serverstate == UPAPSS_LISTEN) {
	syslog(LOG_ERR, "PAP authentication of peer failed (protocol-reject)");
	auth_peer_fail(unit, PPP_PAP);
    }
    upap_lowerdown(unit);
}


/*
 * upap_input - Input UPAP packet.
 */
static void
upap_input(int unit, u_char *inpacket, int l)
{
    upap_state *u = &upap[unit];
    u_char *inp;
    u_char code, id;
    int len;

    /*
     * Parse header (code, id and length).
     * If packet too short, drop it.
     */
    inp = inpacket;
    if (l < UPAP_HEADERLEN) {
	UPAPDEBUG((LOG_INFO, "pap_input: rcvd short header."));
	return;
    }
    GETCHAR(code, inp);
    GETCHAR(id, inp);
    GETSHORT(len, inp);
    if (len < UPAP_HEADERLEN) {
	UPAPDEBUG((LOG_INFO, "pap_input: rcvd illegal length."));
	return;
    }
    if (len > l) {
	UPAPDEBUG((LOG_INFO, "pap_input: rcvd short packet."));
	return;
    }
    len -= UPAP_HEADERLEN;

    /*
     * Action depends on code.
     */
    switch (code) {
    case UPAP_AUTHREQ:
	upap_rauthreq(u, inp, id, len);
	break;

    case UPAP_AUTHACK:
	upap_rauthack(u, inp, id, len);
	break;

    case UPAP_AUTHNAK:
	upap_rauthnak(u, inp, id, len);
	break;

    default:				/* XXX Need code reject */
	break;
    }
}


/*
 * upap_rauth - Receive Authenticate.
 */
static void
upap_rauthreq(upap_state *u, u_char *inp, int id, int len)
{
    u_char ruserlen, rpasswdlen;
    char *ruser, *rpasswd;
    int retcode;
    char *msg;
    int msglen;

    UPAPDEBUG((LOG_INFO, "pap_rauth: Rcvd id %d.", id));

    if (u->us_serverstate < UPAPSS_LISTEN)
	return;

    /*
     * If we receive a duplicate authenticate-request, we are
     * supposed to return the same status as for the first request.
     */
    if (u->us_serverstate == UPAPSS_OPEN) {
	upap_sresp(u, UPAP_AUTHACK, id, "", 0);	/* return auth-ack */
	return;
    }
    if (u->us_serverstate == UPAPSS_BADAUTH) {
	upap_sresp(u, UPAP_AUTHNAK, id, "", 0);	/* return auth-nak */
	return;
    }

    /*
     * Parse user/passwd.
     */
    if (len < sizeof (u_char)) {
	UPAPDEBUG((LOG_INFO, "pap_rauth: rcvd short packet."));
	return;
    }
    GETCHAR(ruserlen, inp);
    len -= sizeof (u_char) + ruserlen + sizeof (u_char);
    if (len < 0) {
	UPAPDEBUG((LOG_INFO, "pap_rauth: rcvd short packet."));
	return;
    }
    ruser = (char *) inp;
    INCPTR(ruserlen, inp);
    GETCHAR(rpasswdlen, inp);
    if (len < rpasswdlen) {
	UPAPDEBUG((LOG_INFO, "pap_rauth: rcvd short packet."));
	return;
    }
    rpasswd = (char *) inp;

    /*
     * Check the username and password given.
     */
    retcode = check_passwd(u->us_unit, ruser, ruserlen, rpasswd,
			   rpasswdlen, &msg, &msglen);
    EXPLICIT_BZERO(rpasswd, rpasswdlen);

    upap_sresp(u, retcode, id, msg, msglen);

    if (retcode == UPAP_AUTHACK) {
	u->us_serverstate = UPAPSS_OPEN;
	auth_peer_success(u->us_unit, PPP_PAP, ruser, ruserlen);
    } else {
	u->us_serverstate = UPAPSS_BADAUTH;
	auth_peer_fail(u->us_unit, PPP_PAP);
    }

    if (u->us_reqtimeout > 0)
	UNTIMEOUT(upap_reqtimeout, u);
}


/*
 * upap_rauthack - Receive Authenticate-Ack.
 */
static void
upap_rauthack(upap_state *u, u_char *inp, int id, int len)
{
    u_char msglen;
    char *msg;

    UPAPDEBUG((LOG_INFO, "pap_rauthack: Rcvd id %d.", id));
    if (u->us_clientstate != UPAPCS_AUTHREQ) /* XXX */
	return;

    /*
     * Parse message.
     */
    if (len < sizeof (u_char)) {
	UPAPDEBUG((LOG_INFO, "pap_rauthack: rcvd short packet."));
	return;
    }
    GETCHAR(msglen, inp);
    len -= sizeof (u_char);
    if (len < msglen) {
	UPAPDEBUG((LOG_INFO, "pap_rauthack: rcvd short packet."));
	return;
    }
    msg = (char *) inp;
    PRINTMSG(msg, msglen);

    u->us_clientstate = UPAPCS_OPEN;

    auth_withpeer_success(u->us_unit, PPP_PAP);
}


/*
 * upap_rauthnak - Receive Authenticate-Nakk.
 */
static void
upap_rauthnak(upap_state *u, u_char *inp, int id, int len)
{
    u_char msglen;
    char *msg;

    UPAPDEBUG((LOG_INFO, "pap_rauthnak: Rcvd id %d.", id));
    if (u->us_clientstate != UPAPCS_AUTHREQ) /* XXX */
	return;

    /*
     * Parse message.
     */
    if (len < sizeof (u_char)) {
	UPAPDEBUG((LOG_INFO, "pap_rauthnak: rcvd short packet."));
	return;
    }
    GETCHAR(msglen, inp);
    len -= sizeof (u_char);
    if (len < msglen) {
	UPAPDEBUG((LOG_INFO, "pap_rauthnak: rcvd short packet."));
	return;
    }
    msg = (char *) inp;
    PRINTMSG(msg, msglen);

    u->us_clientstate = UPAPCS_BADAUTH;

    syslog(LOG_ERR, "PAP authentication failed");
    auth_withpeer_fail(u->us_unit, PPP_PAP);
}


/*
 * upap_sauthreq - Send an Authenticate-Request.
 */
static void
upap_sauthreq(upap_state *u)
{
    u_char *outp;
    int outlen;

    outlen = UPAP_HEADERLEN + 2 * sizeof (u_char) +
	u->us_userlen + u->us_passwdlen;
    outp = outpacket_buf;
    
    MAKEHEADER(outp, PPP_PAP);

    PUTCHAR(UPAP_AUTHREQ, outp);
    PUTCHAR(++u->us_id, outp);
    PUTSHORT(outlen, outp);
    PUTCHAR(u->us_userlen, outp);
    BCOPY(u->us_user, outp, u->us_userlen);
    INCPTR(u->us_userlen, outp);
    PUTCHAR(u->us_passwdlen, outp);
    BCOPY(u->us_passwd, outp, u->us_passwdlen);

    output(u->us_unit, outpacket_buf, outlen + PPP_HDRLEN);

    UPAPDEBUG((LOG_INFO, "pap_sauth: Sent id %d.", u->us_id));

    TIMEOUT(upap_timeout, u, u->us_timeouttime);
    ++u->us_transmits;
    u->us_clientstate = UPAPCS_AUTHREQ;
}


/*
 * upap_sresp - Send a response (ack or nak).
 */
static void
upap_sresp(upap_state *u, int code, int id, char *msg, int msglen)
{
    u_char *outp;
    int outlen;

    outlen = UPAP_HEADERLEN + sizeof (u_char) + msglen;
    outp = outpacket_buf;
    MAKEHEADER(outp, PPP_PAP);

    PUTCHAR(code, outp);
    PUTCHAR(id, outp);
    PUTSHORT(outlen, outp);
    PUTCHAR(msglen, outp);
    BCOPY(msg, outp, msglen);
    output(u->us_unit, outpacket_buf, outlen + PPP_HDRLEN);

    UPAPDEBUG((LOG_INFO, "pap_sresp: Sent code %d, id %d.", code, id));
}

/*
 * upap_printpkt - print the contents of a PAP packet.
 */
static char *upap_codenames[] = {
    "AuthReq", "AuthAck", "AuthNak"
};

static int
upap_printpkt(u_char *p, int plen, void (*printer)(void *, char *, ...), void *arg)
{
    int code, id, len;
    int mlen, ulen, wlen;
    char *user, *pwd, *msg;
    u_char *pstart;

    if (plen < UPAP_HEADERLEN)
	return 0;
    pstart = p;
    GETCHAR(code, p);
    GETCHAR(id, p);
    GETSHORT(len, p);
    if (len < UPAP_HEADERLEN || len > plen)
	return 0;

    if (code >= 1 && code <= sizeof(upap_codenames) / sizeof(char *))
	printer(arg, " %s", upap_codenames[code-1]);
    else
	printer(arg, " code=0x%x", code);
    printer(arg, " id=0x%x", id);
    len -= UPAP_HEADERLEN;
    switch (code) {
    case UPAP_AUTHREQ:
	if (len < 1)
	    break;
	ulen = p[0];
	if (len < ulen + 2)
	    break;
	wlen = p[ulen + 1];
	if (len < ulen + wlen + 2)
	    break;
	user = (char *) (p + 1);
	pwd = (char *) (p + ulen + 2);
	p += ulen + wlen + 2;
	len -= ulen + wlen + 2;
	printer(arg, " user=");
	print_string(user, ulen, printer, arg);
	printer(arg, " password=");
	print_string(pwd, wlen, printer, arg);
	break;
    case UPAP_AUTHACK:
    case UPAP_AUTHNAK:
	if (len < 1)
	    break;
	mlen = p[0];
	if (len < mlen + 1)
	    break;
	msg = (char *) (p + 1);
	p += mlen + 1;
	len -= mlen + 1;
	printer(arg, " ");
	print_string(msg, mlen, printer, arg);
	break;
    }

    /* print the rest of the bytes in the packet */
    for (; len > 0; --len) {
	GETCHAR(code, p);
	printer(arg, " %.2x", code);
    }

    return p - pstart;
}
