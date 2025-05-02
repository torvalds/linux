// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-256, as specified in
 * http://csrc.nist.gov/groups/STM/cavp/documents/shs/sha256-384-512.pdf
 *
 * SHA-256 code by Jean-Luc Cooke <jlcooke@certainkey.com>.
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2014 Red Hat Inc.
 */

#include <crypto/internal/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

/*
 * If __DISABLE_EXPORTS is defined, then this file is being compiled for a
 * pre-boot environment.  In that case, ignore the kconfig options, pull the
 * generic code into the same translation unit, and use that only.
 */
#ifdef __DISABLE_EXPORTS
#include "sha256-generic.c"
#endif

static inline bool sha256_purgatory(void)
{
	return __is_defined(__DISABLE_EXPORTS);
}

static inline void sha256_blocks(u32 state[SHA256_STATE_WORDS], const u8 *data,
				 size_t nblocks, bool force_generic)
{
	sha256_choose_blocks(state, data, nblocks,
			     force_generic || sha256_purgatory(), false);
}

static inline void __sha256_update(struct sha256_state *sctx, const u8 *data,
				   size_t len, bool force_generic)
{
	size_t partial = sctx->count % SHA256_BLOCK_SIZE;

	sctx->count += len;

	if (partial + len >= SHA256_BLOCK_SIZE) {
		size_t nblocks;

		if (partial) {
			size_t l = SHA256_BLOCK_SIZE - partial;

			memcpy(&sctx->buf[partial], data, l);
			data += l;
			len -= l;

			sha256_blocks(sctx->state, sctx->buf, 1, force_generic);
		}

		nblocks = len / SHA256_BLOCK_SIZE;
		len %= SHA256_BLOCK_SIZE;

		if (nblocks) {
			sha256_blocks(sctx->state, data, nblocks,
				      force_generic);
			data += nblocks * SHA256_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(&sctx->buf[partial], data, len);
}

void sha256_update(struct sha256_state *sctx, const u8 *data, size_t len)
{
	__sha256_update(sctx, data, len, false);
}
EXPORT_SYMBOL(sha256_update);

static inline void __sha256_final(struct sha256_state *sctx, u8 *out,
				  size_t digest_size, bool force_generic)
{
	size_t partial = sctx->count % SHA256_BLOCK_SIZE;

	sha256_finup(&sctx->ctx, sctx->buf, partial, out, digest_size,
		     force_generic || sha256_purgatory(), false);
	memzero_explicit(sctx, sizeof(*sctx));
}

void sha256_final(struct sha256_state *sctx, u8 out[SHA256_DIGEST_SIZE])
{
	__sha256_final(sctx, out, SHA256_DIGEST_SIZE, false);
}
EXPORT_SYMBOL(sha256_final);

void sha224_final(struct sha256_state *sctx, u8 out[SHA224_DIGEST_SIZE])
{
	__sha256_final(sctx, out, SHA224_DIGEST_SIZE, false);
}
EXPORT_SYMBOL(sha224_final);

void sha256(const u8 *data, size_t len, u8 out[SHA256_DIGEST_SIZE])
{
	struct sha256_state sctx;

	sha256_init(&sctx);
	sha256_update(&sctx, data, len);
	sha256_final(&sctx, out);
}
EXPORT_SYMBOL(sha256);

#if IS_ENABLED(CONFIG_CRYPTO_SHA256) && !defined(__DISABLE_EXPORTS)
void sha256_update_generic(struct sha256_state *sctx,
			   const u8 *data, size_t len)
{
	__sha256_update(sctx, data, len, true);
}
EXPORT_SYMBOL(sha256_update_generic);

void sha256_final_generic(struct sha256_state *sctx, u8 out[SHA256_DIGEST_SIZE])
{
	__sha256_final(sctx, out, SHA256_DIGEST_SIZE, true);
}
EXPORT_SYMBOL(sha256_final_generic);

void sha224_final_generic(struct sha256_state *sctx, u8 out[SHA224_DIGEST_SIZE])
{
	__sha256_final(sctx, out, SHA224_DIGEST_SIZE, true);
}
EXPORT_SYMBOL(sha224_final_generic);
#endif

MODULE_DESCRIPTION("SHA-256 Algorithm");
MODULE_LICENSE("GPL");
