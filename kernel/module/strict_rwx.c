// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module strict rwx
 *
 * Copyright (C) 2015 Rusty Russell
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/set_memory.h>
#include "internal.h"

/*
 * LKM RO/NX protection: protect module's text/ro-data
 * from modification and any data from execution.
 *
 * General layout of module is:
 *          [text] [read-only-data] [ro-after-init] [writable data]
 * text_size -----^                ^               ^               ^
 * ro_size ------------------------|               |               |
 * ro_after_init_size -----------------------------|               |
 * size -----------------------------------------------------------|
 *
 * These values are always page-aligned (as is base) when
 * CONFIG_STRICT_MODULE_RWX is set.
 */

/*
 * Since some arches are moving towards PAGE_KERNEL module allocations instead
 * of PAGE_KERNEL_EXEC, keep frob_text() and module_enable_x() independent of
 * CONFIG_STRICT_MODULE_RWX because they are needed regardless of whether we
 * are strict.
 */
static void frob_text(const struct module_layout *layout,
		      int (*set_memory)(unsigned long start, int num_pages))
{
	set_memory((unsigned long)layout->base,
		   PAGE_ALIGN(layout->text_size) >> PAGE_SHIFT);
}

static void frob_rodata(const struct module_layout *layout,
		 int (*set_memory)(unsigned long start, int num_pages))
{
	set_memory((unsigned long)layout->base + layout->text_size,
		   (layout->ro_size - layout->text_size) >> PAGE_SHIFT);
}

static void frob_ro_after_init(const struct module_layout *layout,
			int (*set_memory)(unsigned long start, int num_pages))
{
	set_memory((unsigned long)layout->base + layout->ro_size,
		   (layout->ro_after_init_size - layout->ro_size) >> PAGE_SHIFT);
}

static void frob_writable_data(const struct module_layout *layout,
			int (*set_memory)(unsigned long start, int num_pages))
{
	set_memory((unsigned long)layout->base + layout->ro_after_init_size,
		   (layout->size - layout->ro_after_init_size) >> PAGE_SHIFT);
}

static bool layout_check_misalignment(const struct module_layout *layout)
{
	return WARN_ON(!PAGE_ALIGNED(layout->base)) ||
	       WARN_ON(!PAGE_ALIGNED(layout->text_size)) ||
	       WARN_ON(!PAGE_ALIGNED(layout->ro_size)) ||
	       WARN_ON(!PAGE_ALIGNED(layout->ro_after_init_size)) ||
	       WARN_ON(!PAGE_ALIGNED(layout->size));
}

bool module_check_misalignment(const struct module *mod)
{
	if (!IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		return false;

	return layout_check_misalignment(&mod->core_layout) ||
	       layout_check_misalignment(&mod->data_layout) ||
	       layout_check_misalignment(&mod->init_layout);
}

void module_enable_x(const struct module *mod)
{
	if (!PAGE_ALIGNED(mod->core_layout.base) ||
	    !PAGE_ALIGNED(mod->init_layout.base))
		return;

	frob_text(&mod->core_layout, set_memory_x);
	frob_text(&mod->init_layout, set_memory_x);
}

void module_enable_ro(const struct module *mod, bool after_init)
{
	if (!IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		return;
#ifdef CONFIG_STRICT_MODULE_RWX
	if (!rodata_enabled)
		return;
#endif

	set_vm_flush_reset_perms(mod->core_layout.base);
	set_vm_flush_reset_perms(mod->init_layout.base);
	frob_text(&mod->core_layout, set_memory_ro);

	frob_rodata(&mod->data_layout, set_memory_ro);
	frob_text(&mod->init_layout, set_memory_ro);
	frob_rodata(&mod->init_layout, set_memory_ro);

	if (after_init)
		frob_ro_after_init(&mod->data_layout, set_memory_ro);
}

void module_enable_nx(const struct module *mod)
{
	if (!IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		return;

	frob_rodata(&mod->data_layout, set_memory_nx);
	frob_ro_after_init(&mod->data_layout, set_memory_nx);
	frob_writable_data(&mod->data_layout, set_memory_nx);
	frob_rodata(&mod->init_layout, set_memory_nx);
	frob_writable_data(&mod->init_layout, set_memory_nx);
}

int module_enforce_rwx_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
				char *secstrings, struct module *mod)
{
	const unsigned long shf_wx = SHF_WRITE | SHF_EXECINSTR;
	int i;

	if (!IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		return 0;

	for (i = 0; i < hdr->e_shnum; i++) {
		if ((sechdrs[i].sh_flags & shf_wx) == shf_wx) {
			pr_err("%s: section %s (index %d) has invalid WRITE|EXEC flags\n",
			       mod->name, secstrings + sechdrs[i].sh_name, i);
			return -ENOEXEC;
		}
	}

	return 0;
}
