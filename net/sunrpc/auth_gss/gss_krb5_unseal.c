/*
 *  linux/net/sunrpc/gss_krb5_unseal.c
 *
 *  Adapted from MIT Kerberos 5-1.2.1 lib/gssapi/krb5/k5unseal.c
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
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
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif


/* read_token is a mic token, and message_buffer is the data that the mic was
 * supposedly taken over. */

u32
gss_verify_mic_kerberos(struct gss_ctx *gss_ctx,
		struct xdr_buf *message_buffer, struct xdr_netobj *read_token)
{
	struct krb5_ctx		*ctx = gss_ctx->internal_ctx_id;
	int			signalg;
	int			sealalg;
	char			cksumdata[16];
	struct xdr_netobj	md5cksum = {.len = 0, .data = cksumdata};
	s32			now;
	int			direction;
	s32			seqnum;
	unsigned char		*ptr = (unsigned char *)read_token->data;
	int			bodysize;

	dprintk("RPC:      krb5_read_token\n");

	if (g_verify_token_header(&ctx->mech_used, &bodysize, &ptr,
					read_token->len))
		return GSS_S_DEFECTIVE_TOKEN;

	if ((*ptr++ != ((KG_TOK_MIC_MSG>>8)&0xff)) ||
	    (*ptr++ != ( KG_TOK_MIC_MSG    &0xff))   )
		return GSS_S_DEFECTIVE_TOKEN;

	/* XXX sanity-check bodysize?? */

	signalg = ptr[0] + (ptr[1] << 8);
	if (signalg != SGN_ALG_DES_MAC_MD5)
		return GSS_S_DEFECTIVE_TOKEN;

	sealalg = ptr[2] + (ptr[3] << 8);
	if (sealalg != SEAL_ALG_NONE)
		return GSS_S_DEFECTIVE_TOKEN;

	if ((ptr[4] != 0xff) || (ptr[5] != 0xff))
		return GSS_S_DEFECTIVE_TOKEN;

	if (make_checksum("md5", ptr - 2, 8, message_buffer, 0, &md5cksum))
		return GSS_S_FAILURE;

	if (krb5_encrypt(ctx->seq, NULL, md5cksum.data, md5cksum.data, 16))
		return GSS_S_FAILURE;

	if (memcmp(md5cksum.data + 8, ptr + 14, 8))
		return GSS_S_BAD_SIG;

	/* it got through unscathed.  Make sure the context is unexpired */

	now = get_seconds();

	if (now > ctx->endtime)
		return GSS_S_CONTEXT_EXPIRED;

	/* do sequencing checks */

	if (krb5_get_seq_num(ctx->seq, ptr + 14, ptr + 6, &direction, &seqnum))
		return GSS_S_FAILURE;

	if ((ctx->initiate && direction != 0xff) ||
	    (!ctx->initiate && direction != 0))
		return GSS_S_BAD_SIG;

	return GSS_S_COMPLETE;
}
