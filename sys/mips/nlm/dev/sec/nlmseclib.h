/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NLMSECLIB_H_
#define	_NLMSECLIB_H_

/*
 * Cryptographic parameter definitions
 */
#define	XLP_SEC_DES_KEY_LENGTH		8	/* Bytes */
#define	XLP_SEC_3DES_KEY_LENGTH		24	/* Bytes */
#define	XLP_SEC_AES128_KEY_LENGTH	16	/* Bytes */
#define	XLP_SEC_AES192_KEY_LENGTH	24	/* Bytes */
#define	XLP_SEC_AES256_KEY_LENGTH	32	/* Bytes */
#define	XLP_SEC_AES128F8_KEY_LENGTH	32	/* Bytes */
#define	XLP_SEC_AES192F8_KEY_LENGTH	48	/* Bytes */
#define	XLP_SEC_AES256F8_KEY_LENGTH	64	/* Bytes */
#define	XLP_SEC_KASUMI_F8_KEY_LENGTH	16	/* Bytes */
#define	XLP_SEC_MAX_CRYPT_KEY_LENGTH	XLP_SEC_AES256F8_KEY_LENGTH


#define	XLP_SEC_DES_IV_LENGTH		8	/* Bytes */
#define	XLP_SEC_AES_IV_LENGTH		16	/* Bytes */
#define	XLP_SEC_ARC4_IV_LENGTH		0	/* Bytes */
#define	XLP_SEC_KASUMI_F8_IV_LENGTH	16	/* Bytes */
#define	XLP_SEC_MAX_IV_LENGTH		16	/* Bytes */
#define	XLP_SEC_IV_LENGTH_BYTES		8	/* Bytes */

#define	XLP_SEC_AES_BLOCK_SIZE		16	/* Bytes */
#define	XLP_SEC_DES_BLOCK_SIZE		8	/* Bytes */
#define	XLP_SEC_3DES_BLOCK_SIZE		8	/* Bytes */

#define	XLP_SEC_MD5_BLOCK_SIZE		64	/* Bytes */
#define	XLP_SEC_SHA1_BLOCK_SIZE		64	/* Bytes */
#define	XLP_SEC_SHA256_BLOCK_SIZE	64	/* Bytes */
#define	XLP_SEC_SHA384_BLOCK_SIZE	128	/* Bytes */
#define	XLP_SEC_SHA512_BLOCK_SIZE	128	/* Bytes */
#define	XLP_SEC_GCM_BLOCK_SIZE		16	/* XXX: Bytes */
#define	XLP_SEC_KASUMI_F9_BLOCK_SIZE	16	/* XXX: Bytes */
#define	XLP_SEC_MAX_BLOCK_SIZE		64	/* Max of MD5/SHA */
#define	XLP_SEC_MD5_LENGTH		16	/* Bytes */
#define	XLP_SEC_SHA1_LENGTH		20	/* Bytes */
#define	XLP_SEC_SHA256_LENGTH		32	/* Bytes */
#define	XLP_SEC_SHA384_LENGTH		64	/* Bytes */
#define	XLP_SEC_SHA512_LENGTH		64	/* Bytes */
#define	XLP_SEC_GCM_LENGTH		16	/* Bytes */
#define	XLP_SEC_KASUMI_F9_LENGTH	16	/* Bytes */
#define	XLP_SEC_KASUMI_F9_RESULT_LENGTH	4	/* Bytes */
#define	XLP_SEC_HMAC_LENGTH		64	/* Max of MD5/SHA/SHA256 */
#define	XLP_SEC_MAX_AUTH_KEY_LENGTH	XLP_SEC_SHA512_BLOCK_SIZE
#define	XLP_SEC_MAX_RC4_STATE_SIZE	264	/* char s[256], int i, int j */

#define	CRYPTO_ERROR(msg1)	((unsigned int)msg1)

#define	NLM_CRYPTO_LEFT_REQS (CMS_DEFAULT_CREDIT/2)
#define	NLM_CRYPTO_NUM_SEGS_REQD(__bufsize)				\
	((__bufsize + NLM_CRYPTO_MAX_SEG_LEN - 1) / NLM_CRYPTO_MAX_SEG_LEN)

#define	NLM_CRYPTO_PKT_DESC_SIZE(nsegs) (32 + (nsegs * 16))

extern unsigned int creditleft;

struct xlp_sec_command {
	struct cryptop *crp;
	struct cryptodesc *enccrd, *maccrd;
	struct xlp_sec_session *ses;
	struct nlm_crypto_pkt_ctrl *ctrlp;
	struct nlm_crypto_pkt_param *paramp;
	void *iv;
	uint8_t des3key[24];
	uint8_t *hashdest;
	uint8_t hashsrc;
	uint8_t hmacpad;
	uint32_t hashoff;
	uint32_t hashlen;
	uint32_t cipheroff;
	uint32_t cipherlen;
	uint32_t ivoff;
	uint32_t ivlen;
	uint32_t hashalg;
	uint32_t hashmode;
	uint32_t cipheralg;
	uint32_t ciphermode;
	uint32_t nsegs;
	uint32_t hash_dst_len; /* used to store hash alg dst size */
};

struct xlp_sec_session {
	int hs_mlen;
	uint8_t ses_iv[EALG_MAX_BLOCK_LEN];
	struct xlp_sec_command cmd;
};

/*
 * Holds data specific to nlm security accelerators
 */
struct xlp_sec_softc {
	device_t sc_dev;	/* device backpointer */
	uint64_t sec_base;
	int32_t sc_cid;
	int sc_needwakeup;
	uint32_t sec_vc_start;
	uint32_t sec_vc_end;
	uint32_t sec_msgsz;
};

#ifdef NLM_SEC_DEBUG
void	print_crypto_params(struct xlp_sec_command *cmd, struct nlm_fmn_msg m);
void	xlp_sec_print_data(struct cryptop *crp);
void	print_cmd(struct xlp_sec_command *cmd);
#endif
int	nlm_crypto_form_srcdst_segs(struct xlp_sec_command *cmd);
int	nlm_crypto_do_cipher(struct xlp_sec_softc *sc,
	    struct xlp_sec_command *cmd);
int	nlm_crypto_do_digest(struct xlp_sec_softc *sc,
	    struct xlp_sec_command *cmd);
int	nlm_crypto_do_cipher_digest(struct xlp_sec_softc *sc,
	    struct xlp_sec_command *cmd);
int	nlm_get_digest_param(struct xlp_sec_command *cmd);
int	nlm_get_cipher_param(struct xlp_sec_command *cmd);

#endif /* _NLMSECLIB_H_ */
