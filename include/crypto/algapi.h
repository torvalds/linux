/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API for algorithms (i.e., low-level API).
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _CRYPTO_ALGAPI_H
#define _CRYPTO_ALGAPI_H

#include <linux/align.h>
#include <linux/crypto.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/types.h>

#include <asm/unaligned.h>

/*
 * Maximum values for blocksize and alignmask, used to allocate
 * static buffers that are big enough for any combination of
 * algs and architectures. Ciphers have a lower maximum size.
 */
#define MAX_ALGAPI_BLOCKSIZE		160
#define MAX_ALGAPI_ALIGNMASK		63
#define MAX_CIPHER_BLOCKSIZE		16
#define MAX_CIPHER_ALIGNMASK		15

struct crypto_aead;
struct crypto_instance;
struct module;
struct notifier_block;
struct rtattr;
struct seq_file;
struct sk_buff;

struct crypto_type {
	unsigned int (*ctxsize)(struct crypto_alg *alg, u32 type, u32 mask);
	unsigned int (*extsize)(struct crypto_alg *alg);
	int (*init)(struct crypto_tfm *tfm, u32 type, u32 mask);
	int (*init_tfm)(struct crypto_tfm *tfm);
	void (*show)(struct seq_file *m, struct crypto_alg *alg);
	int (*report)(struct sk_buff *skb, struct crypto_alg *alg);
	void (*free)(struct crypto_instance *inst);

	unsigned int type;
	unsigned int maskclear;
	unsigned int maskset;
	unsigned int tfmsize;
};

struct crypto_instance {
	struct crypto_alg alg;

	struct crypto_template *tmpl;

	union {
		/* Node in list of instances after registration. */
		struct hlist_node list;
		/* List of attached spawns before registration. */
		struct crypto_spawn *spawns;
	};

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
void __crypto_xor(u8 *dst, const u8 *src1, const u8 *src2, unsigned int size);

static inline void crypto_xor(u8 *dst, const u8 *src, unsigned int size)
{
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    __builtin_constant_p(size) &&
	    (size % sizeof(unsigned long)) == 0) {
		unsigned long *d = (unsigned long *)dst;
		unsigned long *s = (unsigned long *)src;
		unsigned long l;

		while (size > 0) {
			l = get_unaligned(d) ^ get_unaligned(s++);
			put_unaligned(l, d++);
			size -= sizeof(unsigned long);
		}
	} else {
		__crypto_xor(dst, dst, src, size);
	}
}

static inline void crypto_xor_cpy(u8 *dst, const u8 *src1, const u8 *src2,
				  unsigned int size)
{
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    __builtin_constant_p(size) &&
	    (size % sizeof(unsigned long)) == 0) {
		unsigned long *d = (unsigned long *)dst;
		unsigned long *s1 = (unsigned long *)src1;
		unsigned long *s2 = (unsigned long *)src2;
		unsigned long l;

		while (size > 0) {
			l = get_unaligned(s1++) ^ get_unaligned(s2++);
			put_unaligned(l, d++);
			size -= sizeof(unsigned long);
		}
	} else {
		__crypto_xor(dst, src1, src2, size);
	}
}

static inline void *crypto_tfm_ctx_aligned(struct crypto_tfm *tfm)
{
	return PTR_ALIGN(crypto_tfm_ctx(tfm),
			 crypto_tfm_alg_alignmask(tfm) + 1);
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

noinline unsigned long __crypto_memneq(const void *a, const void *b, size_t size);

/**
 * crypto_memneq - Compare two areas of memory without leaking
 *		   timing information.
 *
 * @a: One area of memory
 * @b: Another area of memory
 * @size: The size of the area.
 *
 * Returns 0 when data is equal, 1 otherwise.
 */
static inline int crypto_memneq(const void *a, const void *b, size_t size)
{
	return __crypto_memneq(a, b, size) != 0UL ? 1 : 0;
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
	crypto_completion_t complete = req->complete;
	complete(req, err);
}

#endif	/* _CRYPTO_ALGAPI_H */
