/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Module internals
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/elf.h>
#include <asm/module.h>
#include <linux/mutex.h>

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG - 1))
/* Maximum number of characters written by module_flags() */
#define MODULE_FLAGS_BUF_SIZE (TAINT_FLAGS_COUNT + 4)

extern struct mutex module_mutex;
extern struct list_head modules;

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___ksymtab_gpl[];
extern const struct kernel_symbol __stop___ksymtab_gpl[];
extern const s32 __start___kcrctab[];
extern const s32 __start___kcrctab_gpl[];

struct load_info {
	const char *name;
	/* pointer to module in temporary copy, freed at end of load_module() */
	struct module *mod;
	Elf_Ehdr *hdr;
	unsigned long len;
	Elf_Shdr *sechdrs;
	char *secstrings, *strtab;
	unsigned long symoffs, stroffs, init_typeoffs, core_typeoffs;
	struct _ddebug *debug;
	unsigned int num_debug;
	bool sig_ok;
#ifdef CONFIG_KALLSYMS
	unsigned long mod_kallsyms_init_off;
#endif
#ifdef CONFIG_MODULE_DECOMPRESS
	struct page **pages;
	unsigned int max_pages;
	unsigned int used_pages;
#endif
	struct {
		unsigned int sym, str, mod, vers, info, pcpu;
	} index;
};

extern int mod_verify_sig(const void *mod, struct load_info *info);

#ifdef CONFIG_MODULE_DECOMPRESS
int module_decompress(struct load_info *info, const void *buf, size_t size);
void module_decompress_cleanup(struct load_info *info);
#else
static inline int module_decompress(struct load_info *info,
				    const void *buf, size_t size)
{
	return -EOPNOTSUPP;
}
static inline void module_decompress_cleanup(struct load_info *info)
{
}
#endif
