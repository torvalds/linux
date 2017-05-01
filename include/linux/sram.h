/*
 * Generic SRAM Driver Interface
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __LINUX_SRAM_H__
#define __LINUX_SRAM_H__

struct gen_pool;

#ifdef CONFIG_SRAM_EXEC
int sram_exec_copy(struct gen_pool *pool, void *dst, void *src, size_t size);
#else
static inline int sram_exec_copy(struct gen_pool *pool, void *dst, void *src,
				 size_t size)
{
	return -ENODEV;
}
#endif /* CONFIG_SRAM_EXEC */
#endif /* __LINUX_SRAM_H__ */
