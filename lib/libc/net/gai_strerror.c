/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <netdb.h>
#if defined(NLS)
#include <nl_types.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "reentrant.h"
#endif
#include "un-namespace.h"

/* Entries EAI_ADDRFAMILY (1) and EAI_NODATA (7) are obsoleted, but left */
/* for backwards compatibility with userland code prior to RFC2553bis-02 */
static const char *ai_errlist[] = {
	"Success",					/* 0 */
	"Address family for hostname not supported",	/* 1: Obsolete */
	"Name could not be resolved at this time",	/* EAI_AGAIN */
	"Flags parameter had an invalid value",		/* EAI_BADFLAGS */
	"Non-recoverable failure in name resolution",	/* EAI_FAIL */
	"Address family not recognized",		/* EAI_FAMILY */
	"Memory allocation failure", 			/* EAI_MEMORY */
	"No address associated with hostname",		/* 7: Obsolete*/
	"Name does not resolve",			/* EAI_NONAME */
	"Service was not recognized for socket type",	/* EAI_SERVICE */
	"Intended socket type was not recognized",	/* EAI_SOCKTYPE */
	"System error returned in errno", 		/* EAI_SYSTEM */
	"Invalid value for hints",			/* EAI_BADHINTS */
	"Resolved protocol is unknown",			/* EAI_PROTOCOL */
	"Argument buffer overflow"			/* EAI_OVERFLOW */
};

#if defined(NLS)
static char		gai_buf[NL_TEXTMAX];
static once_t		gai_init_once = ONCE_INITIALIZER;
static thread_key_t	gai_key;
static int		gai_keycreated = 0;

static void
gai_keycreate(void)
{
	gai_keycreated = (thr_keycreate(&gai_key, free) == 0);
}
#endif

const char *
gai_strerror(int ecode)
{
#if defined(NLS)
	nl_catd catd;
	char *buf;

	if (thr_main() != 0)
		buf = gai_buf;
	else {
		if (thr_once(&gai_init_once, gai_keycreate) != 0 ||
		    !gai_keycreated)
			goto thr_err;
		if ((buf = thr_getspecific(gai_key)) == NULL) {
			if ((buf = malloc(sizeof(gai_buf))) == NULL)
				goto thr_err;
			if (thr_setspecific(gai_key, buf) != 0) {
				free(buf);
				goto thr_err;
			}
		}
	}

	catd = catopen("libc", NL_CAT_LOCALE);
	if (ecode > 0 && ecode < EAI_MAX)
		strlcpy(buf, catgets(catd, 3, ecode, ai_errlist[ecode]),
		    sizeof(gai_buf));
	else if (ecode == 0)
		strlcpy(buf, catgets(catd, 3, NL_MSGMAX - 1, "Success"),
		    sizeof(gai_buf));
	else
		strlcpy(buf, catgets(catd, 3, NL_MSGMAX, "Unknown error"),
		    sizeof(gai_buf));
	catclose(catd);
	return buf;

thr_err:
#endif
	if (ecode >= 0 && ecode < EAI_MAX)
		return ai_errlist[ecode];
	return "Unknown error";
}
