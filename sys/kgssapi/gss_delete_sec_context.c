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
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <kgssapi/gssapi.h>
#include <kgssapi/gssapi_impl.h>

#include "gssd.h"

OM_uint32
gss_delete_sec_context(OM_uint32 *minor_status, gss_ctx_id_t *context_handle,
	gss_buffer_t output_token)
{
	struct delete_sec_context_res res;
	struct delete_sec_context_args args;
	enum clnt_stat stat;
	gss_ctx_id_t ctx;
	CLIENT *cl;

	*minor_status = 0;

	if (!kgss_gssd_handle)
		return (GSS_S_FAILURE);

	if (*context_handle) {
		ctx = *context_handle;

		/*
		 * If we are past the context establishment phase, let
		 * the in-kernel code do the delete, otherwise
		 * userland needs to deal with it.
		 */
		if (ctx->handle) {
			args.ctx = ctx->handle;
			cl = kgss_gssd_client();
			if (cl == NULL)
				return (GSS_S_FAILURE);
	
			bzero(&res, sizeof(res));
			stat = gssd_delete_sec_context_1(&args, &res, cl);
			CLNT_RELEASE(cl);
			if (stat != RPC_SUCCESS) {
				*minor_status = stat;
				return (GSS_S_FAILURE);
			}

			if (output_token)
				kgss_copy_buffer(&res.output_token,
				    output_token);
			xdr_free((xdrproc_t) xdr_delete_sec_context_res, &res);

			kgss_delete_context(ctx, NULL);
		} else {
			kgss_delete_context(ctx, output_token);
		}
		*context_handle = NULL;
	} else {
		if (output_token) {
			output_token->length = 0;
			output_token->value = NULL;
		}
	}

	return (GSS_S_COMPLETE);
}
