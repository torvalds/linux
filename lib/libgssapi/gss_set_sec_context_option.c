/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2004, PADL Software Pty Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* $FreeBSD$ */
/* RCSID("$Id: gss_set_sec_context_option.c 19928 2007-01-16 10:37:54Z lha $"); */

#include <gssapi/gssapi.h>

#include "mech_switch.h"
#include "context.h"

OM_uint32
gss_set_sec_context_option (OM_uint32 *minor_status,
			    gss_ctx_id_t *context_handle,
			    const gss_OID object,
			    const gss_buffer_t value)
{
	struct _gss_context	*ctx;
	OM_uint32		major_status;
	struct _gss_mech_switch	*m;
	int			one_ok = 0;

	*minor_status = 0;

	if (context_handle == NULL) {
		_gss_load_mech();
		major_status = GSS_S_BAD_MECH;
		SLIST_FOREACH(m, &_gss_mechs, gm_link) {
			if (!m->gm_set_sec_context_option)
				continue;
			major_status = m->gm_set_sec_context_option(
				minor_status,
				NULL, object, value);
			if (major_status == GSS_S_COMPLETE)
				one_ok = 1;
		}
		if (one_ok) {
			*minor_status = 0;
			return (GSS_S_COMPLETE);
		}
		return (major_status);
	}

	ctx = (struct _gss_context *) *context_handle;

	if (ctx == NULL)
		return (GSS_S_NO_CONTEXT);

	m = ctx->gc_mech;

	if (m == NULL)
		return (GSS_S_BAD_MECH);

	if (m->gm_set_sec_context_option != NULL) {
		major_status = m->gm_set_sec_context_option(minor_status,
		    &ctx->gc_ctx, object, value);
		if (major_status != GSS_S_COMPLETE)
			_gss_mg_error(m, major_status, *minor_status);
	} else
		major_status = (GSS_S_BAD_MECH);

	return (major_status);
}

