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

static void frob_rodata(const struct module_layout *layout,
		 int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON(!PAGE_ALIGNED(layout->base));
	BUG_ON(!PAGE_ALIGNED(layout->text_size));
	BUG_ON(!PAGE_ALIGNED(layout->ro_size));
	set_memory((unsigned long)layout->base + layout->text_size,
		   (layout->ro_size - layout->text_size) >> PAGE_SHIFT);
}

static void frob_ro_after_init(const struct module_layout *layout,
			int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON(!PAGE_ALIGNED(layout->base));
	BUG_ON(!PAGE_ALIGNED(layout->ro_size));
	BUG_ON(!PAGE_ALIGNED(layout->ro_after_init_size));
	set_memory((unsigned long)layout->base + layout->ro_size,
		   (layout->ro_after_init_size - layout->ro_size) >> PAGE_SHIFT);
}

static void frob_writable_data(const struct module_layout *layout,
			int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON(!PAGE_ALIGNED(layout->base));
	BUG_ON(!PAGE_ALIGNED(layout->ro_after_init_size));
	BUG_ON(!PAGE_ALIGNED(layout->size));
	set_memory((unsigned long)layout->base + layout->ro_after_init_size,
		   (layout->size - layout->ro_after_init_size) >> PAGE_SHIFT);
}

void module_enable_ro(const struct module *mod, bool after_init)
{
	if (!rodata_enabled)
		return;

	set_vm_flush_reset_perms(mod->core_layout.base);
	set_vm_flush_reset_perms(mod->init_layout.base);
	frob_text(&mod->core_layout, set_memory_ro);

	frob_rodata(&mod->core_layout, set_memory_ro);
	frob_text(&mod->init_layout, set_memory_ro);
	frob_rodata(&mod->init_layout, set_memory_ro);

	if (after_init)
		frob_ro_after_init(&mod->core_layout, set_memory_ro);
}

void module_enable_nx(const struct module *mod)
{
	frob_rodata(&mod->core_layout, set_memory_nx);
	frob_ro_after_init(&mod->core_layout, set_memory_nx);
	frob_writable_data(&mod->core_layout, set_memory_nx);
	frob_rodata(&mod->init_layout, set_memory_nx);
	frob_writable_data(&mod->init_layout, set_memory_nx);
}

int module_enforce_rwx_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
				char *secstrings, struct module *mod)
{
	const unsigned long shf_wx = SHF_WRITE | SHF_EXECINSTR;
	int i;

	for (i = 0; i < hdr->e_shnum; i++) {
		if ((sechdrs[i].sh_flags & shf_wx) == shf_wx) {
			pr_err("%s: section %s (index %d) has invalid WRITE|EXEC flags\n",
			       mod->name, secstrings + sechdrs[i].sh_name, i);
			return -ENOEXEC;
		}
	}

	return 0;
}
