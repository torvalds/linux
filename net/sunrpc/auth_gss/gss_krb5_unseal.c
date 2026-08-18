/*
 *  linux/net/sunrpc/gss_krb5_unseal.c
 *
 *  Adapted from MIT Kerberos 5-1.2.1 lib/gssapi/krb5/k5unseal.c
 *
 *  Copyright (c) 2000-2008 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 */

/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "gss_krb5_internal.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

u32
gss_krb5_verify_mic_v2(struct krb5_ctx *ctx, struct xdr_buf *message_buffer,
		       struct xdr_netobj *read_token)
{
	const struct krb5_enctype *krb5 = ctx->krb5e;
	struct crypto_shash *shash = ctx->initiate ?
		ctx->acceptor_sign_shash : ctx->initiator_sign_shash;
	unsigned int cksum_len = krb5->cksum_len;
	struct scatterlist sg_head[XDR_BUF_TO_SG_NENTS];
	struct scatterlist *sg_overflow;
	size_t mic_offset, mic_len;
	u8 *ptr = read_token->data;
	__be16 be16_ptr;
	time64_t now;
	u8 flags;
	int nsg, i;
	int ret;

	dprintk("RPC:       %s\n", __func__);

	memcpy(&be16_ptr, (char *) ptr, 2);
	if (be16_to_cpu(be16_ptr) != KG2_TOK_MIC)
		return GSS_S_DEFECTIVE_TOKEN;

	flags = ptr[2];
	if ((!ctx->initiate && (flags & KG2_TOKEN_FLAG_SENTBYACCEPTOR)) ||
	    (ctx->initiate && !(flags & KG2_TOKEN_FLAG_SENTBYACCEPTOR)))
		return GSS_S_BAD_SIG;

	if (flags & KG2_TOKEN_FLAG_SEALED) {
		dprintk("%s: token has unexpected sealed flag\n", __func__);
		return GSS_S_FAILURE;
	}

	for (i = 3; i < 8; i++)
		if (ptr[i] != 0xff)
			return GSS_S_DEFECTIVE_TOKEN;

	nsg = gss_krb5_mic_build_sg(message_buffer,
				    ptr + GSS_KRB5_TOK_HDR_LEN,
				    cksum_len, ptr,
				    sg_head, &sg_overflow);
	if (nsg < 0)
		return GSS_S_FAILURE;

	mic_offset = 0;
	mic_len = cksum_len + message_buffer->len + GSS_KRB5_TOK_HDR_LEN;
	ret = crypto_krb5_verify_mic(krb5, shash, NULL, sg_head, nsg,
				     &mic_offset, &mic_len);
	kfree(sg_overflow);
	if (ret)
		return gss_krb5_errno_to_status(ret);

	/* it got through unscathed.  Make sure the context is unexpired */
	now = ktime_get_real_seconds();
	if (now > ctx->endtime)
		return GSS_S_CONTEXT_EXPIRED;

	/*
	 * NOTE: the sequence number at ptr + 8 is skipped, rpcsec_gss
	 * doesn't want it checked; see page 6 of rfc 2203.
	 */

	return GSS_S_COMPLETE;
}
