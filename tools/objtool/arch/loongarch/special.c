// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include <objtool/special.h>
#include <objtool/warn.h>

bool arch_support_alt_relocation(struct special_alt *special_alt,
				 struct instruction *insn,
				 struct reloc *reloc)
{
	return false;
}

struct table_info {
	struct list_head jump_info;
	unsigned long insn_offset;
	unsigned long rodata_offset;
};

static void get_rodata_table_size_by_table_annotate(struct objtool_file *file,
						    struct instruction *insn,
						    unsigned long *table_size)
{
	struct section *rsec;
	struct reloc *reloc;
	struct list_head table_list;
	struct table_info *orig_table;
	struct table_info *next_table;
	unsigned long tmp_insn_offset;
	unsigned long tmp_rodata_offset;

	rsec = find_section_by_name(file->elf, ".rela.discard.tablejump_annotate");
	if (!rsec)
		return;

	INIT_LIST_HEAD(&table_list);

	for_each_reloc(rsec, reloc) {
		orig_table = malloc(sizeof(struct table_info));
		if (!orig_table) {
			WARN("malloc failed");
			return;
		}

		orig_table->insn_offset = reloc->sym->offset + reloc_addend(reloc);
		reloc++;
		orig_table->rodata_offset = reloc->sym->offset + reloc_addend(reloc);

		list_add_tail(&orig_table->jump_info, &table_list);

		if (reloc_idx(reloc) + 1 == sec_num_entries(rsec))
			break;
	}

	list_for_each_entry(orig_table, &table_list, jump_info) {
		next_table = list_next_entry(orig_table, jump_info);
		list_for_each_entry_from(next_table, &table_list, jump_info) {
			if (next_table->rodata_offset < orig_table->rodata_offset) {
				tmp_insn_offset = next_table->insn_offset;
				tmp_rodata_offset = next_table->rodata_offset;
				next_table->insn_offset = orig_table->insn_offset;
				next_table->rodata_offset = orig_table->rodata_offset;
				orig_table->insn_offset = tmp_insn_offset;
				orig_table->rodata_offset = tmp_rodata_offset;
			}
		}
	}

	list_for_each_entry(orig_table, &table_list, jump_info) {
		if (insn->offset == orig_table->insn_offset) {
			next_table = list_next_entry(orig_table, jump_info);
			if (&next_table->jump_info == &table_list) {
				*table_size = 0;
				return;
			}

			while (next_table->rodata_offset == orig_table->rodata_offset) {
				next_table = list_next_entry(next_table, jump_info);
				if (&next_table->jump_info == &table_list) {
					*table_size = 0;
					return;
				}
			}

			*table_size = next_table->rodata_offset - orig_table->rodata_offset;
		}
	}
}

static struct reloc *find_reloc_by_table_annotate(struct objtool_file *file,
						  struct instruction *insn,
						  unsigned long *table_size)
{
	struct section *rsec;
	struct reloc *reloc;
	unsigned long offset;

	rsec = find_section_by_name(file->elf, ".rela.discard.tablejump_annotate");
	if (!rsec)
		return NULL;

	for_each_reloc(rsec, reloc) {
		if (reloc->sym->sec->rodata)
			continue;

		if (strcmp(insn->sec->name, reloc->sym->sec->name))
			continue;

		offset = reloc->sym->offset + reloc_addend(reloc);
		if (insn->offset == offset) {
			get_rodata_table_size_by_table_annotate(file, insn, table_size);
			reloc++;
			return reloc;
		}
	}

	return NULL;
}

static struct reloc *find_reloc_of_rodata_c_jump_table(struct section *sec,
						       unsigned long offset,
						       unsigned long *table_size)
{
	struct section *rsec;
	struct reloc *reloc;

	rsec = sec->rsec;
	if (!rsec)
		return NULL;

	for_each_reloc(rsec, reloc) {
		if (reloc_offset(reloc) > offset)
			break;

		if (!strcmp(reloc->sym->sec->name, C_JUMP_TABLE_SECTION)) {
			*table_size = 0;
			return reloc;
		}
	}

	return NULL;
}

struct reloc *arch_find_switch_table(struct objtool_file *file,
				     struct instruction *insn,
				     unsigned long *table_size)
{
	struct reloc *annotate_reloc;
	struct reloc *rodata_reloc;
	struct section *table_sec;
	unsigned long table_offset;

	annotate_reloc = find_reloc_by_table_annotate(file, insn, table_size);
	if (!annotate_reloc) {
		annotate_reloc = find_reloc_of_rodata_c_jump_table(
				 insn->sec, insn->offset, table_size);
		if (!annotate_reloc)
			return NULL;
	}

	table_sec = annotate_reloc->sym->sec;
	table_offset = annotate_reloc->sym->offset + reloc_addend(annotate_reloc);

	/*
	 * Each table entry has a rela associated with it.  The rela
	 * should reference text in the same function as the original
	 * instruction.
	 */
	rodata_reloc = find_reloc_by_dest(file->elf, table_sec, table_offset);
	if (!rodata_reloc)
		return NULL;

	return rodata_reloc;
}
