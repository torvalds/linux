/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013,2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CCP_H__
#define __CCP_H__

#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <crypto/aes.h>
#include <crypto/sha.h>

struct ccp_device;
struct ccp_cmd;

#if defined(CONFIG_CRYPTO_DEV_SP_CCP)

/**
 * ccp_present - check if a CCP device is present
 *
 * Returns zero if a CCP device is present, -ENODEV otherwise.
 */
int ccp_present(void);

#define	CCP_VSIZE 16
#define	CCP_VMASK		((unsigned int)((1 << CCP_VSIZE) - 1))
#define	CCP_VERSION(v, r)	((unsigned int)((v << CCP_VSIZE) \
					       | (r & CCP_VMASK)))

/**
 * ccp_version - get the version of the CCP
 *
 * Returns a positive version number, or zero if no CCP
 */
unsigned int ccp_version(void);

/**
 * ccp_enqueue_cmd - queue an operation for processing by the CCP
 *
 * @cmd: ccp_cmd struct to be processed
 *
 * Refer to the ccp_cmd struct below for required fields.
 *
 * Queue a cmd to be processed by the CCP. If queueing the cmd
 * would exceed the defined length of the cmd queue the cmd will
 * only be queued if the CCP_CMD_MAY_BACKLOG flag is set and will
 * result in a return code of -EBUSY.
 *
 * The callback routine specified in the ccp_cmd struct will be
 * called to notify the caller of completion (if the cmd was not
 * backlogged) or advancement out of the backlog. If the cmd has
 * advanced out of the backlog the "err" value of the callback
 * will be -EINPROGRESS. Any other "err" value during callback is
 * the result of the operation.
 *
 * The cmd has been successfully queued if:
 *   the return code is -EINPROGRESS or
 *   the return code is -EBUSY and CCP_CMD_MAY_BACKLOG flag is set
 */
int ccp_enqueue_cmd(struct ccp_cmd *cmd);

#else /* CONFIG_CRYPTO_DEV_CCP_SP_DEV is not enabled */

static inline int ccp_present(void)
{
	return -ENODEV;
}

static inline unsigned int ccp_version(void)
{
	return 0;
}

static inline int ccp_enqueue_cmd(struct ccp_cmd *cmd)
{
	return -ENODEV;
}

#endif /* CONFIG_CRYPTO_DEV_SP_CCP */


/***** AES engine *****/
/**
 * ccp_aes_type - AES key size
 *
 * @CCP_AES_TYPE_128: 128-bit key
 * @CCP_AES_TYPE_192: 192-bit key
 * @CCP_AES_TYPE_256: 256-bit key
 */
enum ccp_aes_type {
	CCP_AES_TYPE_128 = 0,
	CCP_AES_TYPE_192,
	CCP_AES_TYPE_256,
	CCP_AES_TYPE__LAST,
};

/**
 * ccp_aes_mode - AES operation mode
 *
 * @CCP_AES_MODE_ECB: ECB mode
 * @CCP_AES_MODE_CBC: CBC mode
 * @CCP_AES_MODE_OFB: OFB mode
 * @CCP_AES_MODE_CFB: CFB mode
 * @CCP_AES_MODE_CTR: CTR mode
 * @CCP_AES_MODE_CMAC: CMAC mode
 */
enum ccp_aes_mode {
	CCP_AES_MODE_ECB = 0,
	CCP_AES_MODE_CBC,
	CCP_AES_MODE_OFB,
	CCP_AES_MODE_CFB,
	CCP_AES_MODE_CTR,
	CCP_AES_MODE_CMAC,
	CCP_AES_MODE_GHASH,
	CCP_AES_MODE_GCTR,
	CCP_AES_MODE_GCM,
	CCP_AES_MODE_GMAC,
	CCP_AES_MODE__LAST,
};

/**
 * ccp_aes_mode - AES operation mode
 *
 * @CCP_AES_ACTION_DECRYPT: AES decrypt operation
 * @CCP_AES_ACTION_ENCRYPT: AES encrypt operation
 */
enum ccp_aes_action {
	CCP_AES_ACTION_DECRYPT = 0,
	CCP_AES_ACTION_ENCRYPT,
	CCP_AES_ACTION__LAST,
};
/* Overloaded field */
#define	CCP_AES_GHASHAAD	CCP_AES_ACTION_DECRYPT
#define	CCP_AES_GHASHFINAL	CCP_AES_ACTION_ENCRYPT

/**
 * struct ccp_aes_engine - CCP AES operation
 * @type: AES operation key size
 * @mode: AES operation mode
 * @action: AES operation (decrypt/encrypt)
 * @key: key to be used for this AES operation
 * @key_len: length in bytes of key
 * @iv: IV to be used for this AES operation
 * @iv_len: length in bytes of iv
 * @src: data to be used for this operation
 * @dst: data produced by this operation
 * @src_len: length in bytes of data used for this operation
 * @cmac_final: indicates final operation when running in CMAC mode
 * @cmac_key: K1/K2 key used in final CMAC operation
 * @cmac_key_len: length in bytes of cmac_key
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - type, mode, action, key, key_len, src, dst, src_len
 *   - iv, iv_len for any mode other than ECB
 *   - cmac_final for CMAC mode
 *   - cmac_key, cmac_key_len for CMAC mode if cmac_final is non-zero
 *
 * The iv variable is used as both input and output. On completion of the
 * AES operation the new IV overwrites the old IV.
 */
struct ccp_aes_engine {
	enum ccp_aes_type type;
	enum ccp_aes_mode mode;
	enum ccp_aes_action action;

	struct scatterlist *key;
	u32 key_len;		/* In bytes */

	struct scatterlist *iv;
	u32 iv_len;		/* In bytes */

	struct scatterlist *src, *dst;
	u64 src_len;		/* In bytes */

	u32 cmac_final;		/* Indicates final cmac cmd */
	struct scatterlist *cmac_key;	/* K1/K2 cmac key required for
					 * final cmac cmd */
	u32 cmac_key_len;	/* In bytes */

	u32 aad_len;		/* In bytes */
};

/***** XTS-AES engine *****/
/**
 * ccp_xts_aes_unit_size - XTS unit size
 *
 * @CCP_XTS_AES_UNIT_SIZE_16: Unit size of 16 bytes
 * @CCP_XTS_AES_UNIT_SIZE_512: Unit size of 512 bytes
 * @CCP_XTS_AES_UNIT_SIZE_1024: Unit size of 1024 bytes
 * @CCP_XTS_AES_UNIT_SIZE_2048: Unit size of 2048 bytes
 * @CCP_XTS_AES_UNIT_SIZE_4096: Unit size of 4096 bytes
 */
enum ccp_xts_aes_unit_size {
	CCP_XTS_AES_UNIT_SIZE_16 = 0,
	CCP_XTS_AES_UNIT_SIZE_512,
	CCP_XTS_AES_UNIT_SIZE_1024,
	CCP_XTS_AES_UNIT_SIZE_2048,
	CCP_XTS_AES_UNIT_SIZE_4096,
	CCP_XTS_AES_UNIT_SIZE__LAST,
};

/**
 * struct ccp_xts_aes_engine - CCP XTS AES operation
 * @action: AES operation (decrypt/encrypt)
 * @unit_size: unit size of the XTS operation
 * @key: key to be used for this XTS AES operation
 * @key_len: length in bytes of key
 * @iv: IV to be used for this XTS AES operation
 * @iv_len: length in bytes of iv
 * @src: data to be used for this operation
 * @dst: data produced by this operation
 * @src_len: length in bytes of data used for this operation
 * @final: indicates final XTS operation
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - action, unit_size, key, key_len, iv, iv_len, src, dst, src_len, final
 *
 * The iv variable is used as both input and output. On completion of the
 * AES operation the new IV overwrites the old IV.
 */
struct ccp_xts_aes_engine {
	enum ccp_aes_action action;
	enum ccp_xts_aes_unit_size unit_size;

	struct scatterlist *key;
	u32 key_len;		/* In bytes */

	struct scatterlist *iv;
	u32 iv_len;		/* In bytes */

	struct scatterlist *src, *dst;
	u64 src_len;		/* In bytes */

	u32 final;
};

/***** SHA engine *****/
/**
 * ccp_sha_type - type of SHA operation
 *
 * @CCP_SHA_TYPE_1: SHA-1 operation
 * @CCP_SHA_TYPE_224: SHA-224 operation
 * @CCP_SHA_TYPE_256: SHA-256 operation
 */
enum ccp_sha_type {
	CCP_SHA_TYPE_1 = 1,
	CCP_SHA_TYPE_224,
	CCP_SHA_TYPE_256,
	CCP_SHA_TYPE_384,
	CCP_SHA_TYPE_512,
	CCP_SHA_TYPE__LAST,
};

/**
 * struct ccp_sha_engine - CCP SHA operation
 * @type: Type of SHA operation
 * @ctx: current hash value
 * @ctx_len: length in bytes of hash value
 * @src: data to be used for this operation
 * @src_len: length in bytes of data used for this operation
 * @opad: data to be used for final HMAC operation
 * @opad_len: length in bytes of data used for final HMAC operation
 * @first: indicates first SHA operation
 * @final: indicates final SHA operation
 * @msg_bits: total length of the message in bits used in final SHA operation
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - type, ctx, ctx_len, src, src_len, final
 *   - msg_bits if final is non-zero
 *
 * The ctx variable is used as both input and output. On completion of the
 * SHA operation the new hash value overwrites the old hash value.
 */
struct ccp_sha_engine {
	enum ccp_sha_type type;

	struct scatterlist *ctx;
	u32 ctx_len;		/* In bytes */

	struct scatterlist *src;
	u64 src_len;		/* In bytes */

	struct scatterlist *opad;
	u32 opad_len;		/* In bytes */

	u32 first;		/* Indicates first sha cmd */
	u32 final;		/* Indicates final sha cmd */
	u64 msg_bits;		/* Message length in bits required for
				 * final sha cmd */
};

/***** 3DES engine *****/
enum ccp_des3_mode {
	CCP_DES3_MODE_ECB = 0,
	CCP_DES3_MODE_CBC,
	CCP_DES3_MODE_CFB,
	CCP_DES3_MODE__LAST,
};

enum ccp_des3_type {
	CCP_DES3_TYPE_168 = 1,
	CCP_DES3_TYPE__LAST,
	};

enum ccp_des3_action {
	CCP_DES3_ACTION_DECRYPT = 0,
	CCP_DES3_ACTION_ENCRYPT,
	CCP_DES3_ACTION__LAST,
};

/**
 * struct ccp_des3_engine - CCP SHA operation
 * @type: Type of 3DES operation
 * @mode: cipher mode
 * @action: 3DES operation (decrypt/encrypt)
 * @key: key to be used for this 3DES operation
 * @key_len: length of key (in bytes)
 * @iv: IV to be used for this AES operation
 * @iv_len: length in bytes of iv
 * @src: input data to be used for this operation
 * @src_len: length of input data used for this operation (in bytes)
 * @dst: output data produced by this operation
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - type, mode, action, key, key_len, src, dst, src_len
 *   - iv, iv_len for any mode other than ECB
 *
 * The iv variable is used as both input and output. On completion of the
 * 3DES operation the new IV overwrites the old IV.
 */
struct ccp_des3_engine {
	enum ccp_des3_type type;
	enum ccp_des3_mode mode;
	enum ccp_des3_action action;

	struct scatterlist *key;
	u32 key_len;	    /* In bytes */

	struct scatterlist *iv;
	u32 iv_len;	     /* In bytes */

	struct scatterlist *src, *dst;
	u64 src_len;	    /* In bytes */
};

/***** RSA engine *****/
/**
 * struct ccp_rsa_engine - CCP RSA operation
 * @key_size: length in bits of RSA key
 * @exp: RSA exponent
 * @exp_len: length in bytes of exponent
 * @mod: RSA modulus
 * @mod_len: length in bytes of modulus
 * @src: data to be used for this operation
 * @dst: data produced by this operation
 * @src_len: length in bytes of data used for this operation
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - key_size, exp, exp_len, mod, mod_len, src, dst, src_len
 */
struct ccp_rsa_engine {
	u32 key_size;		/* In bits */

	struct scatterlist *exp;
	u32 exp_len;		/* In bytes */

	struct scatterlist *mod;
	u32 mod_len;		/* In bytes */

	struct scatterlist *src, *dst;
	u32 src_len;		/* In bytes */
};

/***** Passthru engine *****/
/**
 * ccp_passthru_bitwise - type of bitwise passthru operation
 *
 * @CCP_PASSTHRU_BITWISE_NOOP: no bitwise operation performed
 * @CCP_PASSTHRU_BITWISE_AND: perform bitwise AND of src with mask
 * @CCP_PASSTHRU_BITWISE_OR: perform bitwise OR of src with mask
 * @CCP_PASSTHRU_BITWISE_XOR: perform bitwise XOR of src with mask
 * @CCP_PASSTHRU_BITWISE_MASK: overwrite with mask
 */
enum ccp_passthru_bitwise {
	CCP_PASSTHRU_BITWISE_NOOP = 0,
	CCP_PASSTHRU_BITWISE_AND,
	CCP_PASSTHRU_BITWISE_OR,
	CCP_PASSTHRU_BITWISE_XOR,
	CCP_PASSTHRU_BITWISE_MASK,
	CCP_PASSTHRU_BITWISE__LAST,
};

/**
 * ccp_passthru_byteswap - type of byteswap passthru operation
 *
 * @CCP_PASSTHRU_BYTESWAP_NOOP: no byte swapping performed
 * @CCP_PASSTHRU_BYTESWAP_32BIT: swap bytes within 32-bit words
 * @CCP_PASSTHRU_BYTESWAP_256BIT: swap bytes within 256-bit words
 */
enum ccp_passthru_byteswap {
	CCP_PASSTHRU_BYTESWAP_NOOP = 0,
	CCP_PASSTHRU_BYTESWAP_32BIT,
	CCP_PASSTHRU_BYTESWAP_256BIT,
	CCP_PASSTHRU_BYTESWAP__LAST,
};

/**
 * struct ccp_passthru_engine - CCP pass-through operation
 * @bit_mod: bitwise operation to perform
 * @byte_swap: byteswap operation to perform
 * @mask: mask to be applied to data
 * @mask_len: length in bytes of mask
 * @src: data to be used for this operation
 * @dst: data produced by this operation
 * @src_len: length in bytes of data used for this operation
 * @final: indicate final pass-through operation
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - bit_mod, byte_swap, src, dst, src_len
 *   - mask, mask_len if bit_mod is not CCP_PASSTHRU_BITWISE_NOOP
 */
struct ccp_passthru_engine {
	enum ccp_passthru_bitwise bit_mod;
	enum ccp_passthru_byteswap byte_swap;

	struct scatterlist *mask;
	u32 mask_len;		/* In bytes */

	struct scatterlist *src, *dst;
	u64 src_len;		/* In bytes */

	u32 final;
};

/**
 * struct ccp_passthru_nomap_engine - CCP pass-through operation
 *   without performing DMA mapping
 * @bit_mod: bitwise operation to perform
 * @byte_swap: byteswap operation to perform
 * @mask: mask to be applied to data
 * @mask_len: length in bytes of mask
 * @src: data to be used for this operation
 * @dst: data produced by this operation
 * @src_len: length in bytes of data used for this operation
 * @final: indicate final pass-through operation
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - bit_mod, byte_swap, src, dst, src_len
 *   - mask, mask_len if bit_mod is not CCP_PASSTHRU_BITWISE_NOOP
 */
struct ccp_passthru_nomap_engine {
	enum ccp_passthru_bitwise bit_mod;
	enum ccp_passthru_byteswap byte_swap;

	dma_addr_t mask;
	u32 mask_len;		/* In bytes */

	dma_addr_t src_dma, dst_dma;
	u64 src_len;		/* In bytes */

	u32 final;
};

/***** ECC engine *****/
#define CCP_ECC_MODULUS_BYTES	48	/* 384-bits */
#define CCP_ECC_MAX_OPERANDS	6
#define CCP_ECC_MAX_OUTPUTS	3

/**
 * ccp_ecc_function - type of ECC function
 *
 * @CCP_ECC_FUNCTION_MMUL_384BIT: 384-bit modular multiplication
 * @CCP_ECC_FUNCTION_MADD_384BIT: 384-bit modular addition
 * @CCP_ECC_FUNCTION_MINV_384BIT: 384-bit multiplicative inverse
 * @CCP_ECC_FUNCTION_PADD_384BIT: 384-bit point addition
 * @CCP_ECC_FUNCTION_PMUL_384BIT: 384-bit point multiplication
 * @CCP_ECC_FUNCTION_PDBL_384BIT: 384-bit point doubling
 */
enum ccp_ecc_function {
	CCP_ECC_FUNCTION_MMUL_384BIT = 0,
	CCP_ECC_FUNCTION_MADD_384BIT,
	CCP_ECC_FUNCTION_MINV_384BIT,
	CCP_ECC_FUNCTION_PADD_384BIT,
	CCP_ECC_FUNCTION_PMUL_384BIT,
	CCP_ECC_FUNCTION_PDBL_384BIT,
};

/**
 * struct ccp_ecc_modular_math - CCP ECC modular math parameters
 * @operand_1: first operand for the modular math operation
 * @operand_1_len: length of the first operand
 * @operand_2: second operand for the modular math operation
 *	       (not used for CCP_ECC_FUNCTION_MINV_384BIT)
 * @operand_2_len: length of the second operand
 *	       (not used for CCP_ECC_FUNCTION_MINV_384BIT)
 * @result: result of the modular math operation
 * @result_len: length of the supplied result buffer
 */
struct ccp_ecc_modular_math {
	struct scatterlist *operand_1;
	unsigned int operand_1_len;	/* In bytes */

	struct scatterlist *operand_2;
	unsigned int operand_2_len;	/* In bytes */

	struct scatterlist *result;
	unsigned int result_len;	/* In bytes */
};

/**
 * struct ccp_ecc_point - CCP ECC point definition
 * @x: the x coordinate of the ECC point
 * @x_len: the length of the x coordinate
 * @y: the y coordinate of the ECC point
 * @y_len: the length of the y coordinate
 */
struct ccp_ecc_point {
	struct scatterlist *x;
	unsigned int x_len;	/* In bytes */

	struct scatterlist *y;
	unsigned int y_len;	/* In bytes */
};

/**
 * struct ccp_ecc_point_math - CCP ECC point math parameters
 * @point_1: the first point of the ECC point math operation
 * @point_2: the second point of the ECC point math operation
 *	     (only used for CCP_ECC_FUNCTION_PADD_384BIT)
 * @domain_a: the a parameter of the ECC curve
 * @domain_a_len: the length of the a parameter
 * @scalar: the scalar parameter for the point match operation
 *	    (only used for CCP_ECC_FUNCTION_PMUL_384BIT)
 * @scalar_len: the length of the scalar parameter
 *		(only used for CCP_ECC_FUNCTION_PMUL_384BIT)
 * @result: the point resulting from the point math operation
 */
struct ccp_ecc_point_math {
	struct ccp_ecc_point point_1;
	struct ccp_ecc_point point_2;

	struct scatterlist *domain_a;
	unsigned int domain_a_len;	/* In bytes */

	struct scatterlist *scalar;
	unsigned int scalar_len;	/* In bytes */

	struct ccp_ecc_point result;
};

/**
 * struct ccp_ecc_engine - CCP ECC operation
 * @function: ECC function to perform
 * @mod: ECC modulus
 * @mod_len: length in bytes of modulus
 * @mm: module math parameters
 * @pm: point math parameters
 * @ecc_result: result of the ECC operation
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - function, mod, mod_len
 *   - operand, operand_len, operand_count, output, output_len, output_count
 *   - ecc_result
 */
struct ccp_ecc_engine {
	enum ccp_ecc_function function;

	struct scatterlist *mod;
	u32 mod_len;		/* In bytes */

	union {
		struct ccp_ecc_modular_math mm;
		struct ccp_ecc_point_math pm;
	} u;

	u16 ecc_result;
};


/**
 * ccp_engine - CCP operation identifiers
 *
 * @CCP_ENGINE_AES: AES operation
 * @CCP_ENGINE_XTS_AES: 128-bit XTS AES operation
 * @CCP_ENGINE_RSVD1: unused
 * @CCP_ENGINE_SHA: SHA operation
 * @CCP_ENGINE_RSA: RSA operation
 * @CCP_ENGINE_PASSTHRU: pass-through operation
 * @CCP_ENGINE_ZLIB_DECOMPRESS: unused
 * @CCP_ENGINE_ECC: ECC operation
 */
enum ccp_engine {
	CCP_ENGINE_AES = 0,
	CCP_ENGINE_XTS_AES_128,
	CCP_ENGINE_DES3,
	CCP_ENGINE_SHA,
	CCP_ENGINE_RSA,
	CCP_ENGINE_PASSTHRU,
	CCP_ENGINE_ZLIB_DECOMPRESS,
	CCP_ENGINE_ECC,
	CCP_ENGINE__LAST,
};

/* Flag values for flags member of ccp_cmd */
#define CCP_CMD_MAY_BACKLOG		0x00000001
#define CCP_CMD_PASSTHRU_NO_DMA_MAP	0x00000002

/**
 * struct ccp_cmd - CCP operation request
 * @entry: list element (ccp driver use only)
 * @work: work element used for callbacks (ccp driver use only)
 * @ccp: CCP device to be run on
 * @ret: operation return code (ccp driver use only)
 * @flags: cmd processing flags
 * @engine: CCP operation to perform
 * @engine_error: CCP engine return code
 * @u: engine specific structures, refer to specific engine struct below
 * @callback: operation completion callback function
 * @data: parameter value to be supplied to the callback function
 *
 * Variables required to be set when calling ccp_enqueue_cmd():
 *   - engine, callback
 *   - See the operation structures below for what is required for each
 *     operation.
 */
struct ccp_cmd {
	/* The list_head, work_struct, ccp and ret variables are for use
	 * by the CCP driver only.
	 */
	struct list_head entry;
	struct work_struct work;
	struct ccp_device *ccp;
	int ret;

	u32 flags;

	enum ccp_engine engine;
	u32 engine_error;

	union {
		struct ccp_aes_engine aes;
		struct ccp_xts_aes_engine xts;
		struct ccp_des3_engine des3;
		struct ccp_sha_engine sha;
		struct ccp_rsa_engine rsa;
		struct ccp_passthru_engine passthru;
		struct ccp_passthru_nomap_engine passthru_nomap;
		struct ccp_ecc_engine ecc;
	} u;

	/* Completion callback support */
	void (*callback)(void *data, int err);
	void *data;
};

#endif
