// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "server.h"
#include "connection.h"
#include "ksmbd_work.h"
#include "mgmt/ksmbd_ida.h"

static struct kmem_cache *work_cache;
static struct workqueue_struct *ksmbd_wq;

struct ksmbd_work *ksmbd_alloc_work_struct(void)
{
	struct ksmbd_work *work = kmem_cache_zalloc(work_cache, KSMBD_DEFAULT_GFP);

	if (work) {
		work->compound_fid = KSMBD_NO_FID;
		work->compound_pfid = KSMBD_NO_FID;
		INIT_LIST_HEAD(&work->request_entry);
		INIT_LIST_HEAD(&work->async_request_entry);
		INIT_LIST_HEAD(&work->fp_entry);
		INIT_LIST_HEAD(&work->aux_read_list);
		work->iov_alloc_cnt = 4;
		work->iov = kcalloc(work->iov_alloc_cnt, sizeof(struct kvec),
				    KSMBD_DEFAULT_GFP);
		if (!work->iov) {
			kmem_cache_free(work_cache, work);
			work = NULL;
		}
	}
	return work;
}

void ksmbd_free_work_struct(struct ksmbd_work *work)
{
	struct aux_read *ar, *tmp;

	WARN_ON(work->saved_cred != NULL);

	kvfree(work->response_buf);

	list_for_each_entry_safe(ar, tmp, &work->aux_read_list, entry) {
		kvfree(ar->buf);
		list_del(&ar->entry);
		kfree(ar);
	}

	kfree(work->tr_buf);
	kvfree(work->request_buf);
	kfree(work->iov);

	if (work->async_id)
		ksmbd_release_id(&work->conn->async_ida, work->async_id);
	kmem_cache_free(work_cache, work);
}

void ksmbd_work_pool_destroy(void)
{
	kmem_cache_destroy(work_cache);
}

int ksmbd_work_pool_init(void)
{
	work_cache = kmem_cache_create("ksmbd_work_cache",
				       sizeof(struct ksmbd_work), 0,
				       SLAB_HWCACHE_ALIGN, NULL);
	if (!work_cache)
		return -ENOMEM;
	return 0;
}

int ksmbd_workqueue_init(void)
{
	ksmbd_wq = alloc_workqueue("ksmbd-io", 0, 0);
	if (!ksmbd_wq)
		return -ENOMEM;
	return 0;
}

void ksmbd_workqueue_destroy(void)
{
	destroy_workqueue(ksmbd_wq);
	ksmbd_wq = NULL;
}

bool ksmbd_queue_work(struct ksmbd_work *work)
{
	return queue_work(ksmbd_wq, &work->work);
}

static inline void __ksmbd_iov_pin(struct ksmbd_work *work, void *ib,
				   unsigned int ib_len)
{
	work->iov[++work->iov_idx].iov_base = ib;
	work->iov[work->iov_idx].iov_len = ib_len;
	work->iov_cnt++;
}

static int __ksmbd_iov_pin_rsp(struct ksmbd_work *work, void *ib, int len,
			       void *aux_buf, unsigned int aux_size)
{
	struct aux_read *ar = NULL;
	int need_iov_cnt = 1;

	if (aux_size) {
		need_iov_cnt++;
		ar = kmalloc(sizeof(struct aux_read), KSMBD_DEFAULT_GFP);
		if (!ar)
			return -ENOMEM;
	}

	if (work->iov_alloc_cnt < work->iov_cnt + need_iov_cnt) {
		struct kvec *new;

		work->iov_alloc_cnt += 4;
		new = krealloc(work->iov,
			       sizeof(struct kvec) * work->iov_alloc_cnt,
			       KSMBD_DEFAULT_GFP | __GFP_ZERO);
		if (!new) {
			kfree(ar);
			work->iov_alloc_cnt -= 4;
			return -ENOMEM;
		}
		work->iov = new;
	}

	/* Plus rfc_length size on first iov */
	if (!work->iov_idx) {
		work->iov[work->iov_idx].iov_base = work->response_buf;
		*(__be32 *)work->iov[0].iov_base = 0;
		work->iov[work->iov_idx].iov_len = 4;
		work->iov_cnt++;
	}

	__ksmbd_iov_pin(work, ib, len);
	inc_rfc1001_len(work->iov[0].iov_base, len);

	if (aux_size) {
		__ksmbd_iov_pin(work, aux_buf, aux_size);
		inc_rfc1001_len(work->iov[0].iov_base, aux_size);

		ar->buf = aux_buf;
		list_add(&ar->entry, &work->aux_read_list);
	}

	return 0;
}

int ksmbd_iov_pin_rsp(struct ksmbd_work *work, void *ib, int len)
{
	return __ksmbd_iov_pin_rsp(work, ib, len, NULL, 0);
}

int ksmbd_iov_pin_rsp_read(struct ksmbd_work *work, void *ib, int len,
			   void *aux_buf, unsigned int aux_size)
{
	return __ksmbd_iov_pin_rsp(work, ib, len, aux_buf, aux_size);
}

int allocate_interim_rsp_buf(struct ksmbd_work *work)
{
	work->response_buf = kzalloc(MAX_CIFS_SMALL_BUFFER_SIZE, KSMBD_DEFAULT_GFP);
	if (!work->response_buf)
		return -ENOMEM;
	work->response_sz = MAX_CIFS_SMALL_BUFFER_SIZE;
	return 0;
}
