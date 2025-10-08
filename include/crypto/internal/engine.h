/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Crypto engine API
 *
 * Copyright (c) 2016 Baolin Wang <baolin.wang@linaro.org>
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _CRYPTO_INTERNAL_ENGINE_H
#define _CRYPTO_INTERNAL_ENGINE_H

#include <crypto/algapi.h>
#include <crypto/engine.h>
#include <linux/kthread.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

#define ENGINE_NAME_LEN	30

struct device;

/*
 * struct crypto_engine - crypto hardware engine
 * @name: the engine name
 * @busy: request pump is busy
 * @running: the engine is on working
 * @retry_support: indication that the hardware allows re-execution
 * of a failed backlog request
 * crypto-engine, in head position to keep order
 * @rt: whether this queue is set to run as a realtime task
 * @list: link with the global crypto engine list
 * @queue_lock: spinlock to synchronise access to request queue
 * @queue: the crypto queue of the engine
 * @kworker: kthread worker struct for request pump
 * @pump_requests: work struct for scheduling work to the request pump
 * @priv_data: the engine private data
 * @cur_req: the current request which is on processing
 */
struct crypto_engine {
	char			name[ENGINE_NAME_LEN];
	bool			busy;
	bool			running;

	bool			retry_support;
	bool			rt;

	struct list_head	list;
	spinlock_t		queue_lock;
	struct crypto_queue	queue;
	struct device		*dev;

	struct kthread_worker           *kworker;
	struct kthread_work             pump_requests;

	void				*priv_data;
	struct crypto_async_request	*cur_req;
};

#endif
