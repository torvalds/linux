// SPDX-License-Identifier: GPL-2.0

/*
 * Helper functions for finding the symbol in an ELF which is "nearest"
 * to a given address.
 */
#include <xalloc.h>
#include "modpost.h"

struct syminfo {
	unsigned int symbol_index;
	unsigned int section_index;
	Elf_Addr addr;
};

/*
 * Container used to hold an entire binary search table.
 * Entries in table are ascending, sorted first by section_index,
 * then by addr, and last by symbol_index.  The sorting by
 * symbol_index is used to ensure predictable behavior when
 * multiple symbols are present with the same address; all
 * symbols past the first are effectively ignored, by eliding
 * them in symsearch_fixup().
 */
struct symsearch {
	unsigned int table_size;
	struct syminfo table[];
};

static int syminfo_compare(const void *s1, const void *s2)
{
	const struct syminfo *sym1 = s1;
	const struct syminfo *sym2 = s2;

	if (sym1->section_index > sym2->section_index)
		return 1;
	if (sym1->section_index < sym2->section_index)
		return -1;
	if (sym1->addr > sym2->addr)
		return 1;
	if (sym1->addr < sym2->addr)
		return -1;
	if (sym1->symbol_index > sym2->symbol_index)
		return 1;
	if (sym1->symbol_index < sym2->symbol_index)
		return -1;
	return 0;
}

static unsigned int symbol_count(struct elf_info *elf)
{
	unsigned int result = 0;

	for (Elf_Sym *sym = elf->symtab_start; sym < elf->symtab_stop; sym++) {
		if (is_valid_name(elf, sym))
			result++;
	}
	return result;
}

/*
 * Populate the search array that we just allocated.
 * Be slightly paranoid here.  The ELF file is mmap'd and could
 * conceivably change between symbol_count() and symsearch_populate().
 * If we notice any difference, bail out rather than potentially
 * propagating errors or crashing.
 */
static void symsearch_populate(struct elf_info *elf,
			       struct syminfo *table,
			       unsigned int table_size)
{
	bool is_arm = (elf->hdr->e_machine == EM_ARM);

	for (Elf_Sym *sym = elf->symtab_start; sym < elf->symtab_stop; sym++) {
		if (is_valid_name(elf, sym)) {
			if (table_size-- == 0)
				fatal("%s: size mismatch\n", __func__);
			table->symbol_index = sym - elf->symtab_start;
			table->section_index = get_secindex(elf, sym);
			table->addr = sym->st_value;

			/*
			 * For ARM Thumb instruction, the bit 0 of st_value is
			 * set if the symbol is STT_FUNC type. Mask it to get
			 * the address.
			 */
			if (is_arm && ELF_ST_TYPE(sym->st_info) == STT_FUNC)
				table->addr &= ~1;

			table++;
		}
	}

	if (table_size != 0)
		fatal("%s: size mismatch\n", __func__);
}

/*
 * Do any fixups on the table after sorting.
 * For now, this just finds adjacent entries which have
 * the same section_index and addr, and it propagates
 * the first symbol_index over the subsequent entries,
 * so that only one symbol_index is seen for any given
 * section_index and addr.  This ensures that whether
 * we're looking at an address from "above" or "below"
 * that we see the same symbol_index.
 * This does leave some duplicate entries in the table;
 * in practice, these are a small fraction of the
 * total number of entries, and they are harmless to
 * the binary search algorithm other than a few occasional
 * unnecessary comparisons.
 */
static void symsearch_fixup(struct syminfo *table, unsigned int table_size)
{
	/* Don't look at index 0, it will never change. */
	for (unsigned int i = 1; i < table_size; i++) {
		if (table[i].addr == table[i - 1].addr &&
		    table[i].section_index == table[i - 1].section_index) {
			table[i].symbol_index = table[i - 1].symbol_index;
		}
	}
}

void symsearch_init(struct elf_info *elf)
{
	unsigned int table_size = symbol_count(elf);

	elf->symsearch = xmalloc(sizeof(struct symsearch) +
				       sizeof(struct syminfo) * table_size);
	elf->symsearch->table_size = table_size;

	symsearch_populate(elf, elf->symsearch->table, table_size);
	qsort(elf->symsearch->table, table_size,
	      sizeof(struct syminfo), syminfo_compare);

	symsearch_fixup(elf->symsearch->table, table_size);
}

void symsearch_finish(struct elf_info *elf)
{
	free(elf->symsearch);
	elf->symsearch = NULL;
}

/*
 * Find the syminfo which is in secndx and "nearest" to addr.
 * allow_negative: allow returning a symbol whose address is > addr.
 * min_distance: ignore symbols which are further away than this.
 *
 * Returns a pointer into the symbol table for success.
 * Returns NULL if no legal symbol is found within the requested range.
 */
Elf_Sym *symsearch_find_nearest(struct elf_info *elf, Elf_Addr addr,
				unsigned int secndx, bool allow_negative,
				Elf_Addr min_distance)
{
	unsigned int hi = elf->symsearch->table_size;
	unsigned int lo = 0;
	struct syminfo *table = elf->symsearch->table;
	struct syminfo target;

	target.addr = addr;
	target.section_index = secndx;
	target.symbol_index = ~0;  /* compares greater than any actual index */
	while (hi > lo) {
		unsigned int mid = lo + (hi - lo) / 2;  /* Avoids overflow */

		if (syminfo_compare(&table[mid], &target) > 0)
			hi = mid;
		else
			lo = mid + 1;
	}

	/*
	 * table[hi], if it exists, is the first entry in the array which
	 * lies beyond target.  table[hi - 1], if it exists, is the last
	 * entry in the array which comes before target, including the
	 * case where it perfectly matches the section and the address.
	 *
	 * Note -- if the address we're looking up falls perfectly
	 * in the middle of two symbols, this is written to always
	 * prefer the symbol with the lower address.
	 */
	Elf_Sym *result = NULL;

	if (allow_negative &&
	    hi < elf->symsearch->table_size &&
	    table[hi].section_index == secndx &&
	    table[hi].addr - addr <= min_distance) {
		min_distance = table[hi].addr - addr;
		result = &elf->symtab_start[table[hi].symbol_index];
	}
	if (hi > 0 &&
	    table[hi - 1].section_index == secndx &&
	    addr - table[hi - 1].addr <= min_distance) {
		result = &elf->symtab_start[table[hi - 1].symbol_index];
	}
	return result;
}
