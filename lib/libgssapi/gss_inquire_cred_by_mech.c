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

#include "mech_switch.h"
#include "cred.h"
#include "name.h"

OM_uint32
gss_inquire_cred_by_mech(OM_uint32 *minor_status,
    const gss_cred_id_t cred_handle,
    const gss_OID mech_type,
    gss_name_t *cred_name,
    OM_uint32 *initiator_lifetime,
    OM_uint32 *acceptor_lifetime,
    gss_cred_usage_t *cred_usage)
{
	OM_uint32 major_status;
	struct _gss_mech_switch *m;
	struct _gss_mechanism_cred *mcp;
	gss_cred_id_t mc;
	gss_name_t mn;
	struct _gss_name *name;

	*minor_status = 0;
	if (cred_name)
		*cred_name = GSS_C_NO_NAME;
	if (initiator_lifetime)
		*initiator_lifetime = 0;
	if (acceptor_lifetime)
		*acceptor_lifetime = 0;
	if (cred_usage)
		*cred_usage = 0;

	m = _gss_find_mech_switch(mech_type);
	if (!m)
		return (GSS_S_NO_CRED);

	if (cred_handle != GSS_C_NO_CREDENTIAL) {
		struct _gss_cred *cred = (struct _gss_cred *) cred_handle;
		SLIST_FOREACH(mcp, &cred->gc_mc, gmc_link)
			if (mcp->gmc_mech == m)
				break;
		if (!mcp)
			return (GSS_S_NO_CRED);
		mc = mcp->gmc_cred;
	} else {
		mc = GSS_C_NO_CREDENTIAL;
	}

	major_status = m->gm_inquire_cred_by_mech(minor_status, mc, mech_type,
	    &mn, initiator_lifetime, acceptor_lifetime, cred_usage);
	if (major_status != GSS_S_COMPLETE) {
		_gss_mg_error(m, major_status, *minor_status);
		return (major_status);
	}

	if (cred_name) {
		name = _gss_make_name(m, mn);
		if (!name) {
			m->gm_release_name(minor_status, &mn);
			return (GSS_S_NO_CRED);
		}
		*cred_name = (gss_name_t) name;
	} else {
		m->gm_release_name(minor_status, &mn);
	}

	return (GSS_S_COMPLETE);
}
