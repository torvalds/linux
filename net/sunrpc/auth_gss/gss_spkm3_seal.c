/*
 *  linux/net/sunrpc/gss_spkm3_seal.c
 *
 *  Copyright (c) 2003 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/sunrpc/gss_spkm3.h>
#include <linux/random.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

/*
 * spkm3_make_token()
 *
 * Only SPKM_MIC_TOK with md5 intg-alg is supported
 */

u32
spkm3_make_token(struct spkm3_ctx *ctx,
		   struct xdr_buf * text, struct xdr_netobj * token,
		   int toktype)
{
	s32			checksum_type;
	char			tokhdrbuf[25];
	struct xdr_netobj	md5cksum = {.len = 0, .data = NULL};
	struct xdr_netobj	mic_hdr = {.len = 0, .data = tokhdrbuf};
	int			tokenlen = 0;
	unsigned char		*ptr;
	s32			now;
	int			ctxelen = 0, ctxzbit = 0;
	int			md5elen = 0, md5zbit = 0;

	dprintk("RPC: spkm3_make_token\n");

	now = jiffies;

	if (ctx->ctx_id.len != 16) {
		dprintk("RPC: spkm3_make_token BAD ctx_id.len %d\n",
			ctx->ctx_id.len);
		goto out_err;
	}
		
	switch (ctx->intg_alg) {
		case NID_md5:
			checksum_type = CKSUMTYPE_RSA_MD5;
			break;
		default:
			dprintk("RPC: gss_spkm3_seal: ctx->signalg %d not"
				" supported\n", ctx->intg_alg);
			goto out_err;
	}
	/* XXX since we don't support WRAP, perhaps we don't care... */
	if (ctx->conf_alg != NID_cast5_cbc) {
		dprintk("RPC: gss_spkm3_seal: ctx->sealalg %d not supported\n",
			ctx->conf_alg);
		goto out_err;
	}

	if (toktype == SPKM_MIC_TOK) {
		/* Calculate checksum over the mic-header */
		asn1_bitstring_len(&ctx->ctx_id, &ctxelen, &ctxzbit);
		spkm3_mic_header(&mic_hdr.data, &mic_hdr.len, ctx->ctx_id.data,
		                         ctxelen, ctxzbit);

		if (make_checksum(checksum_type, mic_hdr.data, mic_hdr.len, 
		                             text, 0, &md5cksum))
			goto out_err;

		asn1_bitstring_len(&md5cksum, &md5elen, &md5zbit);
		tokenlen = 10 + ctxelen + 1 + md5elen + 1;

		/* Create token header using generic routines */
		token->len = g_token_size(&ctx->mech_used, tokenlen);

		ptr = token->data;
		g_make_token_header(&ctx->mech_used, tokenlen, &ptr);

		spkm3_make_mic_token(&ptr, tokenlen, &mic_hdr, &md5cksum, md5elen, md5zbit);
	} else if (toktype == SPKM_WRAP_TOK) { /* Not Supported */
		dprintk("RPC: gss_spkm3_seal: SPKM_WRAP_TOK not supported\n");
		goto out_err;
	}
	kfree(md5cksum.data);

	/* XXX need to implement sequence numbers, and ctx->expired */

	return  GSS_S_COMPLETE;
out_err:
	kfree(md5cksum.data);
	token->data = NULL;
	token->len = 0;
	return GSS_S_FAILURE;
}
