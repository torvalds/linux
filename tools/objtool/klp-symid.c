// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Emit the .klp.symid table which allows "objtool klp diff" to reliably
 * disambiguate duplicate-named local symbols in vmlinux.
 *
 * Livepatch identifies a duplicate-named symbol by its position (sympos)
 * among the same-named kallsyms entries, counted in ascending address order
 * in the final linked vmlinux.  That order can't be derived from vmlinux.o
 * alone: the final link reorders sub-sections (.text.unlikely*, .data..*,
 * etc).
 *
 * Bridge the gap with a table which survives the final link: a single
 * non-alloc section containing an array of { id, addr } entries, where
 * 'id' is a unique counter identifier and 'addr' has a relocation to the
 * symbol.  The linker copies 'id' verbatim and resolves 'addr' to the symbol's
 * final address.
 *
 * The table is only emitted for vmlinux.o, and only when klp-build asks for it
 * with KLP_SYMIDS=1, which adds --klp-symids to the vmlinux.o objtool run.
 *
 * It can't survive --gc-sections, which sweeps the whole section; klp-build
 * rejects CONFIG_LD_DEAD_CODE_DATA_ELIMINATION.
 */
#include <linux/string.h>

#include <objtool/objtool.h>
#include <objtool/warn.h>
#include <objtool/endianness.h>
#include <objtool/klp.h>

static const char * const discarded_secs[] = {
	".discard",
	".exitcall.exit",
	".modinfo",
	".no_trim_symbol",
	"__tracepoint_check",
};

static bool discarded_sec(struct section *sec)
{
	if (!(sec->sh.sh_flags & SHF_ALLOC))
		return true;

	for (int i = 0; i < ARRAY_SIZE(discarded_secs); i++)
		if (strstarts(sec->name, discarded_secs[i]))
			return true;

	return false;
}

static bool symid_needed(struct elf *elf, struct symbol *sym)
{
	struct symbol *s;

	if (!is_local_sym(sym) || is_undef_sym(sym))
		return false;

	if (!is_func_sym(sym) && !is_object_sym(sym))
		return false;

	if (is_prefix_func(sym))
		return false;

	if (discarded_sec(sym->sec))
		return false;

	for_each_sym_by_name(elf, sym->name, s) {
		if (s == sym || is_sec_sym(s) || is_file_sym(s) || is_undef_sym(s))
			continue;
		return true;
	}

	return false;
}

int klp_create_symid_sections(struct objtool_file *file)
{
	struct elf *elf = file->elf;
	struct klp_symid *symids;
	struct section *sec;
	struct symbol *sym;
	u64 nr = 0, i = 0;

	if (!str_ends_with(objname, "vmlinux.o"))
		return 0;

	for_each_sym(elf, sym)
		if (symid_needed(elf, sym))
			nr++;

	if (!nr)
		return 0;

	sec = elf_create_section(elf, KLP_SYMID_SEC, 0, sizeof(struct klp_symid),
				 SHT_PROGBITS, 8, 0);
	if (!sec)
		return -1;

	symids = elf_add_data(elf, sec, NULL, nr * sizeof(struct klp_symid));
	if (!symids)
		return -1;

	for_each_sym(elf, sym) {
		if (!symid_needed(elf, sym))
			continue;

		symids[i].id = bswap_if_needed(elf, i);

		if (!elf_create_reloc(elf, sec,
				      i * sizeof(struct klp_symid) +
				      offsetof(struct klp_symid, addr),
				      sym, 0, R_ABS64))
			return -1;

		i++;
	}

	return 0;
}
