/*	$OpenBSD: blake2s.h,v 1.3 2023/02/03 18:31:16 miod Exp $	*/
/*
 * Copyright (C) 2015-2020 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2020 Matt Dunwoodie <ncon@noconroy.net>.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#ifndef _BLAKE2S_H_
#define _BLAKE2S_H_

enum blake2s_lengths {
	BLAKE2S_BLOCK_SIZE = 64,
	BLAKE2S_HASH_SIZE = 32,
	BLAKE2S_KEY_SIZE = 32
};

struct blake2s_state {
	uint32_t h[8];
	uint32_t t[2];
	uint32_t f[2];
	uint8_t buf[BLAKE2S_BLOCK_SIZE];
	unsigned int buflen;
	unsigned int outlen;
};

void blake2s_init(struct blake2s_state *state, const size_t outlen);
void blake2s_init_key(struct blake2s_state *state, const size_t outlen,
		      const void *key, const size_t keylen);
void blake2s_update(struct blake2s_state *state, const uint8_t *in, size_t inlen);
void blake2s_final(struct blake2s_state *state, uint8_t *out);

static inline void blake2s(
    uint8_t *out, const uint8_t *in, const uint8_t *key,
    const size_t outlen, const size_t inlen, const size_t keylen)
{
	struct blake2s_state state;

	KASSERT((in != NULL || inlen == 0) &&
		out != NULL && outlen <= BLAKE2S_HASH_SIZE &&
		(key != NULL || keylen == 0) && keylen <= BLAKE2S_KEY_SIZE);

	if (keylen)
		blake2s_init_key(&state, outlen, key, keylen);
	else
		blake2s_init(&state, outlen);

	blake2s_update(&state, in, inlen);
	blake2s_final(&state, out);
}

void blake2s_hmac(uint8_t *out, const uint8_t *in, const uint8_t *key,
    const size_t outlen, const size_t inlen, const size_t keylen);

#endif /* _BLAKE2S_H_ */
