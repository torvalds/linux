/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the Polyval hash algorithm
 *
 * Copyright 2021 Google LLC
 */

#ifndef _CRYPTO_POLYVAL_H
#define _CRYPTO_POLYVAL_H

#include <linux/types.h>
#include <linux/crypto.h>

#define POLYVAL_BLOCK_SIZE	16
#define POLYVAL_DIGEST_SIZE	16

void polyval_mul_non4k(u8 *op1, const u8 *op2);

void polyval_update_non4k(const u8 *key, const u8 *in,
			  size_t nblocks, u8 *accumulator);

#endif
