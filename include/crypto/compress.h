/*
 * Compress: Compression algorithms under the cryptographic API.
 *
 * Copyright 2008 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CRYPTO_COMPRESS_H
#define _CRYPTO_COMPRESS_H

#include <linux/crypto.h>


struct comp_request {
	const void *next_in;		/* next input byte */
	void *next_out;			/* next output byte */
	unsigned int avail_in;		/* bytes available at next_in */
	unsigned int avail_out;		/* bytes available at next_out */
};

enum zlib_comp_params {
	ZLIB_COMP_LEVEL = 1,		/* e.g. Z_DEFAULT_COMPRESSION */
	ZLIB_COMP_METHOD,		/* e.g. Z_DEFLATED */
	ZLIB_COMP_WINDOWBITS,		/* e.g. MAX_WBITS */
	ZLIB_COMP_MEMLEVEL,		/* e.g. DEF_MEM_LEVEL */
	ZLIB_COMP_STRATEGY,		/* e.g. Z_DEFAULT_STRATEGY */
	__ZLIB_COMP_MAX,
};

#define ZLIB_COMP_MAX	(__ZLIB_COMP_MAX - 1)


enum zlib_decomp_params {
	ZLIB_DECOMP_WINDOWBITS = 1,	/* e.g. DEF_WBITS */
	__ZLIB_DECOMP_MAX,
};

#define ZLIB_DECOMP_MAX	(__ZLIB_DECOMP_MAX - 1)


struct crypto_pcomp {
	struct crypto_tfm base;
};

struct pcomp_alg {
	int (*compress_setup)(struct crypto_pcomp *tfm, const void *params,
			      unsigned int len);
	int (*compress_init)(struct crypto_pcomp *tfm);
	int (*compress_update)(struct crypto_pcomp *tfm,
			       struct comp_request *req);
	int (*compress_final)(struct crypto_pcomp *tfm,
			      struct comp_request *req);
	int (*decompress_setup)(struct crypto_pcomp *tfm, const void *params,
				unsigned int len);
	int (*decompress_init)(struct crypto_pcomp *tfm);
	int (*decompress_update)(struct crypto_pcomp *tfm,
				 struct comp_request *req);
	int (*decompress_final)(struct crypto_pcomp *tfm,
				struct comp_request *req);

	struct crypto_alg base;
};

extern struct crypto_pcomp *crypto_alloc_pcomp(const char *alg_name, u32 type,
					       u32 mask);

static inline struct crypto_tfm *crypto_pcomp_tfm(struct crypto_pcomp *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_pcomp(struct crypto_pcomp *tfm)
{
	crypto_destroy_tfm(tfm, crypto_pcomp_tfm(tfm));
}

static inline struct pcomp_alg *__crypto_pcomp_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct pcomp_alg, base);
}

static inline struct pcomp_alg *crypto_pcomp_alg(struct crypto_pcomp *tfm)
{
	return __crypto_pcomp_alg(crypto_pcomp_tfm(tfm)->__crt_alg);
}

static inline int crypto_compress_setup(struct crypto_pcomp *tfm,
					const void *params, unsigned int len)
{
	return crypto_pcomp_alg(tfm)->compress_setup(tfm, params, len);
}

static inline int crypto_compress_init(struct crypto_pcomp *tfm)
{
	return crypto_pcomp_alg(tfm)->compress_init(tfm);
}

static inline int crypto_compress_update(struct crypto_pcomp *tfm,
					 struct comp_request *req)
{
	return crypto_pcomp_alg(tfm)->compress_update(tfm, req);
}

static inline int crypto_compress_final(struct crypto_pcomp *tfm,
					struct comp_request *req)
{
	return crypto_pcomp_alg(tfm)->compress_final(tfm, req);
}

static inline int crypto_decompress_setup(struct crypto_pcomp *tfm,
					  const void *params, unsigned int len)
{
	return crypto_pcomp_alg(tfm)->decompress_setup(tfm, params, len);
}

static inline int crypto_decompress_init(struct crypto_pcomp *tfm)
{
	return crypto_pcomp_alg(tfm)->decompress_init(tfm);
}

static inline int crypto_decompress_update(struct crypto_pcomp *tfm,
					   struct comp_request *req)
{
	return crypto_pcomp_alg(tfm)->decompress_update(tfm, req);
}

static inline int crypto_decompress_final(struct crypto_pcomp *tfm,
					  struct comp_request *req)
{
	return crypto_pcomp_alg(tfm)->decompress_final(tfm, req);
}

#endif	/* _CRYPTO_COMPRESS_H */
