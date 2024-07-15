// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Meta, Inc */
#include <linux/bpf.h>
#include <linux/bpf_crypto.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/filter.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <crypto/skcipher.h>

struct bpf_crypto_type_list {
	const struct bpf_crypto_type *type;
	struct list_head list;
};

/* BPF crypto initialization parameters struct */
/**
 * struct bpf_crypto_params - BPF crypto initialization parameters structure
 * @type:	The string of crypto operation type.
 * @reserved:	Reserved member, will be reused for more options in future
 *		Values:
 *		  0
 * @algo:	The string of algorithm to initialize.
 * @key:	The cipher key used to init crypto algorithm.
 * @key_len:	The length of cipher key.
 * @authsize:	The length of authentication tag used by algorithm.
 */
struct bpf_crypto_params {
	char type[14];
	u8 reserved[2];
	char algo[128];
	u8 key[256];
	u32 key_len;
	u32 authsize;
};

static LIST_HEAD(bpf_crypto_types);
static DECLARE_RWSEM(bpf_crypto_types_sem);

/**
 * struct bpf_crypto_ctx - refcounted BPF crypto context structure
 * @type:	The pointer to bpf crypto type
 * @tfm:	The pointer to instance of crypto API struct.
 * @siv_len:    Size of IV and state storage for cipher
 * @rcu:	The RCU head used to free the crypto context with RCU safety.
 * @usage:	Object reference counter. When the refcount goes to 0, the
 *		memory is released back to the BPF allocator, which provides
 *		RCU safety.
 */
struct bpf_crypto_ctx {
	const struct bpf_crypto_type *type;
	void *tfm;
	u32 siv_len;
	struct rcu_head rcu;
	refcount_t usage;
};

int bpf_crypto_register_type(const struct bpf_crypto_type *type)
{
	struct bpf_crypto_type_list *node;
	int err = -EEXIST;

	down_write(&bpf_crypto_types_sem);
	list_for_each_entry(node, &bpf_crypto_types, list) {
		if (!strcmp(node->type->name, type->name))
			goto unlock;
	}

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	err = -ENOMEM;
	if (!node)
		goto unlock;

	node->type = type;
	list_add(&node->list, &bpf_crypto_types);
	err = 0;

unlock:
	up_write(&bpf_crypto_types_sem);

	return err;
}
EXPORT_SYMBOL_GPL(bpf_crypto_register_type);

int bpf_crypto_unregister_type(const struct bpf_crypto_type *type)
{
	struct bpf_crypto_type_list *node;
	int err = -ENOENT;

	down_write(&bpf_crypto_types_sem);
	list_for_each_entry(node, &bpf_crypto_types, list) {
		if (strcmp(node->type->name, type->name))
			continue;

		list_del(&node->list);
		kfree(node);
		err = 0;
		break;
	}
	up_write(&bpf_crypto_types_sem);

	return err;
}
EXPORT_SYMBOL_GPL(bpf_crypto_unregister_type);

static const struct bpf_crypto_type *bpf_crypto_get_type(const char *name)
{
	const struct bpf_crypto_type *type = ERR_PTR(-ENOENT);
	struct bpf_crypto_type_list *node;

	down_read(&bpf_crypto_types_sem);
	list_for_each_entry(node, &bpf_crypto_types, list) {
		if (strcmp(node->type->name, name))
			continue;

		if (try_module_get(node->type->owner))
			type = node->type;
		break;
	}
	up_read(&bpf_crypto_types_sem);

	return type;
}

__bpf_kfunc_start_defs();

/**
 * bpf_crypto_ctx_create() - Create a mutable BPF crypto context.
 *
 * Allocates a crypto context that can be used, acquired, and released by
 * a BPF program. The crypto context returned by this function must either
 * be embedded in a map as a kptr, or freed with bpf_crypto_ctx_release().
 * As crypto API functions use GFP_KERNEL allocations, this function can
 * only be used in sleepable BPF programs.
 *
 * bpf_crypto_ctx_create() allocates memory for crypto context.
 * It may return NULL if no memory is available.
 * @params:	pointer to struct bpf_crypto_params which contains all the
 *		details needed to initialise crypto context.
 * @params__sz:	size of steuct bpf_crypto_params usef by bpf program
 * @err:	integer to store error code when NULL is returned.
 */
__bpf_kfunc struct bpf_crypto_ctx *
bpf_crypto_ctx_create(const struct bpf_crypto_params *params, u32 params__sz,
		      int *err)
{
	const struct bpf_crypto_type *type;
	struct bpf_crypto_ctx *ctx;

	if (!params || params->reserved[0] || params->reserved[1] ||
	    params__sz != sizeof(struct bpf_crypto_params)) {
		*err = -EINVAL;
		return NULL;
	}

	type = bpf_crypto_get_type(params->type);
	if (IS_ERR(type)) {
		*err = PTR_ERR(type);
		return NULL;
	}

	if (!type->has_algo(params->algo)) {
		*err = -EOPNOTSUPP;
		goto err_module_put;
	}

	if (!!params->authsize ^ !!type->setauthsize) {
		*err = -EOPNOTSUPP;
		goto err_module_put;
	}

	if (!params->key_len || params->key_len > sizeof(params->key)) {
		*err = -EINVAL;
		goto err_module_put;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		*err = -ENOMEM;
		goto err_module_put;
	}

	ctx->type = type;
	ctx->tfm = type->alloc_tfm(params->algo);
	if (IS_ERR(ctx->tfm)) {
		*err = PTR_ERR(ctx->tfm);
		goto err_free_ctx;
	}

	if (params->authsize) {
		*err = type->setauthsize(ctx->tfm, params->authsize);
		if (*err)
			goto err_free_tfm;
	}

	*err = type->setkey(ctx->tfm, params->key, params->key_len);
	if (*err)
		goto err_free_tfm;

	if (type->get_flags(ctx->tfm) & CRYPTO_TFM_NEED_KEY) {
		*err = -EINVAL;
		goto err_free_tfm;
	}

	ctx->siv_len = type->ivsize(ctx->tfm) + type->statesize(ctx->tfm);

	refcount_set(&ctx->usage, 1);

	return ctx;

err_free_tfm:
	type->free_tfm(ctx->tfm);
err_free_ctx:
	kfree(ctx);
err_module_put:
	module_put(type->owner);

	return NULL;
}

static void crypto_free_cb(struct rcu_head *head)
{
	struct bpf_crypto_ctx *ctx;

	ctx = container_of(head, struct bpf_crypto_ctx, rcu);
	ctx->type->free_tfm(ctx->tfm);
	module_put(ctx->type->owner);
	kfree(ctx);
}

/**
 * bpf_crypto_ctx_acquire() - Acquire a reference to a BPF crypto context.
 * @ctx: The BPF crypto context being acquired. The ctx must be a trusted
 *	     pointer.
 *
 * Acquires a reference to a BPF crypto context. The context returned by this function
 * must either be embedded in a map as a kptr, or freed with
 * bpf_crypto_ctx_release().
 */
__bpf_kfunc struct bpf_crypto_ctx *
bpf_crypto_ctx_acquire(struct bpf_crypto_ctx *ctx)
{
	if (!refcount_inc_not_zero(&ctx->usage))
		return NULL;
	return ctx;
}

/**
 * bpf_crypto_ctx_release() - Release a previously acquired BPF crypto context.
 * @ctx: The crypto context being released.
 *
 * Releases a previously acquired reference to a BPF crypto context. When the final
 * reference of the BPF crypto context has been released, its memory
 * will be released.
 */
__bpf_kfunc void bpf_crypto_ctx_release(struct bpf_crypto_ctx *ctx)
{
	if (refcount_dec_and_test(&ctx->usage))
		call_rcu(&ctx->rcu, crypto_free_cb);
}

static int bpf_crypto_crypt(const struct bpf_crypto_ctx *ctx,
			    const struct bpf_dynptr_kern *src,
			    const struct bpf_dynptr_kern *dst,
			    const struct bpf_dynptr_kern *siv,
			    bool decrypt)
{
	u32 src_len, dst_len, siv_len;
	const u8 *psrc;
	u8 *pdst, *piv;
	int err;

	if (__bpf_dynptr_is_rdonly(dst))
		return -EINVAL;

	siv_len = __bpf_dynptr_size(siv);
	src_len = __bpf_dynptr_size(src);
	dst_len = __bpf_dynptr_size(dst);
	if (!src_len || !dst_len)
		return -EINVAL;

	if (siv_len != ctx->siv_len)
		return -EINVAL;

	psrc = __bpf_dynptr_data(src, src_len);
	if (!psrc)
		return -EINVAL;
	pdst = __bpf_dynptr_data_rw(dst, dst_len);
	if (!pdst)
		return -EINVAL;

	piv = siv_len ? __bpf_dynptr_data_rw(siv, siv_len) : NULL;
	if (siv_len && !piv)
		return -EINVAL;

	err = decrypt ? ctx->type->decrypt(ctx->tfm, psrc, pdst, src_len, piv)
		      : ctx->type->encrypt(ctx->tfm, psrc, pdst, src_len, piv);

	return err;
}

/**
 * bpf_crypto_decrypt() - Decrypt buffer using configured context and IV provided.
 * @ctx:	The crypto context being used. The ctx must be a trusted pointer.
 * @src:	bpf_dynptr to the encrypted data. Must be a trusted pointer.
 * @dst:	bpf_dynptr to the buffer where to store the result. Must be a trusted pointer.
 * @siv:	bpf_dynptr to IV data and state data to be used by decryptor.
 *
 * Decrypts provided buffer using IV data and the crypto context. Crypto context must be configured.
 */
__bpf_kfunc int bpf_crypto_decrypt(struct bpf_crypto_ctx *ctx,
				   const struct bpf_dynptr_kern *src,
				   const struct bpf_dynptr_kern *dst,
				   const struct bpf_dynptr_kern *siv)
{
	return bpf_crypto_crypt(ctx, src, dst, siv, true);
}

/**
 * bpf_crypto_encrypt() - Encrypt buffer using configured context and IV provided.
 * @ctx:	The crypto context being used. The ctx must be a trusted pointer.
 * @src:	bpf_dynptr to the plain data. Must be a trusted pointer.
 * @dst:	bpf_dynptr to buffer where to store the result. Must be a trusted pointer.
 * @siv:	bpf_dynptr to IV data and state data to be used by decryptor.
 *
 * Encrypts provided buffer using IV data and the crypto context. Crypto context must be configured.
 */
__bpf_kfunc int bpf_crypto_encrypt(struct bpf_crypto_ctx *ctx,
				   const struct bpf_dynptr_kern *src,
				   const struct bpf_dynptr_kern *dst,
				   const struct bpf_dynptr_kern *siv)
{
	return bpf_crypto_crypt(ctx, src, dst, siv, false);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(crypt_init_kfunc_btf_ids)
BTF_ID_FLAGS(func, bpf_crypto_ctx_create, KF_ACQUIRE | KF_RET_NULL | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_crypto_ctx_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_crypto_ctx_acquire, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_KFUNCS_END(crypt_init_kfunc_btf_ids)

static const struct btf_kfunc_id_set crypt_init_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &crypt_init_kfunc_btf_ids,
};

BTF_KFUNCS_START(crypt_kfunc_btf_ids)
BTF_ID_FLAGS(func, bpf_crypto_decrypt, KF_RCU)
BTF_ID_FLAGS(func, bpf_crypto_encrypt, KF_RCU)
BTF_KFUNCS_END(crypt_kfunc_btf_ids)

static const struct btf_kfunc_id_set crypt_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &crypt_kfunc_btf_ids,
};

BTF_ID_LIST(bpf_crypto_dtor_ids)
BTF_ID(struct, bpf_crypto_ctx)
BTF_ID(func, bpf_crypto_ctx_release)

static int __init crypto_kfunc_init(void)
{
	int ret;
	const struct btf_id_dtor_kfunc bpf_crypto_dtors[] = {
		{
			.btf_id	      = bpf_crypto_dtor_ids[0],
			.kfunc_btf_id = bpf_crypto_dtor_ids[1]
		},
	};

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS, &crypt_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_ACT, &crypt_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP, &crypt_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL,
					       &crypt_init_kfunc_set);
	return  ret ?: register_btf_id_dtor_kfuncs(bpf_crypto_dtors,
						   ARRAY_SIZE(bpf_crypto_dtors),
						   THIS_MODULE);
}

late_initcall(crypto_kfunc_init);
