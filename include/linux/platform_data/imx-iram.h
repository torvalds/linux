/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <linux/errno.h>

#ifdef CONFIG_IRAM_ALLOC

int __init iram_init(unsigned long base, unsigned long size);
void __iomem *iram_alloc(unsigned int size, unsigned long *dma_addr);
void iram_free(unsigned long dma_addr, unsigned int size);

#else

static inline int __init iram_init(unsigned long base, unsigned long size)
{
	return -ENOMEM;
}

static inline void __iomem *iram_alloc(unsigned int size, unsigned long *dma_addr)
{
	return NULL;
}

static inline void iram_free(unsigned long base, unsigned long size) {}

#endif
