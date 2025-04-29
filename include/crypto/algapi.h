/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API for algorithms (i.e., low-level API).
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _CRYPTO_ALGAPI_H
#define _CRYPTO_ALGAPI_H

#include <crypto/utils.h>
#include <linux/align.h>
#include <linux/cache.h>
#include <linux/crypto.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/*
 * Maximum values for blocksize and alignmask, used to allocate
 * static buffers that are big enough for any combination of
 * algs and architectures. Ciphers have a lower maximum size.
 */
#define MAX_ALGAPI_BLOCKSIZE		160
#define MAX_ALGAPI_ALIGNMASK		127
#define MAX_CIPHER_BLOCKSIZE		16
#define MAX_CIPHER_ALIGNMASK		15

#ifdef ARCH_DMA_MINALIGN
#define CRYPTO_DMA_ALIGN ARCH_DMA_MINALIGN
#else
#define CRYPTO_DMA_ALIGN CRYPTO_MINALIGN
#endif

#define CRYPTO_DMA_PADDING ((CRYPTO_DMA_ALIGN - 1) & ~(CRYPTO_MINALIGN - 1))

/*
 * Autoloaded crypto modules should only use a prefixed name to avoid allowing
 * arbitrary modules to be loaded. Loading from userspace may still need the
 * unprefixed names, so retains those aliases as well.
 * This uses __MODULE_INFO directly instead of MODULE_ALIAS because pre-4.3
 * gcc (e.g. avr32 toolchain) uses __LINE__ for uniqueness, and this macro
 * expands twice on the same line. Instead, use a separate base name for the
 * alias.
 */
#define MODULE_ALIAS_CRYPTO(name)	\
		__MODULE_INFO(alias, alias_userspace, name);	\
		__MODULE_INFO(alias, alias_crypto, "crypto-" name)

struct crypto_aead;
struct crypto_instance;
struct module;
struct notifier_block;
struct rtattr;
struct scatterlist;
struct seq_file;
struct sk_buff;
union crypto_no_such_thing;

struct crypto_instance {
	struct crypto_alg alg;

	struct crypto_template *tmpl;

	union {
		/* Node in list of instances after registration. */
		struct hlist_node list;
		/* List of attached spawns before registration. */
		struct crypto_spawn *spawns;
	};

	struct work_struct free_work;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

struct crypto_template {
	struct list_head list;
	struct hlist_head instances;
	struct module *module;

	int (*create)(struct crypto_template *tmpl, struct rtattr **tb);

	char name[CRYPTO_MAX_ALG_NAME];
};

struct crypto_spawn {
	struct list_head list;
	struct crypto_alg *alg;
	union {
		/* Back pointer to instance after registration.*/
		struct crypto_instance *inst;
		/* Spawn list pointer prior to registration. */
		struct crypto_spawn *next;
	};
	const struct crypto_type *frontend;
	u32 mask;
	bool dead;
	bool registered;
};

struct crypto_queue {
	struct list_head list;
	struct list_head *backlog;

	unsigned int qlen;
	unsigned int max_qlen;
};

struct scatter_walk {
	/* Must be the first member, see struct skcipher_walk. */
	union {
		void *const addr;

		/* Private API field, do not touch. */
		union crypto_no_such_thing *__addr;
	};
	struct scatterlist *sg;
	unsigned int offset;
};

struct crypto_attr_alg {
	char name[CRYPTO_MAX_ALG_NAME];
};

struct crypto_attr_type {
	u32 type;
	u32 mask;
};

/*
 * Algorithm registration interface.
 */
int crypto_register_alg(struct crypto_alg *alg);
void crypto_unregister_alg(struct crypto_alg *alg);
int crypto_register_algs(struct crypto_alg *algs, int count);
void crypto_unregister_algs(struct crypto_alg *algs, int count);

void crypto_mod_put(struct crypto_alg *alg);

int crypto_register_template(struct crypto_template *tmpl);
int crypto_register_templates(struct crypto_template *tmpls, int count);
void crypto_unregister_template(struct crypto_template *tmpl);
void crypto_unregister_templates(struct crypto_template *tmpls, int count);
struct crypto_template *crypto_lookup_template(const char *name);

int crypto_register_instance(struct crypto_template *tmpl,
			     struct crypto_instance *inst);
void crypto_unregister_instance(struct crypto_instance *inst);

int crypto_grab_spawn(struct crypto_spawn *spawn, struct crypto_instance *inst,
		      const char *name, u32 type, u32 mask);
void crypto_drop_spawn(struct crypto_spawn *spawn);
struct crypto_tfm *crypto_spawn_tfm(struct crypto_spawn *spawn, u32 type,
				    u32 mask);
void *crypto_spawn_tfm2(struct crypto_spawn *spawn);

struct crypto_attr_type *crypto_get_attr_type(struct rtattr **tb);
int crypto_check_attr_type(struct rtattr **tb, u32 type, u32 *mask_ret);
const char *crypto_attr_alg_name(struct rtattr *rta);
int crypto_inst_setname(struct crypto_instance *inst, const char *name,
			struct crypto_alg *alg);

void crypto_init_queue(struct crypto_queue *queue, unsigned int max_qlen);
int crypto_enqueue_request(struct crypto_queue *queue,
			   struct crypto_async_request *request);
void crypto_enqueue_request_head(struct crypto_queue *queue,
				 struct crypto_async_request *request);
struct crypto_async_request *crypto_dequeue_request(struct crypto_queue *queue);
static inline unsigned int crypto_queue_len(struct crypto_queue *queue)
{
	return queue->qlen;
}

void crypto_inc(u8 *a, unsigned int size);

static inline void *crypto_tfm_ctx(struct crypto_tfm *tfm)
{
	return tfm->__crt_ctx;
}

static inline void *crypto_tfm_ctx_align(struct crypto_tfm *tfm,
					 unsigned int align)
{
	if (align <= crypto_tfm_ctx_alignment())
		align = 1;

	return PTR_ALIGN(crypto_tfm_ctx(tfm), align);
}

static inline unsigned int crypto_dma_align(void)
{
	return CRYPTO_DMA_ALIGN;
}

static inline unsigned int crypto_dma_padding(void)
{
	return (crypto_dma_align() - 1) & ~(crypto_tfm_ctx_alignment() - 1);
}

static inline void *crypto_tfm_ctx_dma(struct crypto_tfm *tfm)
{
	return crypto_tfm_ctx_align(tfm, crypto_dma_align());
}

static inline struct crypto_instance *crypto_tfm_alg_instance(
	struct crypto_tfm *tfm)
{
	return container_of(tfm->__crt_alg, struct crypto_instance, alg);
}

static inline void *crypto_instance_ctx(struct crypto_instance *inst)
{
	return inst->__ctx;
}

static inline struct crypto_async_request *crypto_get_backlog(
	struct crypto_queue *queue)
{
	return queue->backlog == &queue->list ? NULL :
	       container_of(queue->backlog, struct crypto_async_request, list);
}

static inline u32 crypto_requires_off(struct crypto_attr_type *algt, u32 off)
{
	return (algt->type ^ off) & algt->mask & off;
}

/*
 * When an algorithm uses another algorithm (e.g., if it's an instance of a
 * template), these are the flags that should always be set on the "outer"
 * algorithm if any "inner" algorithm has them set.
 */
#define CRYPTO_ALG_INHERITED_FLAGS	\
	(CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK |	\
	 CRYPTO_ALG_ALLOCATES_MEMORY)

/*
 * Given the type and mask that specify the flags restrictions on a template
 * instance being created, return the mask that should be passed to
 * crypto_grab_*() (along with type=0) to honor any request the user made to
 * have any of the CRYPTO_ALG_INHERITED_FLAGS clear.
 */
static inline u32 crypto_algt_inherited_mask(struct crypto_attr_type *algt)
{
	return crypto_requires_off(algt, CRYPTO_ALG_INHERITED_FLAGS);
}

int crypto_register_notifier(struct notifier_block *nb);
int crypto_unregister_notifier(struct notifier_block *nb);

/* Crypto notification events. */
enum {
	CRYPTO_MSG_ALG_REQUEST,
	CRYPTO_MSG_ALG_REGISTER,
	CRYPTO_MSG_ALG_LOADED,
};

static inline void crypto_request_complete(struct crypto_async_request *req,
					   int err)
{
	req->complete(req->data, err);
}

static inline u32 crypto_tfm_alg_type(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK;
}

static inline bool crypto_request_chained(struct crypto_async_request *req)
{
	return !list_empty(&req->list);
}

static inline bool crypto_tfm_req_chain(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_flags & CRYPTO_ALG_REQ_CHAIN;
}

#endif	/* _CRYPTO_ALGAPI_H */
