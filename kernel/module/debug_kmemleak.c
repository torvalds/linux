// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module kmemleak support
 *
 * Copyright (C) 2009 Catalin Marinas
 */

#include <linux/module.h>
#include <linux/kmemleak.h>
#include "internal.h"

void kmemleak_load_module(const struct module *mod,
			  const struct load_info *info)
{
	unsigned int i;

	/* only scan the sections containing data */
	kmemleak_scan_area(mod, sizeof(struct module), GFP_KERNEL);

	for (i = 1; i < info->hdr->e_shnum; i++) {
		/* Scan all writable sections that's not executable */
		if (!(info->sechdrs[i].sh_flags & SHF_ALLOC) ||
		    !(info->sechdrs[i].sh_flags & SHF_WRITE) ||
		    (info->sechdrs[i].sh_flags & SHF_EXECINSTR))
			continue;

		kmemleak_scan_area((void *)info->sechdrs[i].sh_addr,
				   info->sechdrs[i].sh_size, GFP_KERNEL);
	}
}
