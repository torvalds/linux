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
#include "cred.h"

OM_uint32
gsskrb5_register_acceptor_identity(const char *identity)
{
	struct _gss_mech_switch *m;

	_gss_load_mech();
	SLIST_FOREACH(m, &_gss_mechs, gm_link) {
		if (m->gm_krb5_register_acceptor_identity)
			m->gm_krb5_register_acceptor_identity(identity);
	}

	return (GSS_S_COMPLETE);
}

OM_uint32
gss_krb5_copy_ccache(OM_uint32 *minor_status,
    gss_cred_id_t cred_handle,
    struct krb5_ccache_data *out)
{
	struct _gss_mechanism_cred *mcp;
	struct _gss_cred *cred = (struct _gss_cred *) cred_handle;
	struct _gss_mech_switch *m;

	*minor_status = 0;

	SLIST_FOREACH(mcp, &cred->gc_mc, gmc_link) {
		m = mcp->gmc_mech;
		if (m->gm_krb5_copy_ccache)
			return (m->gm_krb5_copy_ccache(minor_status,
				mcp->gmc_cred, out));
	}

	return (GSS_S_FAILURE);
}

OM_uint32
gss_krb5_compat_des3_mic(OM_uint32 *minor_status,
    gss_ctx_id_t context_handle, int flag)
{
	struct _gss_context *ctx = (struct _gss_context *) context_handle;
	struct _gss_mech_switch *m = ctx->gc_mech;

	*minor_status = 0;

	if (m->gm_krb5_compat_des3_mic)
		return (m->gm_krb5_compat_des3_mic(minor_status,
			ctx->gc_ctx, flag));

	return (GSS_S_FAILURE);
}

