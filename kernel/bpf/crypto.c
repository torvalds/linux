// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Meta, Inc */
#include <linux/bpf.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/filter.h>
#include <linux/skbuff.h>
#include <crypto/aes-cbc.h>
#include <crypto/aes-ecb.h>

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

enum bpf_crypto_algo_id {
	BPF_ALGO_AES_CBC,
	BPF_ALGO_AES_ECB,
};

static const struct {
	const char *type_name;
	const char *algo_name;
	enum bpf_crypto_algo_id algo;
} bpf_crypto_algos[] = {
	{ "skcipher", "cbc(aes)", BPF_ALGO_AES_CBC },
	{ "skcipher", "ecb(aes)", BPF_ALGO_AES_ECB },
};

static bool bpf_crypto_find_algo(const struct bpf_crypto_params *params,
				 enum bpf_crypto_algo_id *id_ret)
{
	for (size_t i = 0; i < ARRAY_SIZE(bpf_crypto_algos); i++) {
		if (strncmp(bpf_crypto_algos[i].type_name, params->type,
			    sizeof(params->type)) == 0 &&
		    strncmp(bpf_crypto_algos[i].algo_name, params->algo,
			    sizeof(params->algo)) == 0) {
			*id_ret = bpf_crypto_algos[i].algo;
			return true;
		}
	}
	return false;
}

/**
 * struct bpf_crypto_ctx - refcounted BPF crypto context structure
 * @key:	The crypto key
 * @algo:	The crypto algorithm ID
 * @rcu:	The RCU head used to free the crypto context with RCU safety.
 * @usage:	Object reference counter. When the refcount goes to 0, the
 *		memory is released back to the BPF allocator, which provides
 *		RCU safety.
 */
struct bpf_crypto_ctx {
	union {
		struct aes_key aes;
	} key;
	enum bpf_crypto_algo_id algo;
	struct rcu_head rcu;
	refcount_t usage;
};

__bpf_kfunc_start_defs();

/**
 * bpf_crypto_ctx_create() - Create a mutable BPF crypto context.
 *
 * Allocates a crypto context that can be used, acquired, and released by
 * a BPF program. The crypto context returned by this function must either
 * be embedded in a map as a kptr, or freed with bpf_crypto_ctx_release().
 * As this uses a GFP_KERNEL allocation, this function can only be used in
 * sleepable BPF programs.
 *
 * bpf_crypto_ctx_create() allocates memory for crypto context.
 * It may return NULL if no memory is available.
 * @params:	pointer to struct bpf_crypto_params which contains all the
 *		details needed to initialise crypto context.
 * @params__sz:	size of struct bpf_crypto_params used by bpf program
 * @err_ret:	integer to store error code when NULL is returned.
 */
__bpf_kfunc struct bpf_crypto_ctx *
bpf_crypto_ctx_create(const struct bpf_crypto_params *params, u32 params__sz,
		      int *err_ret)
{
	struct bpf_crypto_ctx *ctx;
	int err;

	if (!params ||
	    params__sz != sizeof(struct bpf_crypto_params) ||
	    params->reserved[0] || params->reserved[1]) {
		*err_ret = -EINVAL;
		return NULL;
	}

	if (!params->key_len || params->key_len > sizeof(params->key)) {
		*err_ret = -EINVAL;
		return NULL;
	}

	ctx = kzalloc_obj(*ctx);
	if (!ctx) {
		*err_ret = -ENOMEM;
		return NULL;
	}

	if (!bpf_crypto_find_algo(params, &ctx->algo)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	switch (ctx->algo) {
	case BPF_ALGO_AES_CBC:
	case BPF_ALGO_AES_ECB:
		if (params->authsize)
			err = -EOPNOTSUPP;
		else
			err = aes_preparekey(&ctx->key.aes, params->key,
					     params->key_len);
		break;
	default:
		WARN_ON_ONCE(1);
		err = -EOPNOTSUPP;
		break;
	}

out:
	if (err) {
		kfree_sensitive(ctx);
		*err_ret = err;
		return NULL;
	}
	refcount_set(&ctx->usage, 1);
	*err_ret = 0;
	return ctx;
}

static void crypto_free_cb(struct rcu_head *head)
{
	struct bpf_crypto_ctx *ctx;

	ctx = container_of(head, struct bpf_crypto_ctx, rcu);
	kfree_sensitive(ctx);
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

__bpf_kfunc void bpf_crypto_ctx_release_dtor(void *ctx)
{
	bpf_crypto_ctx_release(ctx);
}
CFI_NOSEAL(bpf_crypto_ctx_release_dtor);

static int bpf_aes_cbc_crypt(u8 *dst, u32 dst_len, const u8 *src, u32 src_len,
			     u8 *iv, u32 iv_len,
			     const struct bpf_crypto_ctx *ctx, bool decrypt)
{
	if (iv_len != AES_BLOCK_SIZE)
		return -EINVAL;
	if (src_len % AES_BLOCK_SIZE || dst_len < src_len)
		return -EINVAL;
	if (decrypt)
		aes_cbc_decrypt(dst, src, src_len, iv, &ctx->key.aes);
	else
		aes_cbc_encrypt(dst, src, src_len, iv, &ctx->key.aes);
	return 0;
}

static int bpf_aes_ecb_crypt(u8 *dst, u32 dst_len, const u8 *src, u32 src_len,
			     u8 *iv, u32 iv_len,
			     const struct bpf_crypto_ctx *ctx, bool decrypt)
{
	if (iv_len != 0)
		return -EINVAL;
	if (src_len % AES_BLOCK_SIZE || dst_len < src_len)
		return -EINVAL;
	if (decrypt)
		aes_ecb_decrypt(dst, src, src_len, &ctx->key.aes);
	else
		aes_ecb_encrypt(dst, src, src_len, &ctx->key.aes);
	return 0;
}

static int bpf_crypto_crypt(const struct bpf_crypto_ctx *ctx,
			    const struct bpf_dynptr_kern *src,
			    const struct bpf_dynptr_kern *dst,
			    const struct bpf_dynptr_kern *iv,
			    bool decrypt)
{
	u32 src_len, dst_len, iv_len;
	const u8 *psrc;
	u8 *pdst, *piv;

	if (__bpf_dynptr_is_rdonly(dst))
		return -EINVAL;

	iv_len = iv ? __bpf_dynptr_size(iv) : 0;
	src_len = __bpf_dynptr_size(src);
	dst_len = __bpf_dynptr_size(dst);
	if (!src_len || !dst_len)
		return -EINVAL;

	psrc = __bpf_dynptr_data(src, src_len);
	if (!psrc)
		return -EINVAL;
	pdst = __bpf_dynptr_data_rw(dst, dst_len);
	if (!pdst)
		return -EINVAL;

	piv = iv_len ? __bpf_dynptr_data_rw(iv, iv_len) : NULL;
	if (iv_len && !piv)
		return -EINVAL;

	switch (ctx->algo) {
	case BPF_ALGO_AES_CBC:
		return bpf_aes_cbc_crypt(pdst, dst_len, psrc, src_len, piv,
					 iv_len, ctx, decrypt);
	case BPF_ALGO_AES_ECB:
		return bpf_aes_ecb_crypt(pdst, dst_len, psrc, src_len, piv,
					 iv_len, ctx, decrypt);
	default:
		return -EINVAL;
	}
}

/**
 * bpf_crypto_decrypt() - Decrypt buffer using configured context and IV provided.
 * @ctx:		The crypto context being used. The ctx must be a trusted pointer.
 * @src:		bpf_dynptr to the encrypted data. Must be a trusted pointer.
 * @dst:		bpf_dynptr to the buffer where to store the result. Must be a trusted pointer.
 * @iv__nullable:	bpf_dynptr to the initialization vector. May be NULL.
 *
 * Decrypts provided buffer using IV data and the crypto context. Crypto context must be configured.
 */
__bpf_kfunc int bpf_crypto_decrypt(struct bpf_crypto_ctx *ctx,
				   const struct bpf_dynptr *src,
				   const struct bpf_dynptr *dst,
				   const struct bpf_dynptr *iv__nullable)
{
	const struct bpf_dynptr_kern *src_kern = (struct bpf_dynptr_kern *)src;
	const struct bpf_dynptr_kern *dst_kern = (struct bpf_dynptr_kern *)dst;
	const struct bpf_dynptr_kern *iv_kern = (struct bpf_dynptr_kern *)iv__nullable;

	return bpf_crypto_crypt(ctx, src_kern, dst_kern, iv_kern, true);
}

/**
 * bpf_crypto_encrypt() - Encrypt buffer using configured context and IV provided.
 * @ctx:		The crypto context being used. The ctx must be a trusted pointer.
 * @src:		bpf_dynptr to the plain data. Must be a trusted pointer.
 * @dst:		bpf_dynptr to the buffer where to store the result. Must be a trusted pointer.
 * @iv__nullable:	bpf_dynptr to the initialization vector. May be NULL.
 *
 * Encrypts provided buffer using IV data and the crypto context. Crypto context must be configured.
 */
__bpf_kfunc int bpf_crypto_encrypt(struct bpf_crypto_ctx *ctx,
				   const struct bpf_dynptr *src,
				   const struct bpf_dynptr *dst,
				   const struct bpf_dynptr *iv__nullable)
{
	const struct bpf_dynptr_kern *src_kern = (struct bpf_dynptr_kern *)src;
	const struct bpf_dynptr_kern *dst_kern = (struct bpf_dynptr_kern *)dst;
	const struct bpf_dynptr_kern *iv_kern = (struct bpf_dynptr_kern *)iv__nullable;

	return bpf_crypto_crypt(ctx, src_kern, dst_kern, iv_kern, false);
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
BTF_ID(func, bpf_crypto_ctx_release_dtor)

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
