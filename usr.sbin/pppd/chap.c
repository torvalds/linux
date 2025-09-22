/*	$OpenBSD: chap.c,v 1.21 2024/08/09 05:16:13 deraadt Exp $	*/

/*
 * chap.c - Challenge Handshake Authentication Protocol.
 *
 * Copyright (c) 1989-2002 Paul Mackerras. All rights reserved.
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
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Paul Mackerras
 *     <paulus@samba.org>".
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Copyright (c) 1991 Gregory M. Christy.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Gregory M. Christy.  The name of the author may not be used to
 * endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * TODO:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <syslog.h>
#include <md5.h>

#include "pppd.h"
#include "chap.h"

/*
 * Protocol entry points.
 */
static void ChapInit(int);
static void ChapLowerUp(int);
static void ChapLowerDown(int);
static void ChapInput(int, u_char *, int);
static void ChapProtocolReject(int);
static int  ChapPrintPkt(u_char *, int, void (*)(void *, char *, ...), void *);

struct protent chap_protent = {
    PPP_CHAP,
    ChapInit,
    ChapInput,
    ChapProtocolReject,
    ChapLowerUp,
    ChapLowerDown,
    NULL,
    NULL,
    ChapPrintPkt,
    NULL,
    1,
    "CHAP",
    NULL,
    NULL,
    NULL
};

chap_state chap[NUM_PPP];		/* CHAP state; one for each unit */

static void ChapChallengeTimeout(void *);
static void ChapResponseTimeout(void *);
static void ChapReceiveChallenge(chap_state *, u_char *, int, int);
static void ChapRechallenge(void *);
static void ChapReceiveResponse(chap_state *, u_char *, int, int);
static void ChapReceiveSuccess(chap_state *, u_char *, int, int);
static void ChapReceiveFailure(chap_state *, u_char *, int, int);
static void ChapSendStatus(chap_state *, int);
static void ChapSendChallenge(chap_state *);
static void ChapSendResponse(chap_state *);
static void ChapGenChallenge(chap_state *);

/*
 * ChapInit - Initialize a CHAP unit.
 */
static void
ChapInit(int unit)
{
    chap_state *cstate = &chap[unit];

    BZERO(cstate, sizeof(*cstate));
    cstate->unit = unit;
    cstate->clientstate = CHAPCS_INITIAL;
    cstate->serverstate = CHAPSS_INITIAL;
    cstate->timeouttime = CHAP_DEFTIMEOUT;
    cstate->max_transmits = CHAP_DEFTRANSMITS;
    /* random number generator is initialized in magic_init */
}


/*
 * ChapAuthWithPeer - Authenticate us with our peer (start client).
 *
 */
void
ChapAuthWithPeer(int unit, char *our_name, int digest)
{
    chap_state *cstate = &chap[unit];

    cstate->resp_name = our_name;
    cstate->resp_type = digest;

    if (cstate->clientstate == CHAPCS_INITIAL ||
	cstate->clientstate == CHAPCS_PENDING) {
	/* lower layer isn't up - wait until later */
	cstate->clientstate = CHAPCS_PENDING;
	return;
    }

    /*
     * We get here as a result of LCP coming up.
     * So even if CHAP was open before, we will
     * have to re-authenticate ourselves.
     */
    cstate->clientstate = CHAPCS_LISTEN;
}


/*
 * ChapAuthPeer - Authenticate our peer (start server).
 */
void
ChapAuthPeer(int unit, char *our_name, int digest)
{
    chap_state *cstate = &chap[unit];

    cstate->chal_name = our_name;
    cstate->chal_type = digest;

    if (cstate->serverstate == CHAPSS_INITIAL ||
	cstate->serverstate == CHAPSS_PENDING) {
	/* lower layer isn't up - wait until later */
	cstate->serverstate = CHAPSS_PENDING;
	return;
    }

    ChapGenChallenge(cstate);
    ChapSendChallenge(cstate);		/* crank it up dude! */
    cstate->serverstate = CHAPSS_INITIAL_CHAL;
}


/*
 * ChapChallengeTimeout - Timeout expired on sending challenge.
 */
static void
ChapChallengeTimeout(void *arg)
{
    chap_state *cstate = (chap_state *) arg;

    /* if we aren't sending challenges, don't worry.  then again we */
    /* probably shouldn't be here either */
    if (cstate->serverstate != CHAPSS_INITIAL_CHAL &&
	cstate->serverstate != CHAPSS_RECHALLENGE)
	return;

    if (cstate->chal_transmits >= cstate->max_transmits) {
	/* give up on peer */
	syslog(LOG_ERR, "Peer failed to respond to CHAP challenge");
	cstate->serverstate = CHAPSS_BADAUTH;
	auth_peer_fail(cstate->unit, PPP_CHAP);
	return;
    }

    ChapSendChallenge(cstate);		/* Re-send challenge */
}


/*
 * ChapResponseTimeout - Timeout expired on sending response.
 */
static void
ChapResponseTimeout(void *arg)
{
    chap_state *cstate = (chap_state *) arg;

    /* if we aren't sending a response, don't worry. */
    if (cstate->clientstate != CHAPCS_RESPONSE)
	return;

    ChapSendResponse(cstate);		/* re-send response */
}


/*
 * ChapRechallenge - Time to challenge the peer again.
 */
static void
ChapRechallenge(void *arg)
{
    chap_state *cstate = (chap_state *) arg;

    /* if we aren't sending a response, don't worry. */
    if (cstate->serverstate != CHAPSS_OPEN)
	return;

    ChapGenChallenge(cstate);
    ChapSendChallenge(cstate);
    cstate->serverstate = CHAPSS_RECHALLENGE;
}


/*
 * ChapLowerUp - The lower layer is up.
 *
 * Start up if we have pending requests.
 */
static void
ChapLowerUp(int unit)
{
    chap_state *cstate = &chap[unit];

    if (cstate->clientstate == CHAPCS_INITIAL)
	cstate->clientstate = CHAPCS_CLOSED;
    else if (cstate->clientstate == CHAPCS_PENDING)
	cstate->clientstate = CHAPCS_LISTEN;

    if (cstate->serverstate == CHAPSS_INITIAL)
	cstate->serverstate = CHAPSS_CLOSED;
    else if (cstate->serverstate == CHAPSS_PENDING) {
	ChapGenChallenge(cstate);
	ChapSendChallenge(cstate);
	cstate->serverstate = CHAPSS_INITIAL_CHAL;
    }
}


/*
 * ChapLowerDown - The lower layer is down.
 *
 * Cancel all timeouts.
 */
static void
ChapLowerDown(int unit)
{
    chap_state *cstate = &chap[unit];

    /* Timeout(s) pending?  Cancel if so. */
    if (cstate->serverstate == CHAPSS_INITIAL_CHAL ||
	cstate->serverstate == CHAPSS_RECHALLENGE)
	UNTIMEOUT(ChapChallengeTimeout, cstate);
    else if (cstate->serverstate == CHAPSS_OPEN
	     && cstate->chal_interval != 0)
	UNTIMEOUT(ChapRechallenge, cstate);
    if (cstate->clientstate == CHAPCS_RESPONSE)
	UNTIMEOUT(ChapResponseTimeout, cstate);

    cstate->clientstate = CHAPCS_INITIAL;
    cstate->serverstate = CHAPSS_INITIAL;
}


/*
 * ChapProtocolReject - Peer doesn't grok CHAP.
 */
static void
ChapProtocolReject(int unit)
{
    chap_state *cstate = &chap[unit];

    if (cstate->serverstate != CHAPSS_INITIAL &&
	cstate->serverstate != CHAPSS_CLOSED)
	auth_peer_fail(unit, PPP_CHAP);
    if (cstate->clientstate != CHAPCS_INITIAL &&
	cstate->clientstate != CHAPCS_CLOSED)
	auth_withpeer_fail(unit, PPP_CHAP);
    ChapLowerDown(unit);		/* shutdown chap */
}


/*
 * ChapInput - Input CHAP packet.
 */
static void
ChapInput(int unit, u_char *inpacket, int packet_len)
{
    chap_state *cstate = &chap[unit];
    u_char *inp;
    u_char code, id;
    int len;

    /*
     * Parse header (code, id and length).
     * If packet too short, drop it.
     */
    inp = inpacket;
    if (packet_len < CHAP_HEADERLEN) {
	CHAPDEBUG((LOG_INFO, "ChapInput: rcvd short header."));
	return;
    }
    GETCHAR(code, inp);
    GETCHAR(id, inp);
    GETSHORT(len, inp);
    if (len < CHAP_HEADERLEN) {
	CHAPDEBUG((LOG_INFO, "ChapInput: rcvd illegal length."));
	return;
    }
    if (len > packet_len) {
	CHAPDEBUG((LOG_INFO, "ChapInput: rcvd short packet."));
	return;
    }
    len -= CHAP_HEADERLEN;

    /*
     * Action depends on code (as in fact it usually does :-).
     */
    switch (code) {
    case CHAP_CHALLENGE:
	ChapReceiveChallenge(cstate, inp, id, len);
	break;

    case CHAP_RESPONSE:
	ChapReceiveResponse(cstate, inp, id, len);
	break;

    case CHAP_FAILURE:
	ChapReceiveFailure(cstate, inp, id, len);
	break;

    case CHAP_SUCCESS:
	ChapReceiveSuccess(cstate, inp, id, len);
	break;

    default:				/* Need code reject? */
	syslog(LOG_WARNING, "Unknown CHAP code (%d) received.", code);
	break;
    }
}


/*
 * ChapReceiveChallenge - Receive Challenge and send Response.
 */
static void
ChapReceiveChallenge(chap_state *cstate, u_char *inp, int id, int len)
{
    int rchallenge_len;
    u_char *rchallenge;
    int secret_len;
    char secret[MAXSECRETLEN];
    char rhostname[256];
    MD5_CTX mdContext;
    u_char hash[MD5_SIGNATURE_SIZE];

    CHAPDEBUG((LOG_INFO, "ChapReceiveChallenge: Rcvd id %d.", id));
    if (cstate->clientstate == CHAPCS_CLOSED ||
	cstate->clientstate == CHAPCS_PENDING) {
	CHAPDEBUG((LOG_INFO, "ChapReceiveChallenge: in state %d",
		   cstate->clientstate));
	return;
    }

    if (len < 2) {
	CHAPDEBUG((LOG_INFO, "ChapReceiveChallenge: rcvd short packet."));
	return;
    }

    GETCHAR(rchallenge_len, inp);
    len -= sizeof (u_char) + rchallenge_len;	/* now name field length */
    if (len < 0) {
	CHAPDEBUG((LOG_INFO, "ChapReceiveChallenge: rcvd short packet."));
	return;
    }
    rchallenge = inp;
    INCPTR(rchallenge_len, inp);

    if (len >= sizeof(rhostname))
	len = sizeof(rhostname) - 1;
    BCOPY(inp, rhostname, len);
    rhostname[len] = '\000';

    CHAPDEBUG((LOG_INFO, "ChapReceiveChallenge: received name field '%s'",
	rhostname));

    /* Microsoft doesn't send their name back in the PPP packet */
    if (remote_name[0] != 0 && (explicit_remote || rhostname[0] == 0)) {
	strlcpy(rhostname, remote_name, sizeof(rhostname));
	CHAPDEBUG((LOG_INFO, "ChapReceiveChallenge: using '%s' as remote name",
		  rhostname));
    }

    /* get secret for authenticating ourselves with the specified host */
    if (!get_secret(cstate->unit, cstate->resp_name, rhostname,
		    secret, &secret_len, 0)) {
	secret_len = 0;		/* assume null secret if can't find one */
	syslog(LOG_WARNING, "No CHAP secret found for authenticating us to %s",
	       rhostname);
    }

    /* cancel response send timeout if necessary */
    if (cstate->clientstate == CHAPCS_RESPONSE)
	UNTIMEOUT(ChapResponseTimeout, cstate);

    cstate->resp_id = id;
    cstate->resp_transmits = 0;

    /*  generate MD based on negotiated type */
    switch (cstate->resp_type) {

    case CHAP_DIGEST_MD5:
	MD5Init(&mdContext);
	MD5Update(&mdContext, &cstate->resp_id, 1);
	MD5Update(&mdContext, secret, secret_len);
	MD5Update(&mdContext, rchallenge, rchallenge_len);
	MD5Final(hash, &mdContext);
	BCOPY(hash, cstate->response, MD5_SIGNATURE_SIZE);
	cstate->resp_length = MD5_SIGNATURE_SIZE;
	break;

    default:
	CHAPDEBUG((LOG_INFO, "unknown digest type %d", cstate->resp_type));
	return;
    }

    EXPLICIT_BZERO(secret, sizeof(secret));
    ChapSendResponse(cstate);
}


/*
 * ChapReceiveResponse - Receive and process response.
 */
static void
ChapReceiveResponse(chap_state *cstate, u_char *inp, int id, int len)
{
    u_char *remmd, remmd_len;
    int secret_len, old_state;
    int code;
    char rhostname[256];
    MD5_CTX mdContext;
    char secret[MAXSECRETLEN];
    u_char hash[MD5_SIGNATURE_SIZE];

    CHAPDEBUG((LOG_INFO, "ChapReceiveResponse: Rcvd id %d.", id));

    if (cstate->serverstate == CHAPSS_CLOSED ||
	cstate->serverstate == CHAPSS_PENDING) {
	CHAPDEBUG((LOG_INFO, "ChapReceiveResponse: in state %d",
		   cstate->serverstate));
	return;
    }

    if (id != cstate->chal_id)
	return;			/* doesn't match ID of last challenge */

    /*
     * If we have received a duplicate or bogus Response,
     * we have to send the same answer (Success/Failure)
     * as we did for the first Response we saw.
     */
    if (cstate->serverstate == CHAPSS_OPEN) {
	ChapSendStatus(cstate, CHAP_SUCCESS);
	return;
    }
    if (cstate->serverstate == CHAPSS_BADAUTH) {
	ChapSendStatus(cstate, CHAP_FAILURE);
	return;
    }

    if (len < 2) {
	CHAPDEBUG((LOG_INFO, "ChapReceiveResponse: rcvd short packet."));
	return;
    }
    GETCHAR(remmd_len, inp);		/* get length of MD */
    remmd = inp;			/* get pointer to MD */
    INCPTR(remmd_len, inp);

    len -= sizeof (u_char) + remmd_len;
    if (len < 0) {
	CHAPDEBUG((LOG_INFO, "ChapReceiveResponse: rcvd short packet."));
	return;
    }

    UNTIMEOUT(ChapChallengeTimeout, cstate);

    if (len >= sizeof(rhostname))
	len = sizeof(rhostname) - 1;
    BCOPY(inp, rhostname, len);
    rhostname[len] = '\000';

    CHAPDEBUG((LOG_INFO, "ChapReceiveResponse: received name field: %s",
	       rhostname));

    /*
     * Get secret for authenticating them with us,
     * do the hash ourselves, and compare the result.
     */
    code = CHAP_FAILURE;
    if (!get_secret(cstate->unit, rhostname, cstate->chal_name,
		   secret, &secret_len, 1)) {
	syslog(LOG_WARNING, "No CHAP secret found for authenticating %s",
	       rhostname);
    } else {

	/*  generate MD based on negotiated type */
	switch (cstate->chal_type) {

	case CHAP_DIGEST_MD5:		/* only MD5 is defined for now */
	    if (remmd_len != MD5_SIGNATURE_SIZE)
		break;			/* it's not even the right length */
	    MD5Init(&mdContext);
	    MD5Update(&mdContext, &cstate->chal_id, 1);
	    MD5Update(&mdContext, secret, secret_len);
	    MD5Update(&mdContext, cstate->challenge, cstate->chal_len);
	    MD5Final(hash, &mdContext);

	    /* compare local and remote MDs and send the appropriate status */
	    if (memcmp (hash, remmd, MD5_SIGNATURE_SIZE) == 0)
		code = CHAP_SUCCESS;	/* they are the same! */
	    break;

	default:
	    CHAPDEBUG((LOG_INFO, "unknown digest type %d", cstate->chal_type));
	}
    }

    EXPLICIT_BZERO(secret, sizeof(secret));
    ChapSendStatus(cstate, code);

    if (code == CHAP_SUCCESS) {
	old_state = cstate->serverstate;
	cstate->serverstate = CHAPSS_OPEN;
	if (old_state == CHAPSS_INITIAL_CHAL) {
            auth_peer_success(cstate->unit, PPP_CHAP, rhostname, len);
	}
	if (cstate->chal_interval != 0)
	    TIMEOUT(ChapRechallenge, cstate, cstate->chal_interval);
	syslog(LOG_NOTICE, "CHAP peer authentication succeeded for %s",
		rhostname);

    } else {
	syslog(LOG_ERR, "CHAP peer authentication failed for remote host %s",
		rhostname);
	cstate->serverstate = CHAPSS_BADAUTH;
	auth_peer_fail(cstate->unit, PPP_CHAP);
    }
}

/*
 * ChapReceiveSuccess - Receive Success
 */
static void
ChapReceiveSuccess(chap_state *cstate, u_char *inp, int id, int len)
{

    CHAPDEBUG((LOG_INFO, "ChapReceiveSuccess: Rcvd id %d.", id));

    if (cstate->clientstate == CHAPCS_OPEN)
	/* presumably an answer to a duplicate response */
	return;

    if (cstate->clientstate != CHAPCS_RESPONSE) {
	/* don't know what this is */
	CHAPDEBUG((LOG_INFO, "ChapReceiveSuccess: in state %d\n",
		   cstate->clientstate));
	return;
    }

    UNTIMEOUT(ChapResponseTimeout, cstate);

    /*
     * Print message.
     */
    if (len > 0)
	PRINTMSG(inp, len);

    cstate->clientstate = CHAPCS_OPEN;

    auth_withpeer_success(cstate->unit, PPP_CHAP);
}


/*
 * ChapReceiveFailure - Receive failure.
 */
static void
ChapReceiveFailure(chap_state *cstate, u_char *inp, int id, int len)
{
    CHAPDEBUG((LOG_INFO, "ChapReceiveFailure: Rcvd id %d.", id));

    if (cstate->clientstate != CHAPCS_RESPONSE) {
	/* don't know what this is */
	CHAPDEBUG((LOG_INFO, "ChapReceiveFailure: in state %d\n",
		   cstate->clientstate));
	return;
    }

    UNTIMEOUT(ChapResponseTimeout, cstate);

    /*
     * Print message.
     */
    if (len > 0)
	PRINTMSG(inp, len);

    syslog(LOG_ERR, "CHAP authentication failed");
    auth_withpeer_fail(cstate->unit, PPP_CHAP);
}


/*
 * ChapSendChallenge - Send an Authenticate challenge.
 */
static void
ChapSendChallenge(chap_state *cstate)
{
    u_char *outp;
    int chal_len, name_len;
    int outlen;

    chal_len = cstate->chal_len;
    name_len = strlen(cstate->chal_name);
    outlen = CHAP_HEADERLEN + sizeof (u_char) + chal_len + name_len;
    outp = outpacket_buf;

    MAKEHEADER(outp, PPP_CHAP);		/* paste in a CHAP header */

    PUTCHAR(CHAP_CHALLENGE, outp);
    PUTCHAR(cstate->chal_id, outp);
    PUTSHORT(outlen, outp);

    PUTCHAR(chal_len, outp);		/* put length of challenge */
    BCOPY(cstate->challenge, outp, chal_len);
    INCPTR(chal_len, outp);

    BCOPY(cstate->chal_name, outp, name_len);	/* append hostname */

    output(cstate->unit, outpacket_buf, outlen + PPP_HDRLEN);

    CHAPDEBUG((LOG_INFO, "ChapSendChallenge: Sent id %d.", cstate->chal_id));

    TIMEOUT(ChapChallengeTimeout, cstate, cstate->timeouttime);
    ++cstate->chal_transmits;
}


/*
 * ChapSendStatus - Send a status response (ack or nak).
 */
static void
ChapSendStatus(chap_state *cstate, int code)
{
    u_char *outp;
    int outlen, msglen;
    char msg[256];

    if (code == CHAP_SUCCESS)
	snprintf(msg, sizeof msg, "Welcome to %s.", hostname);
    else
	snprintf(msg, sizeof msg, "I don't like you.  Go 'way.");
    msglen = strlen(msg);

    outlen = CHAP_HEADERLEN + msglen;
    outp = outpacket_buf;

    MAKEHEADER(outp, PPP_CHAP);	/* paste in a header */

    PUTCHAR(code, outp);
    PUTCHAR(cstate->chal_id, outp);
    PUTSHORT(outlen, outp);
    BCOPY(msg, outp, msglen);
    output(cstate->unit, outpacket_buf, outlen + PPP_HDRLEN);

    CHAPDEBUG((LOG_INFO, "ChapSendStatus: Sent code %d, id %d.", code,
	       cstate->chal_id));
}

/*
 * ChapGenChallenge is used to generate a pseudo-random challenge string of
 * a pseudo-random length between min_len and max_len.  The challenge
 * string and its length are stored in *cstate, and various other fields of
 * *cstate are initialized.
 */

static void
ChapGenChallenge(chap_state *cstate)
{
    int chal_len;

    /* pick a random challenge length >= MIN_CHALLENGE_LENGTH and
       <= MAX_CHALLENGE_LENGTH */
    chal_len = MIN_CHALLENGE_LENGTH +
	arc4random_uniform(MAX_CHALLENGE_LENGTH - MIN_CHALLENGE_LENGTH + 1);
			    
    cstate->chal_len = chal_len;
    cstate->chal_id = ++cstate->id;
    cstate->chal_transmits = 0;

    /* generate a random string */
    arc4random_buf(cstate->challenge, chal_len);
}

/*
 * ChapSendResponse - send a response packet with values as specified
 * in *cstate.
 */
static void
ChapSendResponse(chap_state *cstate)
{
    u_char *outp;
    int outlen, md_len, name_len;

    md_len = cstate->resp_length;
    name_len = strlen(cstate->resp_name);
    outlen = CHAP_HEADERLEN + sizeof (u_char) + md_len + name_len;
    outp = outpacket_buf;

    MAKEHEADER(outp, PPP_CHAP);

    PUTCHAR(CHAP_RESPONSE, outp);	/* we are a response */
    PUTCHAR(cstate->resp_id, outp);	/* copy id from challenge packet */
    PUTSHORT(outlen, outp);		/* packet length */

    PUTCHAR(md_len, outp);		/* length of MD */
    BCOPY(cstate->response, outp, md_len);	/* copy MD to buffer */
    INCPTR(md_len, outp);

    BCOPY(cstate->resp_name, outp, name_len); /* append our name */

    /* send the packet */
    output(cstate->unit, outpacket_buf, outlen + PPP_HDRLEN);

    cstate->clientstate = CHAPCS_RESPONSE;
    TIMEOUT(ChapResponseTimeout, cstate, cstate->timeouttime);
    ++cstate->resp_transmits;
}

/*
 * ChapPrintPkt - print the contents of a CHAP packet.
 */
static char *ChapCodenames[] = {
    "Challenge", "Response", "Success", "Failure"
};

static int
ChapPrintPkt(u_char *p, int plen, void (*printer)(void *, char *, ...), void *arg)
{
    int code, id, len;
    int clen, nlen;
    u_char x;

    if (plen < CHAP_HEADERLEN)
	return 0;
    GETCHAR(code, p);
    GETCHAR(id, p);
    GETSHORT(len, p);
    if (len < CHAP_HEADERLEN || len > plen)
	return 0;

    if (code >= 1 && code <= sizeof(ChapCodenames) / sizeof(char *))
	printer(arg, " %s", ChapCodenames[code-1]);
    else
	printer(arg, " code=0x%x", code);
    printer(arg, " id=0x%x", id);
    len -= CHAP_HEADERLEN;
    switch (code) {
    case CHAP_CHALLENGE:
    case CHAP_RESPONSE:
	if (len < 1)
	    break;
	clen = p[0];
	if (len < clen + 1)
	    break;
	++p;
	nlen = len - clen - 1;
	printer(arg, " <");
	for (; clen > 0; --clen) {
	    GETCHAR(x, p);
	    printer(arg, "%.2x", x);
	}
	printer(arg, ">, name = ");
	print_string((char *)p, nlen, printer, arg);
	break;
    case CHAP_FAILURE:
    case CHAP_SUCCESS:
	printer(arg, " ");
	print_string((char *)p, len, printer, arg);
	break;
    default:
	for (clen = len; clen > 0; --clen) {
	    GETCHAR(x, p);
	    printer(arg, " %.2x", x);
	}
    }

    return len + CHAP_HEADERLEN;
}
