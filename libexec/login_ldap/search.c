/*
 * $OpenBSD: search.c,v 1.1 2020/09/12 15:06:12 martijn Exp $
 * Copyright (c) 2002 Institute for Open Systems Technology Australia (IFOST)
 * Copyright (c) 2007 Michael Erdely <merdely@openbsd.org>
 * Copyright (c) 2019 Martijn van Duren <martijn@openbsd.org>
 *
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <login_cap.h>
#include <unistd.h>

#include "aldap.h"
#include "login_ldap.h"

int q = 0;

char *
search(struct auth_ctx *ctx, char *base, char *flt, enum scope scp)
{
	struct aldap_message *m;
	int mcode;
	const char *errstr;
	char *userdn = NULL;

	dlog(1, "%d: search (%s, %s)", q, base ? base : "no base", flt);

	if (aldap_search(ctx->ld, base, scp, flt, NULL, 0, 0, ctx->timeout,
	    NULL) == -1) {
		aldap_get_errno(ctx->ld, &errstr);
		dlog(1, "search result: %s", errstr);
		return NULL;
	}

	q++;

	while ((m = aldap_parse(ctx->ld)) != NULL) {
		dlog(1, "%d: msgid %d, type %02x",q, m->msgid, m->message_type);

		if (m->message_type == LDAP_RES_SEARCH_RESULT) {
			mcode = aldap_get_resultcode(m);
			/*
			 * if its not a referral we're done with this message
			 */
			if (mcode != LDAP_SUCCESS) {
				dlog(0, "%d: unhandled search result %x %s",
				    q, mcode, ldap_resultcode(mcode));
				aldap_freemsg(m);
				return NULL;
			}
			aldap_freemsg(m);
			break;
		} else if (m->message_type == LDAP_RES_SEARCH_ENTRY) {
			userdn = aldap_get_dn(m);
			dlog(1, "%d: SEARCH_ENTRY userdn %s", q, userdn ? userdn : "none");
		}
		aldap_freemsg(m);
	}

	dlog(1, "%d: returning userdn = %s", q, userdn ? userdn : "no user dn");
	q--;
	return userdn;
}
