// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is an implementation of the ChaCha20Poly1305 AEAD construction.
 *
 * Information: https://tools.ietf.org/html/rfc8439
 */

#include <crypto/algapi.h>
#include <crypto/chacha20poly1305.h>
#include <crypto/chacha.h>
#include <crypto/poly1305.h>
#include <crypto/scatterwalk.h>

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>

#define CHACHA_KEY_WORDS	(CHACHA_KEY_SIZE / sizeof(u32))

bool __init chacha20poly1305_selftest(void);

static void chacha_load_key(u32 *k, const u8 *in)
{
	k[0] = get_unaligned_le32(in);
	k[1] = get_unaligned_le32(in + 4);
	k[2] = get_unaligned_le32(in + 8);
	k[3] = get_unaligned_le32(in + 12);
	k[4] = get_unaligned_le32(in + 16);
	k[5] = get_unaligned_le32(in + 20);
	k[6] = get_unaligned_le32(in + 24);
	k[7] = get_unaligned_le32(in + 28);
}

static void xchacha_init(u32 *chacha_state, const u8 *key, const u8 *nonce)
{
	u32 k[CHACHA_KEY_WORDS];
	u8 iv[CHACHA_IV_SIZE];

	memset(iv, 0, 8);
	memcpy(iv + 8, nonce + 16, 8);

	chacha_load_key(k, key);

	/* Compute the subkey given the original key and first 128 nonce bits */
	chacha_init(chacha_state, k, nonce);
	hchacha_block(chacha_state, k, 20);

	chacha_init(chacha_state, k, iv);

	memzero_explicit(k, sizeof(k));
	memzero_explicit(iv, sizeof(iv));
}

static void
__chacha20poly1305_encrypt(u8 *dst, const u8 *src, const size_t src_len,
			   const u8 *ad, const size_t ad_len, u32 *chacha_state)
{
	const u8 *pad0 = page_address(ZERO_PAGE(0));
	struct poly1305_desc_ctx poly1305_state;
	union {
		u8 block0[POLY1305_KEY_SIZE];
		__le64 lens[2];
	} b;

	chacha20_crypt(chacha_state, b.block0, pad0, sizeof(b.block0));
	poly1305_init(&poly1305_state, b.block0);

	poly1305_update(&poly1305_state, ad, ad_len);
	if (ad_len & 0xf)
		poly1305_update(&poly1305_state, pad0, 0x10 - (ad_len & 0xf));

	chacha20_crypt(chacha_state, dst, src, src_len);

	poly1305_update(&poly1305_state, dst, src_len);
	if (src_len & 0xf)
		poly1305_update(&poly1305_state, pad0, 0x10 - (src_len & 0xf));

	b.lens[0] = cpu_to_le64(ad_len);
	b.lens[1] = cpu_to_le64(src_len);
	poly1305_update(&poly1305_state, (u8 *)b.lens, sizeof(b.lens));

	poly1305_final(&poly1305_state, dst + src_len);

	memzero_explicit(chacha_state, CHACHA_STATE_WORDS * sizeof(u32));
	memzero_explicit(&b, sizeof(b));
}

void chacha20poly1305_encrypt(u8 *dst, const u8 *src, const size_t src_len,
			      const u8 *ad, const size_t ad_len,
			      const u64 nonce,
			      const u8 key[CHACHA20POLY1305_KEY_SIZE])
{
	u32 chacha_state[CHACHA_STATE_WORDS];
	u32 k[CHACHA_KEY_WORDS];
	__le64 iv[2];

	chacha_load_key(k, key);

	iv[0] = 0;
	iv[1] = cpu_to_le64(nonce);

	chacha_init(chacha_state, k, (u8 *)iv);
	__chacha20poly1305_encrypt(dst, src, src_len, ad, ad_len, chacha_state);

	memzero_explicit(iv, sizeof(iv));
	memzero_explicit(k, sizeof(k));
}
EXPORT_SYMBOL(chacha20poly1305_encrypt);

void xchacha20poly1305_encrypt(u8 *dst, const u8 *src, const size_t src_len,
			       const u8 *ad, const size_t ad_len,
			       const u8 nonce[XCHACHA20POLY1305_NONCE_SIZE],
			       const u8 key[CHACHA20POLY1305_KEY_SIZE])
{
	u32 chacha_state[CHACHA_STATE_WORDS];

	xchacha_init(chacha_state, key, nonce);
	__chacha20poly1305_encrypt(dst, src, src_len, ad, ad_len, chacha_state);
}
EXPORT_SYMBOL(xchacha20poly1305_encrypt);

static bool
__chacha20poly1305_decrypt(u8 *dst, const u8 *src, const size_t src_len,
			   const u8 *ad, const size_t ad_len, u32 *chacha_state)
{
	const u8 *pad0 = page_address(ZERO_PAGE(0));
	struct poly1305_desc_ctx poly1305_state;
	size_t dst_len;
	int ret;
	union {
		u8 block0[POLY1305_KEY_SIZE];
		u8 mac[POLY1305_DIGEST_SIZE];
		__le64 lens[2];
	} b;

	if (unlikely(src_len < POLY1305_DIGEST_SIZE))
		return false;

	chacha20_crypt(chacha_state, b.block0, pad0, sizeof(b.block0));
	poly1305_init(&poly1305_state, b.block0);

	poly1305_update(&poly1305_state, ad, ad_len);
	if (ad_len & 0xf)
		poly1305_update(&poly1305_state, pad0, 0x10 - (ad_len & 0xf));

	dst_len = src_len - POLY1305_DIGEST_SIZE;
	poly1305_update(&poly1305_state, src, dst_len);
	if (dst_len & 0xf)
		poly1305_update(&poly1305_state, pad0, 0x10 - (dst_len & 0xf));

	b.lens[0] = cpu_to_le64(ad_len);
	b.lens[1] = cpu_to_le64(dst_len);
	poly1305_update(&poly1305_state, (u8 *)b.lens, sizeof(b.lens));

	poly1305_final(&poly1305_state, b.mac);

	ret = crypto_memneq(b.mac, src + dst_len, POLY1305_DIGEST_SIZE);
	if (likely(!ret))
		chacha20_crypt(chacha_state, dst, src, dst_len);

	memzero_explicit(&b, sizeof(b));

	return !ret;
}

bool chacha20poly1305_decrypt(u8 *dst, const u8 *src, const size_t src_len,
			      const u8 *ad, const size_t ad_len,
			      const u64 nonce,
			      const u8 key[CHACHA20POLY1305_KEY_SIZE])
{
	u32 chacha_state[CHACHA_STATE_WORDS];
	u32 k[CHACHA_KEY_WORDS];
	__le64 iv[2];
	bool ret;

	chacha_load_key(k, key);

	iv[0] = 0;
	iv[1] = cpu_to_le64(nonce);

	chacha_init(chacha_state, k, (u8 *)iv);
	ret = __chacha20poly1305_decrypt(dst, src, src_len, ad, ad_len,
					 chacha_state);

	memzero_explicit(chacha_state, sizeof(chacha_state));
	memzero_explicit(iv, sizeof(iv));
	memzero_explicit(k, sizeof(k));
	return ret;
}
EXPORT_SYMBOL(chacha20poly1305_decrypt);

bool xchacha20poly1305_decrypt(u8 *dst, const u8 *src, const size_t src_len,
			       const u8 *ad, const size_t ad_len,
			       const u8 nonce[XCHACHA20POLY1305_NONCE_SIZE],
			       const u8 key[CHACHA20POLY1305_KEY_SIZE])
{
	u32 chacha_state[CHACHA_STATE_WORDS];

	xchacha_init(chacha_state, key, nonce);
	return __chacha20poly1305_decrypt(dst, src, src_len, ad, ad_len,
					  chacha_state);
}
EXPORT_SYMBOL(xchacha20poly1305_decrypt);

static
bool chacha20poly1305_crypt_sg_inplace(struct scatterlist *src,
				       const size_t src_len,
				       const u8 *ad, const size_t ad_len,
				       const u64 nonce,
				       const u8 key[CHACHA20POLY1305_KEY_SIZE],
				       int encrypt)
{
	const u8 *pad0 = page_address(ZERO_PAGE(0));
	struct poly1305_desc_ctx poly1305_state;
	u32 chacha_state[CHACHA_STATE_WORDS];
	struct sg_mapping_iter miter;
	size_t partial = 0;
	unsigned int flags;
	bool ret = true;
	int sl;
	union {
		struct {
			u32 k[CHACHA_KEY_WORDS];
			__le64 iv[2];
		};
		u8 block0[POLY1305_KEY_SIZE];
		u8 chacha_stream[CHACHA_BLOCK_SIZE];
		struct {
			u8 mac[2][POLY1305_DIGEST_SIZE];
		};
		__le64 lens[2];
	} b __aligned(16);

	chacha_load_key(b.k, key);

	b.iv[0] = 0;
	b.iv[1] = cpu_to_le64(nonce);

	chacha_init(chacha_state, b.k, (u8 *)b.iv);
	chacha20_crypt(chacha_state, b.block0, pad0, sizeof(b.block0));
	poly1305_init(&poly1305_state, b.block0);

	if (unlikely(ad_len)) {
		poly1305_update(&poly1305_state, ad, ad_len);
		if (ad_len & 0xf)
			poly1305_update(&poly1305_state, pad0, 0x10 - (ad_len & 0xf));
	}

	flags = SG_MITER_TO_SG;
	if (!preemptible())
		flags |= SG_MITER_ATOMIC;

	sg_miter_start(&miter, src, sg_nents(src), flags);

	for (sl = src_len; sl > 0 && sg_miter_next(&miter); sl -= miter.length) {
		u8 *addr = miter.addr;
		size_t length = min_t(size_t, sl, miter.length);

		if (!encrypt)
			poly1305_update(&poly1305_state, addr, length);

		if (unlikely(partial)) {
			size_t l = min(length, CHACHA_BLOCK_SIZE - partial);

			crypto_xor(addr, b.chacha_stream + partial, l);
			partial = (partial + l) & (CHACHA_BLOCK_SIZE - 1);

			addr += l;
			length -= l;
		}

		if (likely(length >= CHACHA_BLOCK_SIZE || length == sl)) {
			size_t l = length;

			if (unlikely(length < sl))
				l &= ~(CHACHA_BLOCK_SIZE - 1);
			chacha20_crypt(chacha_state, addr, addr, l);
			addr += l;
			length -= l;
		}

		if (unlikely(length > 0)) {
			chacha20_crypt(chacha_state, b.chacha_stream, pad0,
				       CHACHA_BLOCK_SIZE);
			crypto_xor(addr, b.chacha_stream, length);
			partial = length;
		}

		if (encrypt)
			poly1305_update(&poly1305_state, miter.addr,
					min_t(size_t, sl, miter.length));
	}

	if (src_len & 0xf)
		poly1305_update(&poly1305_state, pad0, 0x10 - (src_len & 0xf));

	b.lens[0] = cpu_to_le64(ad_len);
	b.lens[1] = cpu_to_le64(src_len);
	poly1305_update(&poly1305_state, (u8 *)b.lens, sizeof(b.lens));

	if (likely(sl <= -POLY1305_DIGEST_SIZE)) {
		if (encrypt) {
			poly1305_final(&poly1305_state,
				       miter.addr + miter.length + sl);
			ret = true;
		} else {
			poly1305_final(&poly1305_state, b.mac[0]);
			ret = !crypto_memneq(b.mac[0],
					     miter.addr + miter.length + sl,
					     POLY1305_DIGEST_SIZE);
		}
	}

	sg_miter_stop(&miter);

	if (unlikely(sl > -POLY1305_DIGEST_SIZE)) {
		poly1305_final(&poly1305_state, b.mac[1]);
		scatterwalk_map_and_copy(b.mac[encrypt], src, src_len,
					 sizeof(b.mac[1]), encrypt);
		ret = encrypt ||
		      !crypto_memneq(b.mac[0], b.mac[1], POLY1305_DIGEST_SIZE);
	}

	memzero_explicit(chacha_state, sizeof(chacha_state));
	memzero_explicit(&b, sizeof(b));

	return ret;
}

bool chacha20poly1305_encrypt_sg_inplace(struct scatterlist *src, size_t src_len,
					 const u8 *ad, const size_t ad_len,
					 const u64 nonce,
					 const u8 key[CHACHA20POLY1305_KEY_SIZE])
{
	return chacha20poly1305_crypt_sg_inplace(src, src_len, ad, ad_len,
						 nonce, key, 1);
}
EXPORT_SYMBOL(chacha20poly1305_encrypt_sg_inplace);

bool chacha20poly1305_decrypt_sg_inplace(struct scatterlist *src, size_t src_len,
					 const u8 *ad, const size_t ad_len,
					 const u64 nonce,
					 const u8 key[CHACHA20POLY1305_KEY_SIZE])
{
	if (unlikely(src_len < POLY1305_DIGEST_SIZE))
		return false;

	return chacha20poly1305_crypt_sg_inplace(src,
						 src_len - POLY1305_DIGEST_SIZE,
						 ad, ad_len, nonce, key, 0);
}
EXPORT_SYMBOL(chacha20poly1305_decrypt_sg_inplace);

static int __init mod_init(void)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_MANAGER_DISABLE_TESTS) &&
	    WARN_ON(!chacha20poly1305_selftest()))
		return -ENODEV;
	return 0;
}

module_init(mod_init);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChaCha20Poly1305 AEAD construction");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
