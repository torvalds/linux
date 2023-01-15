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

void krb5_make_confounder(u8 *p, int conflen);

u32 gss_krb5_checksum(struct crypto_ahash *tfm, char *header, int hdrlen,
		      const struct xdr_buf *body, int body_offset,
		      struct xdr_netobj *cksumout);

u32 krb5_encrypt(struct crypto_sync_skcipher *key, void *iv, void *in,
		 void *out, int length);

u32 krb5_decrypt(struct crypto_sync_skcipher *key, void *iv, void *in,
		 void *out, int length);

#endif /* _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H */
