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
#include <asm/scatterlist.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

static inline int
gss_krb5_padding(int blocksize, int length) {
	/* Most of the code is block-size independent but in practice we
	 * use only 8: */
	BUG_ON(blocksize != 8);
	return 8 - (length & 7);
}

u32
krb5_make_token(struct krb5_ctx *ctx, int qop_req,
		   struct xdr_buf *text, struct xdr_netobj *token,
		   int toktype)
{
	s32			checksum_type;
	struct xdr_netobj	md5cksum = {.len = 0, .data = NULL};
	int			blocksize = 0, tmsglen;
	unsigned char		*ptr, *krb5_hdr, *msg_start;
	s32			now;

	dprintk("RPC:     gss_krb5_seal\n");

	now = get_seconds();

	if (qop_req != 0)
		goto out_err;

	switch (ctx->signalg) {
		case SGN_ALG_DES_MAC_MD5:
			checksum_type = CKSUMTYPE_RSA_MD5;
			break;
		default:
			dprintk("RPC:      gss_krb5_seal: ctx->signalg %d not"
				" supported\n", ctx->signalg);
			goto out_err;
	}
	if (ctx->sealalg != SEAL_ALG_NONE && ctx->sealalg != SEAL_ALG_DES) {
		dprintk("RPC:      gss_krb5_seal: ctx->sealalg %d not supported\n",
			ctx->sealalg);
		goto out_err;
	}

	if (toktype == KG_TOK_WRAP_MSG) {
		blocksize = crypto_tfm_alg_blocksize(ctx->enc);
		tmsglen = blocksize + text->len
			+ gss_krb5_padding(blocksize, blocksize + text->len);
	} else {
		tmsglen = 0;
	}

	token->len = g_token_size(&ctx->mech_used, 22 + tmsglen);

	ptr = token->data;
	g_make_token_header(&ctx->mech_used, 22 + tmsglen, &ptr);

	*ptr++ = (unsigned char) ((toktype>>8)&0xff);
	*ptr++ = (unsigned char) (toktype&0xff);

	/* ptr now at byte 2 of header described in rfc 1964, section 1.2.1: */
	krb5_hdr = ptr - 2;
	msg_start = krb5_hdr + 24;

	*(u16 *)(krb5_hdr + 2) = htons(ctx->signalg);
	memset(krb5_hdr + 4, 0xff, 4);
	if (toktype == KG_TOK_WRAP_MSG)
		*(u16 *)(krb5_hdr + 4) = htons(ctx->sealalg);

	if (toktype == KG_TOK_WRAP_MSG) {
		/* XXX removing support for now */
		goto out_err;
	} else { /* Sign only.  */
		if (make_checksum(checksum_type, krb5_hdr, 8, text,
				       &md5cksum))
			goto out_err;
	}

	switch (ctx->signalg) {
	case SGN_ALG_DES_MAC_MD5:
		if (krb5_encrypt(ctx->seq, NULL, md5cksum.data,
				  md5cksum.data, md5cksum.len))
			goto out_err;
		memcpy(krb5_hdr + 16,
		       md5cksum.data + md5cksum.len - KRB5_CKSUM_LENGTH,
		       KRB5_CKSUM_LENGTH);

		dprintk("RPC:      make_seal_token: cksum data: \n");
		print_hexl((u32 *) (krb5_hdr + 16), KRB5_CKSUM_LENGTH, 0);
		break;
	default:
		BUG();
	}

	kfree(md5cksum.data);

	if ((krb5_make_seq_num(ctx->seq, ctx->initiate ? 0 : 0xff,
			       ctx->seq_send, krb5_hdr + 16, krb5_hdr + 8)))
		goto out_err;

	ctx->seq_send++;

	return ((ctx->endtime < now) ? GSS_S_CONTEXT_EXPIRED : GSS_S_COMPLETE);
out_err:
	if (md5cksum.data) kfree(md5cksum.data);
	return GSS_S_FAILURE;
}
