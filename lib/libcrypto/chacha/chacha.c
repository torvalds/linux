/* $OpenBSD: chacha.c,v 1.10 2023/07/05 16:17:20 beck Exp $ */
/*
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
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

#include <stdint.h>

#include <openssl/chacha.h>

#include "chacha-merged.c"

void
ChaCha_set_key(ChaCha_ctx *ctx, const unsigned char *key, uint32_t keybits)
{
	chacha_keysetup((chacha_ctx *)ctx, key, keybits);
	ctx->unused = 0;
}
LCRYPTO_ALIAS(ChaCha_set_key);

void
ChaCha_set_iv(ChaCha_ctx *ctx, const unsigned char *iv,
    const unsigned char *counter)
{
	chacha_ivsetup((chacha_ctx *)ctx, iv, counter);
	ctx->unused = 0;
}
LCRYPTO_ALIAS(ChaCha_set_iv);

void
ChaCha(ChaCha_ctx *ctx, unsigned char *out, const unsigned char *in, size_t len)
{
	unsigned char *k;
	uint64_t n;
	int i, l;

	/* Consume remaining keystream, if any exists. */
	if (ctx->unused > 0) {
		k = ctx->ks + 64 - ctx->unused;
		l = (len > ctx->unused) ? ctx->unused : len;
		for (i = 0; i < l; i++)
			*(out++) = *(in++) ^ *(k++);
		ctx->unused -= l;
		len -= l;
	}

	while (len > 0) {
		if ((n = len) > UINT32_MAX)
			n = UINT32_MAX;

		chacha_encrypt_bytes((chacha_ctx *)ctx, in, out, (uint32_t)n);

		in += n;
		out += n;
		len -= n;
	}
}
LCRYPTO_ALIAS(ChaCha);

void
CRYPTO_chacha_20(unsigned char *out, const unsigned char *in, size_t len,
    const unsigned char key[32], const unsigned char iv[8], uint64_t counter)
{
	struct chacha_ctx ctx;
	uint64_t n;

	/*
	 * chacha_ivsetup expects the counter to be in u8. Rather than
	 * converting size_t to u8 and then back again, pass a counter of
	 * NULL and manually assign it afterwards.
	 */
	chacha_keysetup(&ctx, key, 256);
	chacha_ivsetup(&ctx, iv, NULL);
	if (counter != 0) {
		ctx.input[12] = (uint32_t)counter;
		ctx.input[13] = (uint32_t)(counter >> 32);
	}

	while (len > 0) {
		if ((n = len) > UINT32_MAX)
			n = UINT32_MAX;

		chacha_encrypt_bytes(&ctx, in, out, (uint32_t)n);

		in += n;
		out += n;
		len -= n;
	}
}
LCRYPTO_ALIAS(CRYPTO_chacha_20);

void
CRYPTO_xchacha_20(unsigned char *out, const unsigned char *in, size_t len,
    const unsigned char key[32], const unsigned char iv[24])
{
	uint8_t subkey[32];

	CRYPTO_hchacha_20(subkey, key, iv);
	CRYPTO_chacha_20(out, in, len, subkey, iv + 16, 0);
}
LCRYPTO_ALIAS(CRYPTO_xchacha_20);
