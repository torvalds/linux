// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2019 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <crypto/aes.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <trace/hooks/fips140.h>

/*
 * Emit the sbox as volatile const to prevent the compiler from doing
 * constant folding on sbox references involving fixed indexes.
 */
static volatile const u8 __cacheline_aligned aes_sbox[] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static volatile const u8 __cacheline_aligned aes_inv_sbox[] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

extern const u8 crypto_aes_sbox[256] __alias(aes_sbox);
extern const u8 crypto_aes_inv_sbox[256] __alias(aes_inv_sbox);

EXPORT_SYMBOL(crypto_aes_sbox);
EXPORT_SYMBOL(crypto_aes_inv_sbox);

static u32 mul_by_x(u32 w)
{
	u32 x = w & 0x7f7f7f7f;
	u32 y = w & 0x80808080;

	/* multiply by polynomial 'x' (0b10) in GF(2^8) */
	return (x << 1) ^ (y >> 7) * 0x1b;
}

static u32 mul_by_x2(u32 w)
{
	u32 x = w & 0x3f3f3f3f;
	u32 y = w & 0x80808080;
	u32 z = w & 0x40404040;

	/* multiply by polynomial 'x^2' (0b100) in GF(2^8) */
	return (x << 2) ^ (y >> 7) * 0x36 ^ (z >> 6) * 0x1b;
}

static u32 mix_columns(u32 x)
{
	/*
	 * Perform the following matrix multiplication in GF(2^8)
	 *
	 * | 0x2 0x3 0x1 0x1 |   | x[0] |
	 * | 0x1 0x2 0x3 0x1 |   | x[1] |
	 * | 0x1 0x1 0x2 0x3 | x | x[2] |
	 * | 0x3 0x1 0x1 0x2 |   | x[3] |
	 */
	u32 y = mul_by_x(x) ^ ror32(x, 16);

	return y ^ ror32(x ^ y, 8);
}

static u32 inv_mix_columns(u32 x)
{
	/*
	 * Perform the following matrix multiplication in GF(2^8)
	 *
	 * | 0xe 0xb 0xd 0x9 |   | x[0] |
	 * | 0x9 0xe 0xb 0xd |   | x[1] |
	 * | 0xd 0x9 0xe 0xb | x | x[2] |
	 * | 0xb 0xd 0x9 0xe |   | x[3] |
	 *
	 * which can conveniently be reduced to
	 *
	 * | 0x2 0x3 0x1 0x1 |   | 0x5 0x0 0x4 0x0 |   | x[0] |
	 * | 0x1 0x2 0x3 0x1 |   | 0x0 0x5 0x0 0x4 |   | x[1] |
	 * | 0x1 0x1 0x2 0x3 | x | 0x4 0x0 0x5 0x0 | x | x[2] |
	 * | 0x3 0x1 0x1 0x2 |   | 0x0 0x4 0x0 0x5 |   | x[3] |
	 */
	u32 y = mul_by_x2(x);

	return mix_columns(x ^ y ^ ror32(y, 16));
}

static __always_inline u32 subshift(u32 in[], int pos)
{
	return (aes_sbox[in[pos] & 0xff]) ^
	       (aes_sbox[(in[(pos + 1) % 4] >>  8) & 0xff] <<  8) ^
	       (aes_sbox[(in[(pos + 2) % 4] >> 16) & 0xff] << 16) ^
	       (aes_sbox[(in[(pos + 3) % 4] >> 24) & 0xff] << 24);
}

static __always_inline u32 inv_subshift(u32 in[], int pos)
{
	return (aes_inv_sbox[in[pos] & 0xff]) ^
	       (aes_inv_sbox[(in[(pos + 3) % 4] >>  8) & 0xff] <<  8) ^
	       (aes_inv_sbox[(in[(pos + 2) % 4] >> 16) & 0xff] << 16) ^
	       (aes_inv_sbox[(in[(pos + 1) % 4] >> 24) & 0xff] << 24);
}

static u32 subw(u32 in)
{
	return (aes_sbox[in & 0xff]) ^
	       (aes_sbox[(in >>  8) & 0xff] <<  8) ^
	       (aes_sbox[(in >> 16) & 0xff] << 16) ^
	       (aes_sbox[(in >> 24) & 0xff] << 24);
}

/**
 * aes_expandkey - Expands the AES key as described in FIPS-197
 * @ctx:	The location where the computed key will be stored.
 * @in_key:	The supplied key.
 * @key_len:	The length of the supplied key.
 *
 * Returns 0 on success. The function fails only if an invalid key size (or
 * pointer) is supplied.
 * The expanded key size is 240 bytes (max of 14 rounds with a unique 16 bytes
 * key schedule plus a 16 bytes key which is used before the first round).
 * The decryption key is prepared for the "Equivalent Inverse Cipher" as
 * described in FIPS-197. The first slot (16 bytes) of each key (enc or dec) is
 * for the initial combination, the second slot for the first round and so on.
 */
int aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
		  unsigned int key_len)
{
	u32 kwords = key_len / sizeof(u32);
	u32 rc, i, j;
	int err;

#ifndef __DISABLE_EXPORTS
	err = -(MAX_ERRNO + 1);
	trace_android_vh_aes_expandkey(ctx, in_key, key_len, &err);
	if (err != -(MAX_ERRNO + 1))
		return err;
#endif

	err = aes_check_keylen(key_len);
	if (err)
		return err;

	ctx->key_length = key_len;

	for (i = 0; i < kwords; i++)
		ctx->key_enc[i] = get_unaligned_le32(in_key + i * sizeof(u32));

	for (i = 0, rc = 1; i < 10; i++, rc = mul_by_x(rc)) {
		u32 *rki = ctx->key_enc + (i * kwords);
		u32 *rko = rki + kwords;

		rko[0] = ror32(subw(rki[kwords - 1]), 8) ^ rc ^ rki[0];
		rko[1] = rko[0] ^ rki[1];
		rko[2] = rko[1] ^ rki[2];
		rko[3] = rko[2] ^ rki[3];

		if (key_len == AES_KEYSIZE_192) {
			if (i >= 7)
				break;
			rko[4] = rko[3] ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
		} else if (key_len == AES_KEYSIZE_256) {
			if (i >= 6)
				break;
			rko[4] = subw(rko[3]) ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
			rko[6] = rko[5] ^ rki[6];
			rko[7] = rko[6] ^ rki[7];
		}
	}

	/*
	 * Generate the decryption keys for the Equivalent Inverse Cipher.
	 * This involves reversing the order of the round keys, and applying
	 * the Inverse Mix Columns transformation to all but the first and
	 * the last one.
	 */
	ctx->key_dec[0] = ctx->key_enc[key_len + 24];
	ctx->key_dec[1] = ctx->key_enc[key_len + 25];
	ctx->key_dec[2] = ctx->key_enc[key_len + 26];
	ctx->key_dec[3] = ctx->key_enc[key_len + 27];

	for (i = 4, j = key_len + 20; j > 0; i += 4, j -= 4) {
		ctx->key_dec[i]     = inv_mix_columns(ctx->key_enc[j]);
		ctx->key_dec[i + 1] = inv_mix_columns(ctx->key_enc[j + 1]);
		ctx->key_dec[i + 2] = inv_mix_columns(ctx->key_enc[j + 2]);
		ctx->key_dec[i + 3] = inv_mix_columns(ctx->key_enc[j + 3]);
	}

	ctx->key_dec[i]     = ctx->key_enc[0];
	ctx->key_dec[i + 1] = ctx->key_enc[1];
	ctx->key_dec[i + 2] = ctx->key_enc[2];
	ctx->key_dec[i + 3] = ctx->key_enc[3];

	return 0;
}
EXPORT_SYMBOL(aes_expandkey);

/**
 * aes_encrypt - Encrypt a single AES block
 * @ctx:	Context struct containing the key schedule
 * @out:	Buffer to store the ciphertext
 * @in:		Buffer containing the plaintext
 */
void aes_encrypt(const struct crypto_aes_ctx *ctx, u8 *out, const u8 *in)
{
	const u32 *rkp = ctx->key_enc + 4;
	int rounds = 6 + ctx->key_length / 4;
	u32 st0[4], st1[4];
	int round;
#ifndef __DISABLE_EXPORTS
	int hook_inuse = 0;

	trace_android_vh_aes_encrypt(ctx, out, in, &hook_inuse);
	if (hook_inuse)
		return;
#endif

	st0[0] = ctx->key_enc[0] ^ get_unaligned_le32(in);
	st0[1] = ctx->key_enc[1] ^ get_unaligned_le32(in + 4);
	st0[2] = ctx->key_enc[2] ^ get_unaligned_le32(in + 8);
	st0[3] = ctx->key_enc[3] ^ get_unaligned_le32(in + 12);

	/*
	 * Force the compiler to emit data independent Sbox references,
	 * by xoring the input with Sbox values that are known to add up
	 * to zero. This pulls the entire Sbox into the D-cache before any
	 * data dependent lookups are done.
	 */
	st0[0] ^= aes_sbox[ 0] ^ aes_sbox[ 64] ^ aes_sbox[134] ^ aes_sbox[195];
	st0[1] ^= aes_sbox[16] ^ aes_sbox[ 82] ^ aes_sbox[158] ^ aes_sbox[221];
	st0[2] ^= aes_sbox[32] ^ aes_sbox[ 96] ^ aes_sbox[160] ^ aes_sbox[234];
	st0[3] ^= aes_sbox[48] ^ aes_sbox[112] ^ aes_sbox[186] ^ aes_sbox[241];

	for (round = 0;; round += 2, rkp += 8) {
		st1[0] = mix_columns(subshift(st0, 0)) ^ rkp[0];
		st1[1] = mix_columns(subshift(st0, 1)) ^ rkp[1];
		st1[2] = mix_columns(subshift(st0, 2)) ^ rkp[2];
		st1[3] = mix_columns(subshift(st0, 3)) ^ rkp[3];

		if (round == rounds - 2)
			break;

		st0[0] = mix_columns(subshift(st1, 0)) ^ rkp[4];
		st0[1] = mix_columns(subshift(st1, 1)) ^ rkp[5];
		st0[2] = mix_columns(subshift(st1, 2)) ^ rkp[6];
		st0[3] = mix_columns(subshift(st1, 3)) ^ rkp[7];
	}

	put_unaligned_le32(subshift(st1, 0) ^ rkp[4], out);
	put_unaligned_le32(subshift(st1, 1) ^ rkp[5], out + 4);
	put_unaligned_le32(subshift(st1, 2) ^ rkp[6], out + 8);
	put_unaligned_le32(subshift(st1, 3) ^ rkp[7], out + 12);
}
EXPORT_SYMBOL(aes_encrypt);

/**
 * aes_decrypt - Decrypt a single AES block
 * @ctx:	Context struct containing the key schedule
 * @out:	Buffer to store the plaintext
 * @in:		Buffer containing the ciphertext
 */
void aes_decrypt(const struct crypto_aes_ctx *ctx, u8 *out, const u8 *in)
{
	const u32 *rkp = ctx->key_dec + 4;
	int rounds = 6 + ctx->key_length / 4;
	u32 st0[4], st1[4];
	int round;
#ifndef __DISABLE_EXPORTS
	int hook_inuse = 0;

	trace_android_vh_aes_decrypt(ctx, out, in, &hook_inuse);
	if (hook_inuse)
		return;
#endif

	st0[0] = ctx->key_dec[0] ^ get_unaligned_le32(in);
	st0[1] = ctx->key_dec[1] ^ get_unaligned_le32(in + 4);
	st0[2] = ctx->key_dec[2] ^ get_unaligned_le32(in + 8);
	st0[3] = ctx->key_dec[3] ^ get_unaligned_le32(in + 12);

	/*
	 * Force the compiler to emit data independent Sbox references,
	 * by xoring the input with Sbox values that are known to add up
	 * to zero. This pulls the entire Sbox into the D-cache before any
	 * data dependent lookups are done.
	 */
	st0[0] ^= aes_inv_sbox[ 0] ^ aes_inv_sbox[ 64] ^ aes_inv_sbox[129] ^ aes_inv_sbox[200];
	st0[1] ^= aes_inv_sbox[16] ^ aes_inv_sbox[ 83] ^ aes_inv_sbox[150] ^ aes_inv_sbox[212];
	st0[2] ^= aes_inv_sbox[32] ^ aes_inv_sbox[ 96] ^ aes_inv_sbox[160] ^ aes_inv_sbox[236];
	st0[3] ^= aes_inv_sbox[48] ^ aes_inv_sbox[112] ^ aes_inv_sbox[187] ^ aes_inv_sbox[247];

	for (round = 0;; round += 2, rkp += 8) {
		st1[0] = inv_mix_columns(inv_subshift(st0, 0)) ^ rkp[0];
		st1[1] = inv_mix_columns(inv_subshift(st0, 1)) ^ rkp[1];
		st1[2] = inv_mix_columns(inv_subshift(st0, 2)) ^ rkp[2];
		st1[3] = inv_mix_columns(inv_subshift(st0, 3)) ^ rkp[3];

		if (round == rounds - 2)
			break;

		st0[0] = inv_mix_columns(inv_subshift(st1, 0)) ^ rkp[4];
		st0[1] = inv_mix_columns(inv_subshift(st1, 1)) ^ rkp[5];
		st0[2] = inv_mix_columns(inv_subshift(st1, 2)) ^ rkp[6];
		st0[3] = inv_mix_columns(inv_subshift(st1, 3)) ^ rkp[7];
	}

	put_unaligned_le32(inv_subshift(st1, 0) ^ rkp[4], out);
	put_unaligned_le32(inv_subshift(st1, 1) ^ rkp[5], out + 4);
	put_unaligned_le32(inv_subshift(st1, 2) ^ rkp[6], out + 8);
	put_unaligned_le32(inv_subshift(st1, 3) ^ rkp[7], out + 12);
}
EXPORT_SYMBOL(aes_decrypt);

MODULE_DESCRIPTION("Generic AES library");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
