/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values and helper functions for the ChaCha and XChaCha stream ciphers.
 *
 * XChaCha extends ChaCha's nonce to 192 bits, while provably retaining ChaCha's
 * security.  Here they share the same key size, tfm context, and setkey
 * function; only their IV size and encrypt/decrypt function differ.
 *
 * The ChaCha paper specifies 20, 12, and 8-round variants.  In general, it is
 * recommended to use the 20-round variant ChaCha20.  However, the other
 * variants can be needed in some performance-sensitive scenarios.  The generic
 * ChaCha code currently allows only the 20 and 12-round variants.
 */

#ifndef _CRYPTO_CHACHA_H
#define _CRYPTO_CHACHA_H

#include <linux/unaligned.h>
#include <linux/string.h>
#include <linux/types.h>

/* 32-bit stream position, then 96-bit nonce (RFC7539 convention) */
#define CHACHA_IV_SIZE		16

#define CHACHA_KEY_SIZE		32
#define CHACHA_BLOCK_SIZE	64
#define CHACHAPOLY_IV_SIZE	12

#define CHACHA_KEY_WORDS	8
#define CHACHA_STATE_WORDS	16
#define HCHACHA_OUT_WORDS	8

/* 192-bit nonce, then 64-bit stream position */
#define XCHACHA_IV_SIZE		32

struct chacha_state {
	u32 x[CHACHA_STATE_WORDS];
};

void chacha_block_generic(struct chacha_state *state,
			  u8 out[CHACHA_BLOCK_SIZE], int nrounds);
static inline void chacha20_block(struct chacha_state *state,
				  u8 out[CHACHA_BLOCK_SIZE])
{
	chacha_block_generic(state, out, 20);
}

void hchacha_block_generic(const struct chacha_state *state,
			   u32 out[HCHACHA_OUT_WORDS], int nrounds);

void hchacha_block(const struct chacha_state *state,
		   u32 out[HCHACHA_OUT_WORDS], int nrounds);

enum chacha_constants { /* expand 32-byte k */
	CHACHA_CONSTANT_EXPA = 0x61707865U,
	CHACHA_CONSTANT_ND_3 = 0x3320646eU,
	CHACHA_CONSTANT_2_BY = 0x79622d32U,
	CHACHA_CONSTANT_TE_K = 0x6b206574U
};

static inline void chacha_init_consts(struct chacha_state *state)
{
	state->x[0]  = CHACHA_CONSTANT_EXPA;
	state->x[1]  = CHACHA_CONSTANT_ND_3;
	state->x[2]  = CHACHA_CONSTANT_2_BY;
	state->x[3]  = CHACHA_CONSTANT_TE_K;
}

static inline void chacha_init(struct chacha_state *state,
			       const u32 key[CHACHA_KEY_WORDS],
			       const u8 iv[CHACHA_IV_SIZE])
{
	chacha_init_consts(state);
	state->x[4]  = key[0];
	state->x[5]  = key[1];
	state->x[6]  = key[2];
	state->x[7]  = key[3];
	state->x[8]  = key[4];
	state->x[9]  = key[5];
	state->x[10] = key[6];
	state->x[11] = key[7];
	state->x[12] = get_unaligned_le32(iv +  0);
	state->x[13] = get_unaligned_le32(iv +  4);
	state->x[14] = get_unaligned_le32(iv +  8);
	state->x[15] = get_unaligned_le32(iv + 12);
}

void chacha_crypt(struct chacha_state *state, u8 *dst, const u8 *src,
		  unsigned int bytes, int nrounds);

static inline void chacha20_crypt(struct chacha_state *state,
				  u8 *dst, const u8 *src, unsigned int bytes)
{
	chacha_crypt(state, dst, src, bytes, 20);
}

static inline void chacha_zeroize_state(struct chacha_state *state)
{
	memzero_explicit(state, sizeof(*state));
}

#endif /* _CRYPTO_CHACHA_H */
