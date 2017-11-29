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
#ifndef __TEE_SHM_H__
#define __TEE_SHM_H__

#include <linux/tee_client_api.h>
struct tee_context;
struct tee_shm;
struct tee_shm_io;
struct tee;

int tee_shm_alloc_io(struct tee_context *ctx, struct tee_shm_io *shm_io);
void tee_shm_free_io(struct tee_shm *shm);

int tee_shm_fd_for_rpc(struct tee_context *ctx, struct tee_shm_io *shm_io);

struct tee_shm *rk_tee_shm_alloc(struct tee *tee, size_t size, uint32_t flags);
void rk_tee_shm_free(struct tee_shm *shm);

struct tee_shm *tee_shm_get(struct tee_context *ctx, TEEC_SharedMemory *c_shm,
		size_t size, int offset);
void rk_tee_shm_put(struct tee_context *ctx, struct tee_shm *shm);

#endif /* __TEE_SHM_H__ */
