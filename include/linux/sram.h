/* SPDX-License-Identifier: GPL-2.0-only */
/* Generic SRAM Driver Interface */
#ifndef __LINUX_SRAM_H__
#define __LINUX_SRAM_H__

struct gen_pool;

#ifdef CONFIG_SRAM_EXEC
void *sram_exec_copy(struct gen_pool *pool, void *dst, void *src, size_t size);
#else
static inline void *sram_exec_copy(struct gen_pool *pool, void *dst, void *src,
				   size_t size)
{
	return NULL;
}
#endif /* CONFIG_SRAM_EXEC */
#endif /* __LINUX_SRAM_H__ */
