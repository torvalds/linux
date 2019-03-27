/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Doug Rabson
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

OM_uint32 gss_duplicate_oid(OM_uint32 *minor_status,
    const gss_OID src_oid,
    gss_OID *dest_oid_p)
{
	gss_OID dest_oid;

	*minor_status = 0;
	*dest_oid_p = GSS_C_NO_OID;

	if (src_oid == GSS_C_NO_OID)
		return (GSS_S_COMPLETE);

	dest_oid = malloc(sizeof(gss_OID_desc));
	if (!dest_oid) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}

	dest_oid->elements = malloc(src_oid->length);
	if (!dest_oid->elements) {
		free(dest_oid);
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}

	memcpy(dest_oid->elements, src_oid->elements, src_oid->length);
	dest_oid->length = src_oid->length;

	*dest_oid_p = dest_oid;

	return (GSS_S_COMPLETE);
}
