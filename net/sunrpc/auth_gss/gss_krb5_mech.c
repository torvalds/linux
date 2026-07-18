// SPDX-License-Identifier: BSD-3-Clause
/*
 *  linux/net/sunrpc/gss_krb5_mech.c
 *
 *  Copyright (c) 2001-2008 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@umich.edu>
 *  J. Bruce Fields <bfields@umich.edu>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>

#include "auth_gss_internal.h"
#include "gss_krb5_internal.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static struct gss_api_mech gss_kerberos_mech;

/*
 * Candidate enctypes in order of most preferred to least.
 * Each is probed against crypto/krb5 at module init; only
 * enctypes that crypto/krb5 supports are advertised.
 */
static const u32 gss_krb5_enctypes[] = {
	KRB5_ENCTYPE_AES256_CTS_HMAC_SHA384_192,
	KRB5_ENCTYPE_AES128_CTS_HMAC_SHA256_128,
	KRB5_ENCTYPE_CAMELLIA256_CTS_CMAC,
	KRB5_ENCTYPE_CAMELLIA128_CTS_CMAC,
	KRB5_ENCTYPE_AES256_CTS_HMAC_SHA1_96,
	KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96,
};

static char gss_krb5_enctype_priority_list[64];

static void gss_krb5_prepare_enctype_priority_list(void)
{
	size_t total, i;
	char buf[16];
	char *sep;
	int n;

	sep = "";
	gss_krb5_enctype_priority_list[0] = '\0';
	for (total = 0, i = 0; i < ARRAY_SIZE(gss_krb5_enctypes); i++) {
		if (!crypto_krb5_find_enctype(gss_krb5_enctypes[i]))
			continue;
		n = sprintf(buf, "%s%u", sep, gss_krb5_enctypes[i]);
		if (n < 0)
			break;
		if (total + n >= sizeof(gss_krb5_enctype_priority_list))
			break;
		strcat(gss_krb5_enctype_priority_list, buf);
		sep = ",";
		total += n;
	}
}

static int
gss_krb5_import_ctx_v2(struct krb5_ctx *ctx, gfp_t gfp_mask)
{
	struct krb5_buffer TK = {
		.len	= ctx->krb5e->key_len,
		.data	= ctx->Ksess,
	};
	int ret;

	ctx->initiator_enc_aead =
		crypto_krb5_prepare_encryption(ctx->krb5e, &TK,
					       KG_USAGE_INITIATOR_SEAL,
					       gfp_mask);
	if (IS_ERR(ctx->initiator_enc_aead)) {
		ret = PTR_ERR(ctx->initiator_enc_aead);
		goto out_free;
	}
	ctx->acceptor_enc_aead =
		crypto_krb5_prepare_encryption(ctx->krb5e, &TK,
					       KG_USAGE_ACCEPTOR_SEAL,
					       gfp_mask);
	if (IS_ERR(ctx->acceptor_enc_aead)) {
		ret = PTR_ERR(ctx->acceptor_enc_aead);
		goto out_free;
	}
	ctx->initiator_sign_shash =
		crypto_krb5_prepare_checksum(ctx->krb5e, &TK,
					     KG_USAGE_INITIATOR_SIGN,
					     gfp_mask);
	if (IS_ERR(ctx->initiator_sign_shash)) {
		ret = PTR_ERR(ctx->initiator_sign_shash);
		goto out_free;
	}
	ctx->acceptor_sign_shash =
		crypto_krb5_prepare_checksum(ctx->krb5e, &TK,
					     KG_USAGE_ACCEPTOR_SIGN,
					     gfp_mask);
	if (IS_ERR(ctx->acceptor_sign_shash)) {
		ret = PTR_ERR(ctx->acceptor_sign_shash);
		goto out_free;
	}

	return 0;

out_free:
	crypto_free_shash(ctx->acceptor_sign_shash);
	crypto_free_shash(ctx->initiator_sign_shash);
	crypto_free_aead(ctx->acceptor_enc_aead);
	crypto_free_aead(ctx->initiator_enc_aead);
	return ret;
}

static int
gss_import_v2_context(const void *p, const void *end, struct krb5_ctx *ctx,
		gfp_t gfp_mask)
{
	u64 seq_send64;
	int keylen;
	u32 time32;
	int ret;

	p = simple_get_bytes(p, end, &ctx->flags, sizeof(ctx->flags));
	if (IS_ERR(p))
		goto out_err;
	ctx->initiate = ctx->flags & KRB5_CTX_FLAG_INITIATOR;

	p = simple_get_bytes(p, end, &time32, sizeof(time32));
	if (IS_ERR(p))
		goto out_err;
	/* unsigned 32-bit time overflows in year 2106 */
	ctx->endtime = (time64_t)time32;
	p = simple_get_bytes(p, end, &seq_send64, sizeof(seq_send64));
	if (IS_ERR(p))
		goto out_err;
	atomic64_set(&ctx->seq_send64, seq_send64);
	p = simple_get_bytes(p, end, &ctx->enctype, sizeof(ctx->enctype));
	if (IS_ERR(p))
		goto out_err;
	ctx->krb5e = crypto_krb5_find_enctype(ctx->enctype);
	if (!ctx->krb5e) {
		dprintk("gss_kerberos_mech: unsupported krb5 enctype %u\n",
			ctx->enctype);
		p = ERR_PTR(-EINVAL);
		goto out_err;
	}
	keylen = ctx->krb5e->key_len;

	p = simple_get_bytes(p, end, ctx->Ksess, keylen);
	if (IS_ERR(p))
		goto out_err;

	if (p != end) {
		p = ERR_PTR(-EINVAL);
		goto out_err;
	}

	ctx->mech_used.data = kmemdup(gss_kerberos_mech.gm_oid.data,
				      gss_kerberos_mech.gm_oid.len, gfp_mask);
	if (unlikely(ctx->mech_used.data == NULL)) {
		p = ERR_PTR(-ENOMEM);
		goto out_err;
	}
	ctx->mech_used.len = gss_kerberos_mech.gm_oid.len;

	ret = gss_krb5_import_ctx_v2(ctx, gfp_mask);
	if (ret) {
		p = ERR_PTR(ret);
		goto out_free;
	}

	return 0;

out_free:
	kfree(ctx->mech_used.data);
out_err:
	return PTR_ERR(p);
}

static int
gss_krb5_import_sec_context(const void *p, size_t len, struct gss_ctx *ctx_id,
			    time64_t *endtime, gfp_t gfp_mask)
{
	const void *end = (const void *)((const char *)p + len);
	struct  krb5_ctx *ctx;
	int ret;

	ctx = kzalloc_obj(*ctx, gfp_mask);
	if (ctx == NULL)
		return -ENOMEM;

	ret = gss_import_v2_context(p, end, ctx, gfp_mask);
	memzero_explicit(&ctx->Ksess, sizeof(ctx->Ksess));
	if (ret) {
		kfree(ctx);
		return ret;
	}

	ctx_id->internal_ctx_id = ctx;
	if (endtime)
		*endtime = ctx->endtime;
	return 0;
}

static void
gss_krb5_delete_sec_context(void *internal_ctx)
{
	struct krb5_ctx *kctx = internal_ctx;

	crypto_free_shash(kctx->acceptor_sign_shash);
	crypto_free_shash(kctx->initiator_sign_shash);
	crypto_free_aead(kctx->acceptor_enc_aead);
	crypto_free_aead(kctx->initiator_enc_aead);
	kfree(kctx->mech_used.data);
	kfree(kctx);
}

/**
 * gss_krb5_errno_to_status - Map a negative errno to a GSS major status
 * @err: negative errno value, or zero
 *
 * Returns:
 *   %GSS_S_COMPLETE if @err is zero
 *   %GSS_S_BAD_SIG if @err is -EBADMSG (integrity check failure)
 *   %GSS_S_DEFECTIVE_TOKEN if @err is -EPROTO (malformed token)
 *   %GSS_S_FAILURE for all other negative values
 */
u32 gss_krb5_errno_to_status(int err)
{
	switch (err) {
	case 0:
		return GSS_S_COMPLETE;
	case -EBADMSG:
		return GSS_S_BAD_SIG;
	case -EPROTO:
		return GSS_S_DEFECTIVE_TOKEN;
	default:
		return GSS_S_FAILURE;
	}
}

/**
 * gss_krb5_get_mic - get_mic for the Kerberos GSS mechanism
 * @gctx: GSS context
 * @text: plaintext to checksum
 * @token: buffer into which to write the computed checksum
 *
 * Return values:
 *    %GSS_S_COMPLETE - success, and @token is filled in
 *    %GSS_S_FAILURE - checksum could not be generated
 *    %GSS_S_CONTEXT_EXPIRED - Kerberos context is no longer valid
 */
static u32 gss_krb5_get_mic(struct gss_ctx *gctx, struct xdr_buf *text,
			    struct xdr_netobj *token)
{
	struct krb5_ctx *kctx = gctx->internal_ctx_id;

	return gss_krb5_get_mic_v2(kctx, text, token);
}

/**
 * gss_krb5_verify_mic - verify_mic for the Kerberos GSS mechanism
 * @gctx: GSS context
 * @message_buffer: plaintext to check
 * @read_token: received checksum to check
 *
 * Return values:
 *    %GSS_S_COMPLETE - computed and received checksums match
 *    %GSS_S_DEFECTIVE_TOKEN - received checksum is not valid
 *    %GSS_S_BAD_SIG - computed and received checksums do not match
 *    %GSS_S_FAILURE - received checksum could not be checked
 *    %GSS_S_CONTEXT_EXPIRED - Kerberos context is no longer valid
 */
static u32 gss_krb5_verify_mic(struct gss_ctx *gctx,
			       struct xdr_buf *message_buffer,
			       struct xdr_netobj *read_token)
{
	struct krb5_ctx *kctx = gctx->internal_ctx_id;

	return gss_krb5_verify_mic_v2(kctx, message_buffer, read_token);
}

/**
 * gss_krb5_wrap - gss_wrap for the Kerberos GSS mechanism
 * @gctx: initialized GSS context
 * @offset: byte offset in @buf to start writing the cipher text
 * @buf: OUT: send buffer
 * @pages: plaintext to wrap
 *
 * Return values:
 *    %GSS_S_COMPLETE - success, @buf has been updated
 *    %GSS_S_FAILURE - @buf could not be wrapped
 *    %GSS_S_CONTEXT_EXPIRED - Kerberos context is no longer valid
 */
static u32 gss_krb5_wrap(struct gss_ctx *gctx, int offset,
			 struct xdr_buf *buf, struct page **pages)
{
	struct krb5_ctx	*kctx = gctx->internal_ctx_id;

	return gss_krb5_wrap_v2(kctx, offset, buf, pages);
}

/**
 * gss_krb5_unwrap - gss_unwrap for the Kerberos GSS mechanism
 * @gctx: initialized GSS context
 * @offset: starting byte offset into @buf
 * @len: size of ciphertext to unwrap
 * @buf: ciphertext to unwrap
 *
 * Return values:
 *    %GSS_S_COMPLETE - success, @buf has been updated
 *    %GSS_S_DEFECTIVE_TOKEN - received blob is not valid
 *    %GSS_S_BAD_SIG - computed and received checksums do not match
 *    %GSS_S_FAILURE - @buf could not be unwrapped
 *    %GSS_S_CONTEXT_EXPIRED - Kerberos context is no longer valid
 */
static u32 gss_krb5_unwrap(struct gss_ctx *gctx, int offset,
			   int len, struct xdr_buf *buf)
{
	struct krb5_ctx	*kctx = gctx->internal_ctx_id;

	return gss_krb5_unwrap_v2(kctx, offset, len, buf,
				  &gctx->slack, &gctx->align);
}

static const struct gss_api_ops gss_kerberos_ops = {
	.gss_import_sec_context	= gss_krb5_import_sec_context,
	.gss_get_mic		= gss_krb5_get_mic,
	.gss_verify_mic		= gss_krb5_verify_mic,
	.gss_wrap		= gss_krb5_wrap,
	.gss_unwrap		= gss_krb5_unwrap,
	.gss_delete_sec_context	= gss_krb5_delete_sec_context,
};

static struct pf_desc gss_kerberos_pfs[] = {
	[0] = {
		.pseudoflavor = RPC_AUTH_GSS_KRB5,
		.qop = GSS_C_QOP_DEFAULT,
		.service = RPC_GSS_SVC_NONE,
		.name = "krb5",
	},
	[1] = {
		.pseudoflavor = RPC_AUTH_GSS_KRB5I,
		.qop = GSS_C_QOP_DEFAULT,
		.service = RPC_GSS_SVC_INTEGRITY,
		.name = "krb5i",
		.datatouch = true,
	},
	[2] = {
		.pseudoflavor = RPC_AUTH_GSS_KRB5P,
		.qop = GSS_C_QOP_DEFAULT,
		.service = RPC_GSS_SVC_PRIVACY,
		.name = "krb5p",
		.datatouch = true,
	},
};

MODULE_ALIAS("rpc-auth-gss-krb5");
MODULE_ALIAS("rpc-auth-gss-krb5i");
MODULE_ALIAS("rpc-auth-gss-krb5p");
MODULE_ALIAS("rpc-auth-gss-390003");
MODULE_ALIAS("rpc-auth-gss-390004");
MODULE_ALIAS("rpc-auth-gss-390005");
MODULE_ALIAS("rpc-auth-gss-1.2.840.113554.1.2.2");

static struct gss_api_mech gss_kerberos_mech = {
	.gm_name	= "krb5",
	.gm_owner	= THIS_MODULE,
	.gm_oid		= { 9, "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02" },
	.gm_ops		= &gss_kerberos_ops,
	.gm_pf_num	= ARRAY_SIZE(gss_kerberos_pfs),
	.gm_pfs		= gss_kerberos_pfs,
	.gm_upcall_enctypes = gss_krb5_enctype_priority_list,
};

static int __init init_kerberos_module(void)
{
	int status;

	gss_krb5_prepare_enctype_priority_list();
	status = gss_mech_register(&gss_kerberos_mech);
	if (status)
		printk("Failed to register kerberos gss mechanism!\n");
	return status;
}

static void __exit cleanup_kerberos_module(void)
{
	gss_mech_unregister(&gss_kerberos_mech);
}

MODULE_DESCRIPTION("Sun RPC Kerberos 5 module");
MODULE_LICENSE("GPL");
module_init(init_kerberos_module);
module_exit(cleanup_kerberos_module);
