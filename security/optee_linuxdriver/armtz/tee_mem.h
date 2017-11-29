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
#ifndef TEE_MEM_H
#define TEE_MEM_H

#include <linux/types.h>
#include <linux/device.h>

struct shm_pool;

struct shm_pool *tee_shm_pool_create(struct device *dev, size_t shm_size,
				     void *shm_vaddr, unsigned long shm_paddr);

void tee_shm_pool_destroy(struct device *dev, struct shm_pool *pool);

void *tee_shm_pool_p2v(struct device *dev, struct shm_pool *pool,
		       unsigned long paddr);

unsigned long tee_shm_pool_v2p(struct device *dev, struct shm_pool *pool,
			       void *vaddr);

unsigned long tee_shm_pool_alloc(struct device *dev,
				 struct shm_pool *pool,
				 size_t size, size_t alignment);

int rk_tee_shm_pool_free(struct device *dev, struct shm_pool *pool,
		      unsigned long paddr, size_t *size);

bool tee_shm_pool_incref(struct device *dev, struct shm_pool *pool,
			 unsigned long paddr);

void tee_shm_pool_dump(struct device *dev, struct shm_pool *pool, bool forced);

void tee_shm_pool_reset(struct device *dev, struct shm_pool *pool);

bool tee_shm_pool_is_cached(struct shm_pool *pool);

void tee_shm_pool_set_cached(struct shm_pool *pool);

#endif
