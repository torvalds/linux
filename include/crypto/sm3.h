/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SM3 hash algorithm
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

#define SM3_IVA		0x7380166f
#define SM3_IVB		0x4914b2b9
#define SM3_IVC		0x172442d7
#define SM3_IVD		0xda8a0600
#define SM3_IVE		0xa96f30bc
#define SM3_IVF		0x163138aa
#define SM3_IVG		0xe38dee4d
#define SM3_IVH		0xb0fb0e4e

/* State for the SM3 compression function */
struct sm3_block_state {
	u32 h[SM3_DIGEST_SIZE / 4];
};

/**
 * struct sm3_ctx - Context for hashing a message with SM3
 * @state: the compression function state
 * @bytecount: number of bytes processed so far
 * @buf: partial block buffer; bytecount % SM3_BLOCK_SIZE bytes are valid
 */
struct sm3_ctx {
	struct sm3_block_state state;
	u64 bytecount;
	u8 buf[SM3_BLOCK_SIZE] __aligned(__alignof__(__be64));
};

/**
 * sm3_init() - Initialize an SM3 context for a new message
 * @ctx: the context to initialize
 *
 * If you don't need incremental computation, consider sm3() instead.
 *
 * Context: Any context.
 */
void sm3_init(struct sm3_ctx *ctx);

/**
 * sm3_update() - Update an SM3 context with message data
 * @ctx: the context to update; must have been initialized
 * @data: the message data
 * @len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void sm3_update(struct sm3_ctx *ctx, const u8 *data, size_t len);

/**
 * sm3_final() - Finish computing an SM3 message digest
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting SM3 message digest
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void sm3_final(struct sm3_ctx *ctx, u8 out[at_least SM3_DIGEST_SIZE]);

/**
 * sm3() - Compute SM3 message digest in one shot
 * @data: the message data
 * @len: the data length in bytes
 * @out: (output) the resulting SM3 message digest
 *
 * Context: Any context.
 */
void sm3(const u8 *data, size_t len, u8 out[at_least SM3_DIGEST_SIZE]);

#endif /* _CRYPTO_SM3_H */
