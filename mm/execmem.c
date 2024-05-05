// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Richard Henderson
 * Copyright (C) 2001 Rusty Russell, 2002, 2010 Rusty Russell IBM.
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 * Copyright (C) 2024 Mike Rapoport IBM.
 */

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/execmem.h>
#include <linux/moduleloader.h>

static void *__execmem_alloc(size_t size)
{
	return module_alloc(size);
}

void *execmem_alloc(enum execmem_type type, size_t size)
{
	return __execmem_alloc(size);
}

void execmem_free(void *ptr)
{
	/*
	 * This memory may be RO, and freeing RO memory in an interrupt is not
	 * supported by vmalloc.
	 */
	WARN_ON(in_interrupt());
	vfree(ptr);
}
