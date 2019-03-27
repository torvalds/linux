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
#include "utils.h"

OM_uint32
gss_display_name(OM_uint32 *minor_status,
    const gss_name_t input_name,
    gss_buffer_t output_name_buffer,
    gss_OID *output_name_type)
{
	OM_uint32 major_status;
	struct _gss_name *name = (struct _gss_name *) input_name;
	struct _gss_mechanism_name *mn;

	_gss_buffer_zero(output_name_buffer);
	if (output_name_type)
		*output_name_type = GSS_C_NO_OID;

	if (name == NULL) {
		*minor_status = 0;
		return (GSS_S_BAD_NAME);
	}

	/*
	 * If we know it, copy the buffer used to import the name in
	 * the first place. Otherwise, ask all the MNs in turn if
	 * they can display the thing.
	 */
	if (name->gn_value.value) {
		output_name_buffer->value = malloc(name->gn_value.length);
		if (!output_name_buffer->value) {
			*minor_status = ENOMEM;
			return (GSS_S_FAILURE);
		}
		output_name_buffer->length = name->gn_value.length;
		memcpy(output_name_buffer->value, name->gn_value.value,
		    output_name_buffer->length);
		if (output_name_type)
			*output_name_type = &name->gn_type;

		*minor_status = 0;
		return (GSS_S_COMPLETE);
	} else {
		SLIST_FOREACH(mn, &name->gn_mn, gmn_link) {
			major_status = mn->gmn_mech->gm_display_name(
				minor_status, mn->gmn_name,
				output_name_buffer,
				output_name_type);
			if (major_status == GSS_S_COMPLETE)
				return (GSS_S_COMPLETE);
		}
	}

	*minor_status = 0;
	return (GSS_S_FAILURE);
}
