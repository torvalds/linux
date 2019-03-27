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
#include <string.h>
#include <errno.h>

#include "mech_switch.h"
#include "name.h"
#include "cred.h"
#include "context.h"
#include "utils.h"

static gss_cred_id_t
_gss_mech_cred_find(gss_cred_id_t cred_handle, gss_OID mech_type)
{
	struct _gss_cred *cred = (struct _gss_cred *)cred_handle;
	struct _gss_mechanism_cred *mc;

	if (cred == NULL)
		return GSS_C_NO_CREDENTIAL;

	SLIST_FOREACH(mc, &cred->gc_mc, gmc_link) {
		if (gss_oid_equal(mech_type, mc->gmc_mech_oid))
			return mc->gmc_cred;
	}
	return GSS_C_NO_CREDENTIAL;
}

OM_uint32
gss_init_sec_context(OM_uint32 * minor_status,
    const gss_cred_id_t initiator_cred_handle,
    gss_ctx_id_t * context_handle,
    const gss_name_t target_name,
    const gss_OID input_mech_type,
    OM_uint32 req_flags,
    OM_uint32 time_req,
    const gss_channel_bindings_t input_chan_bindings,
    const gss_buffer_t input_token,
    gss_OID * actual_mech_type,
    gss_buffer_t output_token,
    OM_uint32 * ret_flags,
    OM_uint32 * time_rec)
{
	OM_uint32 major_status;
	struct _gss_mech_switch *m;
	struct _gss_name *name = (struct _gss_name *) target_name;
	struct _gss_mechanism_name *mn;
	struct _gss_context *ctx = (struct _gss_context *) *context_handle;
	gss_cred_id_t cred_handle;
	int allocated_ctx;
	gss_OID mech_type = input_mech_type;

	*minor_status = 0;

	_gss_buffer_zero(output_token);
	if (actual_mech_type)
		*actual_mech_type = GSS_C_NO_OID;
	if (ret_flags)
		*ret_flags = 0;
	if (time_rec)
		*time_rec = 0;

	/*
	 * If we haven't allocated a context yet, do so now and lookup
	 * the mechanism switch table. If we have one already, make
	 * sure we use the same mechanism switch as before.
	 */
	if (!ctx) {
		if (mech_type == GSS_C_NO_OID) {
			_gss_load_mech();
			if (_gss_mech_oids == GSS_C_NO_OID_SET
			    || _gss_mech_oids->count == 0)
				return (GSS_S_BAD_MECH);
			mech_type = &_gss_mech_oids->elements[0];
		}

		ctx = malloc(sizeof(struct _gss_context));
		if (!ctx) {
			*minor_status = ENOMEM;
			return (GSS_S_FAILURE);
		}
		memset(ctx, 0, sizeof(struct _gss_context));
		m = ctx->gc_mech = _gss_find_mech_switch(mech_type);
		if (!m) {
			free(ctx);
			return (GSS_S_BAD_MECH);
		}
		allocated_ctx = 1;
	} else {
		m = ctx->gc_mech;
		mech_type = &ctx->gc_mech->gm_mech_oid;
		allocated_ctx = 0;
	}

	/*
	 * Find the MN for this mechanism.
	 */
	major_status = _gss_find_mn(minor_status, name, mech_type, &mn);
	if (major_status != GSS_S_COMPLETE) {
		if (allocated_ctx)
			free(ctx);
		return (major_status);
	}

	/*
	 * If we have a cred, find the cred for this mechanism.
	 */
	cred_handle = _gss_mech_cred_find(initiator_cred_handle, mech_type);

	major_status = m->gm_init_sec_context(minor_status,
	    cred_handle,
	    &ctx->gc_ctx,
	    mn->gmn_name,
	    mech_type,
	    req_flags,
	    time_req,
	    input_chan_bindings,
	    input_token,
	    actual_mech_type,
	    output_token,
	    ret_flags,
	    time_rec);

	if (major_status != GSS_S_COMPLETE
	    && major_status != GSS_S_CONTINUE_NEEDED) {
		if (allocated_ctx)
			free(ctx);
		_gss_buffer_zero(output_token);
		_gss_mg_error(m, major_status, *minor_status);
	} else {
		*context_handle = (gss_ctx_id_t) ctx;
	}

	return (major_status);
}
