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
#include <mech_switch.h>

gss_OID_desc __gss_c_nt_hostbased_service_oid_desc =
    {10, (void *)("\x2a\x86\x48\x86\xf7\x12" "\x01\x02\x01\x04")};

const char *
_gss_name_prefix(void)
{
	return "_gss_spnego";
}

void
gss_mg_collect_error(gss_OID mech, OM_uint32 maj, OM_uint32 min)
{
	_gss_mg_collect_error(mech, maj, min);
}

OM_uint32 _gss_spnego_display_status
           (OM_uint32 * minor_status,
            OM_uint32 status_value,
            int status_type,
            const gss_OID mech_type,
            OM_uint32 * message_context,
            gss_buffer_t status_string
           )
{
    return GSS_S_FAILURE;
}

OM_uint32 _gss_spnego_add_cred (
            OM_uint32 * minor_status,
            const gss_cred_id_t input_cred_handle,
            const gss_name_t desired_name,
            const gss_OID desired_mech,
            gss_cred_usage_t cred_usage,
            OM_uint32 initiator_time_req,
            OM_uint32 acceptor_time_req,
            gss_cred_id_t * output_cred_handle,
            gss_OID_set * actual_mechs,
            OM_uint32 * initiator_time_rec,
            OM_uint32 * acceptor_time_rec
           )
{
	return gss_add_cred(minor_status,
                       input_cred_handle,
                       desired_name,
                       desired_mech,
                       cred_usage,
                       initiator_time_req,
                       acceptor_time_req,
                       output_cred_handle,
                       actual_mechs,
                       initiator_time_rec,
                       acceptor_time_rec);
}
