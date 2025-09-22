/*	$OpenBSD: rfc868time.c,v 1.13 2023/01/04 13:00:11 jsg Exp $	*/
/*	$NetBSD: rdate.c,v 1.4 1996/03/16 12:37:45 pk Exp $	*/

/*
 * Copyright (c) 1994 Christos Zoulas
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * rdate.c: Set the date from the specified host
 *
 *	Uses the rfc868 time protocol at socket 37 (tcp).
 *	Time is returned as the number of seconds since
 *	midnight January 1st 1900.
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <stdio.h>
#include <ctype.h>
#include <err.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>

/* Obviously it is not just for SNTP clients... */
#include "ntpleaps.h"

/* seconds from midnight Jan 1900 - 1970 */
#define DIFFERENCE 2208988800UL

void
rfc868time_client(const char *hostname, int family, struct timeval *new,
    struct timeval *adjust, int leapflag);


void
rfc868time_client(const char *hostname, int family, struct timeval *new,
    struct timeval *adjust, int leapflag)
{
	struct addrinfo hints, *res0, *res;
	struct timeval old;
	u_int32_t tim;	/* RFC 868 states clearly this is an uint32 */
	int s;
	int error;
	u_int64_t td;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(hostname, "time", &hints, &res0);
	if (error) {
		errx(1, "%s: %s", hostname, gai_strerror(error));
		/*NOTREACHED*/
	}

	if (pledge("stdio inet", NULL) == -1)
		err(1, "pledge");

	s = -1;
	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1)
			continue;

		if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
			close(s);
			s = -1;
			continue;
		}

		break;
	}
	if (s == -1)
		err(1, "Could not connect socket");
	freeaddrinfo(res0);

	if (read(s, &tim, sizeof(tim)) != sizeof(tim))
		err(1, "Could not read data");

	(void) close(s);
	tim = ntohl(tim) - DIFFERENCE;

	if (gettimeofday(&old, NULL) == -1)
		err(1, "Could not get local time of day");

	td = SEC_TO_TAI64(old.tv_sec);
	if (leapflag)
		ntpleaps_sub(&td);

	adjust->tv_sec = tim - TAI64_TO_SEC(td);
	adjust->tv_usec = 0;

	new->tv_sec = old.tv_sec + adjust->tv_sec;
	new->tv_usec = 0;
}
