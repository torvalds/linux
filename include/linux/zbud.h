/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ZBUD_H_
#define _ZBUD_H_

#include <linux/types.h>

struct zbud_pool;

struct zbud_ops {
	int (*evict)(struct zbud_pool *pool, unsigned long handle);
};

struct zbud_pool *zbud_create_pool(gfp_t gfp, const struct zbud_ops *ops);
void zbud_destroy_pool(struct zbud_pool *pool);
int zbud_alloc(struct zbud_pool *pool, size_t size, gfp_t gfp,
	unsigned long *handle);
void zbud_free(struct zbud_pool *pool, unsigned long handle);
int zbud_reclaim_page(struct zbud_pool *pool, unsigned int retries);
void *zbud_map(struct zbud_pool *pool, unsigned long handle);
void zbud_unmap(struct zbud_pool *pool, unsigned long handle);
u64 zbud_get_pool_size(struct zbud_pool *pool);

#endif /* _ZBUD_H_ */
