// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>

#include <objtool/special.h>
#include <objtool/builtin.h>

#define X86_FEATURE_POPCNT (4 * 32 + 23)
#define X86_FEATURE_SMAP   (9 * 32 + 20)

void arch_handle_alternative(unsigned short feature, struct special_alt *alt)
{
	static struct special_alt *group, *prev;

	/*
	 * Recompute orig_len for nested ALTERNATIVE()s.
	 */
	if (group && group->orig_sec == alt->orig_sec &&
	             group->orig_off == alt->orig_off) {

		struct special_alt *iter = group;
		for (;;) {
			unsigned int len = max(iter->orig_len, alt->orig_len);
			iter->orig_len = alt->orig_len = len;

			if (iter == prev)
				break;

			iter = list_next_entry(iter, list);
		}

	} else group = alt;

	prev = alt;

	switch (feature) {
	case X86_FEATURE_SMAP:
		/*
		 * If UACCESS validation is enabled; force that alternative;
		 * otherwise force it the other way.
		 *
		 * What we want to avoid is having both the original and the
		 * alternative code flow at the same time, in that case we can
		 * find paths that see the STAC but take the NOP instead of
		 * CLAC and the other way around.
		 */
		if (opts.uaccess)
			alt->skip_orig = true;
		else
			alt->skip_alt = true;
		break;
	case X86_FEATURE_POPCNT:
		/*
		 * It has been requested that we don't validate the !POPCNT
		 * feature path which is a "very very small percentage of
		 * machines".
		 */
		alt->skip_orig = true;
		break;
	default:
		break;
	}
}

bool arch_support_alt_relocation(struct special_alt *special_alt,
				 struct instruction *insn,
				 struct reloc *reloc)
{
	return true;
}

/*
 * There are 3 basic jump table patterns:
 *
 * 1. jmpq *[rodata addr](,%reg,8)
 *
 *    This is the most common case by far.  It jumps to an address in a simple
 *    jump table which is stored in .rodata.
 *
 * 2. jmpq *[rodata addr](%rip)
 *
 *    This is caused by a rare GCC quirk, currently only seen in three driver
 *    functions in the kernel, only with certain obscure non-distro configs.
 *
 *    As part of an optimization, GCC makes a copy of an existing switch jump
 *    table, modifies it, and then hard-codes the jump (albeit with an indirect
 *    jump) to use a single entry in the table.  The rest of the jump table and
 *    some of its jump targets remain as dead code.
 *
 *    In such a case we can just crudely ignore all unreachable instruction
 *    warnings for the entire object file.  Ideally we would just ignore them
 *    for the function, but that would require redesigning the code quite a
 *    bit.  And honestly that's just not worth doing: unreachable instruction
 *    warnings are of questionable value anyway, and this is such a rare issue.
 *
 * 3. mov [rodata addr],%reg1
 *    ... some instructions ...
 *    jmpq *(%reg1,%reg2,8)
 *
 *    This is a fairly uncommon pattern which is new for GCC 6.  As of this
 *    writing, there are 11 occurrences of it in the allmodconfig kernel.
 *
 *    As of GCC 7 there are quite a few more of these and the 'in between' code
 *    is significant. Esp. with KASAN enabled some of the code between the mov
 *    and jmpq uses .rodata itself, which can confuse things.
 *
 *    TODO: Once we have DWARF CFI and smarter instruction decoding logic,
 *    ensure the same register is used in the mov and jump instructions.
 *
 *    NOTE: MITIGATION_RETPOLINE made it harder still to decode dynamic jumps.
 */
struct reloc *arch_find_switch_table(struct objtool_file *file,
				     struct instruction *insn,
				     unsigned long *table_size)
{
	struct reloc  *text_reloc, *rodata_reloc;
	struct section *table_sec;
	unsigned long table_offset;

	/* look for a relocation which references .rodata */
	text_reloc = find_reloc_by_dest_range(file->elf, insn->sec,
					      insn->offset, insn->len);
	if (!text_reloc || text_reloc->sym->type != STT_SECTION ||
	    !text_reloc->sym->sec->rodata)
		return NULL;

	table_offset = reloc_addend(text_reloc);
	table_sec = text_reloc->sym->sec;

	if (reloc_type(text_reloc) == R_X86_64_PC32)
		table_offset += 4;

	/*
	 * Make sure the .rodata address isn't associated with a
	 * symbol.  GCC jump tables are anonymous data.
	 *
	 * Also support C jump tables which are in the same format as
	 * switch jump tables.  For objtool to recognize them, they
	 * need to be placed in the C_JUMP_TABLE_SECTION section.  They
	 * have symbols associated with them.
	 */
	if (find_symbol_containing(table_sec, table_offset) &&
	    strcmp(table_sec->name, C_JUMP_TABLE_SECTION))
		return NULL;

	/*
	 * Each table entry has a rela associated with it.  The rela
	 * should reference text in the same function as the original
	 * instruction.
	 */
	rodata_reloc = find_reloc_by_dest(file->elf, table_sec, table_offset);
	if (!rodata_reloc)
		return NULL;

	/*
	 * Use of RIP-relative switch jumps is quite rare, and
	 * indicates a rare GCC quirk/bug which can leave dead
	 * code behind.
	 */
	if (reloc_type(text_reloc) == R_X86_64_PC32)
		file->ignore_unreachables = true;

	*table_size = 0;
	return rodata_reloc;
}
