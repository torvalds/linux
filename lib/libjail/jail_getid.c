/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 James Gritton.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/jail.h>
#include <sys/uio.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jail.h"


/*
 * Return the JID corresponding to a jail name.
 */
int
jail_getid(const char *name)
{
	char *ep;
	int jid;
	struct iovec jiov[4];

	jid = strtoul(name, &ep, 10);
	if (*name && !*ep)
		return jid;
	jiov[0].iov_base = __DECONST(char *, "name");
	jiov[0].iov_len = sizeof("name");
	jiov[1].iov_len = strlen(name) + 1;
	jiov[1].iov_base = alloca(jiov[1].iov_len);
	strcpy(jiov[1].iov_base, name);
	jiov[2].iov_base = __DECONST(char *, "errmsg");
	jiov[2].iov_len = sizeof("errmsg");
	jiov[3].iov_base = jail_errmsg;
	jiov[3].iov_len = JAIL_ERRMSGLEN;
	jail_errmsg[0] = 0;
	jid = jail_get(jiov, 4, 0);
	if (jid < 0 && !jail_errmsg[0])
		snprintf(jail_errmsg, JAIL_ERRMSGLEN, "jail_get: %s",
		    strerror(errno));
	return jid;
}

/*
 * Return the name corresponding to a JID.
 */
char *
jail_getname(int jid)
{
	struct iovec jiov[6];
	char *name;
	char namebuf[MAXHOSTNAMELEN];

	jiov[0].iov_base = __DECONST(char *, "jid");
	jiov[0].iov_len = sizeof("jid");
	jiov[1].iov_base = &jid;
	jiov[1].iov_len = sizeof(jid);
	jiov[2].iov_base = __DECONST(char *, "name");
	jiov[2].iov_len = sizeof("name");
	jiov[3].iov_base = namebuf;
	jiov[3].iov_len = sizeof(namebuf);
	jiov[4].iov_base = __DECONST(char *, "errmsg");
	jiov[4].iov_len = sizeof("errmsg");
	jiov[5].iov_base = jail_errmsg;
	jiov[5].iov_len = JAIL_ERRMSGLEN;
	jail_errmsg[0] = 0;
	jid = jail_get(jiov, 6, 0);
	if (jid < 0) {
		if (!jail_errmsg[0])
			snprintf(jail_errmsg, JAIL_ERRMSGLEN, "jail_get: %s",
			    strerror(errno));
		return NULL;
	} else {
		name = strdup(namebuf);
		if (name == NULL)
			strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
	}
	return name;
}
