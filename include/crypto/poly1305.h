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
	struct poly1305_key r[1];
};

#endif
