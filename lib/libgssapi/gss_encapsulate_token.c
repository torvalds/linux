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
gss_encapsulate_token(const gss_buffer_t input_token, gss_OID oid,
    gss_buffer_t output_token)
{
	unsigned char *p;
	size_t len, inside_len;
	size_t a, b;
	int i;

	_gss_buffer_zero(output_token);

	/*
	 * First time around, we calculate the size, second time, we
	 * encode the token.
	 */
	p = NULL;
	for (i = 0; i < 2; i++) {
		len = 0;

		/*
		 * Token starts with [APPLICATION 0] SEQUENCE.
		 */
		if (p)
			*p++ = 0x60;
		len++;

		/*
		 * The length embedded in the token is the space
		 * needed for the encapsulated oid plus the length of
		 * the inner token.
		 */
		if (oid->length > 127)
			return (GSS_S_DEFECTIVE_TOKEN);

		inside_len = 2 + oid->length + input_token->length;

		/*
		 * Figure out how to encode the length
		 */
		if (inside_len < 128) {
			if (p)
				*p++ = inside_len;
			len++;
		} else {
			b = 1;
			if (inside_len >= 0x100)
				b++;
			if (inside_len >= 0x10000)
				b++;
			if (inside_len >= 0x1000000)
				b++;
			if (p)
				*p++ = b | 0x80;
			len++;
			a = inside_len << 8*(4 - b);
			while (b) {
				if (p)
					*p++ = (a >> 24);
				a <<= 8;
				len++;
				b--;
			}
		}

		/*
		 * Encode the OID for the mechanism. Simplify life by
		 * assuming that the OID length is less than 128 bytes.
		 */
		if (p)
			*p++ = 0x06;
		len++;
		if (p)
			*p++ = oid->length;
		len++;
		if (p) {
			memcpy(p, oid->elements, oid->length);
			p += oid->length;
		}
		len += oid->length;

		if (p) {
			memcpy(p, input_token->value, input_token->length);
			p += input_token->length;
		}
		len += input_token->length;

		if (i == 0) {
			output_token->length = len;
			output_token->value = malloc(len);
			if (!output_token->value)
				return (GSS_S_DEFECTIVE_TOKEN);
			p = output_token->value;
		}
	}

	return (GSS_S_COMPLETE);
}
