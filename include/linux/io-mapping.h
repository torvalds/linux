/*
 * Copyright © 2008 Keith Packard <keithp@keithp.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _LINUX_IO_MAPPING_H
#define _LINUX_IO_MAPPING_H

#include <linux/types.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/iomap.h>

/*
 * The io_mapping mechanism provides an abstraction for mapping
 * individual pages from an io device to the CPU in an efficient fashion.
 *
 * See Documentation/io_mapping.txt
 */

/* this struct isn't actually defined anywhere */
struct io_mapping;

#ifdef CONFIG_X86_64

/* Create the io_mapping object*/
static inline struct io_mapping *
io_mapping_create_wc(unsigned long base, unsigned long size)
{
	return (struct io_mapping *) ioremap_wc(base, size);
}

static inline void
io_mapping_free(struct io_mapping *mapping)
{
	iounmap(mapping);
}

/* Atomic map/unmap */
static inline void *
io_mapping_map_atomic_wc(struct io_mapping *mapping, unsigned long offset)
{
	return ((char *) mapping) + offset;
}

static inline void
io_mapping_unmap_atomic(void *vaddr)
{
}

/* Non-atomic map/unmap */
static inline void *
io_mapping_map_wc(struct io_mapping *mapping, unsigned long offset)
{
	return ((char *) mapping) + offset;
}

static inline void
io_mapping_unmap(void *vaddr)
{
}

#endif /* CONFIG_X86_64 */

#ifdef CONFIG_X86_32
static inline struct io_mapping *
io_mapping_create_wc(unsigned long base, unsigned long size)
{
	return (struct io_mapping *) base;
}

static inline void
io_mapping_free(struct io_mapping *mapping)
{
}

/* Atomic map/unmap */
static inline void *
io_mapping_map_atomic_wc(struct io_mapping *mapping, unsigned long offset)
{
	offset += (unsigned long) mapping;
	return iomap_atomic_prot_pfn(offset >> PAGE_SHIFT, KM_USER0,
				     __pgprot(__PAGE_KERNEL_WC));
}

static inline void
io_mapping_unmap_atomic(void *vaddr)
{
	iounmap_atomic(vaddr, KM_USER0);
}

static inline void *
io_mapping_map_wc(struct io_mapping *mapping, unsigned long offset)
{
	offset += (unsigned long) mapping;
	return ioremap_wc(offset, PAGE_SIZE);
}

static inline void
io_mapping_unmap(void *vaddr)
{
	iounmap(vaddr);
}
#endif /* CONFIG_X86_32 */

#endif /* _LINUX_IO_MAPPING_H */
