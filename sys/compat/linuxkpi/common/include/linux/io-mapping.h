/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LINUX_IO_MAPPING_H_
#define	_LINUX_IO_MAPPING_H_

#include <sys/types.h>
#include <machine/vm.h>

#include <linux/types.h>
#include <linux/io.h>
#include <linux/slab.h>

struct io_mapping {
	unsigned long base;
	unsigned long size;
	void *mem;
	vm_memattr_t attr;
};

static inline struct io_mapping *
io_mapping_init_wc(struct io_mapping *mapping, resource_size_t base,
    unsigned long size)
{

	mapping->base = base;
	mapping->size = size;
#ifdef VM_MEMATTR_WRITE_COMBINING
	mapping->mem = ioremap_wc(base, size);
	mapping->attr = VM_MEMATTR_WRITE_COMBINING;
#else
	mapping->mem = ioremap_nocache(base, size);
	mapping->attr = VM_MEMATTR_UNCACHEABLE;
#endif
	return (mapping);
}

static inline struct io_mapping *
io_mapping_create_wc(resource_size_t base, unsigned long size)
{
	struct io_mapping *mapping;

	mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
	if (mapping == NULL)
		return (NULL);
	return (io_mapping_init_wc(mapping, base, size));
}

static inline void
io_mapping_fini(struct io_mapping *mapping)
{

	iounmap(mapping->mem);
}

static inline void
io_mapping_free(struct io_mapping *mapping)
{

	io_mapping_fini(mapping->mem);
	kfree(mapping);
}

static inline void *
io_mapping_map_atomic_wc(struct io_mapping *mapping, unsigned long offset)
{

	return ((char *)mapping->mem + offset);
}

static inline void
io_mapping_unmap_atomic(void *vaddr)
{
}

static inline void *
io_mapping_map_wc(struct io_mapping *mapping, unsigned long offset,
    unsigned long size)
{

	return ((char *)mapping->mem + offset);
}

static inline void
io_mapping_unmap(void *vaddr)
{
}

#endif /* _LINUX_IO_MAPPING_H_ */
