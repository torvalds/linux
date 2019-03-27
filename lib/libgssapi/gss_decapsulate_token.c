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
 * $FreeBSD$
 */

#include <gssapi/gssapi.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

OM_uint32
gss_decapsulate_token(const gss_buffer_t input_token, gss_OID oid,
    gss_buffer_t output_token)
{
	unsigned char *p = input_token->value;
	size_t len = input_token->length;
	size_t a, b;
	gss_OID_desc mech_oid;

	_gss_buffer_zero(output_token);

	/*
	 * Token must start with [APPLICATION 0] SEQUENCE.
	 */
	if (len == 0 || *p != 0x60)
		return (GSS_S_DEFECTIVE_TOKEN);
	p++;
	len--;

	/*
	 * Decode the length and make sure it agrees with the
	 * token length.
	 */
	if (len == 0)
		return (GSS_S_DEFECTIVE_TOKEN);
	if ((*p & 0x80) == 0) {
		a = *p;
		p++;
		len--;
	} else {
		b = *p & 0x7f;
		p++;
		len--;
		if (len < b)
			return (GSS_S_DEFECTIVE_TOKEN);
		a = 0;
		while (b) {
			a = (a << 8) | *p;
			p++;
			len--;
			b--;
		}
	}
	if (a != len)
		return (GSS_S_DEFECTIVE_TOKEN);

	/*
	 * Decode the OID for the mechanism. Simplify life by
	 * assuming that the OID length is less than 128 bytes.
	 */
	if (len < 2 || *p != 0x06)
		return (GSS_S_DEFECTIVE_TOKEN);
	if ((p[1] & 0x80) || p[1] > (len - 2))
		return (GSS_S_DEFECTIVE_TOKEN);
	mech_oid.length = p[1];
	p += 2;
	len -= 2;
	mech_oid.elements = p;

	if (!gss_oid_equal(&mech_oid, oid))
		return (GSS_S_FAILURE);

	p += mech_oid.length;
	len -= mech_oid.length;

	output_token->length = len;
	output_token->value = malloc(len);
	if (!output_token->value)
		return (GSS_S_DEFECTIVE_TOKEN);
	memcpy(output_token->value, p, len);

	return (GSS_S_COMPLETE);
}
