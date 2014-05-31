/*
 * DRBG based on NIST SP800-90A
 *
 * Copyright Stephan Mueller <smueller@chronox.de>, 2014
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef _DRBG_H
#define _DRBG_H


#include <linux/random.h>
#include <linux/scatterlist.h>
#include <crypto/hash.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/slab.h>
#include <crypto/internal/rng.h>
#include <crypto/rng.h>
#include <linux/fips.h>
#include <linux/spinlock.h>

/*
 * Concatenation Helper and string operation helper
 *
 * SP800-90A requires the concatenation of different data. To avoid copying
 * buffers around or allocate additional memory, the following data structure
 * is used to point to the original memory with its size. In addition, it
 * is used to build a linked list. The linked list defines the concatenation
 * of individual buffers. The order of memory block referenced in that
 * linked list determines the order of concatenation.
 */
struct drbg_string {
	const unsigned char *buf;
	size_t len;
	struct drbg_string *next;
};

static inline void drbg_string_fill(struct drbg_string *string,
				    const unsigned char *buf, size_t len)
{
	string->buf = buf;
	string->len = len;
	string->next = NULL;
}

struct drbg_state;
typedef uint32_t drbg_flag_t;

struct drbg_core {
	drbg_flag_t flags;	/* flags for the cipher */
	__u8 statelen;		/* maximum state length */
	/*
	 * maximum length of personalization string or additional input
	 * string -- exponent for base 2
	 */
	__u8 max_addtllen;
	/* maximum bits per RNG request -- exponent for base 2*/
	__u8 max_bits;
	/* maximum number of requests -- exponent for base 2 */
	__u8 max_req;
	__u8 blocklen_bytes;	/* block size of output in bytes */
	char cra_name[CRYPTO_MAX_ALG_NAME]; /* mapping to kernel crypto API */
	 /* kernel crypto API backend cipher name */
	char backend_cra_name[CRYPTO_MAX_ALG_NAME];
};

struct drbg_state_ops {
	int (*update)(struct drbg_state *drbg, struct drbg_string *seed,
		      int reseed);
	int (*generate)(struct drbg_state *drbg,
			unsigned char *buf, unsigned int buflen,
			struct drbg_string *addtl);
	int (*crypto_init)(struct drbg_state *drbg);
	int (*crypto_fini)(struct drbg_state *drbg);

};

struct drbg_test_data {
	struct drbg_string *testentropy; /* TEST PARAMETER: test entropy */
};

struct drbg_state {
	spinlock_t drbg_lock;	/* lock around DRBG */
	unsigned char *V;	/* internal state 10.1.1.1 1a) */
	/* hash: static value 10.1.1.1 1b) hmac / ctr: key */
	unsigned char *C;
	/* Number of RNG requests since last reseed -- 10.1.1.1 1c) */
	size_t reseed_ctr;
	 /* some memory the DRBG can use for its operation */
	unsigned char *scratchpad;
	void *priv_data;	/* Cipher handle */
	bool seeded;		/* DRBG fully seeded? */
	bool pr;		/* Prediction resistance enabled? */
#ifdef CONFIG_CRYPTO_FIPS
	bool fips_primed;	/* Continuous test primed? */
	unsigned char *prev;	/* FIPS 140-2 continuous test value */
#endif
	const struct drbg_state_ops *d_ops;
	const struct drbg_core *core;
	struct drbg_test_data *test_data;
};

static inline __u8 drbg_statelen(struct drbg_state *drbg)
{
	if (drbg && drbg->core)
		return drbg->core->statelen;
	return 0;
}

static inline __u8 drbg_blocklen(struct drbg_state *drbg)
{
	if (drbg && drbg->core)
		return drbg->core->blocklen_bytes;
	return 0;
}

static inline __u8 drbg_keylen(struct drbg_state *drbg)
{
	if (drbg && drbg->core)
		return (drbg->core->statelen - drbg->core->blocklen_bytes);
	return 0;
}

static inline size_t drbg_max_request_bytes(struct drbg_state *drbg)
{
	/* max_bits is in bits, but buflen is in bytes */
	return (1 << (drbg->core->max_bits - 3));
}

static inline size_t drbg_max_addtl(struct drbg_state *drbg)
{
	return (1UL<<(drbg->core->max_addtllen));
}

static inline size_t drbg_max_requests(struct drbg_state *drbg)
{
	return (1UL<<(drbg->core->max_req));
}

/*
 * kernel crypto API input data structure for DRBG generate in case dlen
 * is set to 0
 */
struct drbg_gen {
	unsigned char *outbuf;	/* output buffer for random numbers */
	unsigned int outlen;	/* size of output buffer */
	struct drbg_string *addtl;	/* additional information string */
	struct drbg_test_data *test_data;	/* test data */
};

/*
 * This is a wrapper to the kernel crypto API function of
 * crypto_rng_get_bytes() to allow the caller to provide additional data.
 *
 * @drng DRBG handle -- see crypto_rng_get_bytes
 * @outbuf output buffer -- see crypto_rng_get_bytes
 * @outlen length of output buffer -- see crypto_rng_get_bytes
 * @addtl_input additional information string input buffer
 * @addtllen length of additional information string buffer
 *
 * return
 *	see crypto_rng_get_bytes
 */
static inline int crypto_drbg_get_bytes_addtl(struct crypto_rng *drng,
			unsigned char *outbuf, unsigned int outlen,
			struct drbg_string *addtl)
{
	int ret;
	struct drbg_gen genbuf;
	genbuf.outbuf = outbuf;
	genbuf.outlen = outlen;
	genbuf.addtl = addtl;
	genbuf.test_data = NULL;
	ret = crypto_rng_get_bytes(drng, (u8 *)&genbuf, 0);
	return ret;
}

/*
 * TEST code
 *
 * This is a wrapper to the kernel crypto API function of
 * crypto_rng_get_bytes() to allow the caller to provide additional data and
 * allow furnishing of test_data
 *
 * @drng DRBG handle -- see crypto_rng_get_bytes
 * @outbuf output buffer -- see crypto_rng_get_bytes
 * @outlen length of output buffer -- see crypto_rng_get_bytes
 * @addtl_input additional information string input buffer
 * @addtllen length of additional information string buffer
 * @test_data filled test data
 *
 * return
 *	see crypto_rng_get_bytes
 */
static inline int crypto_drbg_get_bytes_addtl_test(struct crypto_rng *drng,
			unsigned char *outbuf, unsigned int outlen,
			struct drbg_string *addtl,
			struct drbg_test_data *test_data)
{
	int ret;
	struct drbg_gen genbuf;
	genbuf.outbuf = outbuf;
	genbuf.outlen = outlen;
	genbuf.addtl = addtl;
	genbuf.test_data = test_data;
	ret = crypto_rng_get_bytes(drng, (u8 *)&genbuf, 0);
	return ret;
}

/*
 * TEST code
 *
 * This is a wrapper to the kernel crypto API function of
 * crypto_rng_reset() to allow the caller to provide test_data
 *
 * @drng DRBG handle -- see crypto_rng_reset
 * @pers personalization string input buffer
 * @perslen length of additional information string buffer
 * @test_data filled test data
 *
 * return
 *	see crypto_rng_reset
 */
static inline int crypto_drbg_reset_test(struct crypto_rng *drng,
					 struct drbg_string *pers,
					 struct drbg_test_data *test_data)
{
	int ret;
	struct drbg_gen genbuf;
	genbuf.outbuf = NULL;
	genbuf.outlen = 0;
	genbuf.addtl = pers;
	genbuf.test_data = test_data;
	ret = crypto_rng_reset(drng, (u8 *)&genbuf, 0);
	return ret;
}

/* DRBG type flags */
#define DRBG_CTR	((drbg_flag_t)1<<0)
#define DRBG_HMAC	((drbg_flag_t)1<<1)
#define DRBG_HASH	((drbg_flag_t)1<<2)
#define DRBG_TYPE_MASK	(DRBG_CTR | DRBG_HMAC | DRBG_HASH)
/* DRBG strength flags */
#define DRBG_STRENGTH128	((drbg_flag_t)1<<3)
#define DRBG_STRENGTH192	((drbg_flag_t)1<<4)
#define DRBG_STRENGTH256	((drbg_flag_t)1<<5)
#define DRBG_STRENGTH_MASK	(DRBG_STRENGTH128 | DRBG_STRENGTH192 | \
				 DRBG_STRENGTH256)

enum drbg_prefixes {
	DRBG_PREFIX0 = 0x00,
	DRBG_PREFIX1,
	DRBG_PREFIX2,
	DRBG_PREFIX3
};

#endif /* _DRBG_H */
