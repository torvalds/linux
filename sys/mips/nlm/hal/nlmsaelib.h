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

#ifndef _NLM_HAL_CRYPTO_H_
#define _NLM_HAL_CRYPTO_H_

#define	SAE_CFG_REG		0x00
#define SAE_ENG_SEL_0		0x01
#define SAE_ENG_SEL_1		0x02
#define SAE_ENG_SEL_2		0x03
#define SAE_ENG_SEL_3		0x04
#define SAE_ENG_SEL_4		0x05
#define SAE_ENG_SEL_5		0x06
#define SAE_ENG_SEL_6		0x07
#define SAE_ENG_SEL_7		0x08

#define	RSA_CFG_REG		0x00
#define RSA_ENG_SEL_0		0x01
#define RSA_ENG_SEL_1		0x02
#define RSA_ENG_SEL_2		0x03

#define nlm_read_sec_reg(b, r)		nlm_read_reg(b, r)
#define nlm_write_sec_reg(b, r, v)	nlm_write_reg(b, r, v)
#define nlm_get_sec_pcibase(node)	nlm_pcicfg_base(XLP_IO_SEC_OFFSET(node))
#define nlm_get_sec_regbase(node)        \
                        (nlm_get_sec_pcibase(node) + XLP_IO_PCI_HDRSZ)

#define nlm_read_rsa_reg(b, r)		nlm_read_reg(b, r)
#define nlm_write_rsa_reg(b, r, v)	nlm_write_reg(b, r, v)
#define nlm_get_rsa_pcibase(node)	nlm_pcicfg_base(XLP_IO_RSA_OFFSET(node))
#define nlm_get_rsa_regbase(node)        \
                        (nlm_get_rsa_pcibase(node) + XLP_IO_PCI_HDRSZ)

#define nlm_pcibase_sec(node)     nlm_pcicfg_base(XLP_IO_SEC_OFFSET(node))
#define nlm_qidstart_sec(node)    nlm_qidstart_kseg(nlm_pcibase_sec(node))
#define nlm_qnum_sec(node)        nlm_qnum_kseg(nlm_pcibase_sec(node))

/*
 * Since buffer allocation for crypto at kernel is done as malloc, each
 * segment size is given as page size which is 4K by default
 */
#define NLM_CRYPTO_MAX_SEG_LEN	PAGE_SIZE

#define MAX_KEY_LEN_IN_DW		20

#define left_shift64(x, bitshift, numofbits)			\
    ((uint64_t)(x) << (bitshift))

#define left_shift64_mask(x, bitshift, numofbits)			\
    (((uint64_t)(x) & ((1ULL << (numofbits)) - 1)) << (bitshift))

/**
* @brief cipher algorithms
* @ingroup crypto
*/
enum nlm_cipher_algo {
	NLM_CIPHER_BYPASS = 0,
	NLM_CIPHER_DES = 1,
	NLM_CIPHER_3DES = 2,
	NLM_CIPHER_AES128 = 3,
	NLM_CIPHER_AES192 = 4,
	NLM_CIPHER_AES256 = 5,
	NLM_CIPHER_ARC4 = 6,
	NLM_CIPHER_KASUMI_F8 = 7,
	NLM_CIPHER_SNOW3G_F8 = 8,
	NLM_CIPHER_CAMELLIA128 = 9,
	NLM_CIPHER_CAMELLIA192 = 0xA,
	NLM_CIPHER_CAMELLIA256 = 0xB,
	NLM_CIPHER_MAX = 0xC,
};

/**
* @brief cipher modes
* @ingroup crypto
*/
enum nlm_cipher_mode {
	NLM_CIPHER_MODE_ECB = 0,
	NLM_CIPHER_MODE_CBC = 1,
	NLM_CIPHER_MODE_CFB = 2,
	NLM_CIPHER_MODE_OFB = 3,
	NLM_CIPHER_MODE_CTR = 4,
	NLM_CIPHER_MODE_AES_F8 = 5,
	NLM_CIPHER_MODE_GCM = 6,
	NLM_CIPHER_MODE_CCM = 7,
	NLM_CIPHER_MODE_UNDEFINED1 = 8,
	NLM_CIPHER_MODE_UNDEFINED2 = 9,
	NLM_CIPHER_MODE_LRW = 0xA,
	NLM_CIPHER_MODE_XTS = 0xB,
	NLM_CIPHER_MODE_MAX = 0xC,
};

/**
* @brief hash algorithms
* @ingroup crypto
*/
enum nlm_hash_algo {
	NLM_HASH_BYPASS = 0,
	NLM_HASH_MD5 = 1,
	NLM_HASH_SHA = 2,
	NLM_HASH_UNDEFINED = 3,
	NLM_HASH_AES128 = 4,
	NLM_HASH_AES192 = 5,
	NLM_HASH_AES256 = 6,
	NLM_HASH_KASUMI_F9 = 7,
	NLM_HASH_SNOW3G_F9 = 8,
	NLM_HASH_CAMELLIA128 = 9,
	NLM_HASH_CAMELLIA192 = 0xA,
	NLM_HASH_CAMELLIA256 = 0xB,
	NLM_HASH_GHASH = 0xC,
	NLM_HASH_MAX = 0xD
};

/**
* @brief hash modes
* @ingroup crypto
*/
enum nlm_hash_mode {
	NLM_HASH_MODE_SHA1 = 0,	/* Only SHA */
	NLM_HASH_MODE_SHA224 = 1,	/* Only SHA */
	NLM_HASH_MODE_SHA256 = 2,	/* Only SHA */
	NLM_HASH_MODE_SHA384 = 3,	/* Only SHA */
	NLM_HASH_MODE_SHA512 = 4,	/* Only SHA */
	NLM_HASH_MODE_CMAC = 5,	/* AES and Camellia */
	NLM_HASH_MODE_XCBC = 6,	/* AES and Camellia */
	NLM_HASH_MODE_CBC_MAC = 7,	/* AES and Camellia */
	NLM_HASH_MODE_CCM = 8,	/* AES */
	NLM_HASH_MODE_GCM = 9,	/* AES */
	NLM_HASH_MODE_MAX = 0xA,
};

/**
* @brief crypto control descriptor, should be cache aligned
* @ingroup crypto
*/
struct nlm_crypto_pkt_ctrl {
	uint64_t desc0;
	/* combination of cipher and hash keys */
	uint64_t key[MAX_KEY_LEN_IN_DW];
	uint32_t cipherkeylen;
	uint32_t hashkeylen;
	uint32_t taglen;
};

/**
* @brief crypto packet descriptor, should be cache aligned
* @ingroup crypto
*/
struct nlm_crypto_pkt_param {
	uint64_t desc0;
	uint64_t desc1;
	uint64_t desc2;
	uint64_t desc3;
	uint64_t segment[1][2];
};

static __inline__ uint64_t
nlm_crypto_form_rsa_ecc_fmn_entry0(unsigned int l3alloc, unsigned int type,
    unsigned int func, uint64_t srcaddr)
{
	return (left_shift64(l3alloc, 61, 1) |
	    left_shift64(type, 46, 7) |
	    left_shift64(func, 40, 6) |
	    left_shift64(srcaddr, 0, 40));
}

static __inline__ uint64_t
nlm_crypto_form_rsa_ecc_fmn_entry1(unsigned int dstclobber,
    unsigned int l3alloc, unsigned int fbvc, uint64_t dstaddr)
{
	return (left_shift64(dstclobber, 62, 1) |
	    left_shift64(l3alloc, 61, 1) |
	    left_shift64(fbvc, 40, 12) |
	    left_shift64(dstaddr, 0, 40));
}

/**
* @brief Generate cypto control descriptor
* @ingroup crypto
* hmac : 1 for hash with hmac
* hashalg, see hash_alg enums
* hashmode, see hash_mode enums
* cipherhalg, see  cipher_alg enums
* ciphermode, see  cipher_mode enums
* arc4_cipherkeylen : length of arc4 cipher key, 0 is interpreted as 32
* arc4_keyinit :
* cfbmask : cipher text for feedback,
*           0(1 bit), 1(2 bits), 2(4 bits), 3(8 bits), 4(16bits), 5(32 bits),
*           6(64 bits), 7(128 bits)
*/
static __inline__ uint64_t
nlm_crypto_form_pkt_ctrl_desc(unsigned int hmac, unsigned int hashalg,
    unsigned int hashmode, unsigned int cipheralg, unsigned int ciphermode,
    unsigned int arc4_cipherkeylen, unsigned int arc4_keyinit,
    unsigned int cfbmask)
{
	return (left_shift64(hmac, 61, 1) |
	    left_shift64(hashalg, 52, 8) |
	    left_shift64(hashmode, 43, 8) |
	    left_shift64(cipheralg, 34, 8) |
	    left_shift64(ciphermode, 25, 8) |
	    left_shift64(arc4_cipherkeylen, 18, 5) |
	    left_shift64(arc4_keyinit, 17, 1) |
	    left_shift64(cfbmask, 0, 3));
}
/**
* @brief Generate cypto packet descriptor 0
* @ingroup crypto
* tls : 1 (tls enabled) 0(tls disabled)
* hash_source : 1 (encrypted data is sent to the auth engine)
*               0 (plain data is sent to the auth engine)
* hashout_l3alloc : 1 (auth output is transited through l3 cache)
* encrypt : 1 (for encrypt) 0 (for decrypt)
* ivlen : iv length in bytes
* hashdst_addr : hash out physical address, byte aligned
*/
static __inline__ uint64_t
nlm_crypto_form_pkt_desc0(unsigned int tls, unsigned int hash_source,
    unsigned int hashout_l3alloc, unsigned int encrypt, unsigned int ivlen,
    uint64_t hashdst_addr)
{
	return (left_shift64(tls, 63, 1) |
	    left_shift64(hash_source, 62, 1) |
	    left_shift64(hashout_l3alloc, 60, 1) |
	    left_shift64(encrypt, 59, 1) |
	    left_shift64_mask((ivlen - 1), 41, 16) |
	    left_shift64(hashdst_addr, 0, 40));
}

/**
* @brief Generate cypto packet descriptor 1
* @ingroup crypto
* cipherlen : cipher length in bytes
* hashlen : hash length in bytes
*/
static __inline__ uint64_t
nlm_crypto_form_pkt_desc1(unsigned int cipherlen, unsigned int hashlen)
{
	return (left_shift64_mask((cipherlen - 1), 32, 32) |
	    left_shift64_mask((hashlen - 1), 0, 32));
}

/**
* @brief Generate cypto packet descriptor 2
* @ingroup crypto
* ivoff : iv offset, offset from start of src data addr
* ciperbit_cnt : number of valid bits in the last input byte to the cipher,
*                0 (8 bits), 1 (1 bit)..7 (7 bits)
* cipheroff : cipher offset, offset from start of src data addr
* hashbit_cnt : number of valid bits in the last input byte to the auth
*              0 (8 bits), 1 (1 bit)..7 (7 bits)
* hashclobber : 1 (hash output will be written as multiples of cachelines, no
*              read modify write)
* hashoff : hash offset, offset from start of src data addr
*/

static __inline__ uint64_t
nlm_crypto_form_pkt_desc2(unsigned int ivoff, unsigned int cipherbit_cnt,
    unsigned int cipheroff, unsigned int hashbit_cnt, unsigned int hashclobber,
    unsigned int hashoff)
{
	return (left_shift64(ivoff , 45, 16) |
	    left_shift64(cipherbit_cnt, 42, 3) |
	    left_shift64(cipheroff, 22, 16) |
	    left_shift64(hashbit_cnt, 19, 3) |
	    left_shift64(hashclobber, 18, 1) |
	    left_shift64(hashoff, 0, 16));
}

/**
* @brief Generate cypto packet descriptor 3
* @ingroup crypto
* designer_vc : designer freeback fmn destination id
* taglen : length in bits of the tag generated by the auth engine
*          md5 (128 bits), sha1 (160), sha224 (224), sha384 (384),
*          sha512 (512), Kasumi (32), snow3g (32), gcm (128)
* hmacpad : 1 if hmac padding is already done
*/
static  __inline__ uint64_t
nlm_crypto_form_pkt_desc3(unsigned int designer_vc, unsigned int taglen,
    unsigned int arc4_state_save_l3, unsigned int arc4_save_state,
    unsigned int hmacpad)
{
	return (left_shift64(designer_vc, 48, 16) |
	    left_shift64(taglen, 11, 16) |
	    left_shift64(arc4_state_save_l3, 8, 1) |
	    left_shift64(arc4_save_state, 6, 1) |
	    left_shift64(hmacpad, 5, 1));
}

/**
* @brief Generate cypto packet descriptor 4
* @ingroup crypto
* srcfraglen : length of the source fragment(header + data + tail) in bytes
* srcfragaddr : physical address of the srouce fragment
*/
static __inline__ uint64_t
nlm_crypto_form_pkt_desc4(uint64_t srcfraglen,
    unsigned int srcfragaddr )
{
	return (left_shift64_mask((srcfraglen - 1), 48, 16) |
	    left_shift64(srcfragaddr, 0, 40));
}

/**
* @brief Generate cypto packet descriptor 5
* @ingroup crypto
* dstfraglen : length of the dst fragment(header + data + tail) in bytes
* chipherout_l3alloc : 1(cipher output is transited through l3 cache)
* cipherclobber : 1 (cipher output will be written as multiples of cachelines,
*                 no read modify write)
* chiperdst_addr : physical address of the cipher destination address
*/
static __inline__ uint64_t
nlm_crypto_form_pkt_desc5(unsigned int dstfraglen,
    unsigned int cipherout_l3alloc, unsigned int cipherclobber,
    uint64_t cipherdst_addr)

{
	return (left_shift64_mask((dstfraglen - 1), 48, 16) |
	    left_shift64(cipherout_l3alloc, 46, 1) |
	    left_shift64(cipherclobber, 41, 1) |
	    left_shift64(cipherdst_addr, 0, 40));
}

/**
  * @brief Generate crypto packet fmn message entry 0
  * @ingroup crypto
  * freeback_vc: freeback response destination address
  * designer_fblen : Designer freeback length, 1 - 4
  * designerdesc_valid : designer desc valid or not
  * cipher_keylen : cipher key length in bytes
  * ctrldesc_addr : physicall address of the control descriptor
  */
static __inline__ uint64_t
nlm_crypto_form_pkt_fmn_entry0(unsigned int freeback_vc,
    unsigned int designer_fblen, unsigned int designerdesc_valid,
    unsigned int cipher_keylen, uint64_t cntldesc_addr)
{
	return (left_shift64(freeback_vc, 48, 16) |
	    left_shift64_mask(designer_fblen - 1, 46, 2) |
	    left_shift64(designerdesc_valid, 45, 1) |
	    left_shift64_mask(((cipher_keylen + 7) >> 3), 40, 5) |
	    left_shift64(cntldesc_addr >> 6, 0, 34));
}

/**
  * @brief Generate crypto packet fmn message entry 1
  * @ingroup crypto
  * arc4load_state : 1 if load state required 0 otherwise
  * hash_keylen : hash key length in bytes
  * pktdesc_size : packet descriptor size in bytes
  * pktdesc_addr : physicall address of the packet descriptor
  */
static __inline__ uint64_t
nlm_crypto_form_pkt_fmn_entry1(unsigned int arc4load_state,
    unsigned int hash_keylen, unsigned int pktdesc_size,
    uint64_t pktdesc_addr)
{
	return (left_shift64(arc4load_state, 63, 1) |
	    left_shift64_mask(((hash_keylen + 7) >> 3), 56, 5) |
	    left_shift64_mask(((pktdesc_size >> 4) - 1), 43, 12) |
	    left_shift64(pktdesc_addr >> 6, 0, 34));
}

static __inline__ int
nlm_crypto_get_hklen_taglen(enum nlm_hash_algo hashalg,
    enum nlm_hash_mode hashmode, unsigned int *taglen, unsigned int *hklen)
{
	if (hashalg == NLM_HASH_MD5) {
		*taglen = 128;
		*hklen  = 64;
	} else if (hashalg == NLM_HASH_SHA) {
		switch (hashmode) {
		case NLM_HASH_MODE_SHA1:
			*taglen = 160;
			*hklen  = 64;
			break;
		case NLM_HASH_MODE_SHA224:
			*taglen = 224;
			*hklen  = 64;
			break;
		case NLM_HASH_MODE_SHA256:
			*taglen = 256;
			*hklen  = 64;
			break;
		case NLM_HASH_MODE_SHA384:
			*taglen = 384;
			*hklen  = 128;
			break;
		case NLM_HASH_MODE_SHA512:
			*taglen = 512;
			*hklen  = 128;
			break;
		default:
			printf("Error : invalid shaid (%s)\n", __func__);
			return (-1);
		}
	} else if (hashalg == NLM_HASH_KASUMI_F9) {
		*taglen = 32;
		*hklen  = 0;
	} else if (hashalg == NLM_HASH_SNOW3G_F9) {
		*taglen = 32;
		*hklen  = 0;
	} else if (hashmode == NLM_HASH_MODE_XCBC) {
		*taglen = 128;
		*hklen  = 0;
	} else if (hashmode == NLM_HASH_MODE_GCM) {
		*taglen = 128;
		*hklen  = 0;
	} else if (hashalg == NLM_HASH_BYPASS) {
		*taglen = 0;
		*hklen  = 0;
	} else {
		printf("Error:Hash alg/mode not found\n");
		return (-1);
	}

	/* TODO : Add remaining cases */
	return (0);
}

/**
* @brief Generate fill cryto control info structure
* @ingroup crypto
* hmac : 1 for hash with hmac
* hashalg: see above,  hash_alg enums
* hashmode: see above, hash_mode enums
* cipherhalg: see above,  cipher_alg enums
* ciphermode: see above, cipher_mode enums
*
*/
static __inline__ int
nlm_crypto_fill_pkt_ctrl(struct nlm_crypto_pkt_ctrl *ctrl, unsigned int hmac,
    enum nlm_hash_algo hashalg, enum nlm_hash_mode hashmode,
    enum nlm_cipher_algo cipheralg, enum nlm_cipher_mode ciphermode,
    unsigned char *cipherkey, unsigned int cipherkeylen,
    unsigned char *hashkey, unsigned int hashkeylen)
{
	unsigned int taglen = 0, hklen = 0;

	ctrl->desc0 = nlm_crypto_form_pkt_ctrl_desc(hmac, hashalg, hashmode,
	    cipheralg, ciphermode, 0, 0, 0);
	memset(ctrl->key, 0, sizeof(ctrl->key));
	if (cipherkey)
		memcpy(ctrl->key, cipherkey, cipherkeylen);
	if (hashkey)
		memcpy((unsigned char *)&ctrl->key[(cipherkeylen + 7) / 8],
			    hashkey, hashkeylen);
	if (nlm_crypto_get_hklen_taglen(hashalg, hashmode, &taglen, &hklen)
	    < 0)
		return (-1);

	ctrl->cipherkeylen = cipherkeylen;
	ctrl->hashkeylen = hklen;
	ctrl->taglen = taglen;

	/* TODO : add the invalid checks and return error */
	return (0);
}

/**
* @brief Top level function for generation pkt desc 0 to 3 for cipher auth
* @ingroup crypto
* ctrl : pointer to control structure
* param : pointer to the param structure
* encrypt : 1(for encrypt) 0(for decrypt)
* hash_source : 1(encrypted data is sent to the auth engine) 0(plain data is
*		sent to the auth engine)
* ivoff : iv offset from start of data
* ivlen : iv length in bytes
* hashoff : hash offset from start of data
* hashlen : hash length in bytes
* hmacpad : hmac padding required or not, 1 if already padded
* cipheroff : cipher offset from start of data
* cipherlen : cipher length in bytes
* hashdst_addr : hash destination physical address
*/
static __inline__ void
nlm_crypto_fill_cipher_auth_pkt_param(struct nlm_crypto_pkt_ctrl *ctrl,
    struct nlm_crypto_pkt_param *param, unsigned int encrypt,
    unsigned int hash_source, unsigned int ivoff, unsigned int ivlen,
    unsigned int hashoff, unsigned int hashlen, unsigned int hmacpad,
    unsigned int cipheroff, unsigned int cipherlen, unsigned char *hashdst_addr)
{
	param->desc0 = nlm_crypto_form_pkt_desc0(0, hash_source, 1, encrypt,
			   ivlen, vtophys(hashdst_addr));
	param->desc1 = nlm_crypto_form_pkt_desc1(cipherlen, hashlen);
	param->desc2 = nlm_crypto_form_pkt_desc2(ivoff, 0, cipheroff, 0, 0,
			   hashoff);
	param->desc3 = nlm_crypto_form_pkt_desc3(0, ctrl->taglen, 0, 0,
			   hmacpad);
}

/**
* @brief Top level function for generation pkt desc 0 to 3 for cipher operation
* @ingroup crypto
* ctrl : pointer to control structure
* param : pointer to the param structure
* encrypt : 1(for encrypt) 0(for decrypt)
* ivoff : iv offset from start of data
* ivlen : iv length in bytes
* cipheroff : cipher offset from start of data
* cipherlen : cipher length in bytes
*/
static __inline__ void
nlm_crypto_fill_cipher_pkt_param(struct nlm_crypto_pkt_ctrl *ctrl,
    struct nlm_crypto_pkt_param *param, unsigned int encrypt,
    unsigned int ivoff, unsigned int ivlen, unsigned int cipheroff,
    unsigned int cipherlen)
{
	param->desc0 = nlm_crypto_form_pkt_desc0(0, 0, 0, encrypt, ivlen, 0ULL);
	param->desc1 = nlm_crypto_form_pkt_desc1(cipherlen, 1);
	param->desc2 = nlm_crypto_form_pkt_desc2(ivoff, 0, cipheroff, 0, 0, 0);
	param->desc3 = nlm_crypto_form_pkt_desc3(0, ctrl->taglen, 0, 0, 0);
}

/**
* @brief Top level function for generation pkt desc 0 to 3 for auth operation
* @ingroup crypto
* ctrl : pointer to control structure
* param : pointer to the param structure
* hashoff : hash offset from start of data
* hashlen : hash length in bytes
* hmacpad : hmac padding required or not, 1 if already padded
* hashdst_addr : hash destination physical address
*/
static __inline__ void
nlm_crypto_fill_auth_pkt_param(struct nlm_crypto_pkt_ctrl *ctrl,
    struct nlm_crypto_pkt_param *param, unsigned int hashoff,
    unsigned int hashlen, unsigned int hmacpad, unsigned char *hashdst_addr)
{
	param->desc0 = nlm_crypto_form_pkt_desc0(0, 0, 1, 0, 1,
			   vtophys(hashdst_addr));
	param->desc1 = nlm_crypto_form_pkt_desc1(1, hashlen);
	param->desc2 = nlm_crypto_form_pkt_desc2(0, 0, 0, 0, 0, hashoff);
	param->desc3 = nlm_crypto_form_pkt_desc3(0, ctrl->taglen, 0, 0,
			   hmacpad);
}

static __inline__ unsigned int
nlm_crypto_fill_src_seg(struct nlm_crypto_pkt_param *param, int seg,
    unsigned char *input, unsigned int inlen)
{
	unsigned off = 0, len = 0;
	unsigned int remlen = inlen;

	for (; remlen > 0;) {
		len = remlen > NLM_CRYPTO_MAX_SEG_LEN ?
		    NLM_CRYPTO_MAX_SEG_LEN : remlen;
		param->segment[seg][0] = nlm_crypto_form_pkt_desc4(len,
		    vtophys(input + off));
		remlen -= len;
		off += len;
		seg++;
	}
	return (seg);
}

static __inline__ unsigned int
nlm_crypto_fill_dst_seg(struct nlm_crypto_pkt_param *param,
		int seg, unsigned char *output, unsigned int outlen)
{
	unsigned off = 0, len = 0;
	unsigned int remlen = outlen;

	for (; remlen > 0;) {
		len = remlen > NLM_CRYPTO_MAX_SEG_LEN ?
		    NLM_CRYPTO_MAX_SEG_LEN : remlen;
		param->segment[seg][1] = nlm_crypto_form_pkt_desc5(len, 1, 0,
		    vtophys(output + off));
		remlen -= len;
		off += len;
		seg++;
	}
	return (seg);
}

#endif
