/*
 *  linux/net/sunrpc/gss_spkm3_mech.c
 *
 *  Copyright (c) 2003 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@umich.edu>
 *  J. Bruce Fields <bfields@umich.edu>
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

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sunrpc/auth.h>
#include <linux/in.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/gss_spkm3.h>
#include <linux/sunrpc/xdr.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static const void *
simple_get_bytes(const void *p, const void *end, void *res, int len)
{
	const void *q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	memcpy(res, p, len);
	return q;
}

static const void *
simple_get_netobj(const void *p, const void *end, struct xdr_netobj *res)
{
	const void *q;
	unsigned int len;
	p = simple_get_bytes(p, end, &len, sizeof(len));
	if (IS_ERR(p))
		return p;
	res->len = len;
	if (len == 0) {
		res->data = NULL;
		return p;
	}
	q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	res->data = kmemdup(p, len, GFP_NOFS);
	if (unlikely(res->data == NULL))
		return ERR_PTR(-ENOMEM);
	return q;
}

static int
gss_import_sec_context_spkm3(const void *p, size_t len,
				struct gss_ctx *ctx_id,
				gfp_t gfp_mask)
{
	const void *end = (const void *)((const char *)p + len);
	struct	spkm3_ctx *ctx;
	int	version;

	if (!(ctx = kzalloc(sizeof(*ctx), gfp_mask)))
		goto out_err;

	p = simple_get_bytes(p, end, &version, sizeof(version));
	if (IS_ERR(p))
		goto out_err_free_ctx;
	if (version != 1) {
		dprintk("RPC:       unknown spkm3 token format: "
				"obsolete nfs-utils?\n");
		goto out_err_free_ctx;
	}

	p = simple_get_netobj(p, end, &ctx->ctx_id);
	if (IS_ERR(p))
		goto out_err_free_ctx;

	p = simple_get_bytes(p, end, &ctx->endtime, sizeof(ctx->endtime));
	if (IS_ERR(p))
		goto out_err_free_ctx_id;

	p = simple_get_netobj(p, end, &ctx->mech_used);
	if (IS_ERR(p))
		goto out_err_free_ctx_id;

	p = simple_get_bytes(p, end, &ctx->ret_flags, sizeof(ctx->ret_flags));
	if (IS_ERR(p))
		goto out_err_free_mech;

	p = simple_get_netobj(p, end, &ctx->conf_alg);
	if (IS_ERR(p))
		goto out_err_free_mech;

	p = simple_get_netobj(p, end, &ctx->derived_conf_key);
	if (IS_ERR(p))
		goto out_err_free_conf_alg;

	p = simple_get_netobj(p, end, &ctx->intg_alg);
	if (IS_ERR(p))
		goto out_err_free_conf_key;

	p = simple_get_netobj(p, end, &ctx->derived_integ_key);
	if (IS_ERR(p))
		goto out_err_free_intg_alg;

	if (p != end)
		goto out_err_free_intg_key;

	ctx_id->internal_ctx_id = ctx;

	dprintk("RPC:       Successfully imported new spkm context.\n");
	return 0;

out_err_free_intg_key:
	kfree(ctx->derived_integ_key.data);
out_err_free_intg_alg:
	kfree(ctx->intg_alg.data);
out_err_free_conf_key:
	kfree(ctx->derived_conf_key.data);
out_err_free_conf_alg:
	kfree(ctx->conf_alg.data);
out_err_free_mech:
	kfree(ctx->mech_used.data);
out_err_free_ctx_id:
	kfree(ctx->ctx_id.data);
out_err_free_ctx:
	kfree(ctx);
out_err:
	return PTR_ERR(p);
}

static void
gss_delete_sec_context_spkm3(void *internal_ctx)
{
	struct spkm3_ctx *sctx = internal_ctx;

	kfree(sctx->derived_integ_key.data);
	kfree(sctx->intg_alg.data);
	kfree(sctx->derived_conf_key.data);
	kfree(sctx->conf_alg.data);
	kfree(sctx->mech_used.data);
	kfree(sctx->ctx_id.data);
	kfree(sctx);
}

static u32
gss_verify_mic_spkm3(struct gss_ctx		*ctx,
			struct xdr_buf		*signbuf,
			struct xdr_netobj	*checksum)
{
	u32 maj_stat = 0;
	struct spkm3_ctx *sctx = ctx->internal_ctx_id;

	maj_stat = spkm3_read_token(sctx, checksum, signbuf, SPKM_MIC_TOK);

	dprintk("RPC:       gss_verify_mic_spkm3 returning %d\n", maj_stat);
	return maj_stat;
}

static u32
gss_get_mic_spkm3(struct gss_ctx	*ctx,
		     struct xdr_buf	*message_buffer,
		     struct xdr_netobj	*message_token)
{
	u32 err = 0;
	struct spkm3_ctx *sctx = ctx->internal_ctx_id;

	err = spkm3_make_token(sctx, message_buffer,
				message_token, SPKM_MIC_TOK);
	dprintk("RPC:       gss_get_mic_spkm3 returning %d\n", err);
	return err;
}

static const struct gss_api_ops gss_spkm3_ops = {
	.gss_import_sec_context	= gss_import_sec_context_spkm3,
	.gss_get_mic		= gss_get_mic_spkm3,
	.gss_verify_mic		= gss_verify_mic_spkm3,
	.gss_delete_sec_context	= gss_delete_sec_context_spkm3,
};

static struct pf_desc gss_spkm3_pfs[] = {
	{RPC_AUTH_GSS_SPKM, RPC_GSS_SVC_NONE, "spkm3"},
	{RPC_AUTH_GSS_SPKMI, RPC_GSS_SVC_INTEGRITY, "spkm3i"},
};

static struct gss_api_mech gss_spkm3_mech = {
	.gm_name	= "spkm3",
	.gm_owner	= THIS_MODULE,
	.gm_oid		= {7, "\053\006\001\005\005\001\003"},
	.gm_ops		= &gss_spkm3_ops,
	.gm_pf_num	= ARRAY_SIZE(gss_spkm3_pfs),
	.gm_pfs		= gss_spkm3_pfs,
};

static int __init init_spkm3_module(void)
{
	int status;

	status = gss_mech_register(&gss_spkm3_mech);
	if (status)
		printk("Failed to register spkm3 gss mechanism!\n");
	return status;
}

static void __exit cleanup_spkm3_module(void)
{
	gss_mech_unregister(&gss_spkm3_mech);
}

MODULE_LICENSE("GPL");
module_init(init_spkm3_module);
module_exit(cleanup_spkm3_module);
