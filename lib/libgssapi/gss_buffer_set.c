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

#include <gssapi/gssapi.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* RCSID("$Id: gss_buffer_set.c 18885 2006-10-24 21:53:02Z lha $"); */

OM_uint32 
gss_create_empty_buffer_set(OM_uint32 * minor_status,
    gss_buffer_set_t *buffer_set)
{
	gss_buffer_set_t set;

	set = (gss_buffer_set_desc *) malloc(sizeof(*set));
	if (set == GSS_C_NO_BUFFER_SET) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}

	set->count = 0;
	set->elements = NULL;

	*buffer_set = set;

	*minor_status = 0;
	return (GSS_S_COMPLETE);
}

OM_uint32
gss_add_buffer_set_member(OM_uint32 * minor_status,
    const gss_buffer_t member_buffer, gss_buffer_set_t *buffer_set)
{
	gss_buffer_set_t set;
	gss_buffer_t p;
	OM_uint32 ret;

	if (*buffer_set == GSS_C_NO_BUFFER_SET) {
		ret = gss_create_empty_buffer_set(minor_status,
		    buffer_set);
		if (ret) {
			return (ret);
		}
	}

	set = *buffer_set;
	set->elements = reallocarray(set->elements, set->count + 1,
	    sizeof(set->elements[0]));
	if (set->elements == NULL) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}

	p = &set->elements[set->count];

	p->value = malloc(member_buffer->length);
	if (p->value == NULL) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}
	memcpy(p->value, member_buffer->value, member_buffer->length);
	p->length = member_buffer->length;

	set->count++;

	*minor_status = 0;
	return (GSS_S_COMPLETE);
}

OM_uint32
gss_release_buffer_set(OM_uint32 * minor_status, gss_buffer_set_t *buffer_set)
{
	size_t i;
	OM_uint32 minor;

	*minor_status = 0;

	if (*buffer_set == GSS_C_NO_BUFFER_SET)
		return (GSS_S_COMPLETE);

	for (i = 0; i < (*buffer_set)->count; i++)
		gss_release_buffer(&minor, &((*buffer_set)->elements[i]));

	free((*buffer_set)->elements);

	(*buffer_set)->elements = NULL;
	(*buffer_set)->count = 0;

	free(*buffer_set);
	*buffer_set = GSS_C_NO_BUFFER_SET;

	return (GSS_S_COMPLETE);
}

