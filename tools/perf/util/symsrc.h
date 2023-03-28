/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SYMSRC_
#define __PERF_SYMSRC_ 1

#include <stdbool.h>
#include <stddef.h>
#include "dso.h"

#ifdef HAVE_LIBELF_SUPPORT
#include <libelf.h>
#include <gelf.h>
#endif
#include <elf.h>

struct symsrc {
	char		     *name;
	int		     fd;
	enum dso_binary_type type;

#ifdef HAVE_LIBELF_SUPPORT
	Elf		     *elf;
	GElf_Ehdr	     ehdr;

	Elf_Scn		     *opdsec;
	size_t		     opdidx;
	GElf_Shdr	     opdshdr;

	Elf_Scn		     *symtab;
	size_t		     symtab_idx;
	GElf_Shdr	     symshdr;

	Elf_Scn		     *dynsym;
	size_t		     dynsym_idx;
	GElf_Shdr	     dynshdr;

	bool		     adjust_symbols;
	bool		     is_64_bit;
#endif
};

int symsrc__init(struct symsrc *ss, struct dso *dso, const char *name, enum dso_binary_type type);
void symsrc__destroy(struct symsrc *ss);

bool symsrc__has_symtab(struct symsrc *ss);
bool symsrc__possibly_runtime(struct symsrc *ss);

#endif /* __PERF_SYMSRC_ */
