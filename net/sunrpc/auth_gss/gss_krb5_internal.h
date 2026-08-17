/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * SunRPC GSS Kerberos 5 mechanism internal definitions
 *
 * Copyright (c) 2022 Oracle and/or its affiliates.
 */

#ifndef _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H
#define _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H

#include <crypto/krb5.h>

struct krb5_ctx;

/* krb5_ctx flags definitions */
#define KRB5_CTX_FLAG_INITIATOR         0x00000001
#define KRB5_CTX_FLAG_ACCEPTOR_SUBKEY   0x00000004

struct krb5_ctx {
	int			initiate; /* 1 = initiating, 0 = accepting */
	u32			enctype;
	u32			flags;
	const struct krb5_enctype *krb5e; /* crypto/krb5 enctype */
	struct crypto_aead	*initiator_enc_aead;
	struct crypto_aead	*acceptor_enc_aead;
	struct crypto_shash	*initiator_sign_shash;
	struct crypto_shash	*acceptor_sign_shash;
	u8			Ksess[GSS_KRB5_MAX_KEYLEN]; /* session key */
	atomic64_t		seq_send64;
	time64_t		endtime;
	struct xdr_netobj	mech_used;
};

/*
 * GSS Kerberos 5 mechanism Per-Message calls.
 */

u32 gss_krb5_get_mic_v2(struct krb5_ctx *ctx, struct xdr_buf *text,
			struct xdr_netobj *token);

u32 gss_krb5_verify_mic_v2(struct krb5_ctx *ctx, struct xdr_buf *message_buffer,
			   struct xdr_netobj *read_token);

u32 gss_krb5_wrap_v2(struct krb5_ctx *kctx, int offset,
		     struct xdr_buf *buf, struct page **pages);

u32 gss_krb5_unwrap_v2(struct krb5_ctx *kctx, int offset, int len,
		       struct xdr_buf *buf, unsigned int *slack,
		       unsigned int *align);

/*
 * Implementation internal functions
 */

int xdr_extend_head(struct xdr_buf *buf, unsigned int base,
		    unsigned int shiftlen);

u32 gss_krb5_errno_to_status(int err);

int gss_krb5_mic_build_sg(const struct xdr_buf *body,
			  void *cksum, unsigned int cksum_len,
			  void *hdr,
			  struct scatterlist *sg_head,
			  struct scatterlist **sg_overflow);

u32 gss_krb5_aead_encrypt(struct krb5_ctx *kctx, u32 offset,
			  struct xdr_buf *buf, struct page **pages);
u32 gss_krb5_aead_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
			  struct xdr_buf *buf, u32 *headskip, u32 *tailskip);


#endif /* _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H */
