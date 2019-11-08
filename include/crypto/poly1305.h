/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the Poly1305 algorithm
 */

#ifndef _CRYPTO_POLY1305_H
#define _CRYPTO_POLY1305_H

#include <linux/types.h>
#include <linux/crypto.h>

#define POLY1305_BLOCK_SIZE	16
#define POLY1305_KEY_SIZE	32
#define POLY1305_DIGEST_SIZE	16

struct poly1305_key {
	u32 r[5];	/* key, base 2^26 */
};

struct poly1305_state {
	u32 h[5];	/* accumulator, base 2^26 */
};

struct poly1305_desc_ctx {
	/* partial buffer */
	u8 buf[POLY1305_BLOCK_SIZE];
	/* bytes used in partial buffer */
	unsigned int buflen;
	/* how many keys have been set in r[] */
	unsigned short rset;
	/* whether s[] has been set */
	bool sset;
	/* finalize key */
	u32 s[4];
	/* accumulator */
	struct poly1305_state h;
	/* key */
	struct poly1305_key r[CONFIG_CRYPTO_LIB_POLY1305_RSIZE];
};

void poly1305_init_arch(struct poly1305_desc_ctx *desc, const u8 *key);
void poly1305_init_generic(struct poly1305_desc_ctx *desc, const u8 *key);

static inline void poly1305_init(struct poly1305_desc_ctx *desc, const u8 *key)
{
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_POLY1305))
		poly1305_init_arch(desc, key);
	else
		poly1305_init_generic(desc, key);
}

void poly1305_update_arch(struct poly1305_desc_ctx *desc, const u8 *src,
			  unsigned int nbytes);
void poly1305_update_generic(struct poly1305_desc_ctx *desc, const u8 *src,
			     unsigned int nbytes);

static inline void poly1305_update(struct poly1305_desc_ctx *desc,
				   const u8 *src, unsigned int nbytes)
{
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_POLY1305))
		poly1305_update_arch(desc, src, nbytes);
	else
		poly1305_update_generic(desc, src, nbytes);
}

void poly1305_final_arch(struct poly1305_desc_ctx *desc, u8 *digest);
void poly1305_final_generic(struct poly1305_desc_ctx *desc, u8 *digest);

static inline void poly1305_final(struct poly1305_desc_ctx *desc, u8 *digest)
{
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_POLY1305))
		poly1305_final_arch(desc, digest);
	else
		poly1305_final_generic(desc, digest);
}

#endif
