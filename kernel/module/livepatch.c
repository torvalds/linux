// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module livepatch support
 *
 * Copyright (C) 2016 Jessica Yu <jeyu@redhat.com>
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "internal.h"

/*
 * Persist ELF information about a module. Copy the ELF header,
 * section header table, section string table, and symtab section
 * index from info to mod->klp_info.
 */
int copy_module_elf(struct module *mod, struct load_info *info)
{
	unsigned int size, symndx;
	int ret;

	size = sizeof(*mod->klp_info);
	mod->klp_info = kmalloc(size, GFP_KERNEL);
	if (!mod->klp_info)
		return -ENOMEM;

	/* ELF header */
	size = sizeof(mod->klp_info->hdr);
	memcpy(&mod->klp_info->hdr, info->hdr, size);

	/* ELF section header table */
	size = sizeof(*info->sechdrs) * info->hdr->e_shnum;
	mod->klp_info->sechdrs = kmemdup(info->sechdrs, size, GFP_KERNEL);
	if (!mod->klp_info->sechdrs) {
		ret = -ENOMEM;
		goto free_info;
	}

	/* ELF section name string table */
	size = info->sechdrs[info->hdr->e_shstrndx].sh_size;
	mod->klp_info->secstrings = kmemdup(info->secstrings, size, GFP_KERNEL);
	if (!mod->klp_info->secstrings) {
		ret = -ENOMEM;
		goto free_sechdrs;
	}

	/* ELF symbol section index */
	symndx = info->index.sym;
	mod->klp_info->symndx = symndx;

	/*
	 * For livepatch modules, core_kallsyms.symtab is a complete
	 * copy of the original symbol table. Adjust sh_addr to point
	 * to core_kallsyms.symtab since the copy of the symtab in module
	 * init memory is freed at the end of do_init_module().
	 */
	mod->klp_info->sechdrs[symndx].sh_addr = (unsigned long)mod->core_kallsyms.symtab;

	return 0;

free_sechdrs:
	kfree(mod->klp_info->sechdrs);
free_info:
	kfree(mod->klp_info);
	return ret;
}

void free_module_elf(struct module *mod)
{
	kfree(mod->klp_info->sechdrs);
	kfree(mod->klp_info->secstrings);
	kfree(mod->klp_info);
}
