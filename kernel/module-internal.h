/* Module internals
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/elf.h>
#include <asm/module.h>

struct load_info {
	const char *name;
	/* pointer to module in temporary copy, freed at end of load_module() */
	struct module *mod;
	Elf_Ehdr *hdr;
	unsigned long len;
	Elf_Shdr *sechdrs;
	char *secstrings, *strtab;
	unsigned long symoffs, stroffs;
	struct _ddebug *debug;
	unsigned int num_debug;
	bool sig_ok;
#ifdef CONFIG_KALLSYMS
	unsigned long mod_kallsyms_init_off;
#endif
	struct {
		unsigned int sym, str, mod, vers, info, pcpu;
	} index;
};

extern int mod_verify_sig(const void *mod, struct load_info *info);
