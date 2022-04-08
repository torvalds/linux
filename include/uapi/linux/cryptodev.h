/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */

/* This is a source compatible implementation with the original API of
 * cryptodev by Angelos D. Keromytis, found at openbsd cryptodev.h.
 * Placed under public domain */

#ifndef _UAPI_CRYPTODEV_H
#define _UAPI_CRYPTODEV_H

#include <linux/types.h>
#include <linux/version.h>
#ifndef __KERNEL__
#define __user
#endif

/* API extensions for linux */
#define CRYPTO_HMAC_MAX_KEY_LEN		512
#define CRYPTO_CIPHER_MAX_KEY_LEN	64

/* All the supported algorithms
 */
enum cryptodev_crypto_op_t {
	CRYPTO_DES_CBC = 1,
	CRYPTO_3DES_CBC = 2,
	CRYPTO_BLF_CBC = 3,
	CRYPTO_CAST_CBC = 4,
	CRYPTO_SKIPJACK_CBC = 5,
	CRYPTO_MD5_HMAC = 6,
	CRYPTO_SHA1_HMAC = 7,
	CRYPTO_RIPEMD160_HMAC = 8,
	CRYPTO_MD5_KPDK = 9,
	CRYPTO_SHA1_KPDK = 10,
	CRYPTO_RIJNDAEL128_CBC = 11,
	CRYPTO_AES_CBC = CRYPTO_RIJNDAEL128_CBC,
	CRYPTO_ARC4 = 12,
	CRYPTO_MD5 = 13,
	CRYPTO_SHA1 = 14,
	CRYPTO_DEFLATE_COMP = 15,
	CRYPTO_NULL = 16,
	CRYPTO_LZS_COMP = 17,
	CRYPTO_SHA2_256_HMAC = 18,
	CRYPTO_SHA2_384_HMAC = 19,
	CRYPTO_SHA2_512_HMAC = 20,
	CRYPTO_AES_CTR = 21,
	CRYPTO_AES_XTS = 22,
	CRYPTO_AES_ECB = 23,
	CRYPTO_AES_GCM = 50,

	CRYPTO_CAMELLIA_CBC = 101,
	CRYPTO_RIPEMD160,
	CRYPTO_SHA2_224,
	CRYPTO_SHA2_256,
	CRYPTO_SHA2_384,
	CRYPTO_SHA2_512,
	CRYPTO_SHA2_224_HMAC,
	CRYPTO_TLS11_AES_CBC_HMAC_SHA1,
	CRYPTO_TLS12_AES_CBC_HMAC_SHA256,

	CRYPTO_RK_DES_ECB = 150,
	CRYPTO_RK_DES_CBC,
	CRYPTO_RK_DES_CFB,
	CRYPTO_RK_DES_OFB,
	CRYPTO_RK_3DES_ECB,
	CRYPTO_RK_3DES_CBC,
	CRYPTO_RK_3DES_CFB,
	CRYPTO_RK_3DES_OFB,
	CRYPTO_RK_SM4_ECB,
	CRYPTO_RK_SM4_CBC,
	CRYPTO_RK_SM4_CFB,
	CRYPTO_RK_SM4_OFB,
	CRYPTO_RK_SM4_CTS,
	CRYPTO_RK_SM4_CTR,
	CRYPTO_RK_SM4_XTS,
	CRYPTO_RK_SM4_CCM,
	CRYPTO_RK_SM4_GCM,
	CRYPTO_RK_SM4_CMAC,
	CRYPTO_RK_SM4_CBC_MAC,
	CRYPTO_RK_AES_ECB,
	CRYPTO_RK_AES_CBC,
	CRYPTO_RK_AES_CFB,
	CRYPTO_RK_AES_OFB,
	CRYPTO_RK_AES_CTS,
	CRYPTO_RK_AES_CTR,
	CRYPTO_RK_AES_XTS,
	CRYPTO_RK_AES_CCM,
	CRYPTO_RK_AES_GCM,
	CRYPTO_RK_AES_CMAC,
	CRYPTO_RK_AES_CBC_MAC,
	CRYPTO_RK_MD5,
	CRYPTO_RK_SHA1,
	CRYPTO_RK_SHA224,
	CRYPTO_RK_SHA256,
	CRYPTO_RK_SHA384,
	CRYPTO_RK_SHA512,
	CRYPTO_RK_SHA512_224,
	CRYPTO_RK_SHA512_256,
	CRYPTO_RK_MD5_HMAC,
	CRYPTO_RK_SHA1_HMAC,
	CRYPTO_RK_SHA256_HMAC,
	CRYPTO_RK_SHA512_HMAC,
	CRYPTO_RK_SM3,
	CRYPTO_RK_SM3_HMAC,

	CRYPTO_ALGORITHM_ALL, /* Keep updated - see below */
};

#define	CRYPTO_ALGORITHM_MAX	(CRYPTO_ALGORITHM_ALL - 1)

/* Values for ciphers */
#define DES_BLOCK_LEN		8
#define DES3_BLOCK_LEN		8
#define RIJNDAEL128_BLOCK_LEN	16
#define AES_BLOCK_LEN		RIJNDAEL128_BLOCK_LEN
#define CAMELLIA_BLOCK_LEN      16
#define BLOWFISH_BLOCK_LEN	8
#define SKIPJACK_BLOCK_LEN	8
#define CAST128_BLOCK_LEN	8

/* the maximum of the above */
#define EALG_MAX_BLOCK_LEN	16

/* Values for hashes/MAC */
#define AALG_MAX_RESULT_LEN		64

/* maximum length of verbose alg names (depends on CRYPTO_MAX_ALG_NAME) */
#define CRYPTODEV_MAX_ALG_NAME		64

#define HASH_MAX_LEN 64

/* input of CIOCGSESSION */
struct session_op {
	/* Specify either cipher or mac
	 */
	__u32	cipher;		/* cryptodev_crypto_op_t */
	__u32	mac;		/* cryptodev_crypto_op_t */

	__u32	keylen;
	__u8	__user *key;
	__u32	mackeylen;
	__u8	__user *mackey;

	__u32	ses;		/* session identifier */
};

struct session_info_op {
	__u32 ses;		/* session identifier */

	/* verbose names for the requested ciphers */
	struct alg_info {
		char cra_name[CRYPTODEV_MAX_ALG_NAME];
		char cra_driver_name[CRYPTODEV_MAX_ALG_NAME];
	} cipher_info, hash_info;

	__u16	alignmask;	/* alignment constraints */
	__u32   flags;          /* SIOP_FLAGS_* */
};

/* If this flag is set then this algorithm uses
 * a driver only available in kernel (software drivers,
 * or drivers based on instruction sets do not set this flag).
 *
 * If multiple algorithms are involved (as in AEAD case), then
 * if one of them is kernel-driver-only this flag will be set.
 */
#define SIOP_FLAG_KERNEL_DRIVER_ONLY 1

#define	COP_ENCRYPT	0
#define COP_DECRYPT	1

/* input of CIOCCRYPT */
struct crypt_op {
	__u32	ses;		/* session identifier */
	__u16	op;		/* COP_ENCRYPT or COP_DECRYPT */
	__u16	flags;		/* see COP_FLAG_* */
	__u32	len;		/* length of source data */
	__u8	__user *src;	/* source data */
	__u8	__user *dst;	/* pointer to output data */
	/* pointer to output data for hash/MAC operations */
	__u8	__user *mac;
	/* initialization vector for encryption operations */
	__u8	__user *iv;
};

/* input of CIOCAUTHCRYPT */
struct crypt_auth_op {
	__u32	ses;		/* session identifier */
	__u16	op;		/* COP_ENCRYPT or COP_DECRYPT */
	__u16	flags;		/* see COP_FLAG_AEAD_* */
	__u32	len;		/* length of source data */
	__u32	auth_len;	/* length of auth data */
	__u8	__user *auth_src;	/* authenticated-only data */

	/* The current implementation is more efficient if data are
	 * encrypted in-place (src==dst). */
	__u8	__user *src;	/* data to be encrypted and authenticated */
	__u8	__user *dst;	/* pointer to output data. Must have
				 * space for tag. For TLS this should be at least
				 * len + tag_size + block_size for padding
				 */

	__u8    __user *tag;    /* where the tag will be copied to. TLS mode
                                 * doesn't use that as tag is copied to dst.
                                 * SRTP mode copies tag there. */
	__u32	tag_len;	/* the length of the tag. Use zero for digest size or max tag. */

	/* initialization vector for encryption operations */
	__u8	__user *iv;
	__u32   iv_len;
};

/* In plain AEAD mode the following are required:
 *  flags   : 0
 *  iv      : the initialization vector (12 bytes)
 *  auth_len: the length of the data to be authenticated
 *  auth_src: the data to be authenticated
 *  len     : length of data to be encrypted
 *  src     : the data to be encrypted
 *  dst     : space to hold encrypted data. It must have
 *            at least a size of len + tag_size.
 *  tag_size: the size of the desired authentication tag or zero to use
 *            the maximum tag output.
 *
 * Note tag isn't being used because the Linux AEAD interface
 * copies the tag just after data.
 */

/* In TLS mode (used for CBC ciphers that required padding)
 * the following are required:
 *  flags   : COP_FLAG_AEAD_TLS_TYPE
 *  iv      : the initialization vector
 *  auth_len: the length of the data to be authenticated only
 *  len     : length of data to be encrypted
 *  auth_src: the data to be authenticated
 *  src     : the data to be encrypted
 *  dst     : space to hold encrypted data (preferably in-place). It must have
 *            at least a size of len + tag_size + blocksize.
 *  tag_size: the size of the desired authentication tag or zero to use
 *            the default mac output.
 *
 * Note that the padding used is the minimum padding.
 */

/* In SRTP mode the following are required:
 *  flags   : COP_FLAG_AEAD_SRTP_TYPE
 *  iv      : the initialization vector
 *  auth_len: the length of the data to be authenticated. This must
 *            include the SRTP header + SRTP payload (data to be encrypted) + rest
 *
 *  len     : length of data to be encrypted
 *  auth_src: pointer the data to be authenticated. Should point at the same buffer as src.
 *  src     : pointer to the data to be encrypted.
 *  dst     : This is mandatory to be the same as src (in-place only).
 *  tag_size: the size of the desired authentication tag or zero to use
 *            the default mac output.
 *  tag     : Pointer to an address where the authentication tag will be copied.
 */


/* struct crypt_op flags */

#define COP_FLAG_NONE		(0 << 0) /* totally no flag */
#define COP_FLAG_UPDATE		(1 << 0) /* multi-update hash mode */
#define COP_FLAG_FINAL		(1 << 1) /* multi-update final hash mode */
#define COP_FLAG_WRITE_IV	(1 << 2) /* update the IV during operation */
#define COP_FLAG_NO_ZC		(1 << 3) /* do not zero-copy */
#define COP_FLAG_AEAD_TLS_TYPE  (1 << 4) /* authenticate and encrypt using the
                                          * TLS protocol rules */
#define COP_FLAG_AEAD_SRTP_TYPE  (1 << 5) /* authenticate and encrypt using the
                                           * SRTP protocol rules */
#define COP_FLAG_RESET		(1 << 6) /* multi-update reset the state.
                                          * should be used in combination
                                          * with COP_FLAG_UPDATE */
#define COP_FLAG_AEAD_RK_TYPE	(1 << 11) /* authenticate and encrypt using the
					   * rock-chips define rules
					   */


/* Stuff for bignum arithmetic and public key
 * cryptography - not supported yet by linux
 * cryptodev.
 */

#define	CRYPTO_ALG_FLAG_SUPPORTED	1
#define	CRYPTO_ALG_FLAG_RNG_ENABLE	2
#define	CRYPTO_ALG_FLAG_DSA_SHA		4

struct crparam {
	__u8	*crp_p;
	__u32	crp_nbits;
};

#define CRK_MAXPARAM	8

/* input of CIOCKEY */
struct crypt_kop {
	__u32	crk_op;		/* cryptodev_crk_op_t */
	__u32	crk_status;
	__u16	crk_iparams;
	__u16	crk_oparams;
	__u32	crk_pad1;
	struct crparam	crk_param[CRK_MAXPARAM];
};

enum cryptodev_crk_op_t {
	CRK_MOD_EXP = 0,
	CRK_MOD_EXP_CRT = 1,
	CRK_DSA_SIGN = 2,
	CRK_DSA_VERIFY = 3,
	CRK_DH_COMPUTE_KEY = 4,
	CRK_ALGORITHM_ALL
};

/* input of CIOCCPHASH
 *  dst_ses : destination session identifier
 *  src_ses : source session identifier
 *  dst_ses must have been created with CIOGSESSION first
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
struct cphash_op {
	__u32	dst_ses;
	__u32	src_ses;
};
#endif

#define CRK_ALGORITHM_MAX	(CRK_ALGORITHM_ALL-1)

/* features to be queried with CIOCASYMFEAT ioctl
 */
#define CRF_MOD_EXP		(1 << CRK_MOD_EXP)
#define CRF_MOD_EXP_CRT		(1 << CRK_MOD_EXP_CRT)
#define CRF_DSA_SIGN		(1 << CRK_DSA_SIGN)
#define CRF_DSA_VERIFY		(1 << CRK_DSA_VERIFY)
#define CRF_DH_COMPUTE_KEY	(1 << CRK_DH_COMPUTE_KEY)


/* ioctl's. Compatible with old linux cryptodev.h
 */
#define CRIOGET         _IOWR('c', 101, __u32)
#define CIOCGSESSION    _IOWR('c', 102, struct session_op)
#define CIOCFSESSION    _IOW('c', 103, __u32)
#define CIOCCRYPT       _IOWR('c', 104, struct crypt_op)
#define CIOCKEY         _IOWR('c', 105, struct crypt_kop)
#define CIOCASYMFEAT    _IOR('c', 106, __u32)
#define CIOCGSESSINFO	_IOWR('c', 107, struct session_info_op)

/* to indicate that CRIOGET is not required in linux
 */
#define CRIOGET_NOT_NEEDED 1

/* additional ioctls for AEAD */
#define CIOCAUTHCRYPT   _IOWR('c', 109, struct crypt_auth_op)

/* additional ioctls for asynchronous operation.
 * These are conditionally enabled since version 1.6.
 */
#define CIOCASYNCCRYPT    _IOW('c', 110, struct crypt_op)
#define CIOCASYNCFETCH    _IOR('c', 111, struct crypt_op)

/* additional ioctl for copying of hash/mac session state data
 * between sessions.
 * The cphash_op parameter should contain the session id of
 * the source and destination sessions. Both sessions
 * must have been created with CIOGSESSION.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
#define CIOCCPHASH	_IOW('c', 112, struct cphash_op)
#endif

#endif /* L_CRYPTODEV_H */
