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
	/* only scan writable, non-executable sections */
	for_each_mod_mem_type(type) {
		if (type != MOD_DATA && type != MOD_INIT_DATA)
			kmemleak_no_scan(mod->mem[type].base);
	}
}
