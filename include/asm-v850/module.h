/*
 * include/asm-v850/module.h -- Architecture-specific module hooks
 *
 *  Copyright (C) 2001,02,03,04  NEC Corporation
 *  Copyright (C) 2001,02,03,04  Miles Bader <miles@gnu.org>
 *  Copyright (C) 2001,03  Rusty Russell
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 *
 * Derived in part from include/asm-ppc/module.h
 */

#ifndef __V850_MODULE_H__
#define __V850_MODULE_H__

#define MODULE_SYMBOL_PREFIX "_"

struct v850_plt_entry
{
	/* Indirect jump instruction sequence (6-byte mov + 2-byte jr).  */
	unsigned long tramp[2];
};

struct mod_arch_specific
{
	/* Indices of PLT sections within module. */
	unsigned int core_plt_section, init_plt_section;
};

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr

/* Make empty sections for module_frob_arch_sections to expand. */
#ifdef MODULE
asm(".section .plt,\"ax\",@nobits; .align 3; .previous");
asm(".section .init.plt,\"ax\",@nobits; .align 3; .previous");
#endif

/* We don't do exception tables.  */
struct exception_table_entry;
static inline const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value)
{
	return 0;
}
#define ARCH_HAS_SEARCH_EXTABLE
static inline void
sort_extable(struct exception_table_entry *start,
	     struct exception_table_entry *finish)
{
	/* nada */
}
#define ARCH_HAS_SORT_EXTABLE

#endif /* __V850_MODULE_H__ */
