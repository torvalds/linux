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
#include "utils.h"
#include "name.h"

static OM_uint32
_gss_import_export_name(OM_uint32 *minor_status,
    const gss_buffer_t input_name_buffer,
    gss_name_t *output_name)
{
	OM_uint32 major_status;
	unsigned char *p = input_name_buffer->value;
	size_t len = input_name_buffer->length;
	size_t t;
	gss_OID_desc mech_oid;
	struct _gss_mech_switch *m;
	struct _gss_name *name;
	gss_name_t new_canonical_name;

	*minor_status = 0;
	*output_name = 0;

	/*
	 * Make sure that TOK_ID is {4, 1}.
	 */
	if (len < 2)
		return (GSS_S_BAD_NAME);
	if (p[0] != 4 || p[1] != 1)
		return (GSS_S_BAD_NAME);
	p += 2;
	len -= 2;

	/*
	 * Get the mech length and the name length and sanity
	 * check the size of of the buffer.
	 */
	if (len < 2)
		return (GSS_S_BAD_NAME);
	t = (p[0] << 8) + p[1];
	p += 2;
	len -= 2;

	/*
	 * Check the DER encoded OID to make sure it agrees with the
	 * length we just decoded.
	 */
	if (p[0] != 6)		/* 6=OID */
		return (GSS_S_BAD_NAME);
	p++;
	len--;
	t--;
	if (p[0] & 0x80) {
		int digits = p[0];
		p++;
		len--;
		t--;
		mech_oid.length = 0;
		while (digits--) {
			mech_oid.length = (mech_oid.length << 8) | p[0];
			p++;
			len--;
			t--;
		}
	} else {
		mech_oid.length = p[0];
		p++;
		len--;
		t--;
	}
	if (mech_oid.length != t)
		return (GSS_S_BAD_NAME);

	mech_oid.elements = p;

	if (len < t + 4)
		return (GSS_S_BAD_NAME);
	p += t;
	len -= t;

	t = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	p += 4;
	len -= 4;

	if (len != t)
		return (GSS_S_BAD_NAME);

	m = _gss_find_mech_switch(&mech_oid);
	if (!m)
		return (GSS_S_BAD_MECH);

	/*
	 * Ask the mechanism to import the name.
	 */
	major_status = m->gm_import_name(minor_status,
	    input_name_buffer, GSS_C_NT_EXPORT_NAME, &new_canonical_name);
	if (major_status != GSS_S_COMPLETE) {
		_gss_mg_error(m, major_status, *minor_status);
		return (major_status);
	}

	/*
	 * Now we make a new name and mark it as an MN.
	 */
	name = _gss_make_name(m, new_canonical_name);
	if (!name) {
		m->gm_release_name(minor_status, &new_canonical_name);
		return (GSS_S_FAILURE);
	}

	*output_name = (gss_name_t) name;

	*minor_status = 0;
	return (GSS_S_COMPLETE);
}

OM_uint32
gss_import_name(OM_uint32 *minor_status,
    const gss_buffer_t input_name_buffer,
    const gss_OID input_name_type,
    gss_name_t *output_name)
{
	gss_OID			name_type = input_name_type;
	OM_uint32		major_status;
	struct _gss_name	*name;

	*output_name = GSS_C_NO_NAME;

	if (input_name_buffer->length == 0) {
		*minor_status = 0;
		return (GSS_S_BAD_NAME);
	}

	/*
	 * Use GSS_NT_USER_NAME as default name type.
	 */
	if (name_type == GSS_C_NO_OID)
		name_type = GSS_C_NT_USER_NAME;

	/*
	 * If this is an exported name, we need to parse it to find
	 * the mechanism and then import it as an MN. See RFC 2743
	 * section 3.2 for a description of the format.
	 */
	if (gss_oid_equal(name_type, GSS_C_NT_EXPORT_NAME)) {
		return _gss_import_export_name(minor_status,
		    input_name_buffer, output_name);
	}

	/*
	 * Only allow certain name types. This is pretty bogus - we
	 * should figure out the list of supported name types using
	 * gss_inquire_names_for_mech.
	 */
	if (!gss_oid_equal(name_type, GSS_C_NT_USER_NAME)
	    && !gss_oid_equal(name_type, GSS_C_NT_MACHINE_UID_NAME)
	    && !gss_oid_equal(name_type, GSS_C_NT_STRING_UID_NAME)
	    && !gss_oid_equal(name_type, GSS_C_NT_HOSTBASED_SERVICE_X)
	    && !gss_oid_equal(name_type, GSS_C_NT_HOSTBASED_SERVICE)
	    && !gss_oid_equal(name_type, GSS_C_NT_ANONYMOUS)
	    && !gss_oid_equal(name_type, GSS_KRB5_NT_PRINCIPAL_NAME)) {
		*minor_status = 0;
		return (GSS_S_BAD_NAMETYPE);
	}

	*minor_status = 0;
	name = malloc(sizeof(struct _gss_name));
	if (!name) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}
	memset(name, 0, sizeof(struct _gss_name));

	major_status = _gss_copy_oid(minor_status,
	    name_type, &name->gn_type);
	if (major_status) {
		free(name);
		return (GSS_S_FAILURE);
	}

	major_status = _gss_copy_buffer(minor_status,
	    input_name_buffer, &name->gn_value);
	if (major_status) {
		gss_name_t rname = (gss_name_t)name;
		gss_release_name(minor_status, &rname);
		return (GSS_S_FAILURE);
	}

	SLIST_INIT(&name->gn_mn);

	*output_name = (gss_name_t) name;
	return (GSS_S_COMPLETE);
}
