/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
/* $FreeBSD$ */

#include <errno.h>
#include <pwd.h>

#include "krb5/gsskrb5_locl.h"

OM_uint32
_gsskrb5_pname_to_uid(OM_uint32 *minor_status, const gss_name_t pname,
    const gss_OID mech, uid_t *uidp)
{
	krb5_context context;
	krb5_const_principal name = (krb5_const_principal) pname;
	krb5_error_code kret;
	char lname[MAXLOGNAME + 1], buf[1024], *bufp;
	struct passwd pwd, *pw;
	size_t buflen;
	int error;
	OM_uint32 ret;
	static size_t buflen_hint = 1024;

	GSSAPI_KRB5_INIT (&context);

	kret = krb5_aname_to_localname(context, name, sizeof(lname), lname);
	if (kret) {
		*minor_status = kret;
		return (GSS_S_FAILURE);
	}

	*minor_status = 0;
	buflen = buflen_hint;
	for (;;) {
		pw = NULL;
		bufp = buf;
		if (buflen > sizeof(buf))
			bufp = malloc(buflen);
		if (bufp == NULL)
			break;
		error = getpwnam_r(lname, &pwd, bufp, buflen, &pw);
		if (error != ERANGE)
			break;
		if (buflen > sizeof(buf))
			free(bufp);
		buflen += 1024;
		if (buflen > buflen_hint)
			buflen_hint = buflen;
	}
	if (pw) {
		*uidp = pw->pw_uid;
		ret = GSS_S_COMPLETE;
	} else {
		ret = GSS_S_FAILURE;
	}
	if (bufp != NULL && buflen > sizeof(buf))
		free(bufp);
	return (ret);
}
