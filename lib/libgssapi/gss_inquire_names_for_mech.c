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

OM_uint32
gss_inquire_names_for_mech(OM_uint32 *minor_status,
    const gss_OID mechanism,
    gss_OID_set *name_types)
{
	OM_uint32 major_status;
	struct _gss_mech_switch *m = _gss_find_mech_switch(mechanism);

	*minor_status = 0;
	*name_types = GSS_C_NO_OID_SET;
	if (!m)
		return (GSS_S_BAD_MECH);

	/*
	 * If the implementation can do it, ask it for a list of
	 * names, otherwise fake it.
	 */
	if (m->gm_inquire_names_for_mech) {
		return (m->gm_inquire_names_for_mech(minor_status,
			    mechanism, name_types));
	} else {
		major_status = gss_create_empty_oid_set(minor_status,
		    name_types);
		if (major_status)
			return (major_status);
		major_status = gss_add_oid_set_member(minor_status,
		    GSS_C_NT_HOSTBASED_SERVICE, name_types);
		if (major_status) {
			OM_uint32 junk;
			gss_release_oid_set(&junk, name_types);
			return (major_status);
		}
		major_status = gss_add_oid_set_member(minor_status,
		    GSS_C_NT_USER_NAME, name_types);
		if (major_status) {
			OM_uint32 junk;
			gss_release_oid_set(&junk, name_types);
			return (major_status);
		}
	}

	return (GSS_S_COMPLETE);
}
