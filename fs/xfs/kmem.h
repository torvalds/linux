/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SUPPORT_KMEM_H__
#define __XFS_SUPPORT_KMEM_H__

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

/*
 * General memory allocation interfaces
 */

static inline void  kmem_free(const void *ptr)
{
	kvfree(ptr);
}

/*
 * Zone interfaces
 */
static inline struct page *
kmem_to_page(void *addr)
{
	if (is_vmalloc_addr(addr))
		return vmalloc_to_page(addr);
	return virt_to_page(addr);
}

#endif /* __XFS_SUPPORT_KMEM_H__ */
