/*	$OpenBSD: upap.h,v 1.6 2002/07/01 19:31:37 deraadt Exp $	*/

/*
 * upap.h - User/Password Authentication Protocol definitions.
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
 * Packet header = Code, id, length.
 */
#define UPAP_HEADERLEN	(sizeof (u_char) + sizeof (u_char) + sizeof (u_short))


/*
 * UPAP codes.
 */
#define UPAP_AUTHREQ	1	/* Authenticate-Request */
#define UPAP_AUTHACK	2	/* Authenticate-Ack */
#define UPAP_AUTHNAK	3	/* Authenticate-Nak */


/*
 * Each interface is described by upap structure.
 */
typedef struct upap_state {
    int us_unit;		/* Interface unit number */
    char *us_user;		/* User */
    int us_userlen;		/* User length */
    char *us_passwd;		/* Password */
    int us_passwdlen;		/* Password length */
    int us_clientstate;		/* Client state */
    int us_serverstate;		/* Server state */
    u_char us_id;		/* Current id */
    int us_timeouttime;		/* Timeout (seconds) for auth-req retrans. */
    int us_transmits;		/* Number of auth-reqs sent */
    int us_maxtransmits;	/* Maximum number of auth-reqs to send */
    int us_reqtimeout;		/* Time to wait for auth-req from peer */
} upap_state;


/*
 * Client states.
 */
#define UPAPCS_INITIAL	0	/* Connection down */
#define UPAPCS_CLOSED	1	/* Connection up, haven't requested auth */
#define UPAPCS_PENDING	2	/* Connection down, have requested auth */
#define UPAPCS_AUTHREQ	3	/* We've sent an Authenticate-Request */
#define UPAPCS_OPEN	4	/* We've received an Ack */
#define UPAPCS_BADAUTH	5	/* We've received a Nak */

/*
 * Server states.
 */
#define UPAPSS_INITIAL	0	/* Connection down */
#define UPAPSS_CLOSED	1	/* Connection up, haven't requested auth */
#define UPAPSS_PENDING	2	/* Connection down, have requested auth */
#define UPAPSS_LISTEN	3	/* Listening for an Authenticate */
#define UPAPSS_OPEN	4	/* We've sent an Ack */
#define UPAPSS_BADAUTH	5	/* We've sent a Nak */


/*
 * Timeouts.
 */
#define UPAP_DEFTIMEOUT	3	/* Timeout (seconds) for retransmitting req */
#define UPAP_DEFREQTIME	30	/* Time to wait for auth-req from peer */

extern upap_state upap[];

void upap_authwithpeer(int, char *, char *);
void upap_authpeer(int);

extern struct protent pap_protent;
