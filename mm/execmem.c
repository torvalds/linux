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

static struct execmem_info *execmem_info __ro_after_init;

static void *__execmem_alloc(struct execmem_range *range, size_t size)
{
	unsigned long start = range->start;
	unsigned long end = range->end;
	unsigned int align = range->alignment;
	pgprot_t pgprot = range->pgprot;

	return __vmalloc_node_range(size, align, start, end,
				    GFP_KERNEL, pgprot, VM_FLUSH_RESET_PERMS,
				    NUMA_NO_NODE, __builtin_return_address(0));
}

void *execmem_alloc(enum execmem_type type, size_t size)
{
	struct execmem_range *range;

	if (!execmem_info)
		return module_alloc(size);

	range = &execmem_info->ranges[type];

	return __execmem_alloc(range, size);
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

static bool execmem_validate(struct execmem_info *info)
{
	struct execmem_range *r = &info->ranges[EXECMEM_DEFAULT];

	if (!r->alignment || !r->start || !r->end || !pgprot_val(r->pgprot)) {
		pr_crit("Invalid parameters for execmem allocator, module loading will fail");
		return false;
	}

	return true;
}

static void execmem_init_missing(struct execmem_info *info)
{
	struct execmem_range *default_range = &info->ranges[EXECMEM_DEFAULT];

	for (int i = EXECMEM_DEFAULT + 1; i < EXECMEM_TYPE_MAX; i++) {
		struct execmem_range *r = &info->ranges[i];

		if (!r->start) {
			r->pgprot = default_range->pgprot;
			r->alignment = default_range->alignment;
			r->start = default_range->start;
			r->end = default_range->end;
		}
	}
}

struct execmem_info * __weak execmem_arch_setup(void)
{
	return NULL;
}

void __init execmem_init(void)
{
	struct execmem_info *info = execmem_arch_setup();

	if (!info || !execmem_validate(info))
		return;

	execmem_init_missing(info);

	execmem_info = info;
}
