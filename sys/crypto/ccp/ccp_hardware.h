/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
 * All rights reserved.
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

#pragma once

#define CMD_QUEUE_MASK_OFFSET		0x000
#define CMD_QUEUE_PRIO_OFFSET		0x004
#define CMD_REQID_CONFIG_OFFSET		0x008
#define TRNG_OUT_OFFSET			0x00C
#define CMD_CMD_TIMEOUT_OFFSET		0x010
#define LSB_PUBLIC_MASK_LO_OFFSET	0x018
#define LSB_PUBLIC_MASK_HI_OFFSET	0x01C
#define LSB_PRIVATE_MASK_LO_OFFSET	0x020
#define LSB_PRIVATE_MASK_HI_OFFSET	0x024

#define VERSION_REG			0x100
#define VERSION_NUM_MASK		0x3F
#define VERSION_CAP_MASK		0x7FC0
#define VERSION_CAP_AES			(1 << 6)
#define VERSION_CAP_3DES		(1 << 7)
#define VERSION_CAP_SHA			(1 << 8)
#define VERSION_CAP_RSA			(1 << 9)
#define VERSION_CAP_ECC			(1 << 10)
#define VERSION_CAP_ZDE			(1 << 11)
#define VERSION_CAP_ZCE			(1 << 12)
#define VERSION_CAP_TRNG		(1 << 13)
#define VERSION_CAP_ELFC		(1 << 14)
#define VERSION_NUMVQM_SHIFT		15
#define VERSION_NUMVQM_MASK		0xF
#define VERSION_LSBSIZE_SHIFT		19
#define VERSION_LSBSIZE_MASK		0x3FF

#define CMD_Q_CONTROL_BASE		0x000
#define CMD_Q_TAIL_LO_BASE		0x004
#define CMD_Q_HEAD_LO_BASE		0x008
#define CMD_Q_INT_ENABLE_BASE		0x00C
#define CMD_Q_INTERRUPT_STATUS_BASE	0x010

#define CMD_Q_STATUS_BASE		0x100
#define CMD_Q_INT_STATUS_BASE		0x104

#define CMD_Q_STATUS_INCR		0x1000

/* Don't think there's much point in keeping these -- OS can't access: */
#define CMD_CONFIG_0_OFFSET		0x6000
#define CMD_TRNG_CTL_OFFSET		0x6008
#define CMD_AES_MASK_OFFSET		0x6010
#define CMD_CLK_GATE_CTL_OFFSET		0x603C

/* CMD_Q_CONTROL_BASE bits */
#define CMD_Q_RUN			(1 << 0)
#define CMD_Q_HALTED			(1 << 1)
#define CMD_Q_MEM_LOCATION		(1 << 2)
#define CMD_Q_SIZE_SHIFT		3
#define CMD_Q_SIZE_MASK			0x1F
#define CMD_Q_PTR_HI_SHIFT		16
#define CMD_Q_PTR_HI_MASK		0xFFFF

/*
 * The following bits are used for both CMD_Q_INT_ENABLE_BASE and
 * CMD_Q_INTERRUPT_STATUS_BASE.
 */
#define INT_COMPLETION			(1 << 0)
#define INT_ERROR			(1 << 1)
#define INT_QUEUE_STOPPED		(1 << 2)
#define INT_QUEUE_EMPTY			(1 << 3)
#define ALL_INTERRUPTS			(INT_COMPLETION | \
					 INT_ERROR | \
					 INT_QUEUE_STOPPED | \
					 INT_QUEUE_EMPTY)

#define STATUS_ERROR_MASK		0x3F
#define STATUS_JOBSTATUS_SHIFT		7
#define STATUS_JOBSTATUS_MASK		0x7
#define STATUS_ERRORSOURCE_SHIFT	10
#define STATUS_ERRORSOURCE_MASK		0x3
#define STATUS_VLSB_FAULTBLOCK_SHIFT	12
#define STATUS_VLSB_FAULTBLOCK_MASK	0x7

/* From JOBSTATUS field in STATUS register above */
#define JOBSTATUS_IDLE			0
#define JOBSTATUS_ACTIVE_WAITING	1
#define JOBSTATUS_ACTIVE		2
#define JOBSTATUS_WAIT_ABORT		3
#define JOBSTATUS_DYN_ERROR		4
#define JOBSTATUS_PREPARE_HALT		5

/* From ERRORSOURCE field in STATUS register */
#define ERRORSOURCE_INPUT_MEMORY	0
#define ERRORSOURCE_CMD_DESCRIPTOR	1
#define ERRORSOURCE_INPUT_DATA		2
#define ERRORSOURCE_KEY_DATA		3

#define Q_DESC_SIZE			sizeof(struct ccp_desc)

enum ccp_aes_mode {
	CCP_AES_MODE_ECB = 0,
	CCP_AES_MODE_CBC,
	CCP_AES_MODE_OFB,
	CCP_AES_MODE_CFB,
	CCP_AES_MODE_CTR,
	CCP_AES_MODE_CMAC,
	CCP_AES_MODE_GHASH,
	CCP_AES_MODE_GCTR,
	CCP_AES_MODE_IAPM_NIST,
	CCP_AES_MODE_IAPM_IPSEC,

	/* Not a real hardware mode; used as a sentinel value internally. */
	CCP_AES_MODE_XTS,
};

enum ccp_aes_ghash_mode {
	CCP_AES_MODE_GHASH_AAD = 0,
	CCP_AES_MODE_GHASH_FINAL,
};

enum ccp_aes_type {
	CCP_AES_TYPE_128 = 0,
	CCP_AES_TYPE_192,
	CCP_AES_TYPE_256,
};

enum ccp_des_mode {
	CCP_DES_MODE_ECB = 0,
	CCP_DES_MODE_CBC,
	CCP_DES_MODE_CFB,
};

enum ccp_des_type {
	CCP_DES_TYPE_128 = 0,	/* 112 + 16 parity */
	CCP_DES_TYPE_192,	/* 168 + 24 parity */
};

enum ccp_sha_type {
	CCP_SHA_TYPE_1 = 1,
	CCP_SHA_TYPE_224,
	CCP_SHA_TYPE_256,
	CCP_SHA_TYPE_384,
	CCP_SHA_TYPE_512,
	CCP_SHA_TYPE_RSVD1,
	CCP_SHA_TYPE_RSVD2,
	CCP_SHA3_TYPE_224,
	CCP_SHA3_TYPE_256,
	CCP_SHA3_TYPE_384,
	CCP_SHA3_TYPE_512,
};

enum ccp_cipher_algo {
	CCP_CIPHER_ALGO_AES_CBC = 0,
	CCP_CIPHER_ALGO_AES_ECB,
	CCP_CIPHER_ALGO_AES_CTR,
	CCP_CIPHER_ALGO_AES_GCM,
	CCP_CIPHER_ALGO_3DES_CBC,
};

enum ccp_cipher_dir {
	CCP_CIPHER_DIR_DECRYPT = 0,
	CCP_CIPHER_DIR_ENCRYPT = 1,
};

enum ccp_hash_algo {
	CCP_AUTH_ALGO_SHA1 = 0,
	CCP_AUTH_ALGO_SHA1_HMAC,
	CCP_AUTH_ALGO_SHA224,
	CCP_AUTH_ALGO_SHA224_HMAC,
	CCP_AUTH_ALGO_SHA3_224,
	CCP_AUTH_ALGO_SHA3_224_HMAC,
	CCP_AUTH_ALGO_SHA256,
	CCP_AUTH_ALGO_SHA256_HMAC,
	CCP_AUTH_ALGO_SHA3_256,
	CCP_AUTH_ALGO_SHA3_256_HMAC,
	CCP_AUTH_ALGO_SHA384,
	CCP_AUTH_ALGO_SHA384_HMAC,
	CCP_AUTH_ALGO_SHA3_384,
	CCP_AUTH_ALGO_SHA3_384_HMAC,
	CCP_AUTH_ALGO_SHA512,
	CCP_AUTH_ALGO_SHA512_HMAC,
	CCP_AUTH_ALGO_SHA3_512,
	CCP_AUTH_ALGO_SHA3_512_HMAC,
	CCP_AUTH_ALGO_AES_CMAC,
	CCP_AUTH_ALGO_AES_GCM,
};

enum ccp_hash_op {
	CCP_AUTH_OP_GENERATE = 0,
	CCP_AUTH_OP_VERIFY = 1,
};

enum ccp_engine {
	CCP_ENGINE_AES = 0,
	CCP_ENGINE_XTS_AES,
	CCP_ENGINE_3DES,
	CCP_ENGINE_SHA,
	CCP_ENGINE_RSA,
	CCP_ENGINE_PASSTHRU,
	CCP_ENGINE_ZLIB_DECOMPRESS,
	CCP_ENGINE_ECC,
};

enum ccp_xts_unitsize {
	CCP_XTS_AES_UNIT_SIZE_16 = 0,
	CCP_XTS_AES_UNIT_SIZE_512,
	CCP_XTS_AES_UNIT_SIZE_1024,
	CCP_XTS_AES_UNIT_SIZE_2048,
	CCP_XTS_AES_UNIT_SIZE_4096,
};

enum ccp_passthru_bitwise {
	CCP_PASSTHRU_BITWISE_NOOP = 0,
	CCP_PASSTHRU_BITWISE_AND,
	CCP_PASSTHRU_BITWISE_OR,
	CCP_PASSTHRU_BITWISE_XOR,
	CCP_PASSTHRU_BITWISE_MASK,
};

enum ccp_passthru_byteswap {
	CCP_PASSTHRU_BYTESWAP_NOOP = 0,
	CCP_PASSTHRU_BYTESWAP_32BIT,
	CCP_PASSTHRU_BYTESWAP_256BIT,
};

/**
 * descriptor for version 5 CPP commands
 * 8 32-bit words:
 * word 0: function; engine; control bits
 * word 1: length of source data
 * word 2: low 32 bits of source pointer
 * word 3: upper 16 bits of source pointer; source memory type
 * word 4: low 32 bits of destination pointer
 * word 5: upper 16 bits of destination pointer; destination memory
 * type
 * word 6: low 32 bits of key pointer
 * word 7: upper 16 bits of key pointer; key memory type
 */

struct ccp_desc {
	union dword0 {
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t size:7;
			uint32_t encrypt:1;
			uint32_t mode:5;
			uint32_t type:2;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_2:7;
		} aes;
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t size:7;
			uint32_t encrypt:1;
			uint32_t mode:5;
			uint32_t type:2;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_2:7;
		} des;
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t size:7;
			uint32_t encrypt:1;
			uint32_t reserved_2:5;
			uint32_t type:2;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_3:7;
		} aes_xts;
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t reserved_2:10;
			uint32_t type:4;
			uint32_t reserved_3:1;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_4:7;
		} sha;
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t mode:3;
			uint32_t size:12;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_2:7;
		} rsa;
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t byteswap:2;
			uint32_t bitwise:3;
			uint32_t reflect:2;
			uint32_t reserved_2:8;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_3:7;
		} pt;
		struct  {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t reserved_2:13;
			uint32_t reserved_3:2;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_4:7;
		} zlib;
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t size:10;
			uint32_t type:2;
			uint32_t mode:3;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_2:7;
		} ecc;
		struct {
			uint32_t hoc:1;		/* Halt on completion */
			uint32_t ioc:1;		/* Intr. on completion */
			uint32_t reserved_1:1;
			uint32_t som:1;		/* Start of message */
			uint32_t eom:1;		/* End " */
			uint32_t function:15;
			uint32_t engine:4;
			uint32_t prot:1;
			uint32_t reserved_2:7;
		} /* generic */;
	};

	uint32_t length;
	uint32_t src_lo;

	struct dword3 {
		uint32_t src_hi:16;
		uint32_t src_mem:2;
		uint32_t lsb_ctx_id:8;
		uint32_t reserved_3:5;
		uint32_t src_fixed:1;
	};

	union dword4 {
		uint32_t dst_lo;	/* NON-SHA */
		uint32_t sha_len_lo;	/* SHA */
	};

	union dword5 {
		struct {
			uint32_t dst_hi:16;
			uint32_t dst_mem:2;
			uint32_t reserved_4:13;
			uint32_t dst_fixed:1;
		};
		uint32_t sha_len_hi;
	};

	uint32_t key_lo;

	struct dword7 {
		uint32_t key_hi:16;
		uint32_t key_mem:2;
		uint32_t reserved_5:14;
	};
};

enum ccp_memtype {
	CCP_MEMTYPE_SYSTEM = 0,
	CCP_MEMTYPE_SB,
	CCP_MEMTYPE_LOCAL,
};

enum ccp_cmd_order {
	CCP_CMD_CIPHER = 0,
	CCP_CMD_AUTH,
	CCP_CMD_CIPHER_HASH,
	CCP_CMD_HASH_CIPHER,
	CCP_CMD_COMBINED,
	CCP_CMD_NOT_SUPPORTED,
};
