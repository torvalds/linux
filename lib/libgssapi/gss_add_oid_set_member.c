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

OM_uint32
gss_add_oid_set_member(OM_uint32 *minor_status,
    const gss_OID member_oid,
    gss_OID_set *oid_set)
{
	OM_uint32 major_status;
	gss_OID_set set = *oid_set;
	gss_OID new_elements;
	gss_OID new_oid;
	int t;

	*minor_status = 0;

	major_status = gss_test_oid_set_member(minor_status,
	    member_oid, *oid_set, &t);
	if (major_status)
		return (major_status);
	if (t)
		return (GSS_S_COMPLETE);

	new_elements = malloc((set->count + 1) * sizeof(gss_OID_desc));
	if (!new_elements) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}

	new_oid = &new_elements[set->count];
	new_oid->elements = malloc(member_oid->length);
	if (!new_oid->elements) {
		free(new_elements);
		return (GSS_S_FAILURE);
	}
	new_oid->length = member_oid->length;
	memcpy(new_oid->elements, member_oid->elements, member_oid->length);

	if (set->elements) {
		memcpy(new_elements, set->elements,
		    set->count * sizeof(gss_OID_desc));
		free(set->elements);
	}
	set->elements = new_elements;
	set->count++;

	return (GSS_S_COMPLETE);
}
