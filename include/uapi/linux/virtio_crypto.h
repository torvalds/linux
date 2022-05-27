#ifndef _VIRTIO_CRYPTO_H
#define _VIRTIO_CRYPTO_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <linux/types.h>
#include <linux/virtio_types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>


#define VIRTIO_CRYPTO_SERVICE_CIPHER 0
#define VIRTIO_CRYPTO_SERVICE_HASH   1
#define VIRTIO_CRYPTO_SERVICE_MAC    2
#define VIRTIO_CRYPTO_SERVICE_AEAD   3
#define VIRTIO_CRYPTO_SERVICE_AKCIPHER 4

#define VIRTIO_CRYPTO_OPCODE(service, op)   (((service) << 8) | (op))

struct virtio_crypto_ctrl_header {
#define VIRTIO_CRYPTO_CIPHER_CREATE_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_CIPHER, 0x02)
#define VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_CIPHER, 0x03)
#define VIRTIO_CRYPTO_HASH_CREATE_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_HASH, 0x02)
#define VIRTIO_CRYPTO_HASH_DESTROY_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_HASH, 0x03)
#define VIRTIO_CRYPTO_MAC_CREATE_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_MAC, 0x02)
#define VIRTIO_CRYPTO_MAC_DESTROY_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_MAC, 0x03)
#define VIRTIO_CRYPTO_AEAD_CREATE_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AEAD, 0x02)
#define VIRTIO_CRYPTO_AEAD_DESTROY_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AEAD, 0x03)
#define VIRTIO_CRYPTO_AKCIPHER_CREATE_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AKCIPHER, 0x04)
#define VIRTIO_CRYPTO_AKCIPHER_DESTROY_SESSION \
	   VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AKCIPHER, 0x05)
	__le32 opcode;
	__le32 algo;
	__le32 flag;
	/* data virtqueue id */
	__le32 queue_id;
};

struct virtio_crypto_cipher_session_para {
#define VIRTIO_CRYPTO_NO_CIPHER                 0
#define VIRTIO_CRYPTO_CIPHER_ARC4               1
#define VIRTIO_CRYPTO_CIPHER_AES_ECB            2
#define VIRTIO_CRYPTO_CIPHER_AES_CBC            3
#define VIRTIO_CRYPTO_CIPHER_AES_CTR            4
#define VIRTIO_CRYPTO_CIPHER_DES_ECB            5
#define VIRTIO_CRYPTO_CIPHER_DES_CBC            6
#define VIRTIO_CRYPTO_CIPHER_3DES_ECB           7
#define VIRTIO_CRYPTO_CIPHER_3DES_CBC           8
#define VIRTIO_CRYPTO_CIPHER_3DES_CTR           9
#define VIRTIO_CRYPTO_CIPHER_KASUMI_F8          10
#define VIRTIO_CRYPTO_CIPHER_SNOW3G_UEA2        11
#define VIRTIO_CRYPTO_CIPHER_AES_F8             12
#define VIRTIO_CRYPTO_CIPHER_AES_XTS            13
#define VIRTIO_CRYPTO_CIPHER_ZUC_EEA3           14
	__le32 algo;
	/* length of key */
	__le32 keylen;

#define VIRTIO_CRYPTO_OP_ENCRYPT  1
#define VIRTIO_CRYPTO_OP_DECRYPT  2
	/* encrypt or decrypt */
	__le32 op;
	__le32 padding;
};

struct virtio_crypto_session_input {
	/* Device-writable part */
	__le64 session_id;
	__le32 status;
	__le32 padding;
};

struct virtio_crypto_cipher_session_req {
	struct virtio_crypto_cipher_session_para para;
	__u8 padding[32];
};

struct virtio_crypto_hash_session_para {
#define VIRTIO_CRYPTO_NO_HASH            0
#define VIRTIO_CRYPTO_HASH_MD5           1
#define VIRTIO_CRYPTO_HASH_SHA1          2
#define VIRTIO_CRYPTO_HASH_SHA_224       3
#define VIRTIO_CRYPTO_HASH_SHA_256       4
#define VIRTIO_CRYPTO_HASH_SHA_384       5
#define VIRTIO_CRYPTO_HASH_SHA_512       6
#define VIRTIO_CRYPTO_HASH_SHA3_224      7
#define VIRTIO_CRYPTO_HASH_SHA3_256      8
#define VIRTIO_CRYPTO_HASH_SHA3_384      9
#define VIRTIO_CRYPTO_HASH_SHA3_512      10
#define VIRTIO_CRYPTO_HASH_SHA3_SHAKE128      11
#define VIRTIO_CRYPTO_HASH_SHA3_SHAKE256      12
	__le32 algo;
	/* hash result length */
	__le32 hash_result_len;
	__u8 padding[8];
};

struct virtio_crypto_hash_create_session_req {
	struct virtio_crypto_hash_session_para para;
	__u8 padding[40];
};

struct virtio_crypto_mac_session_para {
#define VIRTIO_CRYPTO_NO_MAC                       0
#define VIRTIO_CRYPTO_MAC_HMAC_MD5                 1
#define VIRTIO_CRYPTO_MAC_HMAC_SHA1                2
#define VIRTIO_CRYPTO_MAC_HMAC_SHA_224             3
#define VIRTIO_CRYPTO_MAC_HMAC_SHA_256             4
#define VIRTIO_CRYPTO_MAC_HMAC_SHA_384             5
#define VIRTIO_CRYPTO_MAC_HMAC_SHA_512             6
#define VIRTIO_CRYPTO_MAC_CMAC_3DES                25
#define VIRTIO_CRYPTO_MAC_CMAC_AES                 26
#define VIRTIO_CRYPTO_MAC_KASUMI_F9                27
#define VIRTIO_CRYPTO_MAC_SNOW3G_UIA2              28
#define VIRTIO_CRYPTO_MAC_GMAC_AES                 41
#define VIRTIO_CRYPTO_MAC_GMAC_TWOFISH             42
#define VIRTIO_CRYPTO_MAC_CBCMAC_AES               49
#define VIRTIO_CRYPTO_MAC_CBCMAC_KASUMI_F9         50
#define VIRTIO_CRYPTO_MAC_XCBC_AES                 53
	__le32 algo;
	/* hash result length */
	__le32 hash_result_len;
	/* length of authenticated key */
	__le32 auth_key_len;
	__le32 padding;
};

struct virtio_crypto_mac_create_session_req {
	struct virtio_crypto_mac_session_para para;
	__u8 padding[40];
};

struct virtio_crypto_aead_session_para {
#define VIRTIO_CRYPTO_NO_AEAD     0
#define VIRTIO_CRYPTO_AEAD_GCM    1
#define VIRTIO_CRYPTO_AEAD_CCM    2
#define VIRTIO_CRYPTO_AEAD_CHACHA20_POLY1305  3
	__le32 algo;
	/* length of key */
	__le32 key_len;
	/* hash result length */
	__le32 hash_result_len;
	/* length of the additional authenticated data (AAD) in bytes */
	__le32 aad_len;
	/* encrypt or decrypt, See above VIRTIO_CRYPTO_OP_* */
	__le32 op;
	__le32 padding;
};

struct virtio_crypto_aead_create_session_req {
	struct virtio_crypto_aead_session_para para;
	__u8 padding[32];
};

struct virtio_crypto_rsa_session_para {
#define VIRTIO_CRYPTO_RSA_RAW_PADDING   0
#define VIRTIO_CRYPTO_RSA_PKCS1_PADDING 1
	__le32 padding_algo;

#define VIRTIO_CRYPTO_RSA_NO_HASH   0
#define VIRTIO_CRYPTO_RSA_MD2       1
#define VIRTIO_CRYPTO_RSA_MD3       2
#define VIRTIO_CRYPTO_RSA_MD4       3
#define VIRTIO_CRYPTO_RSA_MD5       4
#define VIRTIO_CRYPTO_RSA_SHA1      5
#define VIRTIO_CRYPTO_RSA_SHA256    6
#define VIRTIO_CRYPTO_RSA_SHA384    7
#define VIRTIO_CRYPTO_RSA_SHA512    8
#define VIRTIO_CRYPTO_RSA_SHA224    9
	__le32 hash_algo;
};

struct virtio_crypto_ecdsa_session_para {
#define VIRTIO_CRYPTO_CURVE_UNKNOWN   0
#define VIRTIO_CRYPTO_CURVE_NIST_P192 1
#define VIRTIO_CRYPTO_CURVE_NIST_P224 2
#define VIRTIO_CRYPTO_CURVE_NIST_P256 3
#define VIRTIO_CRYPTO_CURVE_NIST_P384 4
#define VIRTIO_CRYPTO_CURVE_NIST_P521 5
	__le32 curve_id;
	__le32 padding;
};

struct virtio_crypto_akcipher_session_para {
#define VIRTIO_CRYPTO_NO_AKCIPHER    0
#define VIRTIO_CRYPTO_AKCIPHER_RSA   1
#define VIRTIO_CRYPTO_AKCIPHER_DSA   2
#define VIRTIO_CRYPTO_AKCIPHER_ECDSA 3
	__le32 algo;

#define VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PUBLIC  1
#define VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE 2
	__le32 keytype;
	__le32 keylen;

	union {
		struct virtio_crypto_rsa_session_para rsa;
		struct virtio_crypto_ecdsa_session_para ecdsa;
	} u;
};

struct virtio_crypto_akcipher_create_session_req {
	struct virtio_crypto_akcipher_session_para para;
	__u8 padding[36];
};

struct virtio_crypto_alg_chain_session_para {
#define VIRTIO_CRYPTO_SYM_ALG_CHAIN_ORDER_HASH_THEN_CIPHER  1
#define VIRTIO_CRYPTO_SYM_ALG_CHAIN_ORDER_CIPHER_THEN_HASH  2
	__le32 alg_chain_order;
/* Plain hash */
#define VIRTIO_CRYPTO_SYM_HASH_MODE_PLAIN    1
/* Authenticated hash (mac) */
#define VIRTIO_CRYPTO_SYM_HASH_MODE_AUTH     2
/* Nested hash */
#define VIRTIO_CRYPTO_SYM_HASH_MODE_NESTED   3
	__le32 hash_mode;
	struct virtio_crypto_cipher_session_para cipher_param;
	union {
		struct virtio_crypto_hash_session_para hash_param;
		struct virtio_crypto_mac_session_para mac_param;
		__u8 padding[16];
	} u;
	/* length of the additional authenticated data (AAD) in bytes */
	__le32 aad_len;
	__le32 padding;
};

struct virtio_crypto_alg_chain_session_req {
	struct virtio_crypto_alg_chain_session_para para;
};

struct virtio_crypto_sym_create_session_req {
	union {
		struct virtio_crypto_cipher_session_req cipher;
		struct virtio_crypto_alg_chain_session_req chain;
		__u8 padding[48];
	} u;

	/* Device-readable part */

/* No operation */
#define VIRTIO_CRYPTO_SYM_OP_NONE  0
/* Cipher only operation on the data */
#define VIRTIO_CRYPTO_SYM_OP_CIPHER  1
/*
 * Chain any cipher with any hash or mac operation. The order
 * depends on the value of alg_chain_order param
 */
#define VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING  2
	__le32 op_type;
	__le32 padding;
};

struct virtio_crypto_destroy_session_req {
	/* Device-readable part */
	__le64  session_id;
	__u8 padding[48];
};

/* The request of the control virtqueue's packet */
struct virtio_crypto_op_ctrl_req {
	struct virtio_crypto_ctrl_header header;

	union {
		struct virtio_crypto_sym_create_session_req
			sym_create_session;
		struct virtio_crypto_hash_create_session_req
			hash_create_session;
		struct virtio_crypto_mac_create_session_req
			mac_create_session;
		struct virtio_crypto_aead_create_session_req
			aead_create_session;
		struct virtio_crypto_akcipher_create_session_req
			akcipher_create_session;
		struct virtio_crypto_destroy_session_req
			destroy_session;
		__u8 padding[56];
	} u;
};

struct virtio_crypto_op_header {
#define VIRTIO_CRYPTO_CIPHER_ENCRYPT \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_CIPHER, 0x00)
#define VIRTIO_CRYPTO_CIPHER_DECRYPT \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_CIPHER, 0x01)
#define VIRTIO_CRYPTO_HASH \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_HASH, 0x00)
#define VIRTIO_CRYPTO_MAC \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_MAC, 0x00)
#define VIRTIO_CRYPTO_AEAD_ENCRYPT \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AEAD, 0x00)
#define VIRTIO_CRYPTO_AEAD_DECRYPT \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AEAD, 0x01)
#define VIRTIO_CRYPTO_AKCIPHER_ENCRYPT \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AKCIPHER, 0x00)
#define VIRTIO_CRYPTO_AKCIPHER_DECRYPT \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AKCIPHER, 0x01)
#define VIRTIO_CRYPTO_AKCIPHER_SIGN \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AKCIPHER, 0x02)
#define VIRTIO_CRYPTO_AKCIPHER_VERIFY \
	VIRTIO_CRYPTO_OPCODE(VIRTIO_CRYPTO_SERVICE_AKCIPHER, 0x03)
	__le32 opcode;
	/* algo should be service-specific algorithms */
	__le32 algo;
	/* session_id should be service-specific algorithms */
	__le64 session_id;
	/* control flag to control the request */
	__le32 flag;
	__le32 padding;
};

struct virtio_crypto_cipher_para {
	/*
	 * Byte Length of valid IV/Counter
	 *
	 * For block ciphers in CBC or F8 mode, or for Kasumi in F8 mode, or for
	 *   SNOW3G in UEA2 mode, this is the length of the IV (which
	 *   must be the same as the block length of the cipher).
	 * For block ciphers in CTR mode, this is the length of the counter
	 *   (which must be the same as the block length of the cipher).
	 * For AES-XTS, this is the 128bit tweak, i, from IEEE Std 1619-2007.
	 *
	 * The IV/Counter will be updated after every partial cryptographic
	 * operation.
	 */
	__le32 iv_len;
	/* length of source data */
	__le32 src_data_len;
	/* length of dst data */
	__le32 dst_data_len;
	__le32 padding;
};

struct virtio_crypto_hash_para {
	/* length of source data */
	__le32 src_data_len;
	/* hash result length */
	__le32 hash_result_len;
};

struct virtio_crypto_mac_para {
	struct virtio_crypto_hash_para hash;
};

struct virtio_crypto_aead_para {
	/*
	 * Byte Length of valid IV data pointed to by the below iv_addr
	 * parameter.
	 *
	 * For GCM mode, this is either 12 (for 96-bit IVs) or 16, in which
	 *   case iv_addr points to J0.
	 * For CCM mode, this is the length of the nonce, which can be in the
	 *   range 7 to 13 inclusive.
	 */
	__le32 iv_len;
	/* length of additional auth data */
	__le32 aad_len;
	/* length of source data */
	__le32 src_data_len;
	/* length of dst data */
	__le32 dst_data_len;
};

struct virtio_crypto_cipher_data_req {
	/* Device-readable part */
	struct virtio_crypto_cipher_para para;
	__u8 padding[24];
};

struct virtio_crypto_hash_data_req {
	/* Device-readable part */
	struct virtio_crypto_hash_para para;
	__u8 padding[40];
};

struct virtio_crypto_mac_data_req {
	/* Device-readable part */
	struct virtio_crypto_mac_para para;
	__u8 padding[40];
};

struct virtio_crypto_alg_chain_data_para {
	__le32 iv_len;
	/* Length of source data */
	__le32 src_data_len;
	/* Length of destination data */
	__le32 dst_data_len;
	/* Starting point for cipher processing in source data */
	__le32 cipher_start_src_offset;
	/* Length of the source data that the cipher will be computed on */
	__le32 len_to_cipher;
	/* Starting point for hash processing in source data */
	__le32 hash_start_src_offset;
	/* Length of the source data that the hash will be computed on */
	__le32 len_to_hash;
	/* Length of the additional auth data */
	__le32 aad_len;
	/* Length of the hash result */
	__le32 hash_result_len;
	__le32 reserved;
};

struct virtio_crypto_alg_chain_data_req {
	/* Device-readable part */
	struct virtio_crypto_alg_chain_data_para para;
};

struct virtio_crypto_sym_data_req {
	union {
		struct virtio_crypto_cipher_data_req cipher;
		struct virtio_crypto_alg_chain_data_req chain;
		__u8 padding[40];
	} u;

	/* See above VIRTIO_CRYPTO_SYM_OP_* */
	__le32 op_type;
	__le32 padding;
};

struct virtio_crypto_aead_data_req {
	/* Device-readable part */
	struct virtio_crypto_aead_para para;
	__u8 padding[32];
};

struct virtio_crypto_akcipher_para {
	__le32 src_data_len;
	__le32 dst_data_len;
};

struct virtio_crypto_akcipher_data_req {
	struct virtio_crypto_akcipher_para para;
	__u8 padding[40];
};

/* The request of the data virtqueue's packet */
struct virtio_crypto_op_data_req {
	struct virtio_crypto_op_header header;

	union {
		struct virtio_crypto_sym_data_req  sym_req;
		struct virtio_crypto_hash_data_req hash_req;
		struct virtio_crypto_mac_data_req mac_req;
		struct virtio_crypto_aead_data_req aead_req;
		struct virtio_crypto_akcipher_data_req akcipher_req;
		__u8 padding[48];
	} u;
};

#define VIRTIO_CRYPTO_OK        0
#define VIRTIO_CRYPTO_ERR       1
#define VIRTIO_CRYPTO_BADMSG    2
#define VIRTIO_CRYPTO_NOTSUPP   3
#define VIRTIO_CRYPTO_INVSESS   4 /* Invalid session id */
#define VIRTIO_CRYPTO_NOSPC     5 /* no free session ID */
#define VIRTIO_CRYPTO_KEY_REJECTED 6 /* Signature verification failed */

/* The accelerator hardware is ready */
#define VIRTIO_CRYPTO_S_HW_READY  (1 << 0)

struct virtio_crypto_config {
	/* See VIRTIO_CRYPTO_OP_* above */
	__le32  status;

	/*
	 * Maximum number of data queue
	 */
	__le32  max_dataqueues;

	/*
	 * Specifies the services mask which the device support,
	 * see VIRTIO_CRYPTO_SERVICE_* above
	 */
	__le32 crypto_services;

	/* Detailed algorithms mask */
	__le32 cipher_algo_l;
	__le32 cipher_algo_h;
	__le32 hash_algo;
	__le32 mac_algo_l;
	__le32 mac_algo_h;
	__le32 aead_algo;
	/* Maximum length of cipher key */
	__le32 max_cipher_key_len;
	/* Maximum length of authenticated key */
	__le32 max_auth_key_len;
	__le32 akcipher_algo;
	/* Maximum size of each crypto request's content */
	__le64 max_size;
};

struct virtio_crypto_inhdr {
	/* See VIRTIO_CRYPTO_* above */
	__u8 status;
};
#endif
