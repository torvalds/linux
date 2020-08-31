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

#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/gss_krb5_enctypes.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static struct gss_api_mech gss_kerberos_mech;	/* forward declaration */

static const struct gss_krb5_enctype supported_gss_krb5_enctypes[] = {
#ifndef CONFIG_SUNRPC_DISABLE_INSECURE_ENCTYPES
	/*
	 * DES (All DES enctypes are mapped to the same gss functionality)
	 */
	{
	  .etype = ENCTYPE_DES_CBC_RAW,
	  .ctype = CKSUMTYPE_RSA_MD5,
	  .name = "des-cbc-crc",
	  .encrypt_name = "cbc(des)",
	  .cksum_name = "md5",
	  .encrypt = krb5_encrypt,
	  .decrypt = krb5_decrypt,
	  .mk_key = NULL,
	  .signalg = SGN_ALG_DES_MAC_MD5,
	  .sealalg = SEAL_ALG_DES,
	  .keybytes = 7,
	  .keylength = 8,
	  .blocksize = 8,
	  .conflen = 8,
	  .cksumlength = 8,
	  .keyed_cksum = 0,
	},
#endif	/* CONFIG_SUNRPC_DISABLE_INSECURE_ENCTYPES */
	/*
	 * 3DES
	 */
	{
	  .etype = ENCTYPE_DES3_CBC_RAW,
	  .ctype = CKSUMTYPE_HMAC_SHA1_DES3,
	  .name = "des3-hmac-sha1",
	  .encrypt_name = "cbc(des3_ede)",
	  .cksum_name = "hmac(sha1)",
	  .encrypt = krb5_encrypt,
	  .decrypt = krb5_decrypt,
	  .mk_key = gss_krb5_des3_make_key,
	  .signalg = SGN_ALG_HMAC_SHA1_DES3_KD,
	  .sealalg = SEAL_ALG_DES3KD,
	  .keybytes = 21,
	  .keylength = 24,
	  .blocksize = 8,
	  .conflen = 8,
	  .cksumlength = 20,
	  .keyed_cksum = 1,
	},
	/*
	 * AES128
	 */
	{
	  .etype = ENCTYPE_AES128_CTS_HMAC_SHA1_96,
	  .ctype = CKSUMTYPE_HMAC_SHA1_96_AES128,
	  .name = "aes128-cts",
	  .encrypt_name = "cts(cbc(aes))",
	  .cksum_name = "hmac(sha1)",
	  .encrypt = krb5_encrypt,
	  .decrypt = krb5_decrypt,
	  .mk_key = gss_krb5_aes_make_key,
	  .encrypt_v2 = gss_krb5_aes_encrypt,
	  .decrypt_v2 = gss_krb5_aes_decrypt,
	  .signalg = -1,
	  .sealalg = -1,
	  .keybytes = 16,
	  .keylength = 16,
	  .blocksize = 16,
	  .conflen = 16,
	  .cksumlength = 12,
	  .keyed_cksum = 1,
	},
	/*
	 * AES256
	 */
	{
	  .etype = ENCTYPE_AES256_CTS_HMAC_SHA1_96,
	  .ctype = CKSUMTYPE_HMAC_SHA1_96_AES256,
	  .name = "aes256-cts",
	  .encrypt_name = "cts(cbc(aes))",
	  .cksum_name = "hmac(sha1)",
	  .encrypt = krb5_encrypt,
	  .decrypt = krb5_decrypt,
	  .mk_key = gss_krb5_aes_make_key,
	  .encrypt_v2 = gss_krb5_aes_encrypt,
	  .decrypt_v2 = gss_krb5_aes_decrypt,
	  .signalg = -1,
	  .sealalg = -1,
	  .keybytes = 32,
	  .keylength = 32,
	  .blocksize = 16,
	  .conflen = 16,
	  .cksumlength = 12,
	  .keyed_cksum = 1,
	},
};

static const int num_supported_enctypes =
	ARRAY_SIZE(supported_gss_krb5_enctypes);

static int
supported_gss_krb5_enctype(int etype)
{
	int i;
	for (i = 0; i < num_supported_enctypes; i++)
		if (supported_gss_krb5_enctypes[i].etype == etype)
			return 1;
	return 0;
}

static const struct gss_krb5_enctype *
get_gss_krb5_enctype(int etype)
{
	int i;
	for (i = 0; i < num_supported_enctypes; i++)
		if (supported_gss_krb5_enctypes[i].etype == etype)
			return &supported_gss_krb5_enctypes[i];
	return NULL;
}

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
	q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	res->data = kmemdup(p, len, GFP_NOFS);
	if (unlikely(res->data == NULL))
		return ERR_PTR(-ENOMEM);
	res->len = len;
	return q;
}

static inline const void *
get_key(const void *p, const void *end,
	struct krb5_ctx *ctx, struct crypto_sync_skcipher **res)
{
	struct xdr_netobj	key;
	int			alg;

	p = simple_get_bytes(p, end, &alg, sizeof(alg));
	if (IS_ERR(p))
		goto out_err;

	switch (alg) {
	case ENCTYPE_DES_CBC_CRC:
	case ENCTYPE_DES_CBC_MD4:
	case ENCTYPE_DES_CBC_MD5:
		/* Map all these key types to ENCTYPE_DES_CBC_RAW */
		alg = ENCTYPE_DES_CBC_RAW;
		break;
	}

	if (!supported_gss_krb5_enctype(alg)) {
		printk(KERN_WARNING "gss_kerberos_mech: unsupported "
			"encryption key algorithm %d\n", alg);
		p = ERR_PTR(-EINVAL);
		goto out_err;
	}
	p = simple_get_netobj(p, end, &key);
	if (IS_ERR(p))
		goto out_err;

	*res = crypto_alloc_sync_skcipher(ctx->gk5e->encrypt_name, 0, 0);
	if (IS_ERR(*res)) {
		printk(KERN_WARNING "gss_kerberos_mech: unable to initialize "
			"crypto algorithm %s\n", ctx->gk5e->encrypt_name);
		*res = NULL;
		goto out_err_free_key;
	}
	if (crypto_sync_skcipher_setkey(*res, key.data, key.len)) {
		printk(KERN_WARNING "gss_kerberos_mech: error setting key for "
			"crypto algorithm %s\n", ctx->gk5e->encrypt_name);
		goto out_err_free_tfm;
	}

	kfree(key.data);
	return p;

out_err_free_tfm:
	crypto_free_sync_skcipher(*res);
out_err_free_key:
	kfree(key.data);
	p = ERR_PTR(-EINVAL);
out_err:
	return p;
}

static int
gss_import_v1_context(const void *p, const void *end, struct krb5_ctx *ctx)
{
	u32 seq_send;
	int tmp;
	u32 time32;

	p = simple_get_bytes(p, end, &ctx->initiate, sizeof(ctx->initiate));
	if (IS_ERR(p))
		goto out_err;

	/* Old format supports only DES!  Any other enctype uses new format */
	ctx->enctype = ENCTYPE_DES_CBC_RAW;

	ctx->gk5e = get_gss_krb5_enctype(ctx->enctype);
	if (ctx->gk5e == NULL) {
		p = ERR_PTR(-EINVAL);
		goto out_err;
	}

	/* The downcall format was designed before we completely understood
	 * the uses of the context fields; so it includes some stuff we
	 * just give some minimal sanity-checking, and some we ignore
	 * completely (like the next twenty bytes): */
	if (unlikely(p + 20 > end || p + 20 < p)) {
		p = ERR_PTR(-EFAULT);
		goto out_err;
	}
	p += 20;
	p = simple_get_bytes(p, end, &tmp, sizeof(tmp));
	if (IS_ERR(p))
		goto out_err;
	if (tmp != SGN_ALG_DES_MAC_MD5) {
		p = ERR_PTR(-ENOSYS);
		goto out_err;
	}
	p = simple_get_bytes(p, end, &tmp, sizeof(tmp));
	if (IS_ERR(p))
		goto out_err;
	if (tmp != SEAL_ALG_DES) {
		p = ERR_PTR(-ENOSYS);
		goto out_err;
	}
	p = simple_get_bytes(p, end, &time32, sizeof(time32));
	if (IS_ERR(p))
		goto out_err;
	/* unsigned 32-bit time overflows in year 2106 */
	ctx->endtime = (time64_t)time32;
	p = simple_get_bytes(p, end, &seq_send, sizeof(seq_send));
	if (IS_ERR(p))
		goto out_err;
	atomic_set(&ctx->seq_send, seq_send);
	p = simple_get_netobj(p, end, &ctx->mech_used);
	if (IS_ERR(p))
		goto out_err;
	p = get_key(p, end, ctx, &ctx->enc);
	if (IS_ERR(p))
		goto out_err_free_mech;
	p = get_key(p, end, ctx, &ctx->seq);
	if (IS_ERR(p))
		goto out_err_free_key1;
	if (p != end) {
		p = ERR_PTR(-EFAULT);
		goto out_err_free_key2;
	}

	return 0;

out_err_free_key2:
	crypto_free_sync_skcipher(ctx->seq);
out_err_free_key1:
	crypto_free_sync_skcipher(ctx->enc);
out_err_free_mech:
	kfree(ctx->mech_used.data);
out_err:
	return PTR_ERR(p);
}

static struct crypto_sync_skcipher *
context_v2_alloc_cipher(struct krb5_ctx *ctx, const char *cname, u8 *key)
{
	struct crypto_sync_skcipher *cp;

	cp = crypto_alloc_sync_skcipher(cname, 0, 0);
	if (IS_ERR(cp)) {
		dprintk("gss_kerberos_mech: unable to initialize "
			"crypto algorithm %s\n", cname);
		return NULL;
	}
	if (crypto_sync_skcipher_setkey(cp, key, ctx->gk5e->keylength)) {
		dprintk("gss_kerberos_mech: error setting key for "
			"crypto algorithm %s\n", cname);
		crypto_free_sync_skcipher(cp);
		return NULL;
	}
	return cp;
}

static inline void
set_cdata(u8 cdata[GSS_KRB5_K5CLENGTH], u32 usage, u8 seed)
{
	cdata[0] = (usage>>24)&0xff;
	cdata[1] = (usage>>16)&0xff;
	cdata[2] = (usage>>8)&0xff;
	cdata[3] = usage&0xff;
	cdata[4] = seed;
}

static int
context_derive_keys_des3(struct krb5_ctx *ctx, gfp_t gfp_mask)
{
	struct xdr_netobj c, keyin, keyout;
	u8 cdata[GSS_KRB5_K5CLENGTH];
	u32 err;

	c.len = GSS_KRB5_K5CLENGTH;
	c.data = cdata;

	keyin.data = ctx->Ksess;
	keyin.len = ctx->gk5e->keylength;
	keyout.len = ctx->gk5e->keylength;

	/* seq uses the raw key */
	ctx->seq = context_v2_alloc_cipher(ctx, ctx->gk5e->encrypt_name,
					   ctx->Ksess);
	if (ctx->seq == NULL)
		goto out_err;

	ctx->enc = context_v2_alloc_cipher(ctx, ctx->gk5e->encrypt_name,
					   ctx->Ksess);
	if (ctx->enc == NULL)
		goto out_free_seq;

	/* derive cksum */
	set_cdata(cdata, KG_USAGE_SIGN, KEY_USAGE_SEED_CHECKSUM);
	keyout.data = ctx->cksum;
	err = krb5_derive_key(ctx->gk5e, &keyin, &keyout, &c, gfp_mask);
	if (err) {
		dprintk("%s: Error %d deriving cksum key\n",
			__func__, err);
		goto out_free_enc;
	}

	return 0;

out_free_enc:
	crypto_free_sync_skcipher(ctx->enc);
out_free_seq:
	crypto_free_sync_skcipher(ctx->seq);
out_err:
	return -EINVAL;
}

static int
context_derive_keys_new(struct krb5_ctx *ctx, gfp_t gfp_mask)
{
	struct xdr_netobj c, keyin, keyout;
	u8 cdata[GSS_KRB5_K5CLENGTH];
	u32 err;

	c.len = GSS_KRB5_K5CLENGTH;
	c.data = cdata;

	keyin.data = ctx->Ksess;
	keyin.len = ctx->gk5e->keylength;
	keyout.len = ctx->gk5e->keylength;

	/* initiator seal encryption */
	set_cdata(cdata, KG_USAGE_INITIATOR_SEAL, KEY_USAGE_SEED_ENCRYPTION);
	keyout.data = ctx->initiator_seal;
	err = krb5_derive_key(ctx->gk5e, &keyin, &keyout, &c, gfp_mask);
	if (err) {
		dprintk("%s: Error %d deriving initiator_seal key\n",
			__func__, err);
		goto out_err;
	}
	ctx->initiator_enc = context_v2_alloc_cipher(ctx,
						     ctx->gk5e->encrypt_name,
						     ctx->initiator_seal);
	if (ctx->initiator_enc == NULL)
		goto out_err;

	/* acceptor seal encryption */
	set_cdata(cdata, KG_USAGE_ACCEPTOR_SEAL, KEY_USAGE_SEED_ENCRYPTION);
	keyout.data = ctx->acceptor_seal;
	err = krb5_derive_key(ctx->gk5e, &keyin, &keyout, &c, gfp_mask);
	if (err) {
		dprintk("%s: Error %d deriving acceptor_seal key\n",
			__func__, err);
		goto out_free_initiator_enc;
	}
	ctx->acceptor_enc = context_v2_alloc_cipher(ctx,
						    ctx->gk5e->encrypt_name,
						    ctx->acceptor_seal);
	if (ctx->acceptor_enc == NULL)
		goto out_free_initiator_enc;

	/* initiator sign checksum */
	set_cdata(cdata, KG_USAGE_INITIATOR_SIGN, KEY_USAGE_SEED_CHECKSUM);
	keyout.data = ctx->initiator_sign;
	err = krb5_derive_key(ctx->gk5e, &keyin, &keyout, &c, gfp_mask);
	if (err) {
		dprintk("%s: Error %d deriving initiator_sign key\n",
			__func__, err);
		goto out_free_acceptor_enc;
	}

	/* acceptor sign checksum */
	set_cdata(cdata, KG_USAGE_ACCEPTOR_SIGN, KEY_USAGE_SEED_CHECKSUM);
	keyout.data = ctx->acceptor_sign;
	err = krb5_derive_key(ctx->gk5e, &keyin, &keyout, &c, gfp_mask);
	if (err) {
		dprintk("%s: Error %d deriving acceptor_sign key\n",
			__func__, err);
		goto out_free_acceptor_enc;
	}

	/* initiator seal integrity */
	set_cdata(cdata, KG_USAGE_INITIATOR_SEAL, KEY_USAGE_SEED_INTEGRITY);
	keyout.data = ctx->initiator_integ;
	err = krb5_derive_key(ctx->gk5e, &keyin, &keyout, &c, gfp_mask);
	if (err) {
		dprintk("%s: Error %d deriving initiator_integ key\n",
			__func__, err);
		goto out_free_acceptor_enc;
	}

	/* acceptor seal integrity */
	set_cdata(cdata, KG_USAGE_ACCEPTOR_SEAL, KEY_USAGE_SEED_INTEGRITY);
	keyout.data = ctx->acceptor_integ;
	err = krb5_derive_key(ctx->gk5e, &keyin, &keyout, &c, gfp_mask);
	if (err) {
		dprintk("%s: Error %d deriving acceptor_integ key\n",
			__func__, err);
		goto out_free_acceptor_enc;
	}

	switch (ctx->enctype) {
	case ENCTYPE_AES128_CTS_HMAC_SHA1_96:
	case ENCTYPE_AES256_CTS_HMAC_SHA1_96:
		ctx->initiator_enc_aux =
			context_v2_alloc_cipher(ctx, "cbc(aes)",
						ctx->initiator_seal);
		if (ctx->initiator_enc_aux == NULL)
			goto out_free_acceptor_enc;
		ctx->acceptor_enc_aux =
			context_v2_alloc_cipher(ctx, "cbc(aes)",
						ctx->acceptor_seal);
		if (ctx->acceptor_enc_aux == NULL) {
			crypto_free_sync_skcipher(ctx->initiator_enc_aux);
			goto out_free_acceptor_enc;
		}
	}

	return 0;

out_free_acceptor_enc:
	crypto_free_sync_skcipher(ctx->acceptor_enc);
out_free_initiator_enc:
	crypto_free_sync_skcipher(ctx->initiator_enc);
out_err:
	return -EINVAL;
}

static int
gss_import_v2_context(const void *p, const void *end, struct krb5_ctx *ctx,
		gfp_t gfp_mask)
{
	u64 seq_send64;
	int keylen;
	u32 time32;

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
	/* set seq_send for use by "older" enctypes */
	atomic_set(&ctx->seq_send, seq_send64);
	if (seq_send64 != atomic_read(&ctx->seq_send)) {
		dprintk("%s: seq_send64 %llx, seq_send %x overflow?\n", __func__,
			seq_send64, atomic_read(&ctx->seq_send));
		p = ERR_PTR(-EINVAL);
		goto out_err;
	}
	p = simple_get_bytes(p, end, &ctx->enctype, sizeof(ctx->enctype));
	if (IS_ERR(p))
		goto out_err;
	/* Map ENCTYPE_DES3_CBC_SHA1 to ENCTYPE_DES3_CBC_RAW */
	if (ctx->enctype == ENCTYPE_DES3_CBC_SHA1)
		ctx->enctype = ENCTYPE_DES3_CBC_RAW;
	ctx->gk5e = get_gss_krb5_enctype(ctx->enctype);
	if (ctx->gk5e == NULL) {
		dprintk("gss_kerberos_mech: unsupported krb5 enctype %u\n",
			ctx->enctype);
		p = ERR_PTR(-EINVAL);
		goto out_err;
	}
	keylen = ctx->gk5e->keylength;

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

	switch (ctx->enctype) {
	case ENCTYPE_DES3_CBC_RAW:
		return context_derive_keys_des3(ctx, gfp_mask);
	case ENCTYPE_AES128_CTS_HMAC_SHA1_96:
	case ENCTYPE_AES256_CTS_HMAC_SHA1_96:
		return context_derive_keys_new(ctx, gfp_mask);
	default:
		return -EINVAL;
	}

out_err:
	return PTR_ERR(p);
}

static int
gss_import_sec_context_kerberos(const void *p, size_t len,
				struct gss_ctx *ctx_id,
				time64_t *endtime,
				gfp_t gfp_mask)
{
	const void *end = (const void *)((const char *)p + len);
	struct  krb5_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), gfp_mask);
	if (ctx == NULL)
		return -ENOMEM;

	if (len == 85)
		ret = gss_import_v1_context(p, end, ctx);
	else
		ret = gss_import_v2_context(p, end, ctx, gfp_mask);

	if (ret == 0) {
		ctx_id->internal_ctx_id = ctx;
		if (endtime)
			*endtime = ctx->endtime;
	} else
		kfree(ctx);

	dprintk("RPC:       %s: returning %d\n", __func__, ret);
	return ret;
}

static void
gss_delete_sec_context_kerberos(void *internal_ctx) {
	struct krb5_ctx *kctx = internal_ctx;

	crypto_free_sync_skcipher(kctx->seq);
	crypto_free_sync_skcipher(kctx->enc);
	crypto_free_sync_skcipher(kctx->acceptor_enc);
	crypto_free_sync_skcipher(kctx->initiator_enc);
	crypto_free_sync_skcipher(kctx->acceptor_enc_aux);
	crypto_free_sync_skcipher(kctx->initiator_enc_aux);
	kfree(kctx->mech_used.data);
	kfree(kctx);
}

static const struct gss_api_ops gss_kerberos_ops = {
	.gss_import_sec_context	= gss_import_sec_context_kerberos,
	.gss_get_mic		= gss_get_mic_kerberos,
	.gss_verify_mic		= gss_verify_mic_kerberos,
	.gss_wrap		= gss_wrap_kerberos,
	.gss_unwrap		= gss_unwrap_kerberos,
	.gss_delete_sec_context	= gss_delete_sec_context_kerberos,
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
	.gm_upcall_enctypes = KRB5_SUPPORTED_ENCTYPES,
};

static int __init init_kerberos_module(void)
{
	int status;

	status = gss_mech_register(&gss_kerberos_mech);
	if (status)
		printk("Failed to register kerberos gss mechanism!\n");
	return status;
}

static void __exit cleanup_kerberos_module(void)
{
	gss_mech_unregister(&gss_kerberos_mech);
}

MODULE_LICENSE("GPL");
module_init(init_kerberos_module);
module_exit(cleanup_kerberos_module);
