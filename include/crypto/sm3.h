/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common values for SM3 algorithm
 *
 * Copyright (C) 2017 ARM Limited or its affiliates.
 * Copyright (C) 2017 Gilad Ben-Yossef <gilad@benyossef.com>
 * Copyright (C) 2021 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#ifndef _CRYPTO_SM3_H
#define _CRYPTO_SM3_H

#include <linux/types.h>

#define SM3_DIGEST_SIZE	32
#define SM3_BLOCK_SIZE	64
#define SM3_STATE_SIZE	40

#define SM3_T1		0x79CC4519
#define SM3_T2		0x7A879D8A

#define SM3_IVA		0x7380166f
#define SM3_IVB		0x4914b2b9
#define SM3_IVC		0x172442d7
#define SM3_IVD		0xda8a0600
#define SM3_IVE		0xa96f30bc
#define SM3_IVF		0x163138aa
#define SM3_IVG		0xe38dee4d
#define SM3_IVH		0xb0fb0e4e

extern const u8 sm3_zero_message_hash[SM3_DIGEST_SIZE];

struct sm3_state {
	u32 state[SM3_DIGEST_SIZE / 4];
	u64 count;
	u8 buffer[SM3_BLOCK_SIZE];
};

/*
 * Stand-alone implementation of the SM3 algorithm. It is designed to
 * have as little dependencies as possible so it can be used in the
 * kexec_file purgatory. In other cases you should generally use the
 * hash APIs from include/crypto/hash.h. Especially when hashing large
 * amounts of data as those APIs may be hw-accelerated.
 *
 * For details see lib/crypto/sm3.c
 */

static inline void sm3_init(struct sm3_state *sctx)
{
	sctx->state[0] = SM3_IVA;
	sctx->state[1] = SM3_IVB;
	sctx->state[2] = SM3_IVC;
	sctx->state[3] = SM3_IVD;
	sctx->state[4] = SM3_IVE;
	sctx->state[5] = SM3_IVF;
	sctx->state[6] = SM3_IVG;
	sctx->state[7] = SM3_IVH;
	sctx->count = 0;
}

void sm3_block_generic(struct sm3_state *sctx, u8 const *data, int blocks);

#endif
