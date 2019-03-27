/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
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
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */
/* $FreeBSD$ */
/* $Id: gss_pseudo_random.c 20053 2007-01-24 01:31:35Z lha $ */

#include <gssapi/gssapi.h>

#include "mech_switch.h"
#include "context.h"
#include "utils.h"

OM_uint32
gss_pseudo_random(OM_uint32 *minor_status,
		  gss_ctx_id_t context,
		  int prf_key,
		  const gss_buffer_t prf_in,
		  ssize_t desired_output_len,
		  gss_buffer_t prf_out)
{
    struct _gss_context *ctx = (struct _gss_context *) context;
    struct _gss_mech_switch *m;
    OM_uint32 major_status;

    _gss_buffer_zero(prf_out);
    *minor_status = 0;

    if (ctx == NULL) {
	*minor_status = 0;
	return GSS_S_NO_CONTEXT;
    }
    m = ctx->gc_mech;

    if (m->gm_pseudo_random == NULL)
	return GSS_S_UNAVAILABLE;
    
    major_status = (*m->gm_pseudo_random)(minor_status, ctx->gc_ctx,
					  prf_key, prf_in, desired_output_len,
					  prf_out);
    if (major_status != GSS_S_COMPLETE)
	    _gss_mg_error(m, major_status, *minor_status);

    return major_status;
}
