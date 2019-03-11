// SPDX-License-Identifier: GPL-2.0
#include <elf.h>
#include <inttypes.h>
#include <stdio.h>

#include "map.h"
#include "symbol.h"

size_t symbol__fprintf(struct symbol *sym, FILE *fp)
{
	return fprintf(fp, " %" PRIx64 "-%" PRIx64 " %c %s\n",
		       sym->start, sym->end,
		       sym->binding == STB_GLOBAL ? 'g' :
		       sym->binding == STB_LOCAL  ? 'l' : 'w',
		       sym->name);
}

size_t __symbol__fprintf_symname_offs(const struct symbol *sym,
				      const struct addr_location *al,
				      bool unknown_as_addr,
				      bool print_offsets, FILE *fp)
{
	unsigned long offset;
	size_t length;

	if (sym) {
		length = fprintf(fp, "%s", sym->name);
		if (al && print_offsets) {
			if (al->addr < sym->end)
				offset = al->addr - sym->start;
			else
				offset = al->addr - al->map->start - sym->start;
			length += fprintf(fp, "+0x%lx", offset);
		}
		return length;
	} else if (al && unknown_as_addr)
		return fprintf(fp, "[%#" PRIx64 "]", al->addr);
	else
		return fprintf(fp, "[unknown]");
}

size_t symbol__fprintf_symname_offs(const struct symbol *sym,
				    const struct addr_location *al,
				    FILE *fp)
{
	return __symbol__fprintf_symname_offs(sym, al, false, true, fp);
}

size_t __symbol__fprintf_symname(const struct symbol *sym,
				 const struct addr_location *al,
				 bool unknown_as_addr, FILE *fp)
{
	return __symbol__fprintf_symname_offs(sym, al, unknown_as_addr, false, fp);
}

size_t symbol__fprintf_symname(const struct symbol *sym, FILE *fp)
{
	return __symbol__fprintf_symname_offs(sym, NULL, false, false, fp);
}

size_t dso__fprintf_symbols_by_name(struct dso *dso,
				    FILE *fp)
{
	size_t ret = 0;
	struct rb_node *nd;
	struct symbol_name_rb_node *pos;

	for (nd = rb_first_cached(&dso->symbol_names); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct symbol_name_rb_node, rb_node);
		fprintf(fp, "%s\n", pos->sym.name);
	}

	return ret;
}
