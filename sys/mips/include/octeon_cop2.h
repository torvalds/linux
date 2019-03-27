/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 */

#ifndef __OCTEON_COP2_H__
#define __OCTEON_COP2_H__

/*
 * COP2 registers of interest
 */
#define	COP2_CRC_IV		0x201
#define	COP2_CRC_IV_SET		COP2_CRC_IV
#define	COP2_CRC_LENGTH		0x202
#define	COP2_CRC_LENGTH_SET	0x1202
#define	COP2_CRC_POLY		0x200
#define	COP2_CRC_POLY_SET	0x4200
#define	COP2_LLM_DAT0		0x402
#define	COP2_LLM_DAT0_SET	COP2_LLM_DAT0
#define	COP2_LLM_DAT1		0x40A
#define	COP2_LLM_DAT1_SET	COP2_LLM_DAT1
#define	COP2_3DES_IV		0x084
#define	COP2_3DES_IV_SET	COP2_3DES_IV
#define	COP2_3DES_KEY0		0x080
#define	COP2_3DES_KEY0_SET	COP2_3DES_KEY0
#define	COP2_3DES_KEY1		0x081
#define	COP2_3DES_KEY1_SET	COP2_3DES_KEY1
#define	COP2_3DES_KEY2		0x082
#define	COP2_3DES_KEY2_SET	COP2_3DES_KEY2
#define	COP2_3DES_RESULT	0x088
#define	COP2_3DES_RESULT_SET	0x098
#define	COP2_AES_INP0		0x111
#define	COP2_AES_INP0_SET	COP2_AES_INP0
#define	COP2_AES_IV0		0x102
#define	COP2_AES_IV0_SET	COP2_AES_IV0
#define	COP2_AES_IV1		0x103
#define	COP2_AES_IV1_SET	COP2_AES_IV1
#define	COP2_AES_KEY0		0x104
#define	COP2_AES_KEY0_SET	COP2_AES_KEY0
#define	COP2_AES_KEY1		0x105
#define	COP2_AES_KEY1_SET	COP2_AES_KEY1
#define	COP2_AES_KEY2		0x106
#define	COP2_AES_KEY2_SET	COP2_AES_KEY2
#define	COP2_AES_KEY3		0x107
#define	COP2_AES_KEY3_SET	COP2_AES_KEY3
#define	COP2_AES_KEYLEN		0x110
#define	COP2_AES_KEYLEN_SET	COP2_AES_KEYLEN
#define	COP2_AES_RESULT0	0x100
#define	COP2_AES_RESULT0_SET	COP2_AES_RESULT0
#define	COP2_AES_RESULT1	0x101
#define	COP2_AES_RESULT1_SET	COP2_AES_RESULT1
#define	COP2_HSH_DATW0		0x240
#define	COP2_HSH_DATW0_SET	COP2_HSH_DATW0
#define	COP2_HSH_DATW1		0x241
#define	COP2_HSH_DATW1_SET	COP2_HSH_DATW1
#define	COP2_HSH_DATW2		0x242
#define	COP2_HSH_DATW2_SET	COP2_HSH_DATW2
#define	COP2_HSH_DATW3		0x243
#define	COP2_HSH_DATW3_SET	COP2_HSH_DATW3
#define	COP2_HSH_DATW4		0x244
#define	COP2_HSH_DATW4_SET	COP2_HSH_DATW4
#define	COP2_HSH_DATW5		0x245
#define	COP2_HSH_DATW5_SET	COP2_HSH_DATW5
#define	COP2_HSH_DATW6		0x246
#define	COP2_HSH_DATW6_SET	COP2_HSH_DATW6
#define	COP2_HSH_DATW7		0x247
#define	COP2_HSH_DATW7_SET	COP2_HSH_DATW7
#define	COP2_HSH_DATW8		0x248
#define	COP2_HSH_DATW8_SET	COP2_HSH_DATW8
#define	COP2_HSH_DATW9		0x249
#define	COP2_HSH_DATW9_SET	COP2_HSH_DATW9
#define	COP2_HSH_DATW10		0x24A
#define	COP2_HSH_DATW10_SET	COP2_HSH_DATW10
#define	COP2_HSH_DATW11		0x24B
#define	COP2_HSH_DATW11_SET	COP2_HSH_DATW11
#define	COP2_HSH_DATW12		0x24C
#define	COP2_HSH_DATW12_SET	COP2_HSH_DATW12
#define	COP2_HSH_DATW13		0x24D
#define	COP2_HSH_DATW13_SET	COP2_HSH_DATW13
#define	COP2_HSH_DATW14		0x24E
#define	COP2_HSH_DATW14_SET	COP2_HSH_DATW14
#define	COP2_HSH_IVW0		0x250
#define	COP2_HSH_IVW0_SET	COP2_HSH_IVW0
#define	COP2_HSH_IVW1		0x251
#define	COP2_HSH_IVW1_SET	COP2_HSH_IVW1
#define	COP2_HSH_IVW2		0x252
#define	COP2_HSH_IVW2_SET	COP2_HSH_IVW2
#define	COP2_HSH_IVW3		0x253
#define	COP2_HSH_IVW3_SET	COP2_HSH_IVW3
#define	COP2_HSH_IVW4		0x254
#define	COP2_HSH_IVW4_SET	COP2_HSH_IVW4
#define	COP2_HSH_IVW5		0x255
#define	COP2_HSH_IVW5_SET	COP2_HSH_IVW5
#define	COP2_HSH_IVW6		0x256
#define	COP2_HSH_IVW6_SET	COP2_HSH_IVW6
#define	COP2_HSH_IVW7		0x257
#define	COP2_HSH_IVW7_SET	COP2_HSH_IVW7
#define	COP2_GFM_MULT0		0x258
#define	COP2_GFM_MULT0_SET	COP2_GFM_MULT0
#define	COP2_GFM_MULT1		0x259
#define	COP2_GFM_MULT1_SET	COP2_GFM_MULT1
#define	COP2_GFM_POLY		0x25E
#define	COP2_GFM_POLY_SET	COP2_GFM_POLY
#define	COP2_GFM_RESULT0	0x25A
#define	COP2_GFM_RESULT0_SET	COP2_GFM_RESULT0
#define	COP2_GFM_RESULT1	0x25B
#define	COP2_GFM_RESULT1_SET	COP2_GFM_RESULT1
#define	COP2_HSH_DATW0_PASS1	0x040
#define	COP2_HSH_DATW0_PASS1_SET	COP2_HSH_DATW0_PASS1
#define	COP2_HSH_DATW1_PASS1	0x041
#define	COP2_HSH_DATW1_PASS1_SET	COP2_HSH_DATW1_PASS1
#define	COP2_HSH_DATW2_PASS1	0x042
#define	COP2_HSH_DATW2_PASS1_SET	COP2_HSH_DATW2_PASS1
#define	COP2_HSH_DATW3_PASS1	0x043
#define	COP2_HSH_DATW3_PASS1_SET	COP2_HSH_DATW3_PASS1
#define	COP2_HSH_DATW4_PASS1	0x044
#define	COP2_HSH_DATW4_PASS1_SET	COP2_HSH_DATW4_PASS1
#define	COP2_HSH_DATW5_PASS1	0x045
#define	COP2_HSH_DATW5_PASS1_SET	COP2_HSH_DATW5_PASS1
#define	COP2_HSH_DATW6_PASS1	0x046
#define	COP2_HSH_DATW6_PASS1_SET	COP2_HSH_DATW6_PASS1
#define	COP2_HSH_IVW0_PASS1	0x048
#define	COP2_HSH_IVW0_PASS1_SET	COP2_HSH_IVW0_PASS1
#define	COP2_HSH_IVW1_PASS1	0x049
#define	COP2_HSH_IVW1_PASS1_SET	COP2_HSH_IVW1_PASS1
#define	COP2_HSH_IVW2_PASS1	0x04A
#define	COP2_HSH_IVW2_PASS1_SET	COP2_HSH_IVW2_PASS1

#ifndef LOCORE

struct octeon_cop2_state {
	/* 3DES */
	/* 0x0084 */
	unsigned long   _3des_iv;
	/* 0x0080..0x0082 */
	unsigned long   _3des_key[3];
	/* 0x0088, set: 0x0098 */
	unsigned long   _3des_result;

	/* AES */
	/* 0x0111 */
	unsigned long   aes_inp0;
	/* 0x0102..0x0103 */
	unsigned long   aes_iv[2];
	/* 0x0104..0x0107 */
	unsigned long   aes_key[4];
	/* 0x0110 */
	unsigned long   aes_keylen;
	/* 0x0100..0x0101 */
	unsigned long   aes_result[2];

	/* CRC */
	/*  0x0201 */
	unsigned long   crc_iv;
	/* 0x0202, set: 0x1202 */
	unsigned long   crc_length;
	/* 0x0200, set: 0x4200 */
	unsigned long   crc_poly;

	/* Low-latency memory stuff */
	/* 0x0402, 0x040A */
	unsigned long   llm_dat[2];

	/* SHA & MD5 */
	/* 0x0240..0x024E */
	unsigned long   hsh_datw[15];
	/* 0x0250..0x0257 */
	unsigned long   hsh_ivw[8];

	/* GFM */
	/*  0x0258..0x0259 */
	unsigned long   gfm_mult[2];
	/* 0x025E */
	unsigned long   gfm_poly;
	/* 0x025A..0x025B */
	unsigned long   gfm_result[2];
};

/* Prototypes */

struct octeon_cop2_state* octeon_cop2_alloc_ctx(void);
void octeon_cop2_free_ctx(struct octeon_cop2_state *);
/*
 * Save/restore part
 */
void octeon_cop2_save(struct octeon_cop2_state *);
void octeon_cop2_restore(struct octeon_cop2_state *);

#endif /* LOCORE */
#endif /* __OCTEON_COP2_H__ */
