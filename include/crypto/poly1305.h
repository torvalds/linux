/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the Poly1305 algorithm
 */

#ifndef _CRYPTO_POLY1305_H
#define _CRYPTO_POLY1305_H

#include <linux/types.h>

#define POLY1305_BLOCK_SIZE	16
#define POLY1305_KEY_SIZE	32
#define POLY1305_DIGEST_SIZE	16

/* The poly1305_key and poly1305_state types are mostly opaque and
 * implementation-defined. Limbs might be in base 2^64 or base 2^26, or
 * different yet. The union type provided keeps these 64-bit aligned for the
 * case in which this is implemented using 64x64 multiplies.
 */

struct poly1305_key {
	union {
		u32 r[5];
		u64 r64[3];
	};
};

struct poly1305_core_key {
	struct poly1305_key key;
	struct poly1305_key precomputed_s;
};

struct poly1305_state {
	union {
		u32 h[5];
		u64 h64[3];
	};
};

/* Combined state for block function. */
struct poly1305_block_state {
	/* accumulator */
	struct poly1305_state h;
	/* key */
	union {
		struct poly1305_key opaque_r[CONFIG_CRYPTO_LIB_POLY1305_RSIZE];
		struct poly1305_core_key core_r;
	};
};

struct poly1305_desc_ctx {
	/* partial buffer */
	u8 buf[POLY1305_BLOCK_SIZE];
	/* bytes used in partial buffer */
	unsigned int buflen;
	/* finalize key */
	u32 s[4];
	struct poly1305_block_state state;
};

void poly1305_init(struct poly1305_desc_ctx *desc,
		   const u8 key[POLY1305_KEY_SIZE]);
void poly1305_update(struct poly1305_desc_ctx *desc,
		     const u8 *src, unsigned int nbytes);
void poly1305_final(struct poly1305_desc_ctx *desc, u8 *digest);

#endif
