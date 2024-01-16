/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef __CHACHA20POLY1305_H
#define __CHACHA20POLY1305_H

#include <linux/types.h>
#include <linux/scatterlist.h>

enum chacha20poly1305_lengths {
	XCHACHA20POLY1305_NONCE_SIZE = 24,
	CHACHA20POLY1305_KEY_SIZE = 32,
	CHACHA20POLY1305_AUTHTAG_SIZE = 16
};

void chacha20poly1305_encrypt(u8 *dst, const u8 *src, const size_t src_len,
			      const u8 *ad, const size_t ad_len,
			      const u64 nonce,
			      const u8 key[CHACHA20POLY1305_KEY_SIZE]);

bool __must_check
chacha20poly1305_decrypt(u8 *dst, const u8 *src, const size_t src_len,
			 const u8 *ad, const size_t ad_len, const u64 nonce,
			 const u8 key[CHACHA20POLY1305_KEY_SIZE]);

void xchacha20poly1305_encrypt(u8 *dst, const u8 *src, const size_t src_len,
			       const u8 *ad, const size_t ad_len,
			       const u8 nonce[XCHACHA20POLY1305_NONCE_SIZE],
			       const u8 key[CHACHA20POLY1305_KEY_SIZE]);

bool __must_check xchacha20poly1305_decrypt(
	u8 *dst, const u8 *src, const size_t src_len, const u8 *ad,
	const size_t ad_len, const u8 nonce[XCHACHA20POLY1305_NONCE_SIZE],
	const u8 key[CHACHA20POLY1305_KEY_SIZE]);

bool chacha20poly1305_encrypt_sg_inplace(struct scatterlist *src, size_t src_len,
					 const u8 *ad, const size_t ad_len,
					 const u64 nonce,
					 const u8 key[CHACHA20POLY1305_KEY_SIZE]);

bool chacha20poly1305_decrypt_sg_inplace(struct scatterlist *src, size_t src_len,
					 const u8 *ad, const size_t ad_len,
					 const u64 nonce,
					 const u8 key[CHACHA20POLY1305_KEY_SIZE]);

bool chacha20poly1305_selftest(void);

#endif /* __CHACHA20POLY1305_H */
