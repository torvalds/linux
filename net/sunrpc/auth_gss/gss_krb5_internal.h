/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * SunRPC GSS Kerberos 5 mechanism internal definitions
 *
 * Copyright (c) 2022 Oracle and/or its affiliates.
 */

#ifndef _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H
#define _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H

/*
 * GSS Kerberos 5 mechanism Per-Message calls.
 */

u32 gss_krb5_get_mic_v1(struct krb5_ctx *ctx, struct xdr_buf *text,
			struct xdr_netobj *token);
u32 gss_krb5_get_mic_v2(struct krb5_ctx *ctx, struct xdr_buf *text,
			struct xdr_netobj *token);

u32 gss_krb5_verify_mic_v1(struct krb5_ctx *ctx, struct xdr_buf *message_buffer,
			   struct xdr_netobj *read_token);
u32 gss_krb5_verify_mic_v2(struct krb5_ctx *ctx, struct xdr_buf *message_buffer,
			   struct xdr_netobj *read_token);

u32 gss_krb5_wrap_v1(struct krb5_ctx *kctx, int offset,
		     struct xdr_buf *buf, struct page **pages);
u32 gss_krb5_wrap_v2(struct krb5_ctx *kctx, int offset,
		     struct xdr_buf *buf, struct page **pages);

u32 gss_krb5_unwrap_v1(struct krb5_ctx *kctx, int offset, int len,
		       struct xdr_buf *buf, unsigned int *slack,
		       unsigned int *align);
u32 gss_krb5_unwrap_v2(struct krb5_ctx *kctx, int offset, int len,
		       struct xdr_buf *buf, unsigned int *slack,
		       unsigned int *align);

/*
 * Implementation internal functions
 */

/* Key Derivation Functions */

int krb5_derive_key_v1(const struct gss_krb5_enctype *gk5e,
		       const struct xdr_netobj *inkey,
		       struct xdr_netobj *outkey,
		       const struct xdr_netobj *label,
		       gfp_t gfp_mask);

int krb5_derive_key_v2(const struct gss_krb5_enctype *gk5e,
		       const struct xdr_netobj *inkey,
		       struct xdr_netobj *outkey,
		       const struct xdr_netobj *label,
		       gfp_t gfp_mask);

int krb5_kdf_hmac_sha2(const struct gss_krb5_enctype *gk5e,
		       const struct xdr_netobj *inkey,
		       struct xdr_netobj *outkey,
		       const struct xdr_netobj *in_constant,
		       gfp_t gfp_mask);

int krb5_kdf_feedback_cmac(const struct gss_krb5_enctype *gk5e,
			   const struct xdr_netobj *inkey,
			   struct xdr_netobj *outkey,
			   const struct xdr_netobj *in_constant,
			   gfp_t gfp_mask);

/**
 * krb5_derive_key - Derive a subkey from a protocol key
 * @kctx: Kerberos 5 context
 * @inkey: base protocol key
 * @outkey: OUT: derived key
 * @usage: key usage value
 * @seed: key usage seed (one octet)
 * @gfp_mask: memory allocation control flags
 *
 * Caller sets @outkey->len to the desired length of the derived key.
 *
 * On success, returns 0 and fills in @outkey. A negative errno value
 * is returned on failure.
 */
static inline int krb5_derive_key(struct krb5_ctx *kctx,
				  const struct xdr_netobj *inkey,
				  struct xdr_netobj *outkey,
				  u32 usage, u8 seed, gfp_t gfp_mask)
{
	const struct gss_krb5_enctype *gk5e = kctx->gk5e;
	u8 label_data[GSS_KRB5_K5CLENGTH];
	struct xdr_netobj label = {
		.len	= sizeof(label_data),
		.data	= label_data,
	};
	__be32 *p = (__be32 *)label_data;

	*p = cpu_to_be32(usage);
	label_data[4] = seed;
	return gk5e->derive_key(gk5e, inkey, outkey, &label, gfp_mask);
}

void krb5_make_confounder(u8 *p, int conflen);

u32 gss_krb5_checksum(struct crypto_ahash *tfm, char *header, int hdrlen,
		      const struct xdr_buf *body, int body_offset,
		      struct xdr_netobj *cksumout);

u32 krb5_encrypt(struct crypto_sync_skcipher *key, void *iv, void *in,
		 void *out, int length);

u32 krb5_decrypt(struct crypto_sync_skcipher *key, void *iv, void *in,
		 void *out, int length);

u32 gss_krb5_aes_encrypt(struct krb5_ctx *kctx, u32 offset,
			 struct xdr_buf *buf, struct page **pages);

u32 gss_krb5_aes_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
			 struct xdr_buf *buf, u32 *plainoffset, u32 *plainlen);

u32 krb5_etm_encrypt(struct krb5_ctx *kctx, u32 offset, struct xdr_buf *buf,
		     struct page **pages);

u32 krb5_etm_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
		     struct xdr_buf *buf, u32 *headskip, u32 *tailskip);

#endif /* _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H */
