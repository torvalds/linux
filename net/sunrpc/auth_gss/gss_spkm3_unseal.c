/*
 *  linux/net/sunrpc/gss_spkm3_unseal.c
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
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

/*
 * spkm3_read_token()
 * 
 * only SPKM_MIC_TOK with md5 intg-alg is supported
 */
u32
spkm3_read_token(struct spkm3_ctx *ctx,
		struct xdr_netobj *read_token,    /* checksum */
		struct xdr_buf *message_buffer, /* signbuf */
		int toktype)
{
	s32			code;
	struct xdr_netobj	wire_cksum = {.len =0, .data = NULL};
	struct xdr_netobj	md5cksum = {.len = 0, .data = NULL};
	unsigned char		*ptr = (unsigned char *)read_token->data;
	unsigned char           *cksum;
	int			bodysize, md5elen;
	int			mic_hdrlen;
	u32			ret = GSS_S_DEFECTIVE_TOKEN;

	dprintk("RPC: spkm3_read_token read_token->len %d\n", read_token->len);

	if (g_verify_token_header((struct xdr_netobj *) &ctx->mech_used,
					&bodysize, &ptr, read_token->len))
		goto out;

	/* decode the token */

	if (toktype == SPKM_MIC_TOK) {

		if ((ret = spkm3_verify_mic_token(&ptr, &mic_hdrlen, &cksum))) 
			goto out;

		if (*cksum++ != 0x03) {
			dprintk("RPC: spkm3_read_token BAD checksum type\n");
			goto out;
		}
		md5elen = *cksum++; 
		cksum++; 	/* move past the zbit */
	
		if(!decode_asn1_bitstring(&wire_cksum, cksum, md5elen - 1, 16))
			goto out;

		/* HARD CODED FOR MD5 */

		/* compute the checksum of the message.
		*  ptr + 2 = start of header piece of checksum
		*  mic_hdrlen + 2 = length of header piece of checksum
		*/
		ret = GSS_S_DEFECTIVE_TOKEN;
		code = make_checksum(CKSUMTYPE_RSA_MD5, ptr + 2, 
					mic_hdrlen + 2, 
		                        message_buffer, &md5cksum);

		if (code)
			goto out;

		dprintk("RPC: spkm3_read_token: digest wire_cksum.len %d:\n", 
			wire_cksum.len);
		dprintk("          md5cksum.data\n");
		print_hexl((u32 *) md5cksum.data, 16, 0);
		dprintk("          cksum.data:\n");
		print_hexl((u32 *) wire_cksum.data, wire_cksum.len, 0);

		ret = GSS_S_BAD_SIG;
		code = memcmp(md5cksum.data, wire_cksum.data, wire_cksum.len);
		if (code)
			goto out;

	} else { 
		dprintk("RPC: BAD or UNSUPPORTED SPKM3 token type: %d\n",toktype);
		goto out;
	}

	/* XXX: need to add expiration and sequencing */
	ret = GSS_S_COMPLETE;
out:
	kfree(md5cksum.data);
	kfree(wire_cksum.data);
	return ret;
}
