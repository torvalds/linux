/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdlib.h>

#include "check.h"
#include "elf.h"
#include "special.h"
#include "arch.h"
#include "warn.h"

#include <linux/hashtable.h>
#include <linux/kernel.h>

struct alternative {
	struct list_head list;
	struct instruction *insn;
};

const char *objname;
static bool nofp;
struct cfi_state initial_func_cfi;

static struct instruction *find_insn(struct objtool_file *file,
				     struct section *sec, unsigned long offset)
{
	struct instruction *insn;

	hash_for_each_possible(file->insn_hash, insn, hash, offset)
		if (insn->sec == sec && insn->offset == offset)
			return insn;

	return NULL;
}

static struct instruction *next_insn_same_sec(struct objtool_file *file,
					      struct instruction *insn)
{
	struct instruction *next = list_next_entry(insn, list);

	if (!next || &next->list == &file->insn_list || next->sec != insn->sec)
		return NULL;

	return next;
}

static bool gcov_enabled(struct objtool_file *file)
{
	struct section *sec;
	struct symbol *sym;

	for_each_sec(file, sec)
		list_for_each_entry(sym, &sec->symbol_list, list)
			if (!strncmp(sym->name, "__gcov_.", 8))
				return true;

	return false;
}

#define func_for_each_insn(file, func, insn)				\
	for (insn = find_insn(file, func->sec, func->offset);		\
	     insn && &insn->list != &file->insn_list &&			\
		insn->sec == func->sec &&				\
		insn->offset < func->offset + func->len;		\
	     insn = list_next_entry(insn, list))

#define func_for_each_insn_continue_reverse(file, func, insn)		\
	for (insn = list_prev_entry(insn, list);			\
	     &insn->list != &file->insn_list &&				\
		insn->sec == func->sec && insn->offset >= func->offset;	\
	     insn = list_prev_entry(insn, list))

#define sec_for_each_insn_from(file, insn)				\
	for (; insn; insn = next_insn_same_sec(file, insn))

#define sec_for_each_insn_continue(file, insn)				\
	for (insn = next_insn_same_sec(file, insn); insn;		\
	     insn = next_insn_same_sec(file, insn))

/*
 * Check if the function has been manually whitelisted with the
 * STACK_FRAME_NON_STANDARD macro, or if it should be automatically whitelisted
 * due to its use of a context switching instruction.
 */
static bool ignore_func(struct objtool_file *file, struct symbol *func)
{
	struct rela *rela;
	struct instruction *insn;

	/* check for STACK_FRAME_NON_STANDARD */
	if (file->whitelist && file->whitelist->rela)
		list_for_each_entry(rela, &file->whitelist->rela->rela_list, list) {
			if (rela->sym->type == STT_SECTION &&
			    rela->sym->sec == func->sec &&
			    rela->addend == func->offset)
				return true;
			if (rela->sym->type == STT_FUNC && rela->sym == func)
				return true;
		}

	/* check if it has a context switching instruction */
	func_for_each_insn(file, func, insn)
		if (insn->type == INSN_CONTEXT_SWITCH)
			return true;

	return false;
}

/*
 * This checks to see if the given function is a "noreturn" function.
 *
 * For global functions which are outside the scope of this object file, we
 * have to keep a manual list of them.
 *
 * For local functions, we have to detect them manually by simply looking for
 * the lack of a return instruction.
 *
 * Returns:
 *  -1: error
 *   0: no dead end
 *   1: dead end
 */
static int __dead_end_function(struct objtool_file *file, struct symbol *func,
			       int recursion)
{
	int i;
	struct instruction *insn;
	bool empty = true;

	/*
	 * Unfortunately these have to be hard coded because the noreturn
	 * attribute isn't provided in ELF data.
	 */
	static const char * const global_noreturns[] = {
		"__stack_chk_fail",
		"panic",
		"do_exit",
		"do_task_dead",
		"__module_put_and_exit",
		"complete_and_exit",
		"kvm_spurious_fault",
		"__reiserfs_panic",
		"lbug_with_loc",
		"fortify_panic",
	};

	if (func->bind == STB_WEAK)
		return 0;

	if (func->bind == STB_GLOBAL)
		for (i = 0; i < ARRAY_SIZE(global_noreturns); i++)
			if (!strcmp(func->name, global_noreturns[i]))
				return 1;

	if (!func->sec)
		return 0;

	func_for_each_insn(file, func, insn) {
		empty = false;

		if (insn->type == INSN_RETURN)
			return 0;
	}

	if (empty)
		return 0;

	/*
	 * A function can have a sibling call instead of a return.  In that
	 * case, the function's dead-end status depends on whether the target
	 * of the sibling call returns.
	 */
	func_for_each_insn(file, func, insn) {
		if (insn->sec != func->sec ||
		    insn->offset >= func->offset + func->len)
			break;

		if (insn->type == INSN_JUMP_UNCONDITIONAL) {
			struct instruction *dest = insn->jump_dest;
			struct symbol *dest_func;

			if (!dest)
				/* sibling call to another file */
				return 0;

			if (dest->sec != func->sec ||
			    dest->offset < func->offset ||
			    dest->offset >= func->offset + func->len) {
				/* local sibling call */
				dest_func = find_symbol_by_offset(dest->sec,
								  dest->offset);
				if (!dest_func)
					continue;

				if (recursion == 5) {
					WARN_FUNC("infinite recursion (objtool bug!)",
						  dest->sec, dest->offset);
					return -1;
				}

				return __dead_end_function(file, dest_func,
							   recursion + 1);
			}
		}

		if (insn->type == INSN_JUMP_DYNAMIC && list_empty(&insn->alts))
			/* sibling call */
			return 0;
	}

	return 1;
}

static int dead_end_function(struct objtool_file *file, struct symbol *func)
{
	return __dead_end_function(file, func, 0);
}

static void clear_insn_state(struct insn_state *state)
{
	int i;

	memset(state, 0, sizeof(*state));
	state->cfa.base = CFI_UNDEFINED;
	for (i = 0; i < CFI_NUM_REGS; i++)
		state->regs[i].base = CFI_UNDEFINED;
	state->drap_reg = CFI_UNDEFINED;
}

/*
 * Call the arch-specific instruction decoder for all the instructions and add
 * them to the global instruction list.
 */
static int decode_instructions(struct objtool_file *file)
{
	struct section *sec;
	struct symbol *func;
	unsigned long offset;
	struct instruction *insn;
	int ret;

	for_each_sec(file, sec) {

		if (!(sec->sh.sh_flags & SHF_EXECINSTR))
			continue;

		for (offset = 0; offset < sec->len; offset += insn->len) {
			insn = malloc(sizeof(*insn));
			if (!insn) {
				WARN("malloc failed");
				return -1;
			}
			memset(insn, 0, sizeof(*insn));
			INIT_LIST_HEAD(&insn->alts);
			clear_insn_state(&insn->state);

			insn->sec = sec;
			insn->offset = offset;

			ret = arch_decode_instruction(file->elf, sec, offset,
						      sec->len - offset,
						      &insn->len, &insn->type,
						      &insn->immediate,
						      &insn->stack_op);
			if (ret)
				return ret;

			if (!insn->type || insn->type > INSN_LAST) {
				WARN_FUNC("invalid instruction type %d",
					  insn->sec, insn->offset, insn->type);
				return -1;
			}

			hash_add(file->insn_hash, &insn->hash, insn->offset);
			list_add_tail(&insn->list, &file->insn_list);
		}

		list_for_each_entry(func, &sec->symbol_list, list) {
			if (func->type != STT_FUNC)
				continue;

			if (!find_insn(file, sec, func->offset)) {
				WARN("%s(): can't find starting instruction",
				     func->name);
				return -1;
			}

			func_for_each_insn(file, func, insn)
				if (!insn->func)
					insn->func = func;
		}
	}

	return 0;
}

/*
 * Find all uses of the unreachable() macro, which are code path dead ends.
 */
static int add_dead_ends(struct objtool_file *file)
{
	struct section *sec;
	struct rela *rela;
	struct instruction *insn;
	bool found;

	sec = find_section_by_name(file->elf, ".rela.discard.unreachable");
	if (!sec)
		return 0;

	list_for_each_entry(rela, &sec->rela_list, list) {
		if (rela->sym->type != STT_SECTION) {
			WARN("unexpected relocation symbol type in %s", sec->name);
			return -1;
		}
		insn = find_insn(file, rela->sym->sec, rela->addend);
		if (insn)
			insn = list_prev_entry(insn, list);
		else if (rela->addend == rela->sym->sec->len) {
			found = false;
			list_for_each_entry_reverse(insn, &file->insn_list, list) {
				if (insn->sec == rela->sym->sec) {
					found = true;
					break;
				}
			}

			if (!found) {
				WARN("can't find unreachable insn at %s+0x%x",
				     rela->sym->sec->name, rela->addend);
				return -1;
			}
		} else {
			WARN("can't find unreachable insn at %s+0x%x",
			     rela->sym->sec->name, rela->addend);
			return -1;
		}

		insn->dead_end = true;
	}

	return 0;
}

/*
 * Warnings shouldn't be reported for ignored functions.
 */
static void add_ignores(struct objtool_file *file)
{
	struct instruction *insn;
	struct section *sec;
	struct symbol *func;

	for_each_sec(file, sec) {
		list_for_each_entry(func, &sec->symbol_list, list) {
			if (func->type != STT_FUNC)
				continue;

			if (!ignore_func(file, func))
				continue;

			func_for_each_insn(file, func, insn)
				insn->ignore = true;
		}
	}
}

/*
 * Find the destination instructions for all jumps.
 */
static int add_jump_destinations(struct objtool_file *file)
{
	struct instruction *insn;
	struct rela *rela;
	struct section *dest_sec;
	unsigned long dest_off;

	for_each_insn(file, insn) {
		if (insn->type != INSN_JUMP_CONDITIONAL &&
		    insn->type != INSN_JUMP_UNCONDITIONAL)
			continue;

		if (insn->ignore)
			continue;

		rela = find_rela_by_dest_range(insn->sec, insn->offset,
					       insn->len);
		if (!rela) {
			dest_sec = insn->sec;
			dest_off = insn->offset + insn->len + insn->immediate;
		} else if (rela->sym->type == STT_SECTION) {
			dest_sec = rela->sym->sec;
			dest_off = rela->addend + 4;
		} else if (rela->sym->sec->idx) {
			dest_sec = rela->sym->sec;
			dest_off = rela->sym->sym.st_value + rela->addend + 4;
		} else {
			/* sibling call */
			insn->jump_dest = 0;
			continue;
		}

		insn->jump_dest = find_insn(file, dest_sec, dest_off);
		if (!insn->jump_dest) {

			/*
			 * This is a special case where an alt instruction
			 * jumps past the end of the section.  These are
			 * handled later in handle_group_alt().
			 */
			if (!strcmp(insn->sec->name, ".altinstr_replacement"))
				continue;

			WARN_FUNC("can't find jump dest instruction at %s+0x%lx",
				  insn->sec, insn->offset, dest_sec->name,
				  dest_off);
			return -1;
		}
	}

	return 0;
}

/*
 * Find the destination instructions for all calls.
 */
static int add_call_destinations(struct objtool_file *file)
{
	struct instruction *insn;
	unsigned long dest_off;
	struct rela *rela;

	for_each_insn(file, insn) {
		if (insn->type != INSN_CALL)
			continue;

		rela = find_rela_by_dest_range(insn->sec, insn->offset,
					       insn->len);
		if (!rela) {
			dest_off = insn->offset + insn->len + insn->immediate;
			insn->call_dest = find_symbol_by_offset(insn->sec,
								dest_off);
			if (!insn->call_dest) {
				WARN_FUNC("can't find call dest symbol at offset 0x%lx",
					  insn->sec, insn->offset, dest_off);
				return -1;
			}
		} else if (rela->sym->type == STT_SECTION) {
			insn->call_dest = find_symbol_by_offset(rela->sym->sec,
								rela->addend+4);
			if (!insn->call_dest ||
			    insn->call_dest->type != STT_FUNC) {
				WARN_FUNC("can't find call dest symbol at %s+0x%x",
					  insn->sec, insn->offset,
					  rela->sym->sec->name,
					  rela->addend + 4);
				return -1;
			}
		} else
			insn->call_dest = rela->sym;
	}

	return 0;
}

/*
 * The .alternatives section requires some extra special care, over and above
 * what other special sections require:
 *
 * 1. Because alternatives are patched in-place, we need to insert a fake jump
 *    instruction at the end so that validate_branch() skips all the original
 *    replaced instructions when validating the new instruction path.
 *
 * 2. An added wrinkle is that the new instruction length might be zero.  In
 *    that case the old instructions are replaced with noops.  We simulate that
 *    by creating a fake jump as the only new instruction.
 *
 * 3. In some cases, the alternative section includes an instruction which
 *    conditionally jumps to the _end_ of the entry.  We have to modify these
 *    jumps' destinations to point back to .text rather than the end of the
 *    entry in .altinstr_replacement.
 *
 * 4. It has been requested that we don't validate the !POPCNT feature path
 *    which is a "very very small percentage of machines".
 */
static int handle_group_alt(struct objtool_file *file,
			    struct special_alt *special_alt,
			    struct instruction *orig_insn,
			    struct instruction **new_insn)
{
	struct instruction *last_orig_insn, *last_new_insn, *insn, *fake_jump;
	unsigned long dest_off;

	last_orig_insn = NULL;
	insn = orig_insn;
	sec_for_each_insn_from(file, insn) {
		if (insn->offset >= special_alt->orig_off + special_alt->orig_len)
			break;

		if (special_alt->skip_orig)
			insn->type = INSN_NOP;

		insn->alt_group = true;
		last_orig_insn = insn;
	}

	if (!next_insn_same_sec(file, last_orig_insn)) {
		WARN("%s: don't know how to handle alternatives at end of section",
		     special_alt->orig_sec->name);
		return -1;
	}

	fake_jump = malloc(sizeof(*fake_jump));
	if (!fake_jump) {
		WARN("malloc failed");
		return -1;
	}
	memset(fake_jump, 0, sizeof(*fake_jump));
	INIT_LIST_HEAD(&fake_jump->alts);
	clear_insn_state(&fake_jump->state);

	fake_jump->sec = special_alt->new_sec;
	fake_jump->offset = -1;
	fake_jump->type = INSN_JUMP_UNCONDITIONAL;
	fake_jump->jump_dest = list_next_entry(last_orig_insn, list);
	fake_jump->ignore = true;

	if (!special_alt->new_len) {
		*new_insn = fake_jump;
		return 0;
	}

	last_new_insn = NULL;
	insn = *new_insn;
	sec_for_each_insn_from(file, insn) {
		if (insn->offset >= special_alt->new_off + special_alt->new_len)
			break;

		last_new_insn = insn;

		if (insn->type != INSN_JUMP_CONDITIONAL &&
		    insn->type != INSN_JUMP_UNCONDITIONAL)
			continue;

		if (!insn->immediate)
			continue;

		dest_off = insn->offset + insn->len + insn->immediate;
		if (dest_off == special_alt->new_off + special_alt->new_len)
			insn->jump_dest = fake_jump;

		if (!insn->jump_dest) {
			WARN_FUNC("can't find alternative jump destination",
				  insn->sec, insn->offset);
			return -1;
		}
	}

	if (!last_new_insn) {
		WARN_FUNC("can't find last new alternative instruction",
			  special_alt->new_sec, special_alt->new_off);
		return -1;
	}

	list_add(&fake_jump->list, &last_new_insn->list);

	return 0;
}

/*
 * A jump table entry can either convert a nop to a jump or a jump to a nop.
 * If the original instruction is a jump, make the alt entry an effective nop
 * by just skipping the original instruction.
 */
static int handle_jump_alt(struct objtool_file *file,
			   struct special_alt *special_alt,
			   struct instruction *orig_insn,
			   struct instruction **new_insn)
{
	if (orig_insn->type == INSN_NOP)
		return 0;

	if (orig_insn->type != INSN_JUMP_UNCONDITIONAL) {
		WARN_FUNC("unsupported instruction at jump label",
			  orig_insn->sec, orig_insn->offset);
		return -1;
	}

	*new_insn = list_next_entry(orig_insn, list);
	return 0;
}

/*
 * Read all the special sections which have alternate instructions which can be
 * patched in or redirected to at runtime.  Each instruction having alternate
 * instruction(s) has them added to its insn->alts list, which will be
 * traversed in validate_branch().
 */
static int add_special_section_alts(struct objtool_file *file)
{
	struct list_head special_alts;
	struct instruction *orig_insn, *new_insn;
	struct special_alt *special_alt, *tmp;
	struct alternative *alt;
	int ret;

	ret = special_get_alts(file->elf, &special_alts);
	if (ret)
		return ret;

	list_for_each_entry_safe(special_alt, tmp, &special_alts, list) {
		alt = malloc(sizeof(*alt));
		if (!alt) {
			WARN("malloc failed");
			ret = -1;
			goto out;
		}

		orig_insn = find_insn(file, special_alt->orig_sec,
				      special_alt->orig_off);
		if (!orig_insn) {
			WARN_FUNC("special: can't find orig instruction",
				  special_alt->orig_sec, special_alt->orig_off);
			ret = -1;
			goto out;
		}

		new_insn = NULL;
		if (!special_alt->group || special_alt->new_len) {
			new_insn = find_insn(file, special_alt->new_sec,
					     special_alt->new_off);
			if (!new_insn) {
				WARN_FUNC("special: can't find new instruction",
					  special_alt->new_sec,
					  special_alt->new_off);
				ret = -1;
				goto out;
			}
		}

		if (special_alt->group) {
			ret = handle_group_alt(file, special_alt, orig_insn,
					       &new_insn);
			if (ret)
				goto out;
		} else if (special_alt->jump_or_nop) {
			ret = handle_jump_alt(file, special_alt, orig_insn,
					      &new_insn);
			if (ret)
				goto out;
		}

		alt->insn = new_insn;
		list_add_tail(&alt->list, &orig_insn->alts);

		list_del(&special_alt->list);
		free(special_alt);
	}

out:
	return ret;
}

static int add_switch_table(struct objtool_file *file, struct symbol *func,
			    struct instruction *insn, struct rela *table,
			    struct rela *next_table)
{
	struct rela *rela = table;
	struct instruction *alt_insn;
	struct alternative *alt;

	list_for_each_entry_from(rela, &file->rodata->rela->rela_list, list) {
		if (rela == next_table)
			break;

		if (rela->sym->sec != insn->sec ||
		    rela->addend <= func->offset ||
		    rela->addend >= func->offset + func->len)
			break;

		alt_insn = find_insn(file, insn->sec, rela->addend);
		if (!alt_insn) {
			WARN("%s: can't find instruction at %s+0x%x",
			     file->rodata->rela->name, insn->sec->name,
			     rela->addend);
			return -1;
		}

		alt = malloc(sizeof(*alt));
		if (!alt) {
			WARN("malloc failed");
			return -1;
		}

		alt->insn = alt_insn;
		list_add_tail(&alt->list, &insn->alts);
	}

	return 0;
}

/*
 * find_switch_table() - Given a dynamic jump, find the switch jump table in
 * .rodata associated with it.
 *
 * There are 3 basic patterns:
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
 *    TODO: Once we have DWARF CFI and smarter instruction decoding logic,
 *    ensure the same register is used in the mov and jump instructions.
 */
static struct rela *find_switch_table(struct objtool_file *file,
				      struct symbol *func,
				      struct instruction *insn)
{
	struct rela *text_rela, *rodata_rela;
	struct instruction *orig_insn = insn;

	text_rela = find_rela_by_dest_range(insn->sec, insn->offset, insn->len);
	if (text_rela && text_rela->sym == file->rodata->sym) {
		/* case 1 */
		rodata_rela = find_rela_by_dest(file->rodata,
						text_rela->addend);
		if (rodata_rela)
			return rodata_rela;

		/* case 2 */
		rodata_rela = find_rela_by_dest(file->rodata,
						text_rela->addend + 4);
		if (!rodata_rela)
			return NULL;
		file->ignore_unreachables = true;
		return rodata_rela;
	}

	/* case 3 */
	func_for_each_insn_continue_reverse(file, func, insn) {
		if (insn->type == INSN_JUMP_DYNAMIC)
			break;

		/* allow small jumps within the range */
		if (insn->type == INSN_JUMP_UNCONDITIONAL &&
		    insn->jump_dest &&
		    (insn->jump_dest->offset <= insn->offset ||
		     insn->jump_dest->offset > orig_insn->offset))
		    break;

		/* look for a relocation which references .rodata */
		text_rela = find_rela_by_dest_range(insn->sec, insn->offset,
						    insn->len);
		if (!text_rela || text_rela->sym != file->rodata->sym)
			continue;

		/*
		 * Make sure the .rodata address isn't associated with a
		 * symbol.  gcc jump tables are anonymous data.
		 */
		if (find_symbol_containing(file->rodata, text_rela->addend))
			continue;

		return find_rela_by_dest(file->rodata, text_rela->addend);
	}

	return NULL;
}

static int add_func_switch_tables(struct objtool_file *file,
				  struct symbol *func)
{
	struct instruction *insn, *prev_jump = NULL;
	struct rela *rela, *prev_rela = NULL;
	int ret;

	func_for_each_insn(file, func, insn) {
		if (insn->type != INSN_JUMP_DYNAMIC)
			continue;

		rela = find_switch_table(file, func, insn);
		if (!rela)
			continue;

		/*
		 * We found a switch table, but we don't know yet how big it
		 * is.  Don't add it until we reach the end of the function or
		 * the beginning of another switch table in the same function.
		 */
		if (prev_jump) {
			ret = add_switch_table(file, func, prev_jump, prev_rela,
					       rela);
			if (ret)
				return ret;
		}

		prev_jump = insn;
		prev_rela = rela;
	}

	if (prev_jump) {
		ret = add_switch_table(file, func, prev_jump, prev_rela, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * For some switch statements, gcc generates a jump table in the .rodata
 * section which contains a list of addresses within the function to jump to.
 * This finds these jump tables and adds them to the insn->alts lists.
 */
static int add_switch_table_alts(struct objtool_file *file)
{
	struct section *sec;
	struct symbol *func;
	int ret;

	if (!file->rodata || !file->rodata->rela)
		return 0;

	for_each_sec(file, sec) {
		list_for_each_entry(func, &sec->symbol_list, list) {
			if (func->type != STT_FUNC)
				continue;

			ret = add_func_switch_tables(file, func);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int decode_sections(struct objtool_file *file)
{
	int ret;

	ret = decode_instructions(file);
	if (ret)
		return ret;

	ret = add_dead_ends(file);
	if (ret)
		return ret;

	add_ignores(file);

	ret = add_jump_destinations(file);
	if (ret)
		return ret;

	ret = add_call_destinations(file);
	if (ret)
		return ret;

	ret = add_special_section_alts(file);
	if (ret)
		return ret;

	ret = add_switch_table_alts(file);
	if (ret)
		return ret;

	return 0;
}

static bool is_fentry_call(struct instruction *insn)
{
	if (insn->type == INSN_CALL &&
	    insn->call_dest->type == STT_NOTYPE &&
	    !strcmp(insn->call_dest->name, "__fentry__"))
		return true;

	return false;
}

static bool has_modified_stack_frame(struct insn_state *state)
{
	int i;

	if (state->cfa.base != initial_func_cfi.cfa.base ||
	    state->cfa.offset != initial_func_cfi.cfa.offset ||
	    state->stack_size != initial_func_cfi.cfa.offset ||
	    state->drap)
		return true;

	for (i = 0; i < CFI_NUM_REGS; i++)
		if (state->regs[i].base != initial_func_cfi.regs[i].base ||
		    state->regs[i].offset != initial_func_cfi.regs[i].offset)
			return true;

	return false;
}

static bool has_valid_stack_frame(struct insn_state *state)
{
	if (state->cfa.base == CFI_BP && state->regs[CFI_BP].base == CFI_CFA &&
	    state->regs[CFI_BP].offset == -16)
		return true;

	if (state->drap && state->regs[CFI_BP].base == CFI_BP)
		return true;

	return false;
}

static void save_reg(struct insn_state *state, unsigned char reg, int base,
		     int offset)
{
	if ((arch_callee_saved_reg(reg) ||
	    (state->drap && reg == state->drap_reg)) &&
	    state->regs[reg].base == CFI_UNDEFINED) {
		state->regs[reg].base = base;
		state->regs[reg].offset = offset;
	}
}

static void restore_reg(struct insn_state *state, unsigned char reg)
{
	state->regs[reg].base = CFI_UNDEFINED;
	state->regs[reg].offset = 0;
}

/*
 * A note about DRAP stack alignment:
 *
 * GCC has the concept of a DRAP register, which is used to help keep track of
 * the stack pointer when aligning the stack.  r10 or r13 is used as the DRAP
 * register.  The typical DRAP pattern is:
 *
 *   4c 8d 54 24 08		lea    0x8(%rsp),%r10
 *   48 83 e4 c0		and    $0xffffffffffffffc0,%rsp
 *   41 ff 72 f8		pushq  -0x8(%r10)
 *   55				push   %rbp
 *   48 89 e5			mov    %rsp,%rbp
 *				(more pushes)
 *   41 52			push   %r10
 *				...
 *   41 5a			pop    %r10
 *				(more pops)
 *   5d				pop    %rbp
 *   49 8d 62 f8		lea    -0x8(%r10),%rsp
 *   c3				retq
 *
 * There are some variations in the epilogues, like:
 *
 *   5b				pop    %rbx
 *   41 5a			pop    %r10
 *   41 5c			pop    %r12
 *   41 5d			pop    %r13
 *   41 5e			pop    %r14
 *   c9				leaveq
 *   49 8d 62 f8		lea    -0x8(%r10),%rsp
 *   c3				retq
 *
 * and:
 *
 *   4c 8b 55 e8		mov    -0x18(%rbp),%r10
 *   48 8b 5d e0		mov    -0x20(%rbp),%rbx
 *   4c 8b 65 f0		mov    -0x10(%rbp),%r12
 *   4c 8b 6d f8		mov    -0x8(%rbp),%r13
 *   c9				leaveq
 *   49 8d 62 f8		lea    -0x8(%r10),%rsp
 *   c3				retq
 *
 * Sometimes r13 is used as the DRAP register, in which case it's saved and
 * restored beforehand:
 *
 *   41 55			push   %r13
 *   4c 8d 6c 24 10		lea    0x10(%rsp),%r13
 *   48 83 e4 f0		and    $0xfffffffffffffff0,%rsp
 *				...
 *   49 8d 65 f0		lea    -0x10(%r13),%rsp
 *   41 5d			pop    %r13
 *   c3				retq
 */
static int update_insn_state(struct instruction *insn, struct insn_state *state)
{
	struct stack_op *op = &insn->stack_op;
	struct cfi_reg *cfa = &state->cfa;
	struct cfi_reg *regs = state->regs;

	/* stack operations don't make sense with an undefined CFA */
	if (cfa->base == CFI_UNDEFINED) {
		if (insn->func) {
			WARN_FUNC("undefined stack state", insn->sec, insn->offset);
			return -1;
		}
		return 0;
	}

	switch (op->dest.type) {

	case OP_DEST_REG:
		switch (op->src.type) {

		case OP_SRC_REG:
			if (cfa->base == op->src.reg && cfa->base == CFI_SP &&
			    op->dest.reg == CFI_BP && regs[CFI_BP].base == CFI_CFA &&
			    regs[CFI_BP].offset == -cfa->offset) {

				/* mov %rsp, %rbp */
				cfa->base = op->dest.reg;
				state->bp_scratch = false;
			} else if (state->drap) {

				/* drap: mov %rsp, %rbp */
				regs[CFI_BP].base = CFI_BP;
				regs[CFI_BP].offset = -state->stack_size;
				state->bp_scratch = false;
			} else if (!nofp) {

				WARN_FUNC("unknown stack-related register move",
					  insn->sec, insn->offset);
				return -1;
			}

			break;

		case OP_SRC_ADD:
			if (op->dest.reg == CFI_SP && op->src.reg == CFI_SP) {

				/* add imm, %rsp */
				state->stack_size -= op->src.offset;
				if (cfa->base == CFI_SP)
					cfa->offset -= op->src.offset;
				break;
			}

			if (op->dest.reg == CFI_SP && op->src.reg == CFI_BP) {

				/* lea disp(%rbp), %rsp */
				state->stack_size = -(op->src.offset + regs[CFI_BP].offset);
				break;
			}

			if (op->dest.reg != CFI_BP && op->src.reg == CFI_SP &&
			    cfa->base == CFI_SP) {

				/* drap: lea disp(%rsp), %drap */
				state->drap_reg = op->dest.reg;
				break;
			}

			if (state->drap && op->dest.reg == CFI_SP &&
			    op->src.reg == state->drap_reg) {

				 /* drap: lea disp(%drap), %rsp */
				cfa->base = CFI_SP;
				cfa->offset = state->stack_size = -op->src.offset;
				state->drap_reg = CFI_UNDEFINED;
				state->drap = false;
				break;
			}

			if (op->dest.reg == state->cfa.base) {
				WARN_FUNC("unsupported stack register modification",
					  insn->sec, insn->offset);
				return -1;
			}

			break;

		case OP_SRC_AND:
			if (op->dest.reg != CFI_SP ||
			    (state->drap_reg != CFI_UNDEFINED && cfa->base != CFI_SP) ||
			    (state->drap_reg == CFI_UNDEFINED && cfa->base != CFI_BP)) {
				WARN_FUNC("unsupported stack pointer realignment",
					  insn->sec, insn->offset);
				return -1;
			}

			if (state->drap_reg != CFI_UNDEFINED) {
				/* drap: and imm, %rsp */
				cfa->base = state->drap_reg;
				cfa->offset = state->stack_size = 0;
				state->drap = true;

			}

			/*
			 * Older versions of GCC (4.8ish) realign the stack
			 * without DRAP, with a frame pointer.
			 */

			break;

		case OP_SRC_POP:
			if (!state->drap && op->dest.type == OP_DEST_REG &&
			    op->dest.reg == cfa->base) {

				/* pop %rbp */
				cfa->base = CFI_SP;
			}

			if (regs[op->dest.reg].offset == -state->stack_size) {

				if (state->drap && cfa->base == CFI_BP_INDIRECT &&
				    op->dest.type == OP_DEST_REG &&
				    op->dest.reg == state->drap_reg) {

					/* drap: pop %drap */
					cfa->base = state->drap_reg;
					cfa->offset = 0;
				}

				restore_reg(state, op->dest.reg);
			}

			state->stack_size -= 8;
			if (cfa->base == CFI_SP)
				cfa->offset -= 8;

			break;

		case OP_SRC_REG_INDIRECT:
			if (state->drap && op->src.reg == CFI_BP &&
			    op->src.offset == regs[op->dest.reg].offset) {

				/* drap: mov disp(%rbp), %reg */
				if (op->dest.reg == state->drap_reg) {
					cfa->base = state->drap_reg;
					cfa->offset = 0;
				}

				restore_reg(state, op->dest.reg);

			} else if (op->src.reg == cfa->base &&
			    op->src.offset == regs[op->dest.reg].offset + cfa->offset) {

				/* mov disp(%rbp), %reg */
				/* mov disp(%rsp), %reg */
				restore_reg(state, op->dest.reg);
			}

			break;

		default:
			WARN_FUNC("unknown stack-related instruction",
				  insn->sec, insn->offset);
			return -1;
		}

		break;

	case OP_DEST_PUSH:
		state->stack_size += 8;
		if (cfa->base == CFI_SP)
			cfa->offset += 8;

		if (op->src.type != OP_SRC_REG)
			break;

		if (state->drap) {
			if (op->src.reg == cfa->base && op->src.reg == state->drap_reg) {

				/* drap: push %drap */
				cfa->base = CFI_BP_INDIRECT;
				cfa->offset = -state->stack_size;

				/* save drap so we know when to undefine it */
				save_reg(state, op->src.reg, CFI_CFA, -state->stack_size);

			} else if (op->src.reg == CFI_BP && cfa->base == state->drap_reg) {

				/* drap: push %rbp */
				state->stack_size = 0;

			} else if (regs[op->src.reg].base == CFI_UNDEFINED) {

				/* drap: push %reg */
				save_reg(state, op->src.reg, CFI_BP, -state->stack_size);
			}

		} else {

			/* push %reg */
			save_reg(state, op->src.reg, CFI_CFA, -state->stack_size);
		}

		/* detect when asm code uses rbp as a scratch register */
		if (!nofp && insn->func && op->src.reg == CFI_BP &&
		    cfa->base != CFI_BP)
			state->bp_scratch = true;
		break;

	case OP_DEST_REG_INDIRECT:

		if (state->drap) {
			if (op->src.reg == cfa->base && op->src.reg == state->drap_reg) {

				/* drap: mov %drap, disp(%rbp) */
				cfa->base = CFI_BP_INDIRECT;
				cfa->offset = op->dest.offset;

				/* save drap so we know when to undefine it */
				save_reg(state, op->src.reg, CFI_CFA, op->dest.offset);
			}

			else if (regs[op->src.reg].base == CFI_UNDEFINED) {

				/* drap: mov reg, disp(%rbp) */
				save_reg(state, op->src.reg, CFI_BP, op->dest.offset);
			}

		} else if (op->dest.reg == cfa->base) {

			/* mov reg, disp(%rbp) */
			/* mov reg, disp(%rsp) */
			save_reg(state, op->src.reg, CFI_CFA,
				 op->dest.offset - state->cfa.offset);
		}

		break;

	case OP_DEST_LEAVE:
		if ((!state->drap && cfa->base != CFI_BP) ||
		    (state->drap && cfa->base != state->drap_reg)) {
			WARN_FUNC("leave instruction with modified stack frame",
				  insn->sec, insn->offset);
			return -1;
		}

		/* leave (mov %rbp, %rsp; pop %rbp) */

		state->stack_size = -state->regs[CFI_BP].offset - 8;
		restore_reg(state, CFI_BP);

		if (!state->drap) {
			cfa->base = CFI_SP;
			cfa->offset -= 8;
		}

		break;

	case OP_DEST_MEM:
		if (op->src.type != OP_SRC_POP) {
			WARN_FUNC("unknown stack-related memory operation",
				  insn->sec, insn->offset);
			return -1;
		}

		/* pop mem */
		state->stack_size -= 8;
		if (cfa->base == CFI_SP)
			cfa->offset -= 8;

		break;

	default:
		WARN_FUNC("unknown stack-related instruction",
			  insn->sec, insn->offset);
		return -1;
	}

	return 0;
}

static bool insn_state_match(struct instruction *insn, struct insn_state *state)
{
	struct insn_state *state1 = &insn->state, *state2 = state;
	int i;

	if (memcmp(&state1->cfa, &state2->cfa, sizeof(state1->cfa))) {
		WARN_FUNC("stack state mismatch: cfa1=%d%+d cfa2=%d%+d",
			  insn->sec, insn->offset,
			  state1->cfa.base, state1->cfa.offset,
			  state2->cfa.base, state2->cfa.offset);

	} else if (memcmp(&state1->regs, &state2->regs, sizeof(state1->regs))) {
		for (i = 0; i < CFI_NUM_REGS; i++) {
			if (!memcmp(&state1->regs[i], &state2->regs[i],
				    sizeof(struct cfi_reg)))
				continue;

			WARN_FUNC("stack state mismatch: reg1[%d]=%d%+d reg2[%d]=%d%+d",
				  insn->sec, insn->offset,
				  i, state1->regs[i].base, state1->regs[i].offset,
				  i, state2->regs[i].base, state2->regs[i].offset);
			break;
		}

	} else if (state1->drap != state2->drap ||
		 (state1->drap && state1->drap_reg != state2->drap_reg)) {
		WARN_FUNC("stack state mismatch: drap1=%d(%d) drap2=%d(%d)",
			  insn->sec, insn->offset,
			  state1->drap, state1->drap_reg,
			  state2->drap, state2->drap_reg);

	} else
		return true;

	return false;
}

/*
 * Follow the branch starting at the given instruction, and recursively follow
 * any other branches (jumps).  Meanwhile, track the frame pointer state at
 * each instruction and validate all the rules described in
 * tools/objtool/Documentation/stack-validation.txt.
 */
static int validate_branch(struct objtool_file *file, struct instruction *first,
			   struct insn_state state)
{
	struct alternative *alt;
	struct instruction *insn;
	struct section *sec;
	struct symbol *func = NULL;
	int ret;

	insn = first;
	sec = insn->sec;

	if (insn->alt_group && list_empty(&insn->alts)) {
		WARN_FUNC("don't know how to handle branch to middle of alternative instruction group",
			  sec, insn->offset);
		return -1;
	}

	while (1) {
		if (file->c_file && insn->func) {
			if (func && func != insn->func) {
				WARN("%s() falls through to next function %s()",
				     func->name, insn->func->name);
				return 1;
			}
		}

		func = insn->func;

		if (func && insn->ignore) {
			WARN_FUNC("BUG: why am I validating an ignored function?",
				  sec, insn->offset);
			return -1;
		}

		if (insn->visited) {
			if (!!insn_state_match(insn, &state))
				return 1;

			return 0;
		}

		insn->state = state;

		insn->visited = true;

		list_for_each_entry(alt, &insn->alts, list) {
			ret = validate_branch(file, alt->insn, state);
			if (ret)
				return 1;
		}

		switch (insn->type) {

		case INSN_RETURN:
			if (func && has_modified_stack_frame(&state)) {
				WARN_FUNC("return with modified stack frame",
					  sec, insn->offset);
				return 1;
			}

			if (state.bp_scratch) {
				WARN("%s uses BP as a scratch register",
				     insn->func->name);
				return 1;
			}

			return 0;

		case INSN_CALL:
			if (is_fentry_call(insn))
				break;

			ret = dead_end_function(file, insn->call_dest);
			if (ret == 1)
				return 0;
			if (ret == -1)
				return 1;

			/* fallthrough */
		case INSN_CALL_DYNAMIC:
			if (!nofp && func && !has_valid_stack_frame(&state)) {
				WARN_FUNC("call without frame pointer save/setup",
					  sec, insn->offset);
				return 1;
			}
			break;

		case INSN_JUMP_CONDITIONAL:
		case INSN_JUMP_UNCONDITIONAL:
			if (insn->jump_dest &&
			    (!func || !insn->jump_dest->func ||
			     func == insn->jump_dest->func)) {
				ret = validate_branch(file, insn->jump_dest,
						      state);
				if (ret)
					return 1;

			} else if (func && has_modified_stack_frame(&state)) {
				WARN_FUNC("sibling call from callable instruction with modified stack frame",
					  sec, insn->offset);
				return 1;
			}

			if (insn->type == INSN_JUMP_UNCONDITIONAL)
				return 0;

			break;

		case INSN_JUMP_DYNAMIC:
			if (func && list_empty(&insn->alts) &&
			    has_modified_stack_frame(&state)) {
				WARN_FUNC("sibling call from callable instruction with modified stack frame",
					  sec, insn->offset);
				return 1;
			}

			return 0;

		case INSN_STACK:
			if (update_insn_state(insn, &state))
				return -1;

			break;

		default:
			break;
		}

		if (insn->dead_end)
			return 0;

		insn = next_insn_same_sec(file, insn);
		if (!insn) {
			WARN("%s: unexpected end of section", sec->name);
			return 1;
		}
	}

	return 0;
}

static bool is_kasan_insn(struct instruction *insn)
{
	return (insn->type == INSN_CALL &&
		!strcmp(insn->call_dest->name, "__asan_handle_no_return"));
}

static bool is_ubsan_insn(struct instruction *insn)
{
	return (insn->type == INSN_CALL &&
		!strcmp(insn->call_dest->name,
			"__ubsan_handle_builtin_unreachable"));
}

static bool ignore_unreachable_insn(struct instruction *insn)
{
	int i;

	if (insn->ignore || insn->type == INSN_NOP)
		return true;

	/*
	 * Ignore any unused exceptions.  This can happen when a whitelisted
	 * function has an exception table entry.
	 */
	if (!strcmp(insn->sec->name, ".fixup"))
		return true;

	/*
	 * Check if this (or a subsequent) instruction is related to
	 * CONFIG_UBSAN or CONFIG_KASAN.
	 *
	 * End the search at 5 instructions to avoid going into the weeds.
	 */
	if (!insn->func)
		return false;
	for (i = 0; i < 5; i++) {

		if (is_kasan_insn(insn) || is_ubsan_insn(insn))
			return true;

		if (insn->type == INSN_JUMP_UNCONDITIONAL && insn->jump_dest) {
			insn = insn->jump_dest;
			continue;
		}

		if (insn->offset + insn->len >= insn->func->offset + insn->func->len)
			break;
		insn = list_next_entry(insn, list);
	}

	return false;
}

static int validate_functions(struct objtool_file *file)
{
	struct section *sec;
	struct symbol *func;
	struct instruction *insn;
	struct insn_state state;
	int ret, warnings = 0;

	clear_insn_state(&state);

	state.cfa = initial_func_cfi.cfa;
	memcpy(&state.regs, &initial_func_cfi.regs,
	       CFI_NUM_REGS * sizeof(struct cfi_reg));
	state.stack_size = initial_func_cfi.cfa.offset;

	for_each_sec(file, sec) {
		list_for_each_entry(func, &sec->symbol_list, list) {
			if (func->type != STT_FUNC)
				continue;

			insn = find_insn(file, sec, func->offset);
			if (!insn || insn->ignore)
				continue;

			ret = validate_branch(file, insn, state);
			warnings += ret;
		}
	}

	return warnings;
}

static int validate_reachable_instructions(struct objtool_file *file)
{
	struct instruction *insn;

	if (file->ignore_unreachables)
		return 0;

	for_each_insn(file, insn) {
		if (insn->visited || ignore_unreachable_insn(insn))
			continue;

		/*
		 * gcov produces a lot of unreachable instructions.  If we get
		 * an unreachable warning and the file has gcov enabled, just
		 * ignore it, and all other such warnings for the file.  Do
		 * this here because this is an expensive function.
		 */
		if (gcov_enabled(file))
			return 0;

		WARN_FUNC("unreachable instruction", insn->sec, insn->offset);
		return 1;
	}

	return 0;
}

static void cleanup(struct objtool_file *file)
{
	struct instruction *insn, *tmpinsn;
	struct alternative *alt, *tmpalt;

	list_for_each_entry_safe(insn, tmpinsn, &file->insn_list, list) {
		list_for_each_entry_safe(alt, tmpalt, &insn->alts, list) {
			list_del(&alt->list);
			free(alt);
		}
		list_del(&insn->list);
		hash_del(&insn->hash);
		free(insn);
	}
	elf_close(file->elf);
}

int check(const char *_objname, bool _nofp)
{
	struct objtool_file file;
	int ret, warnings = 0;

	objname = _objname;
	nofp = _nofp;

	file.elf = elf_open(objname);
	if (!file.elf)
		return 1;

	INIT_LIST_HEAD(&file.insn_list);
	hash_init(file.insn_hash);
	file.whitelist = find_section_by_name(file.elf, ".discard.func_stack_frame_non_standard");
	file.rodata = find_section_by_name(file.elf, ".rodata");
	file.ignore_unreachables = false;
	file.c_file = find_section_by_name(file.elf, ".comment");

	arch_initial_func_cfi_state(&initial_func_cfi);

	ret = decode_sections(&file);
	if (ret < 0)
		goto out;
	warnings += ret;

	if (list_empty(&file.insn_list))
		goto out;

	ret = validate_functions(&file);
	if (ret < 0)
		goto out;
	warnings += ret;

	if (!warnings) {
		ret = validate_reachable_instructions(&file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

out:
	cleanup(&file);

	/* ignore warnings for now until we get all the code cleaned up */
	if (ret || warnings)
		return 0;
	return 0;
}
