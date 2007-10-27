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
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/sunrpc/xdr.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

const struct xdr_netobj hmac_md5_oid = { 8, "\x2B\x06\x01\x05\x05\x08\x01\x01"};
const struct xdr_netobj cast5_cbc_oid = {9, "\x2A\x86\x48\x86\xF6\x7D\x07\x42\x0A"};

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
	char			cksumdata[16];
	struct xdr_netobj	md5cksum = {.len = 0, .data = cksumdata};
	struct xdr_netobj	mic_hdr = {.len = 0, .data = tokhdrbuf};
	int			tokenlen = 0;
	unsigned char		*ptr;
	s32			now;
	int			ctxelen = 0, ctxzbit = 0;
	int			md5elen = 0, md5zbit = 0;

	now = jiffies;

	if (ctx->ctx_id.len != 16) {
		dprintk("RPC:       spkm3_make_token BAD ctx_id.len %d\n",
				ctx->ctx_id.len);
		goto out_err;
	}

	if (!g_OID_equal(&ctx->intg_alg, &hmac_md5_oid)) {
		dprintk("RPC:       gss_spkm3_seal: unsupported I-ALG "
				"algorithm.  only support hmac-md5 I-ALG.\n");
		goto out_err;
	} else
		checksum_type = CKSUMTYPE_HMAC_MD5;

	if (!g_OID_equal(&ctx->conf_alg, &cast5_cbc_oid)) {
		dprintk("RPC:       gss_spkm3_seal: unsupported C-ALG "
				"algorithm\n");
		goto out_err;
	}

	if (toktype == SPKM_MIC_TOK) {
		/* Calculate checksum over the mic-header */
		asn1_bitstring_len(&ctx->ctx_id, &ctxelen, &ctxzbit);
		spkm3_mic_header(&mic_hdr.data, &mic_hdr.len, ctx->ctx_id.data,
				ctxelen, ctxzbit);
		if (make_spkm3_checksum(checksum_type, &ctx->derived_integ_key,
					(char *)mic_hdr.data, mic_hdr.len,
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
		dprintk("RPC:       gss_spkm3_seal: SPKM_WRAP_TOK "
				"not supported\n");
		goto out_err;
	}

	/* XXX need to implement sequence numbers, and ctx->expired */

	return  GSS_S_COMPLETE;
out_err:
	token->data = NULL;
	token->len = 0;
	return GSS_S_FAILURE;
}

static int
spkm3_checksummer(struct scatterlist *sg, void *data)
{
	struct hash_desc *desc = data;

	return crypto_hash_update(desc, sg, sg->length);
}

/* checksum the plaintext data and hdrlen bytes of the token header */
s32
make_spkm3_checksum(s32 cksumtype, struct xdr_netobj *key, char *header,
		    unsigned int hdrlen, struct xdr_buf *body,
		    unsigned int body_offset, struct xdr_netobj *cksum)
{
	char				*cksumname;
	struct hash_desc		desc; /* XXX add to ctx? */
	struct scatterlist		sg[1];
	int err;

	switch (cksumtype) {
		case CKSUMTYPE_HMAC_MD5:
			cksumname = "hmac(md5)";
			break;
		default:
			dprintk("RPC:       spkm3_make_checksum:"
					" unsupported checksum %d", cksumtype);
			return GSS_S_FAILURE;
	}

	if (key->data == NULL || key->len <= 0) return GSS_S_FAILURE;

	desc.tfm = crypto_alloc_hash(cksumname, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc.tfm))
		return GSS_S_FAILURE;
	cksum->len = crypto_hash_digestsize(desc.tfm);
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_hash_setkey(desc.tfm, key->data, key->len);
	if (err)
		goto out;

	err = crypto_hash_init(&desc);
	if (err)
		goto out;

	sg_init_one(sg, header, hdrlen);
	crypto_hash_update(&desc, sg, sg->length);

	xdr_process_buf(body, body_offset, body->len - body_offset,
			spkm3_checksummer, &desc);
	crypto_hash_final(&desc, cksum->data);

out:
	crypto_free_hash(desc.tfm);

	return err ? GSS_S_FAILURE : 0;
}
