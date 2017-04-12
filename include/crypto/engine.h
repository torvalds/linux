/*
 * Crypto engine API
 *
 * Copyright (c) 2016 Baolin Wang <baolin.wang@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_ENGINE_H
#define _CRYPTO_ENGINE_H

#include <linux/crypto.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>

#define ENGINE_NAME_LEN	30
/*
 * struct crypto_engine - crypto hardware engine
 * @name: the engine name
 * @idling: the engine is entering idle state
 * @busy: request pump is busy
 * @running: the engine is on working
 * @cur_req_prepared: current request is prepared
 * @list: link with the global crypto engine list
 * @queue_lock: spinlock to syncronise access to request queue
 * @queue: the crypto queue of the engine
 * @rt: whether this queue is set to run as a realtime task
 * @prepare_crypt_hardware: a request will soon arrive from the queue
 * so the subsystem requests the driver to prepare the hardware
 * by issuing this call
 * @unprepare_crypt_hardware: there are currently no more requests on the
 * queue so the subsystem notifies the driver that it may relax the
 * hardware by issuing this call
 * @prepare_cipher_request: do some prepare if need before handle the current request
 * @unprepare_cipher_request: undo any work done by prepare_cipher_request()
 * @cipher_one_request: do encryption for current request
 * @prepare_hash_request: do some prepare if need before handle the current request
 * @unprepare_hash_request: undo any work done by prepare_hash_request()
 * @hash_one_request: do hash for current request
 * @kworker: kthread worker struct for request pump
 * @pump_requests: work struct for scheduling work to the request pump
 * @priv_data: the engine private data
 * @cur_req: the current request which is on processing
 */
struct crypto_engine {
	char			name[ENGINE_NAME_LEN];
	bool			idling;
	bool			busy;
	bool			running;
	bool			cur_req_prepared;

	struct list_head	list;
	spinlock_t		queue_lock;
	struct crypto_queue	queue;

	bool			rt;

	int (*prepare_crypt_hardware)(struct crypto_engine *engine);
	int (*unprepare_crypt_hardware)(struct crypto_engine *engine);

	int (*prepare_cipher_request)(struct crypto_engine *engine,
				      struct ablkcipher_request *req);
	int (*unprepare_cipher_request)(struct crypto_engine *engine,
					struct ablkcipher_request *req);
	int (*prepare_hash_request)(struct crypto_engine *engine,
				    struct ahash_request *req);
	int (*unprepare_hash_request)(struct crypto_engine *engine,
				      struct ahash_request *req);
	int (*cipher_one_request)(struct crypto_engine *engine,
				  struct ablkcipher_request *req);
	int (*hash_one_request)(struct crypto_engine *engine,
				struct ahash_request *req);

	struct kthread_worker           *kworker;
	struct kthread_work             pump_requests;

	void				*priv_data;
	struct crypto_async_request	*cur_req;
};

int crypto_transfer_cipher_request(struct crypto_engine *engine,
				   struct ablkcipher_request *req,
				   bool need_pump);
int crypto_transfer_cipher_request_to_engine(struct crypto_engine *engine,
					     struct ablkcipher_request *req);
int crypto_transfer_hash_request(struct crypto_engine *engine,
				 struct ahash_request *req, bool need_pump);
int crypto_transfer_hash_request_to_engine(struct crypto_engine *engine,
					   struct ahash_request *req);
void crypto_finalize_cipher_request(struct crypto_engine *engine,
				    struct ablkcipher_request *req, int err);
void crypto_finalize_hash_request(struct crypto_engine *engine,
				  struct ahash_request *req, int err);
int crypto_engine_start(struct crypto_engine *engine);
int crypto_engine_stop(struct crypto_engine *engine);
struct crypto_engine *crypto_engine_alloc_init(struct device *dev, bool rt);
int crypto_engine_exit(struct crypto_engine *engine);

#endif /* _CRYPTO_ENGINE_H */
