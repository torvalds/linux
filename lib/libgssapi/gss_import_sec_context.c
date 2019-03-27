/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Doug Rabson
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
 *
 *	$FreeBSD$
 */

#include <gssapi/gssapi.h>
#include <stdlib.h>
#include <errno.h>

#include "mech_switch.h"
#include "context.h"

OM_uint32
gss_import_sec_context(OM_uint32 *minor_status,
    const gss_buffer_t interprocess_token,
    gss_ctx_id_t *context_handle)
{
	OM_uint32 major_status;
	struct _gss_mech_switch *m;
	struct _gss_context *ctx;
	gss_OID_desc mech_oid;
	gss_buffer_desc buf;
	unsigned char *p;
	size_t len;

	*minor_status = 0;
	*context_handle = GSS_C_NO_CONTEXT;

	/*
	 * We added an oid to the front of the token in
	 * gss_export_sec_context.
	 */
	p = interprocess_token->value;
	len = interprocess_token->length;
	if (len < 2)
		return (GSS_S_DEFECTIVE_TOKEN);
	mech_oid.length = (p[0] << 8) | p[1];
	if (len < mech_oid.length + 2)
		return (GSS_S_DEFECTIVE_TOKEN);
	mech_oid.elements = p + 2;
	buf.length = len - 2 - mech_oid.length;
	buf.value = p + 2 + mech_oid.length;
	
	m = _gss_find_mech_switch(&mech_oid);
	if (!m)
		return (GSS_S_DEFECTIVE_TOKEN);

	ctx = malloc(sizeof(struct _gss_context));
	if (!ctx) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}
	ctx->gc_mech = m;
	major_status = m->gm_import_sec_context(minor_status,
	    &buf, &ctx->gc_ctx);
	if (major_status != GSS_S_COMPLETE) {
		_gss_mg_error(m, major_status, *minor_status);
		free(ctx);
	} else {
		*context_handle = (gss_ctx_id_t) ctx;
	}

	return (major_status);
}
