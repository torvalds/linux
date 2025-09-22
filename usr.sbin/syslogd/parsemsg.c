/*	$OpenBSD: parsemsg.c,v 1.1 2022/01/13 10:34:07 martijn Exp $	*/

/*
 * Copyright (c) 2022 Martijn van Duren <martijn@openbsd>
 * Copyright (c) 2014-2021 Alexander Bluhm <bluhm@genua.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "parsemsg.h"
#include "syslogd.h"

size_t parsemsg_timestamp_bsd(const char *, char *);
size_t parsemsg_timestamp_v1(const char *, char *);
size_t parsemsg_prog(const char *, char *);

struct msg *
parsemsg(const char *msgstr, struct msg *msg)
{
	size_t n;

	msg->m_pri = -1;
	msgstr += parsemsg_priority(msgstr, &msg->m_pri);
	if (msg->m_pri &~ (LOG_FACMASK|LOG_PRIMASK))
		msg->m_pri = -1;

	if ((n = parsemsg_timestamp_bsd(msgstr, msg->m_timestamp)) == 0)
		n = parsemsg_timestamp_v1(msgstr, msg->m_timestamp);
	msgstr += n;

	while (isspace(msgstr[0]))
		msgstr++;

	parsemsg_prog(msgstr, msg->m_prog);

	strlcpy(msg->m_msg, msgstr, sizeof(msg->m_msg));

	return msg;
}

/*
 * Parse a priority code of the form "<123>" into pri, and return the
 * length of the priority code including the surrounding angle brackets.
 */
size_t
parsemsg_priority(const char *msg, int *pri)
{
	size_t nlen;
	char buf[11];
	const char *errstr;
	int maybepri;

	if (*msg++ == '<') {
		nlen = strspn(msg, "1234567890");
		if (nlen > 0 && nlen < sizeof(buf) && msg[nlen] == '>') {
			strlcpy(buf, msg, nlen + 1);
			maybepri = strtonum(buf, 0, INT_MAX, &errstr);
			if (errstr == NULL) {
				*pri = maybepri;
				return nlen + 2;
			}
		}
	}

	return 0;
}

size_t
parsemsg_timestamp_bsd(const char *msg, char *timestamp)
{
	size_t i;

	timestamp[0] = '\0';
	for (i = 0; i < 16; i++) {
		if (msg[i] == '\0')
			return 0;
	}

	if (msg[3] == ' ' && msg[6] == ' ' && msg[9] == ':' && msg[12] == ':' &&
	    msg[15] == ' ') {
		/* BSD syslog TIMESTAMP, RFC 3164 */
		if (!ZuluTime)
			strlcpy(timestamp, msg, 16);
		return 16;
	}

	return 0;
}

size_t
parsemsg_timestamp_v1(const char *msgstr, char *timestamp)
{
	const char *msg;
	size_t msglen, i;

	for (msglen = 0; msglen < 33; msglen++) {
		if(msgstr[msglen] == '\0')
			break;
	}

	msg = msgstr;
	timestamp[0] = '\0';

	if (msglen >= 20 &&
	    isdigit(msg[0]) && isdigit(msg[1]) && isdigit(msg[2]) &&
	    isdigit(msg[3]) && msg[4] == '-' &&
	    isdigit(msg[5]) && isdigit(msg[6]) && msg[7] == '-' &&
	    isdigit(msg[8]) && isdigit(msg[9]) && msg[10] == 'T' &&
	    isdigit(msg[11]) && isdigit(msg[12]) && msg[13] == ':' &&
	    isdigit(msg[14]) && isdigit(msg[15]) && msg[16] == ':' &&
	    isdigit(msg[17]) && isdigit(msg[18]) && (msg[19] == '.' ||
	    msg[19] == 'Z' || msg[19] == '+' || msg[19] == '-')) {
		/* FULL-DATE "T" FULL-TIME, RFC 5424 */
		strlcpy(timestamp, msg, 33);
		msg += 19;
		msglen -= 19;
		i = 0;
		if (msglen >= 3 && msg[0] == '.' && isdigit(msg[1])) {
			/* TIME-SECFRAC */
			msg += 2;
			msglen -= 2;
			i += 2;
			while(i < 7 && msglen >= 1 && isdigit(msg[0])) {
				msg++;
				msglen--;
				i++;
			}
		}
		if (msglen >= 2 && msg[0] == 'Z' && msg[1] == ' ') {
			/* "Z" */
			timestamp[20+i] = '\0';
			msg += 2;
		} else if (msglen >= 7 &&
		    (msg[0] == '+' || msg[0] == '-') &&
		    isdigit(msg[1]) && isdigit(msg[2]) &&
		    msg[3] == ':' &&
		    isdigit(msg[4]) && isdigit(msg[5]) &&
		    msg[6] == ' ') {
			/* TIME-NUMOFFSET */
			timestamp[25+i] = '\0';
			msg += 7;
		} else {
			/* invalid time format, roll back */
			timestamp[0] = '\0';
			return 0;
		}
	} else if (msglen >= 2 && msg[0] == '-' && msg[1] == ' ') {
		/* NILVALUE, RFC 5424 */
		msg += 2;
	}

	return msg - msgstr;
}

size_t
parsemsg_prog(const char *msg, char *prog)
{
	size_t i;

	for (i = 0; i < NAME_MAX; i++) {
		if (!isalnum((unsigned char)msg[i]) &&
		    msg[i] != '-' && msg[i] != '.' && msg[i] != '_')
			break;
		prog[i] = msg[i];
	}
	prog[i] = '\0';

	return i;
}
