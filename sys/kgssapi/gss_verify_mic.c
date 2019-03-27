/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <kgssapi/gssapi.h>
#include <kgssapi/gssapi_impl.h>

#include "kgss_if.h"

OM_uint32
gss_verify_mic(OM_uint32 *minor_status,
    const gss_ctx_id_t ctx,
    const gss_buffer_t message_buffer,
    const gss_buffer_t token_buffer,
    gss_qop_t *qop_state)
{
	OM_uint32 maj_stat;
	struct mbuf *m, *mic;

	if (!ctx) {
		*minor_status = 0;
		return (GSS_S_NO_CONTEXT);
	}

	MGET(m, M_WAITOK, MT_DATA);
	if (message_buffer->length > MLEN)
		MCLGET(m, M_WAITOK);
	m_append(m, message_buffer->length, message_buffer->value);

	MGET(mic, M_WAITOK, MT_DATA);
	if (token_buffer->length > MLEN)
		MCLGET(mic, M_WAITOK);
	m_append(mic, token_buffer->length, token_buffer->value);

	maj_stat = KGSS_VERIFY_MIC(ctx, minor_status, m, mic, qop_state);

	m_freem(m);
	m_freem(mic);

	return (maj_stat);
}

OM_uint32
gss_verify_mic_mbuf(OM_uint32 *minor_status, const gss_ctx_id_t ctx,
    struct mbuf *m, struct mbuf *mic, gss_qop_t *qop_state)
{

	if (!ctx) {
		*minor_status = 0;
		return (GSS_S_NO_CONTEXT);
	}

	return (KGSS_VERIFY_MIC(ctx, minor_status, m, mic, qop_state));
}

