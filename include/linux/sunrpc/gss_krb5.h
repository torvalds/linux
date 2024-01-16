/*
 *  linux/include/linux/sunrpc/gss_krb5_types.h
 *
 *  Adapted from MIT Kerberos 5-1.2.1 lib/include/krb5.h,
 *  lib/gssapi/krb5/gssapiP_krb5.h, and others
 *
 *  Copyright (c) 2000-2008 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 *  Bruce Fields   <bfields@umich.edu>
 */

/*
 * Copyright 1995 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 */

#include <crypto/skcipher.h>
#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/gss_err.h>
#include <linux/sunrpc/gss_asn1.h>

/* Length of constant used in key derivation */
#define GSS_KRB5_K5CLENGTH (5)

/* Maximum key length (in bytes) for the supported crypto algorithms*/
#define GSS_KRB5_MAX_KEYLEN (32)

/* Maximum checksum function output for the supported crypto algorithms */
#define GSS_KRB5_MAX_CKSUM_LEN  (20)

/* Maximum blocksize for the supported crypto algorithms */
#define GSS_KRB5_MAX_BLOCKSIZE  (16)

struct krb5_ctx;

struct gss_krb5_enctype {
	const u32		etype;		/* encryption (key) type */
	const u32		ctype;		/* checksum type */
	const char		*name;		/* "friendly" name */
	const char		*encrypt_name;	/* crypto encrypt name */
	const char		*cksum_name;	/* crypto checksum name */
	const u16		signalg;	/* signing algorithm */
	const u16		sealalg;	/* sealing algorithm */
	const u32		blocksize;	/* encryption blocksize */
	const u32		conflen;	/* confounder length
						   (normally the same as
						   the blocksize) */
	const u32		cksumlength;	/* checksum length */
	const u32		keyed_cksum;	/* is it a keyed cksum? */
	const u32		keybytes;	/* raw key len, in bytes */
	const u32		keylength;	/* final key len, in bytes */
	u32 (*encrypt) (struct crypto_sync_skcipher *tfm,
			void *iv, void *in, void *out,
			int length);		/* encryption function */
	u32 (*decrypt) (struct crypto_sync_skcipher *tfm,
			void *iv, void *in, void *out,
			int length);		/* decryption function */
	u32 (*mk_key) (const struct gss_krb5_enctype *gk5e,
		       struct xdr_netobj *in,
		       struct xdr_netobj *out);	/* complete key generation */
	u32 (*encrypt_v2) (struct krb5_ctx *kctx, u32 offset,
			   struct xdr_buf *buf,
			   struct page **pages); /* v2 encryption function */
	u32 (*decrypt_v2) (struct krb5_ctx *kctx, u32 offset, u32 len,
			   struct xdr_buf *buf, u32 *headskip,
			   u32 *tailskip);	/* v2 decryption function */
};

/* krb5_ctx flags definitions */
#define KRB5_CTX_FLAG_INITIATOR         0x00000001
#define KRB5_CTX_FLAG_CFX               0x00000002
#define KRB5_CTX_FLAG_ACCEPTOR_SUBKEY   0x00000004

struct krb5_ctx {
	int			initiate; /* 1 = initiating, 0 = accepting */
	u32			enctype;
	u32			flags;
	const struct gss_krb5_enctype *gk5e; /* enctype-specific info */
	struct crypto_sync_skcipher *enc;
	struct crypto_sync_skcipher *seq;
	struct crypto_sync_skcipher *acceptor_enc;
	struct crypto_sync_skcipher *initiator_enc;
	struct crypto_sync_skcipher *acceptor_enc_aux;
	struct crypto_sync_skcipher *initiator_enc_aux;
	u8			Ksess[GSS_KRB5_MAX_KEYLEN]; /* session key */
	u8			cksum[GSS_KRB5_MAX_KEYLEN];
	atomic_t		seq_send;
	atomic64_t		seq_send64;
	time64_t		endtime;
	struct xdr_netobj	mech_used;
	u8			initiator_sign[GSS_KRB5_MAX_KEYLEN];
	u8			acceptor_sign[GSS_KRB5_MAX_KEYLEN];
	u8			initiator_seal[GSS_KRB5_MAX_KEYLEN];
	u8			acceptor_seal[GSS_KRB5_MAX_KEYLEN];
	u8			initiator_integ[GSS_KRB5_MAX_KEYLEN];
	u8			acceptor_integ[GSS_KRB5_MAX_KEYLEN];
};

/* The length of the Kerberos GSS token header */
#define GSS_KRB5_TOK_HDR_LEN	(16)

#define KG_TOK_MIC_MSG    0x0101
#define KG_TOK_WRAP_MSG   0x0201

#define KG2_TOK_INITIAL     0x0101
#define KG2_TOK_RESPONSE    0x0202
#define KG2_TOK_MIC         0x0404
#define KG2_TOK_WRAP        0x0504

#define KG2_TOKEN_FLAG_SENTBYACCEPTOR   0x01
#define KG2_TOKEN_FLAG_SEALED           0x02
#define KG2_TOKEN_FLAG_ACCEPTORSUBKEY   0x04

#define KG2_RESP_FLAG_ERROR             0x0001
#define KG2_RESP_FLAG_DELEG_OK          0x0002

enum sgn_alg {
	SGN_ALG_DES_MAC_MD5 = 0x0000,
	SGN_ALG_MD2_5 = 0x0001,
	SGN_ALG_DES_MAC = 0x0002,
	SGN_ALG_3 = 0x0003,		/* not published */
	SGN_ALG_HMAC_SHA1_DES3_KD = 0x0004
};
enum seal_alg {
	SEAL_ALG_NONE = 0xffff,
	SEAL_ALG_DES = 0x0000,
	SEAL_ALG_1 = 0x0001,		/* not published */
	SEAL_ALG_DES3KD = 0x0002
};

#define CKSUMTYPE_CRC32			0x0001
#define CKSUMTYPE_RSA_MD4		0x0002
#define CKSUMTYPE_RSA_MD4_DES		0x0003
#define CKSUMTYPE_DESCBC		0x0004
#define CKSUMTYPE_RSA_MD5		0x0007
#define CKSUMTYPE_RSA_MD5_DES		0x0008
#define CKSUMTYPE_NIST_SHA		0x0009
#define CKSUMTYPE_HMAC_SHA1_DES3	0x000c
#define CKSUMTYPE_HMAC_SHA1_96_AES128   0x000f
#define CKSUMTYPE_HMAC_SHA1_96_AES256   0x0010
#define CKSUMTYPE_HMAC_MD5_ARCFOUR      -138 /* Microsoft md5 hmac cksumtype */

/* from gssapi_err_krb5.h */
#define KG_CCACHE_NOMATCH                        (39756032L)
#define KG_KEYTAB_NOMATCH                        (39756033L)
#define KG_TGT_MISSING                           (39756034L)
#define KG_NO_SUBKEY                             (39756035L)
#define KG_CONTEXT_ESTABLISHED                   (39756036L)
#define KG_BAD_SIGN_TYPE                         (39756037L)
#define KG_BAD_LENGTH                            (39756038L)
#define KG_CTX_INCOMPLETE                        (39756039L)
#define KG_CONTEXT                               (39756040L)
#define KG_CRED                                  (39756041L)
#define KG_ENC_DESC                              (39756042L)
#define KG_BAD_SEQ                               (39756043L)
#define KG_EMPTY_CCACHE                          (39756044L)
#define KG_NO_CTYPES                             (39756045L)

/* per Kerberos v5 protocol spec crypto types from the wire. 
 * these get mapped to linux kernel crypto routines.  
 */
#define ENCTYPE_NULL            0x0000
#define ENCTYPE_DES_CBC_CRC     0x0001	/* DES cbc mode with CRC-32 */
#define ENCTYPE_DES_CBC_MD4     0x0002	/* DES cbc mode with RSA-MD4 */
#define ENCTYPE_DES_CBC_MD5     0x0003	/* DES cbc mode with RSA-MD5 */
#define ENCTYPE_DES_CBC_RAW     0x0004	/* DES cbc mode raw */
/* XXX deprecated? */
#define ENCTYPE_DES3_CBC_SHA    0x0005	/* DES-3 cbc mode with NIST-SHA */
#define ENCTYPE_DES3_CBC_RAW    0x0006	/* DES-3 cbc mode raw */
#define ENCTYPE_DES_HMAC_SHA1   0x0008
#define ENCTYPE_DES3_CBC_SHA1   0x0010
#define ENCTYPE_AES128_CTS_HMAC_SHA1_96 0x0011
#define ENCTYPE_AES256_CTS_HMAC_SHA1_96 0x0012
#define ENCTYPE_ARCFOUR_HMAC            0x0017
#define ENCTYPE_ARCFOUR_HMAC_EXP        0x0018
#define ENCTYPE_UNKNOWN         0x01ff

/*
 * Constants used for key derivation
 */
/* for 3DES */
#define KG_USAGE_SEAL (22)
#define KG_USAGE_SIGN (23)
#define KG_USAGE_SEQ  (24)

/* from rfc3961 */
#define KEY_USAGE_SEED_CHECKSUM         (0x99)
#define KEY_USAGE_SEED_ENCRYPTION       (0xAA)
#define KEY_USAGE_SEED_INTEGRITY        (0x55)

/* from rfc4121 */
#define KG_USAGE_ACCEPTOR_SEAL  (22)
#define KG_USAGE_ACCEPTOR_SIGN  (23)
#define KG_USAGE_INITIATOR_SEAL (24)
#define KG_USAGE_INITIATOR_SIGN (25)

/*
 * This compile-time check verifies that we will not exceed the
 * slack space allotted by the client and server auth_gss code
 * before they call gss_wrap().
 */
#define GSS_KRB5_MAX_SLACK_NEEDED \
	(GSS_KRB5_TOK_HDR_LEN     /* gss token header */         \
	+ GSS_KRB5_MAX_CKSUM_LEN  /* gss token checksum */       \
	+ GSS_KRB5_MAX_BLOCKSIZE  /* confounder */               \
	+ GSS_KRB5_MAX_BLOCKSIZE  /* possible padding */         \
	+ GSS_KRB5_TOK_HDR_LEN    /* encrypted hdr in v2 token */\
	+ GSS_KRB5_MAX_CKSUM_LEN  /* encryption hmac */          \
	+ 4 + 4                   /* RPC verifier */             \
	+ GSS_KRB5_TOK_HDR_LEN                                   \
	+ GSS_KRB5_MAX_CKSUM_LEN)

u32
make_checksum(struct krb5_ctx *kctx, char *header, int hdrlen,
		struct xdr_buf *body, int body_offset, u8 *cksumkey,
		unsigned int usage, struct xdr_netobj *cksumout);

u32
make_checksum_v2(struct krb5_ctx *, char *header, int hdrlen,
		 struct xdr_buf *body, int body_offset, u8 *key,
		 unsigned int usage, struct xdr_netobj *cksum);

u32 gss_get_mic_kerberos(struct gss_ctx *, struct xdr_buf *,
		struct xdr_netobj *);

u32 gss_verify_mic_kerberos(struct gss_ctx *, struct xdr_buf *,
		struct xdr_netobj *);

u32
gss_wrap_kerberos(struct gss_ctx *ctx_id, int offset,
		struct xdr_buf *outbuf, struct page **pages);

u32
gss_unwrap_kerberos(struct gss_ctx *ctx_id, int offset, int len,
		struct xdr_buf *buf);


u32
krb5_encrypt(struct crypto_sync_skcipher *key,
	     void *iv, void *in, void *out, int length);

u32
krb5_decrypt(struct crypto_sync_skcipher *key,
	     void *iv, void *in, void *out, int length); 

int
gss_encrypt_xdr_buf(struct crypto_sync_skcipher *tfm, struct xdr_buf *outbuf,
		    int offset, struct page **pages);

int
gss_decrypt_xdr_buf(struct crypto_sync_skcipher *tfm, struct xdr_buf *inbuf,
		    int offset);

s32
krb5_make_seq_num(struct krb5_ctx *kctx,
		struct crypto_sync_skcipher *key,
		int direction,
		u32 seqnum, unsigned char *cksum, unsigned char *buf);

s32
krb5_get_seq_num(struct krb5_ctx *kctx,
	       unsigned char *cksum,
	       unsigned char *buf, int *direction, u32 *seqnum);

int
xdr_extend_head(struct xdr_buf *buf, unsigned int base, unsigned int shiftlen);

u32
krb5_derive_key(const struct gss_krb5_enctype *gk5e,
		const struct xdr_netobj *inkey,
		struct xdr_netobj *outkey,
		const struct xdr_netobj *in_constant,
		gfp_t gfp_mask);

u32
gss_krb5_des3_make_key(const struct gss_krb5_enctype *gk5e,
		       struct xdr_netobj *randombits,
		       struct xdr_netobj *key);

u32
gss_krb5_aes_make_key(const struct gss_krb5_enctype *gk5e,
		      struct xdr_netobj *randombits,
		      struct xdr_netobj *key);

u32
gss_krb5_aes_encrypt(struct krb5_ctx *kctx, u32 offset,
		     struct xdr_buf *buf,
		     struct page **pages);

u32
gss_krb5_aes_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
		     struct xdr_buf *buf, u32 *plainoffset,
		     u32 *plainlen);

void
gss_krb5_make_confounder(char *p, u32 conflen);
