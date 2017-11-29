/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

#include "tee_shm.h"
#include "tee_core_priv.h"


/**
 * tee_context_dump -	Dump in a buffer the informations (ctx, sess & shm)
 *			associated to a tee.
 */
int tee_context_dump(struct tee *tee, char *buff, size_t len)
{
	struct list_head *ptr_ctx, *ptr_sess, *ptr_shm;
	struct tee_context *ctx;
	struct tee_session *sess;
	struct tee_shm *shm;
	int i = 0;
	int j = 0;

	int pos = 0;

	BUG_ON(!tee);

	if (len < 80 || list_empty(&tee->list_ctx))
		return 0;

	mutex_lock(&tee->lock);

	list_for_each(ptr_ctx, &tee->list_ctx) {
		ctx = list_entry(ptr_ctx, struct tee_context, entry);

		pos += sprintf(buff + pos,
				"[%02d] ctx=%p (refcount=%d) (usr=%d)",
				i, ctx,
				(int)atomic_read(&ctx->refcount.
					refcount),
				ctx->usr_client);
		pos += sprintf(buff + pos, "name=\"%s\" (tgid=%d)\n",
				ctx->name,
				ctx->tgid);
		if ((len - pos) < 80) {
			pos = 0;
			goto out;
		}

		if (list_empty(&ctx->list_sess))
			goto out;

		j = 0;
		list_for_each(ptr_sess, &ctx->list_sess) {
			sess = list_entry(ptr_sess,
					struct tee_session,
					entry);

			pos += sprintf(buff + pos,
					"[%02d.%d] sess=%p sessid=%08x\n",
					i, j, sess,
					sess->sessid);

			if ((len - pos) < 80) {
				pos = 0;
				goto out;
			}

			j++;
		}

		if (list_empty(&ctx->list_shm))
			goto out;

		j = 0;
		list_for_each(ptr_shm, &ctx->list_shm) {
			shm = list_entry(ptr_shm, struct tee_shm, entry);

			pos += sprintf(buff + pos,
					"[%02d.%d] shm=%p paddr=%p kaddr=%p",
					i, j, shm,
					&shm->paddr,
					shm->kaddr);
			pos += sprintf(buff + pos,
					" s=%zu(%zu)\n",
					shm->size_req,
					shm->size_alloc);
			if ((len - pos) < 80) {
				pos = 0;
				goto out;
			}

			j++;
		}

		i++;
	}

out:
	mutex_unlock(&tee->lock);
	return pos;
}

/**
 * tee_context_create - Allocate and create a new context.
 *			Reference on the back-end is requested.
 */
struct tee_context *tee_context_create(struct tee *tee)
{
	int ret;
	struct tee_context *ctx;

	dev_dbg(_DEV(tee), "%s: >\n", __func__);

	ctx = devm_kzalloc(_DEV(tee), sizeof(struct tee_context), GFP_KERNEL);
	if (!ctx) {
		dev_err(_DEV(tee), "%s: tee_context allocation failed\n",
			__func__);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&ctx->refcount);
	INIT_LIST_HEAD(&ctx->list_sess);
	INIT_LIST_HEAD(&ctx->list_shm);

	ctx->tee = tee;
	snprintf(ctx->name, sizeof(ctx->name), "%s", current->comm);
	ctx->tgid = current->tgid;

	ret = tee_get(tee);
	if (ret) {
		devm_kfree(_DEV(tee), ctx);
		return ERR_PTR(ret);
	}

	mutex_lock(&tee->lock);
	tee_inc_stats(&tee->stats[TEE_STATS_CONTEXT_IDX]);
	list_add_tail(&ctx->entry, &tee->list_ctx);
	mutex_unlock(&tee->lock);

	dev_dbg(_DEV(ctx->tee), "%s: < ctx=%p is created\n", __func__, ctx);
	return ctx;
}

/**
 * _tee_context_do_release - Final function to release
 *                           and free a context.
 */
static void _tee_context_do_release(struct kref *kref)
{
	struct tee_context *ctx;
	struct tee *tee;

	ctx = container_of(kref, struct tee_context, refcount);

	BUG_ON(!ctx || !ctx->tee);

	tee = ctx->tee;

	dev_dbg(_DEV(tee), "%s: > ctx=%p\n", __func__, ctx);

	tee_dec_stats(&tee->stats[TEE_STATS_CONTEXT_IDX]);
	list_del(&ctx->entry);

	devm_kfree(_DEV(tee), ctx);
	tee_put(tee);

	dev_dbg(_DEV(tee), "%s: < ctx=%p is destroyed\n", __func__, ctx);
}

/**
 * tee_context_get - Increase the reference count of
 *                   the context.
 */
void tee_context_get(struct tee_context *ctx)
{
	BUG_ON(!ctx || !ctx->tee);

	kref_get(&ctx->refcount);

	dev_dbg(_DEV(ctx->tee), "%s: ctx=%p, kref=%d\n", __func__,
		ctx, (int)atomic_read(&ctx->refcount.refcount));
}

static int is_in_list(struct tee *tee, struct list_head *entry)
{
	int present = 1;

	if ((entry->next == LIST_POISON1) && (entry->prev == LIST_POISON2))
		present = 0;
	return present;
}

/**
 * tee_context_put - Decreases the reference count of
 *                   the context. If 0, the final
 *                   release function is called.
 */
void tee_context_put(struct tee_context *ctx)
{
	struct tee_context *_ctx = ctx;
	struct tee *tee;

	BUG_ON(!ctx || !ctx->tee);
	tee = ctx->tee;

	if (!is_in_list(tee, &ctx->entry))
		return;

	kref_put(&ctx->refcount, _tee_context_do_release);

	dev_dbg(_DEV(tee), "%s: ctx=%p, kref=%d\n", __func__,
		_ctx, (int)atomic_read(&ctx->refcount.refcount));
}

/**
 * tee_context_destroy - Request to destroy a context.
 */
void tee_context_destroy(struct tee_context *ctx)
{
	struct tee *tee;

	if (!ctx || !ctx->tee)
		return;

	tee = ctx->tee;

	dev_dbg(_DEV(tee), "%s: ctx=%p\n", __func__, ctx);

	mutex_lock(&tee->lock);
	tee_context_put(ctx);
	mutex_unlock(&tee->lock);
}

int tee_context_copy_from_client(const struct tee_context *ctx,
				 void *dest, const void *src, size_t size)
{
	int res = 0;

	if (dest && src && (size > 0)) {
		if (ctx->usr_client)
			res = copy_from_user(dest, src, size);
		else
			memcpy(dest, src, size);
	}
	return res;
}

struct tee_shm *tee_context_alloc_shm_tmp(struct tee_context *ctx,
					  size_t size, const void *src,
					  int type)
{
	struct tee_shm *shm;

	type &= (TEEC_MEM_INPUT | TEEC_MEM_OUTPUT);

	shm = rk_tee_shm_alloc(ctx->tee, size,
			TEE_SHM_MAPPED | TEE_SHM_TEMP | type);
	if (IS_ERR_OR_NULL(shm)) {
		dev_err(_DEV(ctx->tee), "%s: buffer allocation failed (%ld)\n",
			__func__, PTR_ERR(shm));
		return shm;
	}

	shm->ctx = ctx;

	if (type & TEEC_MEM_INPUT) {
		if (tee_context_copy_from_client(ctx, shm->kaddr, src, size)) {
			dev_err(_DEV(ctx->tee),
				"%s: tee_context_copy_from_client failed\n",
				__func__);
			rk_tee_shm_free(shm);
			shm = NULL;
		}
	}
	return shm;
}

struct tee_shm *tee_context_create_tmpref_buffer(struct tee_context *ctx,
						 size_t size,
						 const void *buffer, int type)
{
	struct tee_shm *shm = NULL;
	int flags;

	switch (type) {
	case TEEC_MEMREF_TEMP_OUTPUT:
		flags = TEEC_MEM_OUTPUT;
		break;
	case TEEC_MEMREF_TEMP_INPUT:
		flags = TEEC_MEM_INPUT;
		break;
	case TEEC_MEMREF_TEMP_INOUT:
		flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
		break;
	default:
		BUG_ON(1);
	};
	shm = tee_context_alloc_shm_tmp(ctx, size, buffer, flags);
	return shm;
}
