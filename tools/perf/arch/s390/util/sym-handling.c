/*
 * Architecture specific ELF symbol handling and relocation mapping.
 *
 * Copyright 2017 IBM Corp.
 * Author(s): Thomas Richter <tmricht@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */

#include "symbol.h"

#ifdef HAVE_LIBELF_SUPPORT
bool elf__needs_adjust_symbols(GElf_Ehdr ehdr)
{
	if (ehdr.e_type == ET_EXEC)
		return false;
	return ehdr.e_type == ET_REL || ehdr.e_type == ET_DYN;
}

void arch__adjust_sym_map_offset(GElf_Sym *sym,
				 GElf_Shdr *shdr __maybe_unused,
				 struct map *map)
{
	if (map->type == MAP__FUNCTION)
		sym->st_value += map->start;
}
#endif
