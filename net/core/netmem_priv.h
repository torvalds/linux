/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NETMEM_PRIV_H
#define __NETMEM_PRIV_H

static inline unsigned long netmem_get_pp_magic(netmem_ref netmem)
{
	return __netmem_clear_lsb(netmem)->pp_magic;
}

static inline void netmem_or_pp_magic(netmem_ref netmem, unsigned long pp_magic)
{
	__netmem_clear_lsb(netmem)->pp_magic |= pp_magic;
}

static inline void netmem_clear_pp_magic(netmem_ref netmem)
{
	__netmem_clear_lsb(netmem)->pp_magic = 0;
}

static inline void netmem_set_pp(netmem_ref netmem, struct page_pool *pool)
{
	__netmem_clear_lsb(netmem)->pp = pool;
}

static inline void netmem_set_dma_addr(netmem_ref netmem,
				       unsigned long dma_addr)
{
	__netmem_clear_lsb(netmem)->dma_addr = dma_addr;
}
#endif
