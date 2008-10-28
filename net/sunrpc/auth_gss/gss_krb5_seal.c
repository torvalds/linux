/*
 *  linux/net/sunrpc/gss_krb5_seal.c
 *
 *  Adapted from MIT Kerberos 5-1.2.1 lib/gssapi/krb5/k5seal.c
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson	<andros@umich.edu>
 *  J. Bruce Fields	<bfields@umich.edu>
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
#include <linux/random.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

DEFINE_SPINLOCK(krb5_seq_lock);

u32
gss_get_mic_kerberos(struct gss_ctx *gss_ctx, struct xdr_buf *text,
		struct xdr_netobj *token)
{
	struct krb5_ctx		*ctx = gss_ctx->internal_ctx_id;
	char			cksumdata[16];
	struct xdr_netobj	md5cksum = {.len = 0, .data = cksumdata};
	unsigned char		*ptr, *msg_start;
	s32			now;
	u32			seq_send;

	dprintk("RPC:       gss_krb5_seal\n");
	BUG_ON(ctx == NULL);

	now = get_seconds();

	token->len = g_token_size(&ctx->mech_used, GSS_KRB5_TOK_HDR_LEN + 8);

	ptr = token->data;
	g_make_token_header(&ctx->mech_used, GSS_KRB5_TOK_HDR_LEN + 8, &ptr);

	/* ptr now at header described in rfc 1964, section 1.2.1: */
	ptr[0] = (unsigned char) ((KG_TOK_MIC_MSG >> 8) & 0xff);
	ptr[1] = (unsigned char) (KG_TOK_MIC_MSG & 0xff);

	msg_start = ptr + GSS_KRB5_TOK_HDR_LEN + 8;

	*(__be16 *)(ptr + 2) = htons(SGN_ALG_DES_MAC_MD5);
	memset(ptr + 4, 0xff, 4);

	if (make_checksum("md5", ptr, 8, text, 0, &md5cksum))
		return GSS_S_FAILURE;

	if (krb5_encrypt(ctx->seq, NULL, md5cksum.data,
			  md5cksum.data, md5cksum.len))
		return GSS_S_FAILURE;

	memcpy(ptr + GSS_KRB5_TOK_HDR_LEN, md5cksum.data + md5cksum.len - 8, 8);

	spin_lock(&krb5_seq_lock);
	seq_send = ctx->seq_send++;
	spin_unlock(&krb5_seq_lock);

	if (krb5_make_seq_num(ctx->seq, ctx->initiate ? 0 : 0xff,
			      seq_send, ptr + GSS_KRB5_TOK_HDR_LEN,
			      ptr + 8))
		return GSS_S_FAILURE;

	return (ctx->endtime < now) ? GSS_S_CONTEXT_EXPIRED : GSS_S_COMPLETE;
}
