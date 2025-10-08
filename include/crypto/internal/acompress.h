/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Asynchronous Compression operations
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Weigang Li <weigang.li@intel.com>
 *          Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */
#ifndef _CRYPTO_ACOMP_INT_H
#define _CRYPTO_ACOMP_INT_H

#include <crypto/acompress.h>
#include <crypto/algapi.h>
#include <crypto/scatterwalk.h>
#include <linux/compiler_types.h>
#include <linux/cpumask_types.h>
#include <linux/spinlock.h>
#include <linux/workqueue_types.h>

#define ACOMP_FBREQ_ON_STACK(name, req) \
        char __##name##_req[sizeof(struct acomp_req) + \
                            MAX_SYNC_COMP_REQSIZE] CRYPTO_MINALIGN_ATTR; \
        struct acomp_req *name = acomp_fbreq_on_stack_init( \
                __##name##_req, (req))

/**
 * struct acomp_alg - asynchronous compression algorithm
 *
 * @compress:	Function performs a compress operation
 * @decompress:	Function performs a de-compress operation
 * @init:	Initialize the cryptographic transformation object.
 *		This function is used to initialize the cryptographic
 *		transformation object. This function is called only once at
 *		the instantiation time, right after the transformation context
 *		was allocated. In case the cryptographic hardware has some
 *		special requirements which need to be handled by software, this
 *		function shall check for the precise requirement of the
 *		transformation and put any software fallbacks in place.
 * @exit:	Deinitialize the cryptographic transformation object. This is a
 *		counterpart to @init, used to remove various changes set in
 *		@init.
 *
 * @base:	Common crypto API algorithm data structure
 * @calg:	Cmonn algorithm data structure shared with scomp
 */
struct acomp_alg {
	int (*compress)(struct acomp_req *req);
	int (*decompress)(struct acomp_req *req);
	int (*init)(struct crypto_acomp *tfm);
	void (*exit)(struct crypto_acomp *tfm);

	union {
		struct COMP_ALG_COMMON;
		struct comp_alg_common calg;
	};
};

struct crypto_acomp_stream {
	spinlock_t lock;
	void *ctx;
};

struct crypto_acomp_streams {
	/* These must come first because of struct scomp_alg. */
	void *(*alloc_ctx)(void);
	void (*free_ctx)(void *);

	struct crypto_acomp_stream __percpu *streams;
	struct work_struct stream_work;
	cpumask_t stream_want;
};

struct acomp_walk {
	union {
		/* Virtual address of the source. */
		struct {
			struct {
				const void *const addr;
			} virt;
		} src;

		/* Private field for the API, do not use. */
		struct scatter_walk in;
	};

	union {
		/* Virtual address of the destination. */
		struct {
			struct {
				void *const addr;
			} virt;
		} dst;

		/* Private field for the API, do not use. */
		struct scatter_walk out;
	};

	unsigned int slen;
	unsigned int dlen;

	int flags;
};

/*
 * Transform internal helpers.
 */
static inline void *acomp_request_ctx(struct acomp_req *req)
{
	return req->__ctx;
}

static inline void *acomp_tfm_ctx(struct crypto_acomp *tfm)
{
	return tfm->base.__crt_ctx;
}

static inline void acomp_request_complete(struct acomp_req *req,
					  int err)
{
	crypto_request_complete(&req->base, err);
}

/**
 * crypto_register_acomp() -- Register asynchronous compression algorithm
 *
 * Function registers an implementation of an asynchronous
 * compression algorithm
 *
 * @alg:	algorithm definition
 *
 * Return:	zero on success; error code in case of error
 */
int crypto_register_acomp(struct acomp_alg *alg);

/**
 * crypto_unregister_acomp() -- Unregister asynchronous compression algorithm
 *
 * Function unregisters an implementation of an asynchronous
 * compression algorithm
 *
 * @alg:	algorithm definition
 */
void crypto_unregister_acomp(struct acomp_alg *alg);

int crypto_register_acomps(struct acomp_alg *algs, int count);
void crypto_unregister_acomps(struct acomp_alg *algs, int count);

static inline bool acomp_request_issg(struct acomp_req *req)
{
	return !(req->base.flags & (CRYPTO_ACOMP_REQ_SRC_VIRT |
				    CRYPTO_ACOMP_REQ_DST_VIRT));
}

static inline bool acomp_request_src_isvirt(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_SRC_VIRT;
}

static inline bool acomp_request_dst_isvirt(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_DST_VIRT;
}

static inline bool acomp_request_isvirt(struct acomp_req *req)
{
	return req->base.flags & (CRYPTO_ACOMP_REQ_SRC_VIRT |
				  CRYPTO_ACOMP_REQ_DST_VIRT);
}

static inline bool acomp_request_src_isnondma(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_SRC_NONDMA;
}

static inline bool acomp_request_dst_isnondma(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_DST_NONDMA;
}

static inline bool acomp_request_isnondma(struct acomp_req *req)
{
	return req->base.flags & (CRYPTO_ACOMP_REQ_SRC_NONDMA |
				  CRYPTO_ACOMP_REQ_DST_NONDMA);
}

static inline bool crypto_acomp_req_virt(struct crypto_acomp *tfm)
{
	return crypto_tfm_req_virt(&tfm->base);
}

void crypto_acomp_free_streams(struct crypto_acomp_streams *s);
int crypto_acomp_alloc_streams(struct crypto_acomp_streams *s);

struct crypto_acomp_stream *crypto_acomp_lock_stream_bh(
	struct crypto_acomp_streams *s) __acquires(stream);

static inline void crypto_acomp_unlock_stream_bh(
	struct crypto_acomp_stream *stream) __releases(stream)
{
	spin_unlock_bh(&stream->lock);
}

void acomp_walk_done_src(struct acomp_walk *walk, int used);
void acomp_walk_done_dst(struct acomp_walk *walk, int used);
int acomp_walk_next_src(struct acomp_walk *walk);
int acomp_walk_next_dst(struct acomp_walk *walk);
int acomp_walk_virt(struct acomp_walk *__restrict walk,
		    struct acomp_req *__restrict req, bool atomic);

static inline bool acomp_walk_more_src(const struct acomp_walk *walk, int cur)
{
	return walk->slen != cur;
}

static inline u32 acomp_request_flags(struct acomp_req *req)
{
	return crypto_request_flags(&req->base) & ~CRYPTO_ACOMP_REQ_PRIVATE;
}

static inline struct crypto_acomp *crypto_acomp_fb(struct crypto_acomp *tfm)
{
	return __crypto_acomp_tfm(crypto_acomp_tfm(tfm)->fb);
}

static inline struct acomp_req *acomp_fbreq_on_stack_init(
	char *buf, struct acomp_req *old)
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(old);
	struct acomp_req *req = (void *)buf;

	crypto_stack_request_init(&req->base,
				  crypto_acomp_tfm(crypto_acomp_fb(tfm)));
	acomp_request_set_callback(req, acomp_request_flags(old), NULL, NULL);
	req->base.flags &= ~CRYPTO_ACOMP_REQ_PRIVATE;
	req->base.flags |= old->base.flags & CRYPTO_ACOMP_REQ_PRIVATE;
	req->src = old->src;
	req->dst = old->dst;
	req->slen = old->slen;
	req->dlen = old->dlen;

	return req;
}

#endif
