/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __T4_CRYPTO_H__
#define	__T4_CRYPTO_H__

/* From chr_core.h */
#define PAD_ERROR_BIT		1
#define CHK_PAD_ERR_BIT(x)	(((x) >> PAD_ERROR_BIT) & 1)

#define MAC_ERROR_BIT		0
#define CHK_MAC_ERR_BIT(x)	(((x) >> MAC_ERROR_BIT) & 1)
#define MAX_SALT                4

struct _key_ctx {
	__be32 ctx_hdr;
	u8 salt[MAX_SALT];
	__be64 reserverd;
	unsigned char key[0];
};

struct chcr_wr {
	struct fw_crypto_lookaside_wr wreq;
	struct ulp_txpkt ulptx;
	struct ulptx_idata sc_imm;
	struct cpl_tx_sec_pdu sec_cpl;
	struct _key_ctx key_ctx;
};

/* From chr_algo.h */

/* Crypto key context */
#define S_KEY_CONTEXT_CTX_LEN           24
#define M_KEY_CONTEXT_CTX_LEN           0xff
#define V_KEY_CONTEXT_CTX_LEN(x)        ((x) << S_KEY_CONTEXT_CTX_LEN)
#define G_KEY_CONTEXT_CTX_LEN(x) \
	(((x) >> S_KEY_CONTEXT_CTX_LEN) & M_KEY_CONTEXT_CTX_LEN)

#define S_KEY_CONTEXT_DUAL_CK      12
#define M_KEY_CONTEXT_DUAL_CK      0x1
#define V_KEY_CONTEXT_DUAL_CK(x)   ((x) << S_KEY_CONTEXT_DUAL_CK)
#define G_KEY_CONTEXT_DUAL_CK(x)   \
(((x) >> S_KEY_CONTEXT_DUAL_CK) & M_KEY_CONTEXT_DUAL_CK)
#define F_KEY_CONTEXT_DUAL_CK      V_KEY_CONTEXT_DUAL_CK(1U)

#define S_KEY_CONTEXT_OPAD_PRESENT      11
#define M_KEY_CONTEXT_OPAD_PRESENT      0x1
#define V_KEY_CONTEXT_OPAD_PRESENT(x)   ((x) << S_KEY_CONTEXT_OPAD_PRESENT)
#define G_KEY_CONTEXT_OPAD_PRESENT(x)   \
	(((x) >> S_KEY_CONTEXT_OPAD_PRESENT) & \
	 M_KEY_CONTEXT_OPAD_PRESENT)
#define F_KEY_CONTEXT_OPAD_PRESENT      V_KEY_CONTEXT_OPAD_PRESENT(1U)

#define S_KEY_CONTEXT_SALT_PRESENT      10
#define M_KEY_CONTEXT_SALT_PRESENT      0x1
#define V_KEY_CONTEXT_SALT_PRESENT(x)   ((x) << S_KEY_CONTEXT_SALT_PRESENT)
#define G_KEY_CONTEXT_SALT_PRESENT(x)   \
	(((x) >> S_KEY_CONTEXT_SALT_PRESENT) & \
	 M_KEY_CONTEXT_SALT_PRESENT)
#define F_KEY_CONTEXT_SALT_PRESENT      V_KEY_CONTEXT_SALT_PRESENT(1U)

#define S_KEY_CONTEXT_CK_SIZE           6
#define M_KEY_CONTEXT_CK_SIZE           0xf
#define V_KEY_CONTEXT_CK_SIZE(x)        ((x) << S_KEY_CONTEXT_CK_SIZE)
#define G_KEY_CONTEXT_CK_SIZE(x)        \
	(((x) >> S_KEY_CONTEXT_CK_SIZE) & M_KEY_CONTEXT_CK_SIZE)

#define S_KEY_CONTEXT_MK_SIZE           2
#define M_KEY_CONTEXT_MK_SIZE           0xf
#define V_KEY_CONTEXT_MK_SIZE(x)        ((x) << S_KEY_CONTEXT_MK_SIZE)
#define G_KEY_CONTEXT_MK_SIZE(x)        \
	(((x) >> S_KEY_CONTEXT_MK_SIZE) & M_KEY_CONTEXT_MK_SIZE)

#define S_KEY_CONTEXT_VALID     0
#define M_KEY_CONTEXT_VALID     0x1
#define V_KEY_CONTEXT_VALID(x)  ((x) << S_KEY_CONTEXT_VALID)
#define G_KEY_CONTEXT_VALID(x)  \
	(((x) >> S_KEY_CONTEXT_VALID) & \
	 M_KEY_CONTEXT_VALID)
#define F_KEY_CONTEXT_VALID     V_KEY_CONTEXT_VALID(1U)

#define CHCR_HASH_MAX_DIGEST_SIZE 64

#define DUMMY_BYTES 16

#define TRANSHDR_SIZE(kctx_len)\
	(sizeof(struct chcr_wr) +\
	 kctx_len)
#define CIPHER_TRANSHDR_SIZE(kctx_len, sge_pairs) \
	(TRANSHDR_SIZE((kctx_len)) + (sge_pairs) +\
	 sizeof(struct cpl_rx_phys_dsgl))
#define HASH_TRANSHDR_SIZE(kctx_len)\
	(TRANSHDR_SIZE(kctx_len) + DUMMY_BYTES)

#define CRYPTO_MAX_IMM_TX_PKT_LEN 256

struct phys_sge_pairs {
	__be16 len[8];
	__be64 addr[8];
};

/* From chr_crypto.h */
#define CHCR_AES_MAX_KEY_LEN  (AES_XTS_MAX_KEY)
#define CHCR_MAX_CRYPTO_IV_LEN 16 /* AES IV len */

#define CHCR_ENCRYPT_OP 0
#define CHCR_DECRYPT_OP 1

#define SCMD_ENCDECCTRL_ENCRYPT 0
#define SCMD_ENCDECCTRL_DECRYPT 1

#define SCMD_PROTO_VERSION_TLS_1_2 0
#define SCMD_PROTO_VERSION_TLS_1_1 1
#define SCMD_PROTO_VERSION_GENERIC 4

#define SCMD_CIPH_MODE_NOP               0
#define SCMD_CIPH_MODE_AES_CBC           1
#define SCMD_CIPH_MODE_AES_GCM           2
#define SCMD_CIPH_MODE_AES_CTR           3
#define SCMD_CIPH_MODE_GENERIC_AES       4
#define SCMD_CIPH_MODE_AES_XTS           6
#define SCMD_CIPH_MODE_AES_CCM           7

#define SCMD_AUTH_MODE_NOP             0
#define SCMD_AUTH_MODE_SHA1            1
#define SCMD_AUTH_MODE_SHA224          2
#define SCMD_AUTH_MODE_SHA256          3
#define SCMD_AUTH_MODE_GHASH           4
#define SCMD_AUTH_MODE_SHA512_224      5
#define SCMD_AUTH_MODE_SHA512_256      6
#define SCMD_AUTH_MODE_SHA512_384      7
#define SCMD_AUTH_MODE_SHA512_512      8
#define SCMD_AUTH_MODE_CBCMAC          9
#define SCMD_AUTH_MODE_CMAC            10

#define SCMD_HMAC_CTRL_NOP             0
#define SCMD_HMAC_CTRL_NO_TRUNC        1
#define SCMD_HMAC_CTRL_TRUNC_RFC4366   2
#define SCMD_HMAC_CTRL_IPSEC_96BIT     3
#define SCMD_HMAC_CTRL_PL1             4
#define SCMD_HMAC_CTRL_PL2             5
#define SCMD_HMAC_CTRL_PL3             6
#define SCMD_HMAC_CTRL_DIV2            7

/* This are not really mac key size. They are intermediate values
 * of sha engine and its size
 */
#define CHCR_KEYCTX_MAC_KEY_SIZE_128        0
#define CHCR_KEYCTX_MAC_KEY_SIZE_160        1
#define CHCR_KEYCTX_MAC_KEY_SIZE_192        2
#define CHCR_KEYCTX_MAC_KEY_SIZE_256        3
#define CHCR_KEYCTX_MAC_KEY_SIZE_512        4
#define CHCR_KEYCTX_CIPHER_KEY_SIZE_128     0
#define CHCR_KEYCTX_CIPHER_KEY_SIZE_192     1
#define CHCR_KEYCTX_CIPHER_KEY_SIZE_256     2
#define CHCR_KEYCTX_NO_KEY                  15

#define IV_NOP                  0
#define IV_IMMEDIATE            1
#define IV_DSGL			2

#define CHCR_HASH_MAX_BLOCK_SIZE_64  64
#define CHCR_HASH_MAX_BLOCK_SIZE_128 128

#endif /* !__T4_CRYPTO_H__ */
