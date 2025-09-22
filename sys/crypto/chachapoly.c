/*	$OpenBSD: chachapoly.c,v 1.6 2020/07/22 13:54:30 tobhe Exp $	*/
/*
 * Copyright (c) 2015 Mike Belopuhov
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

#include <sys/param.h>
#include <sys/systm.h>
#include <lib/libkern/libkern.h>

#include <crypto/chacha_private.h>
#include <crypto/poly1305.h>
#include <crypto/chachapoly.h>

int
chacha20_setkey(void *sched, u_int8_t *key, int len)
{
	struct chacha20_ctx *ctx = (struct chacha20_ctx *)sched;

	if (len != CHACHA20_KEYSIZE + CHACHA20_SALT)
		return (-1);

	/* initial counter is 1 */
	ctx->nonce[0] = 1;
	memcpy(ctx->nonce + CHACHA20_CTR, key + CHACHA20_KEYSIZE,
	    CHACHA20_SALT);
	chacha_keysetup((chacha_ctx *)&ctx->block, key, CHACHA20_KEYSIZE * 8);
	return (0);
}

void
chacha20_reinit(caddr_t key, u_int8_t *iv)
{
	struct chacha20_ctx *ctx = (struct chacha20_ctx *)key;

	chacha_ivsetup((chacha_ctx *)ctx->block, iv, ctx->nonce);
}

void
chacha20_crypt(caddr_t key, u_int8_t *data)
{
	struct chacha20_ctx *ctx = (struct chacha20_ctx *)key;

	chacha_encrypt_bytes((chacha_ctx *)ctx->block, data, data,
	    CHACHA20_BLOCK_LEN);
}

void
Chacha20_Poly1305_Init(void *xctx)
{
	CHACHA20_POLY1305_CTX *ctx = xctx;

	memset(ctx, 0, sizeof(*ctx));
}

void
Chacha20_Poly1305_Setkey(void *xctx, const uint8_t *key, uint16_t klen)
{
	CHACHA20_POLY1305_CTX *ctx = xctx;

	/* salt is provided with the key material */
	memcpy(ctx->nonce + CHACHA20_CTR, key + CHACHA20_KEYSIZE,
	    CHACHA20_SALT);
	chacha_keysetup((chacha_ctx *)&ctx->chacha, key, CHACHA20_KEYSIZE * 8);
}

void
Chacha20_Poly1305_Reinit(void *xctx, const uint8_t *iv, uint16_t ivlen)
{
	CHACHA20_POLY1305_CTX *ctx = xctx;

	/* initial counter is 0 */
	chacha_ivsetup((chacha_ctx *)&ctx->chacha, iv, ctx->nonce);
	chacha_encrypt_bytes((chacha_ctx *)&ctx->chacha, ctx->key, ctx->key,
	    POLY1305_KEYLEN);
	poly1305_init((poly1305_state *)&ctx->poly, ctx->key);
}

int
Chacha20_Poly1305_Update(void *xctx, const uint8_t *data, uint16_t len)
{
	static const char zeroes[POLY1305_BLOCK_LEN];
	CHACHA20_POLY1305_CTX *ctx = xctx;
	size_t rem;

	poly1305_update((poly1305_state *)&ctx->poly, data, len);

	/* number of bytes in the last 16 byte block */
	rem = (len + POLY1305_BLOCK_LEN) & (POLY1305_BLOCK_LEN - 1);
	if (rem > 0)
		poly1305_update((poly1305_state *)&ctx->poly, zeroes,
		    POLY1305_BLOCK_LEN - rem);
	return (0);
}

void
Chacha20_Poly1305_Final(uint8_t tag[POLY1305_TAGLEN], void *xctx)
{
	CHACHA20_POLY1305_CTX *ctx = xctx;

	poly1305_finish((poly1305_state *)&ctx->poly, tag);
	explicit_bzero(ctx, sizeof(*ctx));
}

static const uint8_t pad0[16] = { 0 };

void
chacha20poly1305_encrypt(
    uint8_t *dst,
    const uint8_t *src,
    const size_t src_len,
    const uint8_t *ad,
    const size_t ad_len,
    const uint64_t nonce,
    const uint8_t key[CHACHA20POLY1305_KEY_SIZE]
) {
	poly1305_state poly1305_ctx;
	chacha_ctx chacha_ctx;
	union {
		uint8_t b0[CHACHA20POLY1305_KEY_SIZE];
		uint64_t lens[2];
	} b = { { 0 } };
	uint64_t le_nonce = htole64(nonce);

	chacha_keysetup(&chacha_ctx, key, CHACHA20POLY1305_KEY_SIZE * 8);
	chacha_ivsetup(&chacha_ctx, (uint8_t *) &le_nonce, NULL);
	chacha_encrypt_bytes(&chacha_ctx, b.b0, b.b0, sizeof(b.b0));
	poly1305_init(&poly1305_ctx, b.b0);

	poly1305_update(&poly1305_ctx, ad, ad_len);
	poly1305_update(&poly1305_ctx, pad0, (0x10 - ad_len) & 0xf);

	chacha_encrypt_bytes(&chacha_ctx, (uint8_t *) src, dst, src_len);

	poly1305_update(&poly1305_ctx, dst, src_len);
	poly1305_update(&poly1305_ctx, pad0, (0x10 - src_len) & 0xf);

	b.lens[0] = htole64(ad_len);
	b.lens[1] = htole64(src_len);
	poly1305_update(&poly1305_ctx, (uint8_t *)b.lens, sizeof(b.lens));

	poly1305_finish(&poly1305_ctx, dst + src_len);

	explicit_bzero(&chacha_ctx, sizeof(chacha_ctx));
	explicit_bzero(&b, sizeof(b));
}

int
chacha20poly1305_decrypt(
    uint8_t *dst,
    const uint8_t *src,
    const size_t src_len,
    const uint8_t *ad,
    const size_t ad_len,
    const uint64_t nonce,
    const uint8_t key[CHACHA20POLY1305_KEY_SIZE]
) {
	poly1305_state poly1305_ctx;
	chacha_ctx chacha_ctx;
	int ret;
	size_t dst_len;
	union {
		uint8_t b0[CHACHA20POLY1305_KEY_SIZE];
		uint8_t mac[CHACHA20POLY1305_AUTHTAG_SIZE];
		uint64_t lens[2];
	} b = { { 0 } };
	uint64_t le_nonce = htole64(nonce);

	if (src_len < CHACHA20POLY1305_AUTHTAG_SIZE)
		return 0;

	chacha_keysetup(&chacha_ctx, key, CHACHA20POLY1305_KEY_SIZE * 8);
	chacha_ivsetup(&chacha_ctx, (uint8_t *) &le_nonce, NULL);
	chacha_encrypt_bytes(&chacha_ctx, b.b0, b.b0, sizeof(b.b0));
	poly1305_init(&poly1305_ctx, b.b0);

	poly1305_update(&poly1305_ctx, ad, ad_len);
	poly1305_update(&poly1305_ctx, pad0, (0x10 - ad_len) & 0xf);

	dst_len = src_len - CHACHA20POLY1305_AUTHTAG_SIZE;
	poly1305_update(&poly1305_ctx, src, dst_len);
	poly1305_update(&poly1305_ctx, pad0, (0x10 - dst_len) & 0xf);

	b.lens[0] = htole64(ad_len);
	b.lens[1] = htole64(dst_len);
	poly1305_update(&poly1305_ctx, (uint8_t *)b.lens, sizeof(b.lens));

	poly1305_finish(&poly1305_ctx, b.mac);

	ret = timingsafe_bcmp(b.mac, src + dst_len, CHACHA20POLY1305_AUTHTAG_SIZE);
	if (!ret)
		chacha_encrypt_bytes(&chacha_ctx, (uint8_t *) src, dst, dst_len);

	explicit_bzero(&chacha_ctx, sizeof(chacha_ctx));
	explicit_bzero(&b, sizeof(b));

	return !ret;
}

void
xchacha20poly1305_encrypt(
    uint8_t *dst,
    const uint8_t *src,
    const size_t src_len,
    const uint8_t *ad,
    const size_t ad_len,
    const uint8_t nonce[XCHACHA20POLY1305_NONCE_SIZE],
    const uint8_t key[CHACHA20POLY1305_KEY_SIZE]
) {
	int i;
	uint32_t derived_key[CHACHA20POLY1305_KEY_SIZE / sizeof(uint32_t)];
	uint64_t h_nonce;

	memcpy(&h_nonce, nonce + 16, sizeof(h_nonce));
	h_nonce = le64toh(h_nonce);
	hchacha20(derived_key, nonce, key);

	for(i = 0; i < (sizeof(derived_key)/sizeof(derived_key[0])); i++)
		(derived_key[i]) = htole32((derived_key[i]));

	chacha20poly1305_encrypt(dst, src, src_len, ad, ad_len,
	    h_nonce, (uint8_t *)derived_key);
	explicit_bzero(derived_key, CHACHA20POLY1305_KEY_SIZE);
}

int
xchacha20poly1305_decrypt(
    uint8_t *dst,
    const uint8_t *src,
    const size_t src_len,
    const uint8_t *ad,
    const size_t ad_len,
    const uint8_t nonce[XCHACHA20POLY1305_NONCE_SIZE],
    const uint8_t key[CHACHA20POLY1305_KEY_SIZE]
) {
	int ret, i;
	uint32_t derived_key[CHACHA20POLY1305_KEY_SIZE / sizeof(uint32_t)];
	uint64_t h_nonce;

	memcpy(&h_nonce, nonce + 16, sizeof(h_nonce));
	h_nonce = le64toh(h_nonce);
	hchacha20(derived_key, nonce, key);
	for(i = 0; i < (sizeof(derived_key)/sizeof(derived_key[0])); i++)
		(derived_key[i]) = htole32((derived_key[i]));

	ret = chacha20poly1305_decrypt(dst, src, src_len, ad, ad_len,
	    h_nonce, (uint8_t *)derived_key);
	explicit_bzero(derived_key, CHACHA20POLY1305_KEY_SIZE);

	return ret;
}
