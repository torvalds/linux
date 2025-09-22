/*
 * $OpenBSD: bind.c,v 1.1 2020/09/12 15:06:12 martijn Exp $
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
#include <login_cap.h>

#include "aldap.h"
#include "login_ldap.h"

int
bind_password(struct auth_ctx *ctx, char *binddn, char *bindpw)
{
	struct aldap_message *m;

	if (aldap_bind(ctx->ld, binddn, bindpw) == -1) {
		dlog(0, "Failed to send bind request");
		return 0;
	}

	if ((m = aldap_parse(ctx->ld)) == NULL) {
		dlog(0, "Failed to receive bind response");
		return 0;
	}

	if (ctx->ld->msgid != m->msgid) {
		dlog(0, "Failed to bind: Received unexpected message id");
		aldap_freemsg(m);
		return 0;
	}
	if (aldap_get_resultcode(m) != LDAP_SUCCESS) {
		dlog(0, "Failed to bind: %s",
		    ldap_resultcode(aldap_get_resultcode(m)));
		aldap_freemsg(m);
		return 0;
	}
	aldap_freemsg(m);

	return 1;
}

int
unbind(struct auth_ctx *ctx)
{
	return aldap_unbind(ctx->ld);
}

