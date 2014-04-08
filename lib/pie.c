/*
 * Copyright 2013 Texas Instruments, Inc.
 *	Russ Dill <russ.dill@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/genalloc.h>
#include <linux/pie.h>
#include <asm/cacheflush.h>

struct pie_chunk {
	struct gen_pool *pool;
	unsigned long addr;
	size_t sz;
};

extern char __pie_common_start[];
extern char __pie_common_end[];
extern char __pie_overlay_start[];

int __weak pie_arch_fill_tail(void *tail, void *common_start, void *common_end,
			void *overlay_start, void *code_start, void *code_end,
			void *rel_start, void *rel_end)
{
	return 0;
}

int __weak pie_arch_fixup(struct pie_chunk *chunk, void *base, void *tail,
							unsigned long offset)
{
	return 0;
}

struct pie_chunk *__pie_load_data(struct gen_pool *pool, bool phys,
		void *code_start, void *code_end,
		void *rel_start, void *rel_end)
{
	struct pie_chunk *chunk;
	unsigned long offset;
	int ret;
	char *tail;
	size_t common_sz;
	size_t code_sz;
	size_t tail_sz;

	/* Calculate the tail size */
	ret = pie_arch_fill_tail(NULL, __pie_common_start, __pie_common_end,
				__pie_overlay_start, code_start, code_end,
				rel_start, rel_end);
	if (ret < 0)
		goto err;
	tail_sz = ret;

	chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
	if (!chunk) {
		ret = -ENOMEM;
		goto err;
	}

	common_sz = __pie_overlay_start - __pie_common_start;
	code_sz = code_end - code_start;

	chunk->pool = pool;
	chunk->sz = common_sz + code_sz + tail_sz;

	chunk->addr = gen_pool_alloc(pool, chunk->sz);
	if (!chunk->addr) {
		ret = -ENOMEM;
		goto err_free;
	}

	/* Copy common code/data */
	tail = (char *) chunk->addr;
	memcpy(tail, __pie_common_start, common_sz);
	tail += common_sz;

	/* Copy chunk specific code/data */
	memcpy(tail, code_start, code_sz);
	tail += code_sz;

	/* Fill in tail data */
	ret = pie_arch_fill_tail(tail, __pie_common_start, __pie_common_end,
				__pie_overlay_start, code_start, code_end,
				rel_start, rel_end);
	if (ret < 0)
		goto err_alloc;

	/* Calculate initial offset */
	if (phys)
		offset = gen_pool_virt_to_phys(pool, chunk->addr);
	else
		offset = chunk->addr;

	/* Perform arch specific code fixups */
	ret = pie_arch_fixup(chunk, (void *) chunk->addr, tail, offset);
	if (ret < 0)
		goto err_alloc;

	flush_icache_range(chunk->addr, chunk->addr + chunk->sz);

	return chunk;

err_alloc:
	gen_pool_free(chunk->pool, chunk->addr, chunk->sz);

err_free:
	kfree(chunk);
err:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(__pie_load_data);

phys_addr_t pie_to_phys(struct pie_chunk *chunk, unsigned long addr)
{
	return gen_pool_virt_to_phys(chunk->pool, addr);
}
EXPORT_SYMBOL_GPL(pie_to_phys);

void __iomem *__kern_to_pie(struct pie_chunk *chunk, void *ptr)
{
	uintptr_t offset = (uintptr_t) ptr;
	offset -= (uintptr_t) __pie_common_start;
	if (offset >= chunk->sz)
		return NULL;
	else
		return (void *) (chunk->addr + offset);
}
EXPORT_SYMBOL_GPL(__kern_to_pie);

void pie_free(struct pie_chunk *chunk)
{
	gen_pool_free(chunk->pool, chunk->addr, chunk->sz);
	kfree(chunk);
}
EXPORT_SYMBOL_GPL(pie_free);
