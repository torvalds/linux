// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>

#include <objtool/builtin.h>
#include <objtool/cfi.h>
#include <objtool/arch.h>
#include <objtool/check.h>
#include <objtool/special.h>
#include <objtool/warn.h>
#include <objtool/endianness.h>

#include <linux/objtool_types.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>
#include <linux/static_call_types.h>
#include <linux/string.h>

struct alternative {
	struct alternative *next;
	struct instruction *insn;
	bool skip_orig;
};

static unsigned long nr_cfi, nr_cfi_reused, nr_cfi_cache;

static struct cfi_init_state initial_func_cfi;
static struct cfi_state init_cfi;
static struct cfi_state func_cfi;
static struct cfi_state force_undefined_cfi;

struct instruction *find_insn(struct objtool_file *file,
			      struct section *sec, unsigned long offset)
{
	struct instruction *insn;

	hash_for_each_possible(file->insn_hash, insn, hash, sec_offset_hash(sec, offset)) {
		if (insn->sec == sec && insn->offset == offset)
			return insn;
	}

	return NULL;
}

struct instruction *next_insn_same_sec(struct objtool_file *file,
				       struct instruction *insn)
{
	if (insn->idx == INSN_CHUNK_MAX)
		return find_insn(file, insn->sec, insn->offset + insn->len);

	insn++;
	if (!insn->len)
		return NULL;

	return insn;
}

static struct instruction *next_insn_same_func(struct objtool_file *file,
					       struct instruction *insn)
{
	struct instruction *next = next_insn_same_sec(file, insn);
	struct symbol *func = insn_func(insn);

	if (!func)
		return NULL;

	if (next && insn_func(next) == func)
		return next;

	/* Check if we're already in the subfunction: */
	if (func == func->cfunc)
		return NULL;

	/* Move to the subfunction: */
	return find_insn(file, func->cfunc->sec, func->cfunc->offset);
}

static struct instruction *prev_insn_same_sec(struct objtool_file *file,
					      struct instruction *insn)
{
	if (insn->idx == 0) {
		if (insn->prev_len)
			return find_insn(file, insn->sec, insn->offset - insn->prev_len);
		return NULL;
	}

	return insn - 1;
}

static struct instruction *prev_insn_same_sym(struct objtool_file *file,
					      struct instruction *insn)
{
	struct instruction *prev = prev_insn_same_sec(file, insn);

	if (prev && insn_func(prev) == insn_func(insn))
		return prev;

	return NULL;
}

#define for_each_insn(file, insn)					\
	for (struct section *__sec, *__fake = (struct section *)1;	\
	     __fake; __fake = NULL)					\
		for_each_sec(file, __sec)				\
			sec_for_each_insn(file, __sec, insn)

#define func_for_each_insn(file, func, insn)				\
	for (insn = find_insn(file, func->sec, func->offset);		\
	     insn;							\
	     insn = next_insn_same_func(file, insn))

#define sym_for_each_insn(file, sym, insn)				\
	for (insn = find_insn(file, sym->sec, sym->offset);		\
	     insn && insn->offset < sym->offset + sym->len;		\
	     insn = next_insn_same_sec(file, insn))

#define sym_for_each_insn_continue_reverse(file, sym, insn)		\
	for (insn = prev_insn_same_sec(file, insn);			\
	     insn && insn->offset >= sym->offset;			\
	     insn = prev_insn_same_sec(file, insn))

#define sec_for_each_insn_from(file, insn)				\
	for (; insn; insn = next_insn_same_sec(file, insn))

#define sec_for_each_insn_continue(file, insn)				\
	for (insn = next_insn_same_sec(file, insn); insn;		\
	     insn = next_insn_same_sec(file, insn))

static inline struct symbol *insn_call_dest(struct instruction *insn)
{
	if (insn->type == INSN_JUMP_DYNAMIC ||
	    insn->type == INSN_CALL_DYNAMIC)
		return NULL;

	return insn->_call_dest;
}

static inline struct reloc *insn_jump_table(struct instruction *insn)
{
	if (insn->type == INSN_JUMP_DYNAMIC ||
	    insn->type == INSN_CALL_DYNAMIC)
		return insn->_jump_table;

	return NULL;
}

static inline unsigned long insn_jump_table_size(struct instruction *insn)
{
	if (insn->type == INSN_JUMP_DYNAMIC ||
	    insn->type == INSN_CALL_DYNAMIC)
		return insn->_jump_table_size;

	return 0;
}

static bool is_jump_table_jump(struct instruction *insn)
{
	struct alt_group *alt_group = insn->alt_group;

	if (insn_jump_table(insn))
		return true;

	/* Retpoline alternative for a jump table? */
	return alt_group && alt_group->orig_group &&
	       insn_jump_table(alt_group->orig_group->first_insn);
}

static bool is_sibling_call(struct instruction *insn)
{
	/*
	 * Assume only STT_FUNC calls have jump-tables.
	 */
	if (insn_func(insn)) {
		/* An indirect jump is either a sibling call or a jump to a table. */
		if (insn->type == INSN_JUMP_DYNAMIC)
			return !is_jump_table_jump(insn);
	}

	/* add_jump_destinations() sets insn_call_dest(insn) for sibling calls. */
	return (is_static_jump(insn) && insn_call_dest(insn));
}

/*
 * Checks if a string ends with another.
 */
static bool str_ends_with(const char *s, const char *sub)
{
	const int slen = strlen(s);
	const int sublen = strlen(sub);

	if (sublen > slen)
		return 0;

	return !memcmp(s + slen - sublen, sub, sublen);
}

/*
 * Checks if a function is a Rust "noreturn" one.
 */
static bool is_rust_noreturn(const struct symbol *func)
{
	/*
	 * If it does not start with "_R", then it is not a Rust symbol.
	 */
	if (strncmp(func->name, "_R", 2))
		return false;

	/*
	 * These are just heuristics -- we do not control the precise symbol
	 * name, due to the crate disambiguators (which depend on the compiler)
	 * as well as changes to the source code itself between versions (since
	 * these come from the Rust standard library).
	 */
	return str_ends_with(func->name, "_4core5sliceSp15copy_from_slice17len_mismatch_fail")		||
	       str_ends_with(func->name, "_4core6option13unwrap_failed")				||
	       str_ends_with(func->name, "_4core6result13unwrap_failed")				||
	       str_ends_with(func->name, "_4core9panicking5panic")					||
	       str_ends_with(func->name, "_4core9panicking9panic_fmt")					||
	       str_ends_with(func->name, "_4core9panicking14panic_explicit")				||
	       str_ends_with(func->name, "_4core9panicking14panic_nounwind")				||
	       str_ends_with(func->name, "_4core9panicking18panic_bounds_check")			||
	       str_ends_with(func->name, "_4core9panicking19assert_failed_inner")			||
	       str_ends_with(func->name, "_4core9panicking36panic_misaligned_pointer_dereference")	||
	       strstr(func->name, "_4core9panicking13assert_failed")					||
	       strstr(func->name, "_4core9panicking11panic_const24panic_const_")			||
	       (strstr(func->name, "_4core5slice5index24slice_") &&
		str_ends_with(func->name, "_fail"));
}

/*
 * This checks to see if the given function is a "noreturn" function.
 *
 * For global functions which are outside the scope of this object file, we
 * have to keep a manual list of them.
 *
 * For local functions, we have to detect them manually by simply looking for
 * the lack of a return instruction.
 */
static bool __dead_end_function(struct objtool_file *file, struct symbol *func,
				int recursion)
{
	int i;
	struct instruction *insn;
	bool empty = true;

#define NORETURN(func) __stringify(func),
	static const char * const global_noreturns[] = {
#include "noreturns.h"
	};
#undef NORETURN

	if (!func)
		return false;

	if (func->bind == STB_GLOBAL || func->bind == STB_WEAK) {
		if (is_rust_noreturn(func))
			return true;

		for (i = 0; i < ARRAY_SIZE(global_noreturns); i++)
			if (!strcmp(func->name, global_noreturns[i]))
				return true;
	}

	if (func->bind == STB_WEAK)
		return false;

	if (!func->len)
		return false;

	insn = find_insn(file, func->sec, func->offset);
	if (!insn || !insn_func(insn))
		return false;

	func_for_each_insn(file, func, insn) {
		empty = false;

		if (insn->type == INSN_RETURN)
			return false;
	}

	if (empty)
		return false;

	/*
	 * A function can have a sibling call instead of a return.  In that
	 * case, the function's dead-end status depends on whether the target
	 * of the sibling call returns.
	 */
	func_for_each_insn(file, func, insn) {
		if (is_sibling_call(insn)) {
			struct instruction *dest = insn->jump_dest;

			if (!dest)
				/* sibling call to another file */
				return false;

			/* local sibling call */
			if (recursion == 5) {
				/*
				 * Infinite recursion: two functions have
				 * sibling calls to each other.  This is a very
				 * rare case.  It means they aren't dead ends.
				 */
				return false;
			}

			return __dead_end_function(file, insn_func(dest), recursion+1);
		}
	}

	return true;
}

static bool dead_end_function(struct objtool_file *file, struct symbol *func)
{
	return __dead_end_function(file, func, 0);
}

static void init_cfi_state(struct cfi_state *cfi)
{
	int i;

	for (i = 0; i < CFI_NUM_REGS; i++) {
		cfi->regs[i].base = CFI_UNDEFINED;
		cfi->vals[i].base = CFI_UNDEFINED;
	}
	cfi->cfa.base = CFI_UNDEFINED;
	cfi->drap_reg = CFI_UNDEFINED;
	cfi->drap_offset = -1;
}

static void init_insn_state(struct objtool_file *file, struct insn_state *state,
			    struct section *sec)
{
	memset(state, 0, sizeof(*state));
	init_cfi_state(&state->cfi);

	/*
	 * We need the full vmlinux for noinstr validation, otherwise we can
	 * not correctly determine insn_call_dest(insn)->sec (external symbols
	 * do not have a section).
	 */
	if (opts.link && opts.noinstr && sec)
		state->noinstr = sec->noinstr;
}

static struct cfi_state *cfi_alloc(void)
{
	struct cfi_state *cfi = calloc(1, sizeof(struct cfi_state));
	if (!cfi) {
		WARN("calloc failed");
		exit(1);
	}
	nr_cfi++;
	return cfi;
}

static int cfi_bits;
static struct hlist_head *cfi_hash;

static inline bool cficmp(struct cfi_state *cfi1, struct cfi_state *cfi2)
{
	return memcmp((void *)cfi1 + sizeof(cfi1->hash),
		      (void *)cfi2 + sizeof(cfi2->hash),
		      sizeof(struct cfi_state) - sizeof(struct hlist_node));
}

static inline u32 cfi_key(struct cfi_state *cfi)
{
	return jhash((void *)cfi + sizeof(cfi->hash),
		     sizeof(*cfi) - sizeof(cfi->hash), 0);
}

static struct cfi_state *cfi_hash_find_or_add(struct cfi_state *cfi)
{
	struct hlist_head *head = &cfi_hash[hash_min(cfi_key(cfi), cfi_bits)];
	struct cfi_state *obj;

	hlist_for_each_entry(obj, head, hash) {
		if (!cficmp(cfi, obj)) {
			nr_cfi_cache++;
			return obj;
		}
	}

	obj = cfi_alloc();
	*obj = *cfi;
	hlist_add_head(&obj->hash, head);

	return obj;
}

static void cfi_hash_add(struct cfi_state *cfi)
{
	struct hlist_head *head = &cfi_hash[hash_min(cfi_key(cfi), cfi_bits)];

	hlist_add_head(&cfi->hash, head);
}

static void *cfi_hash_alloc(unsigned long size)
{
	cfi_bits = max(10, ilog2(size));
	cfi_hash = mmap(NULL, sizeof(struct hlist_head) << cfi_bits,
			PROT_READ|PROT_WRITE,
			MAP_PRIVATE|MAP_ANON, -1, 0);
	if (cfi_hash == (void *)-1L) {
		WARN("mmap fail cfi_hash");
		cfi_hash = NULL;
	}  else if (opts.stats) {
		printf("cfi_bits: %d\n", cfi_bits);
	}

	return cfi_hash;
}

static unsigned long nr_insns;
static unsigned long nr_insns_visited;

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
		struct instruction *insns = NULL;
		u8 prev_len = 0;
		u8 idx = 0;

		if (!(sec->sh.sh_flags & SHF_EXECINSTR))
			continue;

		if (strcmp(sec->name, ".altinstr_replacement") &&
		    strcmp(sec->name, ".altinstr_aux") &&
		    strncmp(sec->name, ".discard.", 9))
			sec->text = true;

		if (!strcmp(sec->name, ".noinstr.text") ||
		    !strcmp(sec->name, ".entry.text") ||
		    !strcmp(sec->name, ".cpuidle.text") ||
		    !strncmp(sec->name, ".text..__x86.", 13))
			sec->noinstr = true;

		/*
		 * .init.text code is ran before userspace and thus doesn't
		 * strictly need retpolines, except for modules which are
		 * loaded late, they very much do need retpoline in their
		 * .init.text
		 */
		if (!strcmp(sec->name, ".init.text") && !opts.module)
			sec->init = true;

		for (offset = 0; offset < sec->sh.sh_size; offset += insn->len) {
			if (!insns || idx == INSN_CHUNK_MAX) {
				insns = calloc(sizeof(*insn), INSN_CHUNK_SIZE);
				if (!insns) {
					WARN("malloc failed");
					return -1;
				}
				idx = 0;
			} else {
				idx++;
			}
			insn = &insns[idx];
			insn->idx = idx;

			INIT_LIST_HEAD(&insn->call_node);
			insn->sec = sec;
			insn->offset = offset;
			insn->prev_len = prev_len;

			ret = arch_decode_instruction(file, sec, offset,
						      sec->sh.sh_size - offset,
						      insn);
			if (ret)
				return ret;

			prev_len = insn->len;

			/*
			 * By default, "ud2" is a dead end unless otherwise
			 * annotated, because GCC 7 inserts it for certain
			 * divide-by-zero cases.
			 */
			if (insn->type == INSN_BUG)
				insn->dead_end = true;

			hash_add(file->insn_hash, &insn->hash, sec_offset_hash(sec, insn->offset));
			nr_insns++;
		}

//		printf("%s: last chunk used: %d\n", sec->name, (int)idx);

		sec_for_each_sym(sec, func) {
			if (func->type != STT_NOTYPE && func->type != STT_FUNC)
				continue;

			if (func->offset == sec->sh.sh_size) {
				/* Heuristic: likely an "end" symbol */
				if (func->type == STT_NOTYPE)
					continue;
				WARN("%s(): STT_FUNC at end of section",
				     func->name);
				return -1;
			}

			if (func->embedded_insn || func->alias != func)
				continue;

			if (!find_insn(file, sec, func->offset)) {
				WARN("%s(): can't find starting instruction",
				     func->name);
				return -1;
			}

			sym_for_each_insn(file, func, insn) {
				insn->sym = func;
				if (func->type == STT_FUNC &&
				    insn->type == INSN_ENDBR &&
				    list_empty(&insn->call_node)) {
					if (insn->offset == func->offset) {
						list_add_tail(&insn->call_node, &file->endbr_list);
						file->nr_endbr++;
					} else {
						file->nr_endbr_int++;
					}
				}
			}
		}
	}

	if (opts.stats)
		printf("nr_insns: %lu\n", nr_insns);

	return 0;
}

/*
 * Read the pv_ops[] .data table to find the static initialized values.
 */
static int add_pv_ops(struct objtool_file *file, const char *symname)
{
	struct symbol *sym, *func;
	unsigned long off, end;
	struct reloc *reloc;
	int idx;

	sym = find_symbol_by_name(file->elf, symname);
	if (!sym)
		return 0;

	off = sym->offset;
	end = off + sym->len;
	for (;;) {
		reloc = find_reloc_by_dest_range(file->elf, sym->sec, off, end - off);
		if (!reloc)
			break;

		func = reloc->sym;
		if (func->type == STT_SECTION)
			func = find_symbol_by_offset(reloc->sym->sec,
						     reloc_addend(reloc));

		idx = (reloc_offset(reloc) - sym->offset) / sizeof(unsigned long);

		objtool_pv_add(file, idx, func);

		off = reloc_offset(reloc) + 1;
		if (off > end)
			break;
	}

	return 0;
}

/*
 * Allocate and initialize file->pv_ops[].
 */
static int init_pv_ops(struct objtool_file *file)
{
	static const char *pv_ops_tables[] = {
		"pv_ops",
		"xen_cpu_ops",
		"xen_irq_ops",
		"xen_mmu_ops",
		NULL,
	};
	const char *pv_ops;
	struct symbol *sym;
	int idx, nr;

	if (!opts.noinstr)
		return 0;

	file->pv_ops = NULL;

	sym = find_symbol_by_name(file->elf, "pv_ops");
	if (!sym)
		return 0;

	nr = sym->len / sizeof(unsigned long);
	file->pv_ops = calloc(sizeof(struct pv_state), nr);
	if (!file->pv_ops)
		return -1;

	for (idx = 0; idx < nr; idx++)
		INIT_LIST_HEAD(&file->pv_ops[idx].targets);

	for (idx = 0; (pv_ops = pv_ops_tables[idx]); idx++)
		add_pv_ops(file, pv_ops);

	return 0;
}

static int create_static_call_sections(struct objtool_file *file)
{
	struct static_call_site *site;
	struct section *sec;
	struct instruction *insn;
	struct symbol *key_sym;
	char *key_name, *tmp;
	int idx;

	sec = find_section_by_name(file->elf, ".static_call_sites");
	if (sec) {
		INIT_LIST_HEAD(&file->static_call_list);
		WARN("file already has .static_call_sites section, skipping");
		return 0;
	}

	if (list_empty(&file->static_call_list))
		return 0;

	idx = 0;
	list_for_each_entry(insn, &file->static_call_list, call_node)
		idx++;

	sec = elf_create_section_pair(file->elf, ".static_call_sites",
				      sizeof(*site), idx, idx * 2);
	if (!sec)
		return -1;

	/* Allow modules to modify the low bits of static_call_site::key */
	sec->sh.sh_flags |= SHF_WRITE;

	idx = 0;
	list_for_each_entry(insn, &file->static_call_list, call_node) {

		/* populate reloc for 'addr' */
		if (!elf_init_reloc_text_sym(file->elf, sec,
					     idx * sizeof(*site), idx * 2,
					     insn->sec, insn->offset))
			return -1;

		/* find key symbol */
		key_name = strdup(insn_call_dest(insn)->name);
		if (!key_name) {
			perror("strdup");
			return -1;
		}
		if (strncmp(key_name, STATIC_CALL_TRAMP_PREFIX_STR,
			    STATIC_CALL_TRAMP_PREFIX_LEN)) {
			WARN("static_call: trampoline name malformed: %s", key_name);
			free(key_name);
			return -1;
		}
		tmp = key_name + STATIC_CALL_TRAMP_PREFIX_LEN - STATIC_CALL_KEY_PREFIX_LEN;
		memcpy(tmp, STATIC_CALL_KEY_PREFIX_STR, STATIC_CALL_KEY_PREFIX_LEN);

		key_sym = find_symbol_by_name(file->elf, tmp);
		if (!key_sym) {
			if (!opts.module) {
				WARN("static_call: can't find static_call_key symbol: %s", tmp);
				free(key_name);
				return -1;
			}

			/*
			 * For modules(), the key might not be exported, which
			 * means the module can make static calls but isn't
			 * allowed to change them.
			 *
			 * In that case we temporarily set the key to be the
			 * trampoline address.  This is fixed up in
			 * static_call_add_module().
			 */
			key_sym = insn_call_dest(insn);
		}
		free(key_name);

		/* populate reloc for 'key' */
		if (!elf_init_reloc_data_sym(file->elf, sec,
					     idx * sizeof(*site) + 4,
					     (idx * 2) + 1, key_sym,
					     is_sibling_call(insn) * STATIC_CALL_SITE_TAIL))
			return -1;

		idx++;
	}

	return 0;
}

static int create_retpoline_sites_sections(struct objtool_file *file)
{
	struct instruction *insn;
	struct section *sec;
	int idx;

	sec = find_section_by_name(file->elf, ".retpoline_sites");
	if (sec) {
		WARN("file already has .retpoline_sites, skipping");
		return 0;
	}

	idx = 0;
	list_for_each_entry(insn, &file->retpoline_call_list, call_node)
		idx++;

	if (!idx)
		return 0;

	sec = elf_create_section_pair(file->elf, ".retpoline_sites",
				      sizeof(int), idx, idx);
	if (!sec)
		return -1;

	idx = 0;
	list_for_each_entry(insn, &file->retpoline_call_list, call_node) {

		if (!elf_init_reloc_text_sym(file->elf, sec,
					     idx * sizeof(int), idx,
					     insn->sec, insn->offset))
			return -1;

		idx++;
	}

	return 0;
}

static int create_return_sites_sections(struct objtool_file *file)
{
	struct instruction *insn;
	struct section *sec;
	int idx;

	sec = find_section_by_name(file->elf, ".return_sites");
	if (sec) {
		WARN("file already has .return_sites, skipping");
		return 0;
	}

	idx = 0;
	list_for_each_entry(insn, &file->return_thunk_list, call_node)
		idx++;

	if (!idx)
		return 0;

	sec = elf_create_section_pair(file->elf, ".return_sites",
				      sizeof(int), idx, idx);
	if (!sec)
		return -1;

	idx = 0;
	list_for_each_entry(insn, &file->return_thunk_list, call_node) {

		if (!elf_init_reloc_text_sym(file->elf, sec,
					     idx * sizeof(int), idx,
					     insn->sec, insn->offset))
			return -1;

		idx++;
	}

	return 0;
}

static int create_ibt_endbr_seal_sections(struct objtool_file *file)
{
	struct instruction *insn;
	struct section *sec;
	int idx;

	sec = find_section_by_name(file->elf, ".ibt_endbr_seal");
	if (sec) {
		WARN("file already has .ibt_endbr_seal, skipping");
		return 0;
	}

	idx = 0;
	list_for_each_entry(insn, &file->endbr_list, call_node)
		idx++;

	if (opts.stats) {
		printf("ibt: ENDBR at function start: %d\n", file->nr_endbr);
		printf("ibt: ENDBR inside functions:  %d\n", file->nr_endbr_int);
		printf("ibt: superfluous ENDBR:       %d\n", idx);
	}

	if (!idx)
		return 0;

	sec = elf_create_section_pair(file->elf, ".ibt_endbr_seal",
				      sizeof(int), idx, idx);
	if (!sec)
		return -1;

	idx = 0;
	list_for_each_entry(insn, &file->endbr_list, call_node) {

		int *site = (int *)sec->data->d_buf + idx;
		struct symbol *sym = insn->sym;
		*site = 0;

		if (opts.module && sym && sym->type == STT_FUNC &&
		    insn->offset == sym->offset &&
		    (!strcmp(sym->name, "init_module") ||
		     !strcmp(sym->name, "cleanup_module")))
			WARN("%s(): not an indirect call target", sym->name);

		if (!elf_init_reloc_text_sym(file->elf, sec,
					     idx * sizeof(int), idx,
					     insn->sec, insn->offset))
			return -1;

		idx++;
	}

	return 0;
}

static int create_cfi_sections(struct objtool_file *file)
{
	struct section *sec;
	struct symbol *sym;
	int idx;

	sec = find_section_by_name(file->elf, ".cfi_sites");
	if (sec) {
		INIT_LIST_HEAD(&file->call_list);
		WARN("file already has .cfi_sites section, skipping");
		return 0;
	}

	idx = 0;
	for_each_sym(file, sym) {
		if (sym->type != STT_FUNC)
			continue;

		if (strncmp(sym->name, "__cfi_", 6))
			continue;

		idx++;
	}

	sec = elf_create_section_pair(file->elf, ".cfi_sites",
				      sizeof(unsigned int), idx, idx);
	if (!sec)
		return -1;

	idx = 0;
	for_each_sym(file, sym) {
		if (sym->type != STT_FUNC)
			continue;

		if (strncmp(sym->name, "__cfi_", 6))
			continue;

		if (!elf_init_reloc_text_sym(file->elf, sec,
					     idx * sizeof(unsigned int), idx,
					     sym->sec, sym->offset))
			return -1;

		idx++;
	}

	return 0;
}

static int create_mcount_loc_sections(struct objtool_file *file)
{
	size_t addr_size = elf_addr_size(file->elf);
	struct instruction *insn;
	struct section *sec;
	int idx;

	sec = find_section_by_name(file->elf, "__mcount_loc");
	if (sec) {
		INIT_LIST_HEAD(&file->mcount_loc_list);
		WARN("file already has __mcount_loc section, skipping");
		return 0;
	}

	if (list_empty(&file->mcount_loc_list))
		return 0;

	idx = 0;
	list_for_each_entry(insn, &file->mcount_loc_list, call_node)
		idx++;

	sec = elf_create_section_pair(file->elf, "__mcount_loc", addr_size,
				      idx, idx);
	if (!sec)
		return -1;

	sec->sh.sh_addralign = addr_size;

	idx = 0;
	list_for_each_entry(insn, &file->mcount_loc_list, call_node) {

		struct reloc *reloc;

		reloc = elf_init_reloc_text_sym(file->elf, sec, idx * addr_size, idx,
					       insn->sec, insn->offset);
		if (!reloc)
			return -1;

		set_reloc_type(file->elf, reloc, addr_size == 8 ? R_ABS64 : R_ABS32);

		idx++;
	}

	return 0;
}

static int create_direct_call_sections(struct objtool_file *file)
{
	struct instruction *insn;
	struct section *sec;
	int idx;

	sec = find_section_by_name(file->elf, ".call_sites");
	if (sec) {
		INIT_LIST_HEAD(&file->call_list);
		WARN("file already has .call_sites section, skipping");
		return 0;
	}

	if (list_empty(&file->call_list))
		return 0;

	idx = 0;
	list_for_each_entry(insn, &file->call_list, call_node)
		idx++;

	sec = elf_create_section_pair(file->elf, ".call_sites",
				      sizeof(unsigned int), idx, idx);
	if (!sec)
		return -1;

	idx = 0;
	list_for_each_entry(insn, &file->call_list, call_node) {

		if (!elf_init_reloc_text_sym(file->elf, sec,
					     idx * sizeof(unsigned int), idx,
					     insn->sec, insn->offset))
			return -1;

		idx++;
	}

	return 0;
}

/*
 * Warnings shouldn't be reported for ignored functions.
 */
static void add_ignores(struct objtool_file *file)
{
	struct instruction *insn;
	struct section *rsec;
	struct symbol *func;
	struct reloc *reloc;

	rsec = find_section_by_name(file->elf, ".rela.discard.func_stack_frame_non_standard");
	if (!rsec)
		return;

	for_each_reloc(rsec, reloc) {
		switch (reloc->sym->type) {
		case STT_FUNC:
			func = reloc->sym;
			break;

		case STT_SECTION:
			func = find_func_by_offset(reloc->sym->sec, reloc_addend(reloc));
			if (!func)
				continue;
			break;

		default:
			WARN("unexpected relocation symbol type in %s: %d",
			     rsec->name, reloc->sym->type);
			continue;
		}

		func_for_each_insn(file, func, insn)
			insn->ignore = true;
	}
}

/*
 * This is a whitelist of functions that is allowed to be called with AC set.
 * The list is meant to be minimal and only contains compiler instrumentation
 * ABI and a few functions used to implement *_{to,from}_user() functions.
 *
 * These functions must not directly change AC, but may PUSHF/POPF.
 */
static const char *uaccess_safe_builtin[] = {
	/* KASAN */
	"kasan_report",
	"kasan_check_range",
	/* KASAN out-of-line */
	"__asan_loadN_noabort",
	"__asan_load1_noabort",
	"__asan_load2_noabort",
	"__asan_load4_noabort",
	"__asan_load8_noabort",
	"__asan_load16_noabort",
	"__asan_storeN_noabort",
	"__asan_store1_noabort",
	"__asan_store2_noabort",
	"__asan_store4_noabort",
	"__asan_store8_noabort",
	"__asan_store16_noabort",
	"__kasan_check_read",
	"__kasan_check_write",
	/* KASAN in-line */
	"__asan_report_load_n_noabort",
	"__asan_report_load1_noabort",
	"__asan_report_load2_noabort",
	"__asan_report_load4_noabort",
	"__asan_report_load8_noabort",
	"__asan_report_load16_noabort",
	"__asan_report_store_n_noabort",
	"__asan_report_store1_noabort",
	"__asan_report_store2_noabort",
	"__asan_report_store4_noabort",
	"__asan_report_store8_noabort",
	"__asan_report_store16_noabort",
	/* KCSAN */
	"__kcsan_check_access",
	"__kcsan_mb",
	"__kcsan_wmb",
	"__kcsan_rmb",
	"__kcsan_release",
	"kcsan_found_watchpoint",
	"kcsan_setup_watchpoint",
	"kcsan_check_scoped_accesses",
	"kcsan_disable_current",
	"kcsan_enable_current_nowarn",
	/* KCSAN/TSAN */
	"__tsan_func_entry",
	"__tsan_func_exit",
	"__tsan_read_range",
	"__tsan_write_range",
	"__tsan_read1",
	"__tsan_read2",
	"__tsan_read4",
	"__tsan_read8",
	"__tsan_read16",
	"__tsan_write1",
	"__tsan_write2",
	"__tsan_write4",
	"__tsan_write8",
	"__tsan_write16",
	"__tsan_read_write1",
	"__tsan_read_write2",
	"__tsan_read_write4",
	"__tsan_read_write8",
	"__tsan_read_write16",
	"__tsan_volatile_read1",
	"__tsan_volatile_read2",
	"__tsan_volatile_read4",
	"__tsan_volatile_read8",
	"__tsan_volatile_read16",
	"__tsan_volatile_write1",
	"__tsan_volatile_write2",
	"__tsan_volatile_write4",
	"__tsan_volatile_write8",
	"__tsan_volatile_write16",
	"__tsan_atomic8_load",
	"__tsan_atomic16_load",
	"__tsan_atomic32_load",
	"__tsan_atomic64_load",
	"__tsan_atomic8_store",
	"__tsan_atomic16_store",
	"__tsan_atomic32_store",
	"__tsan_atomic64_store",
	"__tsan_atomic8_exchange",
	"__tsan_atomic16_exchange",
	"__tsan_atomic32_exchange",
	"__tsan_atomic64_exchange",
	"__tsan_atomic8_fetch_add",
	"__tsan_atomic16_fetch_add",
	"__tsan_atomic32_fetch_add",
	"__tsan_atomic64_fetch_add",
	"__tsan_atomic8_fetch_sub",
	"__tsan_atomic16_fetch_sub",
	"__tsan_atomic32_fetch_sub",
	"__tsan_atomic64_fetch_sub",
	"__tsan_atomic8_fetch_and",
	"__tsan_atomic16_fetch_and",
	"__tsan_atomic32_fetch_and",
	"__tsan_atomic64_fetch_and",
	"__tsan_atomic8_fetch_or",
	"__tsan_atomic16_fetch_or",
	"__tsan_atomic32_fetch_or",
	"__tsan_atomic64_fetch_or",
	"__tsan_atomic8_fetch_xor",
	"__tsan_atomic16_fetch_xor",
	"__tsan_atomic32_fetch_xor",
	"__tsan_atomic64_fetch_xor",
	"__tsan_atomic8_fetch_nand",
	"__tsan_atomic16_fetch_nand",
	"__tsan_atomic32_fetch_nand",
	"__tsan_atomic64_fetch_nand",
	"__tsan_atomic8_compare_exchange_strong",
	"__tsan_atomic16_compare_exchange_strong",
	"__tsan_atomic32_compare_exchange_strong",
	"__tsan_atomic64_compare_exchange_strong",
	"__tsan_atomic8_compare_exchange_weak",
	"__tsan_atomic16_compare_exchange_weak",
	"__tsan_atomic32_compare_exchange_weak",
	"__tsan_atomic64_compare_exchange_weak",
	"__tsan_atomic8_compare_exchange_val",
	"__tsan_atomic16_compare_exchange_val",
	"__tsan_atomic32_compare_exchange_val",
	"__tsan_atomic64_compare_exchange_val",
	"__tsan_atomic_thread_fence",
	"__tsan_atomic_signal_fence",
	"__tsan_unaligned_read16",
	"__tsan_unaligned_write16",
	/* KCOV */
	"write_comp_data",
	"check_kcov_mode",
	"__sanitizer_cov_trace_pc",
	"__sanitizer_cov_trace_const_cmp1",
	"__sanitizer_cov_trace_const_cmp2",
	"__sanitizer_cov_trace_const_cmp4",
	"__sanitizer_cov_trace_const_cmp8",
	"__sanitizer_cov_trace_cmp1",
	"__sanitizer_cov_trace_cmp2",
	"__sanitizer_cov_trace_cmp4",
	"__sanitizer_cov_trace_cmp8",
	"__sanitizer_cov_trace_switch",
	/* KMSAN */
	"kmsan_copy_to_user",
	"kmsan_disable_current",
	"kmsan_enable_current",
	"kmsan_report",
	"kmsan_unpoison_entry_regs",
	"kmsan_unpoison_memory",
	"__msan_chain_origin",
	"__msan_get_context_state",
	"__msan_instrument_asm_store",
	"__msan_metadata_ptr_for_load_1",
	"__msan_metadata_ptr_for_load_2",
	"__msan_metadata_ptr_for_load_4",
	"__msan_metadata_ptr_for_load_8",
	"__msan_metadata_ptr_for_load_n",
	"__msan_metadata_ptr_for_store_1",
	"__msan_metadata_ptr_for_store_2",
	"__msan_metadata_ptr_for_store_4",
	"__msan_metadata_ptr_for_store_8",
	"__msan_metadata_ptr_for_store_n",
	"__msan_poison_alloca",
	"__msan_warning",
	/* UBSAN */
	"ubsan_type_mismatch_common",
	"__ubsan_handle_type_mismatch",
	"__ubsan_handle_type_mismatch_v1",
	"__ubsan_handle_shift_out_of_bounds",
	"__ubsan_handle_load_invalid_value",
	/* STACKLEAK */
	"stackleak_track_stack",
	/* misc */
	"csum_partial_copy_generic",
	"copy_mc_fragile",
	"copy_mc_fragile_handle_tail",
	"copy_mc_enhanced_fast_string",
	"ftrace_likely_update", /* CONFIG_TRACE_BRANCH_PROFILING */
	"rep_stos_alternative",
	"rep_movs_alternative",
	"__copy_user_nocache",
	NULL
};

static void add_uaccess_safe(struct objtool_file *file)
{
	struct symbol *func;
	const char **name;

	if (!opts.uaccess)
		return;

	for (name = uaccess_safe_builtin; *name; name++) {
		func = find_symbol_by_name(file->elf, *name);
		if (!func)
			continue;

		func->uaccess_safe = true;
	}
}

/*
 * Symbols that replace INSN_CALL_DYNAMIC, every (tail) call to such a symbol
 * will be added to the .retpoline_sites section.
 */
__weak bool arch_is_retpoline(struct symbol *sym)
{
	return false;
}

/*
 * Symbols that replace INSN_RETURN, every (tail) call to such a symbol
 * will be added to the .return_sites section.
 */
__weak bool arch_is_rethunk(struct symbol *sym)
{
	return false;
}

/*
 * Symbols that are embedded inside other instructions, because sometimes crazy
 * code exists. These are mostly ignored for validation purposes.
 */
__weak bool arch_is_embedded_insn(struct symbol *sym)
{
	return false;
}

static struct reloc *insn_reloc(struct objtool_file *file, struct instruction *insn)
{
	struct reloc *reloc;

	if (insn->no_reloc)
		return NULL;

	if (!file)
		return NULL;

	reloc = find_reloc_by_dest_range(file->elf, insn->sec,
					 insn->offset, insn->len);
	if (!reloc) {
		insn->no_reloc = 1;
		return NULL;
	}

	return reloc;
}

static void remove_insn_ops(struct instruction *insn)
{
	struct stack_op *op, *next;

	for (op = insn->stack_ops; op; op = next) {
		next = op->next;
		free(op);
	}
	insn->stack_ops = NULL;
}

static void annotate_call_site(struct objtool_file *file,
			       struct instruction *insn, bool sibling)
{
	struct reloc *reloc = insn_reloc(file, insn);
	struct symbol *sym = insn_call_dest(insn);

	if (!sym)
		sym = reloc->sym;

	if (sym->static_call_tramp) {
		list_add_tail(&insn->call_node, &file->static_call_list);
		return;
	}

	if (sym->retpoline_thunk) {
		list_add_tail(&insn->call_node, &file->retpoline_call_list);
		return;
	}

	/*
	 * Many compilers cannot disable KCOV or sanitizer calls with a function
	 * attribute so they need a little help, NOP out any such calls from
	 * noinstr text.
	 */
	if (opts.hack_noinstr && insn->sec->noinstr && sym->profiling_func) {
		if (reloc)
			set_reloc_type(file->elf, reloc, R_NONE);

		elf_write_insn(file->elf, insn->sec,
			       insn->offset, insn->len,
			       sibling ? arch_ret_insn(insn->len)
			               : arch_nop_insn(insn->len));

		insn->type = sibling ? INSN_RETURN : INSN_NOP;

		if (sibling) {
			/*
			 * We've replaced the tail-call JMP insn by two new
			 * insn: RET; INT3, except we only have a single struct
			 * insn here. Mark it retpoline_safe to avoid the SLS
			 * warning, instead of adding another insn.
			 */
			insn->retpoline_safe = true;
		}

		return;
	}

	if (opts.mcount && sym->fentry) {
		if (sibling)
			WARN_INSN(insn, "tail call to __fentry__ !?!?");
		if (opts.mnop) {
			if (reloc)
				set_reloc_type(file->elf, reloc, R_NONE);

			elf_write_insn(file->elf, insn->sec,
				       insn->offset, insn->len,
				       arch_nop_insn(insn->len));

			insn->type = INSN_NOP;
		}

		list_add_tail(&insn->call_node, &file->mcount_loc_list);
		return;
	}

	if (insn->type == INSN_CALL && !insn->sec->init &&
	    !insn->_call_dest->embedded_insn)
		list_add_tail(&insn->call_node, &file->call_list);

	if (!sibling && dead_end_function(file, sym))
		insn->dead_end = true;
}

static void add_call_dest(struct objtool_file *file, struct instruction *insn,
			  struct symbol *dest, bool sibling)
{
	insn->_call_dest = dest;
	if (!dest)
		return;

	/*
	 * Whatever stack impact regular CALLs have, should be undone
	 * by the RETURN of the called function.
	 *
	 * Annotated intra-function calls retain the stack_ops but
	 * are converted to JUMP, see read_intra_function_calls().
	 */
	remove_insn_ops(insn);

	annotate_call_site(file, insn, sibling);
}

static void add_retpoline_call(struct objtool_file *file, struct instruction *insn)
{
	/*
	 * Retpoline calls/jumps are really dynamic calls/jumps in disguise,
	 * so convert them accordingly.
	 */
	switch (insn->type) {
	case INSN_CALL:
		insn->type = INSN_CALL_DYNAMIC;
		break;
	case INSN_JUMP_UNCONDITIONAL:
		insn->type = INSN_JUMP_DYNAMIC;
		break;
	case INSN_JUMP_CONDITIONAL:
		insn->type = INSN_JUMP_DYNAMIC_CONDITIONAL;
		break;
	default:
		return;
	}

	insn->retpoline_safe = true;

	/*
	 * Whatever stack impact regular CALLs have, should be undone
	 * by the RETURN of the called function.
	 *
	 * Annotated intra-function calls retain the stack_ops but
	 * are converted to JUMP, see read_intra_function_calls().
	 */
	remove_insn_ops(insn);

	annotate_call_site(file, insn, false);
}

static void add_return_call(struct objtool_file *file, struct instruction *insn, bool add)
{
	/*
	 * Return thunk tail calls are really just returns in disguise,
	 * so convert them accordingly.
	 */
	insn->type = INSN_RETURN;
	insn->retpoline_safe = true;

	if (add)
		list_add_tail(&insn->call_node, &file->return_thunk_list);
}

static bool is_first_func_insn(struct objtool_file *file,
			       struct instruction *insn, struct symbol *sym)
{
	if (insn->offset == sym->offset)
		return true;

	/* Allow direct CALL/JMP past ENDBR */
	if (opts.ibt) {
		struct instruction *prev = prev_insn_same_sym(file, insn);

		if (prev && prev->type == INSN_ENDBR &&
		    insn->offset == sym->offset + prev->len)
			return true;
	}

	return false;
}

/*
 * A sibling call is a tail-call to another symbol -- to differentiate from a
 * recursive tail-call which is to the same symbol.
 */
static bool jump_is_sibling_call(struct objtool_file *file,
				 struct instruction *from, struct instruction *to)
{
	struct symbol *fs = from->sym;
	struct symbol *ts = to->sym;

	/* Not a sibling call if from/to a symbol hole */
	if (!fs || !ts)
		return false;

	/* Not a sibling call if not targeting the start of a symbol. */
	if (!is_first_func_insn(file, to, ts))
		return false;

	/* Disallow sibling calls into STT_NOTYPE */
	if (ts->type == STT_NOTYPE)
		return false;

	/* Must not be self to be a sibling */
	return fs->pfunc != ts->pfunc;
}

/*
 * Find the destination instructions for all jumps.
 */
static int add_jump_destinations(struct objtool_file *file)
{
	struct instruction *insn, *jump_dest;
	struct reloc *reloc;
	struct section *dest_sec;
	unsigned long dest_off;

	for_each_insn(file, insn) {
		if (insn->jump_dest) {
			/*
			 * handle_group_alt() may have previously set
			 * 'jump_dest' for some alternatives.
			 */
			continue;
		}
		if (!is_static_jump(insn))
			continue;

		reloc = insn_reloc(file, insn);
		if (!reloc) {
			dest_sec = insn->sec;
			dest_off = arch_jump_destination(insn);
		} else if (reloc->sym->type == STT_SECTION) {
			dest_sec = reloc->sym->sec;
			dest_off = arch_dest_reloc_offset(reloc_addend(reloc));
		} else if (reloc->sym->retpoline_thunk) {
			add_retpoline_call(file, insn);
			continue;
		} else if (reloc->sym->return_thunk) {
			add_return_call(file, insn, true);
			continue;
		} else if (insn_func(insn)) {
			/*
			 * External sibling call or internal sibling call with
			 * STT_FUNC reloc.
			 */
			add_call_dest(file, insn, reloc->sym, true);
			continue;
		} else if (reloc->sym->sec->idx) {
			dest_sec = reloc->sym->sec;
			dest_off = reloc->sym->sym.st_value +
				   arch_dest_reloc_offset(reloc_addend(reloc));
		} else {
			/* non-func asm code jumping to another file */
			continue;
		}

		jump_dest = find_insn(file, dest_sec, dest_off);
		if (!jump_dest) {
			struct symbol *sym = find_symbol_by_offset(dest_sec, dest_off);

			/*
			 * This is a special case for retbleed_untrain_ret().
			 * It jumps to __x86_return_thunk(), but objtool
			 * can't find the thunk's starting RET
			 * instruction, because the RET is also in the
			 * middle of another instruction.  Objtool only
			 * knows about the outer instruction.
			 */
			if (sym && sym->embedded_insn) {
				add_return_call(file, insn, false);
				continue;
			}

			WARN_INSN(insn, "can't find jump dest instruction at %s+0x%lx",
				  dest_sec->name, dest_off);
			return -1;
		}

		/*
		 * An intra-TU jump in retpoline.o might not have a relocation
		 * for its jump dest, in which case the above
		 * add_{retpoline,return}_call() didn't happen.
		 */
		if (jump_dest->sym && jump_dest->offset == jump_dest->sym->offset) {
			if (jump_dest->sym->retpoline_thunk) {
				add_retpoline_call(file, insn);
				continue;
			}
			if (jump_dest->sym->return_thunk) {
				add_return_call(file, insn, true);
				continue;
			}
		}

		/*
		 * Cross-function jump.
		 */
		if (insn_func(insn) && insn_func(jump_dest) &&
		    insn_func(insn) != insn_func(jump_dest)) {

			/*
			 * For GCC 8+, create parent/child links for any cold
			 * subfunctions.  This is _mostly_ redundant with a
			 * similar initialization in read_symbols().
			 *
			 * If a function has aliases, we want the *first* such
			 * function in the symbol table to be the subfunction's
			 * parent.  In that case we overwrite the
			 * initialization done in read_symbols().
			 *
			 * However this code can't completely replace the
			 * read_symbols() code because this doesn't detect the
			 * case where the parent function's only reference to a
			 * subfunction is through a jump table.
			 */
			if (!strstr(insn_func(insn)->name, ".cold") &&
			    strstr(insn_func(jump_dest)->name, ".cold")) {
				insn_func(insn)->cfunc = insn_func(jump_dest);
				insn_func(jump_dest)->pfunc = insn_func(insn);
			}
		}

		if (jump_is_sibling_call(file, insn, jump_dest)) {
			/*
			 * Internal sibling call without reloc or with
			 * STT_SECTION reloc.
			 */
			add_call_dest(file, insn, insn_func(jump_dest), true);
			continue;
		}

		insn->jump_dest = jump_dest;
	}

	return 0;
}

static struct symbol *find_call_destination(struct section *sec, unsigned long offset)
{
	struct symbol *call_dest;

	call_dest = find_func_by_offset(sec, offset);
	if (!call_dest)
		call_dest = find_symbol_by_offset(sec, offset);

	return call_dest;
}

/*
 * Find the destination instructions for all calls.
 */
static int add_call_destinations(struct objtool_file *file)
{
	struct instruction *insn;
	unsigned long dest_off;
	struct symbol *dest;
	struct reloc *reloc;

	for_each_insn(file, insn) {
		if (insn->type != INSN_CALL)
			continue;

		reloc = insn_reloc(file, insn);
		if (!reloc) {
			dest_off = arch_jump_destination(insn);
			dest = find_call_destination(insn->sec, dest_off);

			add_call_dest(file, insn, dest, false);

			if (insn->ignore)
				continue;

			if (!insn_call_dest(insn)) {
				WARN_INSN(insn, "unannotated intra-function call");
				return -1;
			}

			if (insn_func(insn) && insn_call_dest(insn)->type != STT_FUNC) {
				WARN_INSN(insn, "unsupported call to non-function");
				return -1;
			}

		} else if (reloc->sym->type == STT_SECTION) {
			dest_off = arch_dest_reloc_offset(reloc_addend(reloc));
			dest = find_call_destination(reloc->sym->sec, dest_off);
			if (!dest) {
				WARN_INSN(insn, "can't find call dest symbol at %s+0x%lx",
					  reloc->sym->sec->name, dest_off);
				return -1;
			}

			add_call_dest(file, insn, dest, false);

		} else if (reloc->sym->retpoline_thunk) {
			add_retpoline_call(file, insn);

		} else
			add_call_dest(file, insn, reloc->sym, false);
	}

	return 0;
}

/*
 * The .alternatives section requires some extra special care over and above
 * other special sections because alternatives are patched in place.
 */
static int handle_group_alt(struct objtool_file *file,
			    struct special_alt *special_alt,
			    struct instruction *orig_insn,
			    struct instruction **new_insn)
{
	struct instruction *last_new_insn = NULL, *insn, *nop = NULL;
	struct alt_group *orig_alt_group, *new_alt_group;
	unsigned long dest_off;

	orig_alt_group = orig_insn->alt_group;
	if (!orig_alt_group) {
		struct instruction *last_orig_insn = NULL;

		orig_alt_group = malloc(sizeof(*orig_alt_group));
		if (!orig_alt_group) {
			WARN("malloc failed");
			return -1;
		}
		orig_alt_group->cfi = calloc(special_alt->orig_len,
					     sizeof(struct cfi_state *));
		if (!orig_alt_group->cfi) {
			WARN("calloc failed");
			return -1;
		}

		insn = orig_insn;
		sec_for_each_insn_from(file, insn) {
			if (insn->offset >= special_alt->orig_off + special_alt->orig_len)
				break;

			insn->alt_group = orig_alt_group;
			last_orig_insn = insn;
		}
		orig_alt_group->orig_group = NULL;
		orig_alt_group->first_insn = orig_insn;
		orig_alt_group->last_insn = last_orig_insn;
		orig_alt_group->nop = NULL;
	} else {
		if (orig_alt_group->last_insn->offset + orig_alt_group->last_insn->len -
		    orig_alt_group->first_insn->offset != special_alt->orig_len) {
			WARN_INSN(orig_insn, "weirdly overlapping alternative! %ld != %d",
				  orig_alt_group->last_insn->offset +
				  orig_alt_group->last_insn->len -
				  orig_alt_group->first_insn->offset,
				  special_alt->orig_len);
			return -1;
		}
	}

	new_alt_group = malloc(sizeof(*new_alt_group));
	if (!new_alt_group) {
		WARN("malloc failed");
		return -1;
	}

	if (special_alt->new_len < special_alt->orig_len) {
		/*
		 * Insert a fake nop at the end to make the replacement
		 * alt_group the same size as the original.  This is needed to
		 * allow propagate_alt_cfi() to do its magic.  When the last
		 * instruction affects the stack, the instruction after it (the
		 * nop) will propagate the new state to the shared CFI array.
		 */
		nop = malloc(sizeof(*nop));
		if (!nop) {
			WARN("malloc failed");
			return -1;
		}
		memset(nop, 0, sizeof(*nop));

		nop->sec = special_alt->new_sec;
		nop->offset = special_alt->new_off + special_alt->new_len;
		nop->len = special_alt->orig_len - special_alt->new_len;
		nop->type = INSN_NOP;
		nop->sym = orig_insn->sym;
		nop->alt_group = new_alt_group;
		nop->ignore = orig_insn->ignore_alts;
	}

	if (!special_alt->new_len) {
		*new_insn = nop;
		goto end;
	}

	insn = *new_insn;
	sec_for_each_insn_from(file, insn) {
		struct reloc *alt_reloc;

		if (insn->offset >= special_alt->new_off + special_alt->new_len)
			break;

		last_new_insn = insn;

		insn->ignore = orig_insn->ignore_alts;
		insn->sym = orig_insn->sym;
		insn->alt_group = new_alt_group;

		/*
		 * Since alternative replacement code is copy/pasted by the
		 * kernel after applying relocations, generally such code can't
		 * have relative-address relocation references to outside the
		 * .altinstr_replacement section, unless the arch's
		 * alternatives code can adjust the relative offsets
		 * accordingly.
		 */
		alt_reloc = insn_reloc(file, insn);
		if (alt_reloc && arch_pc_relative_reloc(alt_reloc) &&
		    !arch_support_alt_relocation(special_alt, insn, alt_reloc)) {

			WARN_INSN(insn, "unsupported relocation in alternatives section");
			return -1;
		}

		if (!is_static_jump(insn))
			continue;

		if (!insn->immediate)
			continue;

		dest_off = arch_jump_destination(insn);
		if (dest_off == special_alt->new_off + special_alt->new_len) {
			insn->jump_dest = next_insn_same_sec(file, orig_alt_group->last_insn);
			if (!insn->jump_dest) {
				WARN_INSN(insn, "can't find alternative jump destination");
				return -1;
			}
		}
	}

	if (!last_new_insn) {
		WARN_FUNC("can't find last new alternative instruction",
			  special_alt->new_sec, special_alt->new_off);
		return -1;
	}

end:
	new_alt_group->orig_group = orig_alt_group;
	new_alt_group->first_insn = *new_insn;
	new_alt_group->last_insn = last_new_insn;
	new_alt_group->nop = nop;
	new_alt_group->cfi = orig_alt_group->cfi;
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
	if (orig_insn->type != INSN_JUMP_UNCONDITIONAL &&
	    orig_insn->type != INSN_NOP) {

		WARN_INSN(orig_insn, "unsupported instruction at jump label");
		return -1;
	}

	if (opts.hack_jump_label && special_alt->key_addend & 2) {
		struct reloc *reloc = insn_reloc(file, orig_insn);

		if (reloc)
			set_reloc_type(file->elf, reloc, R_NONE);
		elf_write_insn(file->elf, orig_insn->sec,
			       orig_insn->offset, orig_insn->len,
			       arch_nop_insn(orig_insn->len));
		orig_insn->type = INSN_NOP;
	}

	if (orig_insn->type == INSN_NOP) {
		if (orig_insn->len == 2)
			file->jl_nop_short++;
		else
			file->jl_nop_long++;

		return 0;
	}

	if (orig_insn->len == 2)
		file->jl_short++;
	else
		file->jl_long++;

	*new_insn = next_insn_same_sec(file, orig_insn);
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
			if (!special_alt->orig_len) {
				WARN_INSN(orig_insn, "empty alternative entry");
				continue;
			}

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

		alt = malloc(sizeof(*alt));
		if (!alt) {
			WARN("malloc failed");
			ret = -1;
			goto out;
		}

		alt->insn = new_insn;
		alt->skip_orig = special_alt->skip_orig;
		orig_insn->ignore_alts |= special_alt->skip_alt;
		alt->next = orig_insn->alts;
		orig_insn->alts = alt;

		list_del(&special_alt->list);
		free(special_alt);
	}

	if (opts.stats) {
		printf("jl\\\tNOP\tJMP\n");
		printf("short:\t%ld\t%ld\n", file->jl_nop_short, file->jl_short);
		printf("long:\t%ld\t%ld\n", file->jl_nop_long, file->jl_long);
	}

out:
	return ret;
}

__weak unsigned long arch_jump_table_sym_offset(struct reloc *reloc, struct reloc *table)
{
	return reloc->sym->offset + reloc_addend(reloc);
}

static int add_jump_table(struct objtool_file *file, struct instruction *insn,
			  struct reloc *next_table)
{
	unsigned long table_size = insn_jump_table_size(insn);
	struct symbol *pfunc = insn_func(insn)->pfunc;
	struct reloc *table = insn_jump_table(insn);
	struct instruction *dest_insn;
	unsigned int prev_offset = 0;
	struct reloc *reloc = table;
	struct alternative *alt;
	unsigned long sym_offset;

	/*
	 * Each @reloc is a switch table relocation which points to the target
	 * instruction.
	 */
	for_each_reloc_from(table->sec, reloc) {

		/* Check for the end of the table: */
		if (table_size && reloc_offset(reloc) - reloc_offset(table) >= table_size)
			break;
		if (reloc != table && reloc == next_table)
			break;

		/* Make sure the table entries are consecutive: */
		if (prev_offset && reloc_offset(reloc) != prev_offset + arch_reloc_size(reloc))
			break;

		sym_offset = arch_jump_table_sym_offset(reloc, table);

		/* Detect function pointers from contiguous objects: */
		if (reloc->sym->sec == pfunc->sec && sym_offset == pfunc->offset)
			break;

		/*
		 * Clang sometimes leaves dangling unused jump table entries
		 * which point to the end of the function.  Ignore them.
		 */
		if (reloc->sym->sec == pfunc->sec &&
		    sym_offset == pfunc->offset + pfunc->len)
			goto next;

		dest_insn = find_insn(file, reloc->sym->sec, sym_offset);
		if (!dest_insn)
			break;

		/* Make sure the destination is in the same function: */
		if (!insn_func(dest_insn) || insn_func(dest_insn)->pfunc != pfunc)
			break;

		alt = malloc(sizeof(*alt));
		if (!alt) {
			WARN("malloc failed");
			return -1;
		}

		alt->insn = dest_insn;
		alt->next = insn->alts;
		insn->alts = alt;
next:
		prev_offset = reloc_offset(reloc);
	}

	if (!prev_offset) {
		WARN_INSN(insn, "can't find switch jump table");
		return -1;
	}

	return 0;
}

/*
 * find_jump_table() - Given a dynamic jump, find the switch jump table
 * associated with it.
 */
static void find_jump_table(struct objtool_file *file, struct symbol *func,
			    struct instruction *insn)
{
	struct reloc *table_reloc;
	struct instruction *dest_insn, *orig_insn = insn;
	unsigned long table_size;
	unsigned long sym_offset;

	/*
	 * Backward search using the @first_jump_src links, these help avoid
	 * much of the 'in between' code. Which avoids us getting confused by
	 * it.
	 */
	for (;
	     insn && insn_func(insn) && insn_func(insn)->pfunc == func;
	     insn = insn->first_jump_src ?: prev_insn_same_sym(file, insn)) {

		if (insn != orig_insn && insn->type == INSN_JUMP_DYNAMIC)
			break;

		/* allow small jumps within the range */
		if (insn->type == INSN_JUMP_UNCONDITIONAL &&
		    insn->jump_dest &&
		    (insn->jump_dest->offset <= insn->offset ||
		     insn->jump_dest->offset > orig_insn->offset))
		    break;

		table_reloc = arch_find_switch_table(file, insn, &table_size);
		if (!table_reloc)
			continue;

		sym_offset = table_reloc->sym->offset + reloc_addend(table_reloc);

		dest_insn = find_insn(file, table_reloc->sym->sec, sym_offset);
		if (!dest_insn || !insn_func(dest_insn) || insn_func(dest_insn)->pfunc != func)
			continue;

		orig_insn->_jump_table = table_reloc;
		orig_insn->_jump_table_size = table_size;
		break;
	}
}

/*
 * First pass: Mark the head of each jump table so that in the next pass,
 * we know when a given jump table ends and the next one starts.
 */
static void mark_func_jump_tables(struct objtool_file *file,
				    struct symbol *func)
{
	struct instruction *insn, *last = NULL;

	func_for_each_insn(file, func, insn) {
		if (!last)
			last = insn;

		/*
		 * Store back-pointers for unconditional forward jumps such
		 * that find_jump_table() can back-track using those and
		 * avoid some potentially confusing code.
		 */
		if (insn->type == INSN_JUMP_UNCONDITIONAL && insn->jump_dest &&
		    insn->offset > last->offset &&
		    insn->jump_dest->offset > insn->offset &&
		    !insn->jump_dest->first_jump_src) {

			insn->jump_dest->first_jump_src = insn;
			last = insn->jump_dest;
		}

		if (insn->type != INSN_JUMP_DYNAMIC)
			continue;

		find_jump_table(file, func, insn);
	}
}

static int add_func_jump_tables(struct objtool_file *file,
				  struct symbol *func)
{
	struct instruction *insn, *insn_t1 = NULL, *insn_t2;
	int ret = 0;

	func_for_each_insn(file, func, insn) {
		if (!insn_jump_table(insn))
			continue;

		if (!insn_t1) {
			insn_t1 = insn;
			continue;
		}

		insn_t2 = insn;

		ret = add_jump_table(file, insn_t1, insn_jump_table(insn_t2));
		if (ret)
			return ret;

		insn_t1 = insn_t2;
	}

	if (insn_t1)
		ret = add_jump_table(file, insn_t1, NULL);

	return ret;
}

/*
 * For some switch statements, gcc generates a jump table in the .rodata
 * section which contains a list of addresses within the function to jump to.
 * This finds these jump tables and adds them to the insn->alts lists.
 */
static int add_jump_table_alts(struct objtool_file *file)
{
	struct symbol *func;
	int ret;

	if (!file->rodata)
		return 0;

	for_each_sym(file, func) {
		if (func->type != STT_FUNC)
			continue;

		mark_func_jump_tables(file, func);
		ret = add_func_jump_tables(file, func);
		if (ret)
			return ret;
	}

	return 0;
}

static void set_func_state(struct cfi_state *state)
{
	state->cfa = initial_func_cfi.cfa;
	memcpy(&state->regs, &initial_func_cfi.regs,
	       CFI_NUM_REGS * sizeof(struct cfi_reg));
	state->stack_size = initial_func_cfi.cfa.offset;
	state->type = UNWIND_HINT_TYPE_CALL;
}

static int read_unwind_hints(struct objtool_file *file)
{
	struct cfi_state cfi = init_cfi;
	struct section *sec;
	struct unwind_hint *hint;
	struct instruction *insn;
	struct reloc *reloc;
	unsigned long offset;
	int i;

	sec = find_section_by_name(file->elf, ".discard.unwind_hints");
	if (!sec)
		return 0;

	if (!sec->rsec) {
		WARN("missing .rela.discard.unwind_hints section");
		return -1;
	}

	if (sec->sh.sh_size % sizeof(struct unwind_hint)) {
		WARN("struct unwind_hint size mismatch");
		return -1;
	}

	file->hints = true;

	for (i = 0; i < sec->sh.sh_size / sizeof(struct unwind_hint); i++) {
		hint = (struct unwind_hint *)sec->data->d_buf + i;

		reloc = find_reloc_by_dest(file->elf, sec, i * sizeof(*hint));
		if (!reloc) {
			WARN("can't find reloc for unwind_hints[%d]", i);
			return -1;
		}

		if (reloc->sym->type == STT_SECTION) {
			offset = reloc_addend(reloc);
		} else if (reloc->sym->local_label) {
			offset = reloc->sym->offset;
		} else {
			WARN("unexpected relocation symbol type in %s", sec->rsec->name);
			return -1;
		}

		insn = find_insn(file, reloc->sym->sec, offset);
		if (!insn) {
			WARN("can't find insn for unwind_hints[%d]", i);
			return -1;
		}

		insn->hint = true;

		if (hint->type == UNWIND_HINT_TYPE_UNDEFINED) {
			insn->cfi = &force_undefined_cfi;
			continue;
		}

		if (hint->type == UNWIND_HINT_TYPE_SAVE) {
			insn->hint = false;
			insn->save = true;
			continue;
		}

		if (hint->type == UNWIND_HINT_TYPE_RESTORE) {
			insn->restore = true;
			continue;
		}

		if (hint->type == UNWIND_HINT_TYPE_REGS_PARTIAL) {
			struct symbol *sym = find_symbol_by_offset(insn->sec, insn->offset);

			if (sym && sym->bind == STB_GLOBAL) {
				if (opts.ibt && insn->type != INSN_ENDBR && !insn->noendbr) {
					WARN_INSN(insn, "UNWIND_HINT_IRET_REGS without ENDBR");
				}
			}
		}

		if (hint->type == UNWIND_HINT_TYPE_FUNC) {
			insn->cfi = &func_cfi;
			continue;
		}

		if (insn->cfi)
			cfi = *(insn->cfi);

		if (arch_decode_hint_reg(hint->sp_reg, &cfi.cfa.base)) {
			WARN_INSN(insn, "unsupported unwind_hint sp base reg %d", hint->sp_reg);
			return -1;
		}

		cfi.cfa.offset = bswap_if_needed(file->elf, hint->sp_offset);
		cfi.type = hint->type;
		cfi.signal = hint->signal;

		insn->cfi = cfi_hash_find_or_add(&cfi);
	}

	return 0;
}

static int read_annotate(struct objtool_file *file,
			 int (*func)(struct objtool_file *file, int type, struct instruction *insn))
{
	struct section *sec;
	struct instruction *insn;
	struct reloc *reloc;
	uint64_t offset;
	int type, ret;

	sec = find_section_by_name(file->elf, ".discard.annotate_insn");
	if (!sec)
		return 0;

	if (!sec->rsec)
		return 0;

	if (sec->sh.sh_entsize != 8) {
		static bool warned = false;
		if (!warned && opts.verbose) {
			WARN("%s: dodgy linker, sh_entsize != 8", sec->name);
			warned = true;
		}
		sec->sh.sh_entsize = 8;
	}

	for_each_reloc(sec->rsec, reloc) {
		type = *(u32 *)(sec->data->d_buf + (reloc_idx(reloc) * sec->sh.sh_entsize) + 4);

		offset = reloc->sym->offset + reloc_addend(reloc);
		insn = find_insn(file, reloc->sym->sec, offset);

		if (!insn) {
			WARN("bad .discard.annotate_insn entry: %d of type %d", reloc_idx(reloc), type);
			return -1;
		}

		ret = func(file, type, insn);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int __annotate_early(struct objtool_file *file, int type, struct instruction *insn)
{
	switch (type) {
	case ANNOTYPE_IGNORE_ALTS:
		insn->ignore_alts = true;
		break;

	/*
	 * Must be before read_unwind_hints() since that needs insn->noendbr.
	 */
	case ANNOTYPE_NOENDBR:
		insn->noendbr = 1;
		break;

	default:
		break;
	}

	return 0;
}

static int __annotate_ifc(struct objtool_file *file, int type, struct instruction *insn)
{
	unsigned long dest_off;

	if (type != ANNOTYPE_INTRA_FUNCTION_CALL)
		return 0;

	if (insn->type != INSN_CALL) {
		WARN_INSN(insn, "intra_function_call not a direct call");
		return -1;
	}

	/*
	 * Treat intra-function CALLs as JMPs, but with a stack_op.
	 * See add_call_destinations(), which strips stack_ops from
	 * normal CALLs.
	 */
	insn->type = INSN_JUMP_UNCONDITIONAL;

	dest_off = arch_jump_destination(insn);
	insn->jump_dest = find_insn(file, insn->sec, dest_off);
	if (!insn->jump_dest) {
		WARN_INSN(insn, "can't find call dest at %s+0x%lx",
			  insn->sec->name, dest_off);
		return -1;
	}

	return 0;
}

static int __annotate_late(struct objtool_file *file, int type, struct instruction *insn)
{
	switch (type) {
	case ANNOTYPE_NOENDBR:
		/* early */
		break;

	case ANNOTYPE_RETPOLINE_SAFE:
		if (insn->type != INSN_JUMP_DYNAMIC &&
		    insn->type != INSN_CALL_DYNAMIC &&
		    insn->type != INSN_RETURN &&
		    insn->type != INSN_NOP) {
			WARN_INSN(insn, "retpoline_safe hint not an indirect jump/call/ret/nop");
			return -1;
		}

		insn->retpoline_safe = true;
		break;

	case ANNOTYPE_INSTR_BEGIN:
		insn->instr++;
		break;

	case ANNOTYPE_INSTR_END:
		insn->instr--;
		break;

	case ANNOTYPE_UNRET_BEGIN:
		insn->unret = 1;
		break;

	case ANNOTYPE_IGNORE_ALTS:
		/* early */
		break;

	case ANNOTYPE_INTRA_FUNCTION_CALL:
		/* ifc */
		break;

	case ANNOTYPE_REACHABLE:
		insn->dead_end = false;
		break;

	default:
		WARN_INSN(insn, "Unknown annotation type: %d", type);
		break;
	}

	return 0;
}

/*
 * Return true if name matches an instrumentation function, where calls to that
 * function from noinstr code can safely be removed, but compilers won't do so.
 */
static bool is_profiling_func(const char *name)
{
	/*
	 * Many compilers cannot disable KCOV with a function attribute.
	 */
	if (!strncmp(name, "__sanitizer_cov_", 16))
		return true;

	/*
	 * Some compilers currently do not remove __tsan_func_entry/exit nor
	 * __tsan_atomic_signal_fence (used for barrier instrumentation) with
	 * the __no_sanitize_thread attribute, remove them. Once the kernel's
	 * minimum Clang version is 14.0, this can be removed.
	 */
	if (!strncmp(name, "__tsan_func_", 12) ||
	    !strcmp(name, "__tsan_atomic_signal_fence"))
		return true;

	return false;
}

static int classify_symbols(struct objtool_file *file)
{
	struct symbol *func;

	for_each_sym(file, func) {
		if (func->type == STT_NOTYPE && strstarts(func->name, ".L"))
			func->local_label = true;

		if (func->bind != STB_GLOBAL)
			continue;

		if (!strncmp(func->name, STATIC_CALL_TRAMP_PREFIX_STR,
			     strlen(STATIC_CALL_TRAMP_PREFIX_STR)))
			func->static_call_tramp = true;

		if (arch_is_retpoline(func))
			func->retpoline_thunk = true;

		if (arch_is_rethunk(func))
			func->return_thunk = true;

		if (arch_is_embedded_insn(func))
			func->embedded_insn = true;

		if (arch_ftrace_match(func->name))
			func->fentry = true;

		if (is_profiling_func(func->name))
			func->profiling_func = true;
	}

	return 0;
}

static void mark_rodata(struct objtool_file *file)
{
	struct section *sec;
	bool found = false;

	/*
	 * Search for the following rodata sections, each of which can
	 * potentially contain jump tables:
	 *
	 * - .rodata: can contain GCC switch tables
	 * - .rodata.<func>: same, if -fdata-sections is being used
	 * - .data.rel.ro.c_jump_table: contains C annotated jump tables
	 *
	 * .rodata.str1.* sections are ignored; they don't contain jump tables.
	 */
	for_each_sec(file, sec) {
		if ((!strncmp(sec->name, ".rodata", 7) &&
		     !strstr(sec->name, ".str1.")) ||
		    !strncmp(sec->name, ".data.rel.ro", 12)) {
			sec->rodata = true;
			found = true;
		}
	}

	file->rodata = found;
}

static int decode_sections(struct objtool_file *file)
{
	int ret;

	mark_rodata(file);

	ret = init_pv_ops(file);
	if (ret)
		return ret;

	/*
	 * Must be before add_{jump_call}_destination.
	 */
	ret = classify_symbols(file);
	if (ret)
		return ret;

	ret = decode_instructions(file);
	if (ret)
		return ret;

	add_ignores(file);
	add_uaccess_safe(file);

	ret = read_annotate(file, __annotate_early);
	if (ret)
		return ret;

	/*
	 * Must be before add_jump_destinations(), which depends on 'func'
	 * being set for alternatives, to enable proper sibling call detection.
	 */
	if (opts.stackval || opts.orc || opts.uaccess || opts.noinstr) {
		ret = add_special_section_alts(file);
		if (ret)
			return ret;
	}

	ret = add_jump_destinations(file);
	if (ret)
		return ret;

	/*
	 * Must be before add_call_destination(); it changes INSN_CALL to
	 * INSN_JUMP.
	 */
	ret = read_annotate(file, __annotate_ifc);
	if (ret)
		return ret;

	ret = add_call_destinations(file);
	if (ret)
		return ret;

	ret = add_jump_table_alts(file);
	if (ret)
		return ret;

	ret = read_unwind_hints(file);
	if (ret)
		return ret;

	/*
	 * Must be after add_call_destinations() such that it can override
	 * dead_end_function() marks.
	 */
	ret = read_annotate(file, __annotate_late);
	if (ret)
		return ret;

	return 0;
}

static bool is_special_call(struct instruction *insn)
{
	if (insn->type == INSN_CALL) {
		struct symbol *dest = insn_call_dest(insn);

		if (!dest)
			return false;

		if (dest->fentry || dest->embedded_insn)
			return true;
	}

	return false;
}

static bool has_modified_stack_frame(struct instruction *insn, struct insn_state *state)
{
	struct cfi_state *cfi = &state->cfi;
	int i;

	if (cfi->cfa.base != initial_func_cfi.cfa.base || cfi->drap)
		return true;

	if (cfi->cfa.offset != initial_func_cfi.cfa.offset)
		return true;

	if (cfi->stack_size != initial_func_cfi.cfa.offset)
		return true;

	for (i = 0; i < CFI_NUM_REGS; i++) {
		if (cfi->regs[i].base != initial_func_cfi.regs[i].base ||
		    cfi->regs[i].offset != initial_func_cfi.regs[i].offset)
			return true;
	}

	return false;
}

static bool check_reg_frame_pos(const struct cfi_reg *reg,
				int expected_offset)
{
	return reg->base == CFI_CFA &&
	       reg->offset == expected_offset;
}

static bool has_valid_stack_frame(struct insn_state *state)
{
	struct cfi_state *cfi = &state->cfi;

	if (cfi->cfa.base == CFI_BP &&
	    check_reg_frame_pos(&cfi->regs[CFI_BP], -cfi->cfa.offset) &&
	    check_reg_frame_pos(&cfi->regs[CFI_RA], -cfi->cfa.offset + 8))
		return true;

	if (cfi->drap && cfi->regs[CFI_BP].base == CFI_BP)
		return true;

	return false;
}

static int update_cfi_state_regs(struct instruction *insn,
				  struct cfi_state *cfi,
				  struct stack_op *op)
{
	struct cfi_reg *cfa = &cfi->cfa;

	if (cfa->base != CFI_SP && cfa->base != CFI_SP_INDIRECT)
		return 0;

	/* push */
	if (op->dest.type == OP_DEST_PUSH || op->dest.type == OP_DEST_PUSHF)
		cfa->offset += 8;

	/* pop */
	if (op->src.type == OP_SRC_POP || op->src.type == OP_SRC_POPF)
		cfa->offset -= 8;

	/* add immediate to sp */
	if (op->dest.type == OP_DEST_REG && op->src.type == OP_SRC_ADD &&
	    op->dest.reg == CFI_SP && op->src.reg == CFI_SP)
		cfa->offset -= op->src.offset;

	return 0;
}

static void save_reg(struct cfi_state *cfi, unsigned char reg, int base, int offset)
{
	if (arch_callee_saved_reg(reg) &&
	    cfi->regs[reg].base == CFI_UNDEFINED) {
		cfi->regs[reg].base = base;
		cfi->regs[reg].offset = offset;
	}
}

static void restore_reg(struct cfi_state *cfi, unsigned char reg)
{
	cfi->regs[reg].base = initial_func_cfi.regs[reg].base;
	cfi->regs[reg].offset = initial_func_cfi.regs[reg].offset;
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
static int update_cfi_state(struct instruction *insn,
			    struct instruction *next_insn,
			    struct cfi_state *cfi, struct stack_op *op)
{
	struct cfi_reg *cfa = &cfi->cfa;
	struct cfi_reg *regs = cfi->regs;

	/* ignore UNWIND_HINT_UNDEFINED regions */
	if (cfi->force_undefined)
		return 0;

	/* stack operations don't make sense with an undefined CFA */
	if (cfa->base == CFI_UNDEFINED) {
		if (insn_func(insn)) {
			WARN_INSN(insn, "undefined stack state");
			return -1;
		}
		return 0;
	}

	if (cfi->type == UNWIND_HINT_TYPE_REGS ||
	    cfi->type == UNWIND_HINT_TYPE_REGS_PARTIAL)
		return update_cfi_state_regs(insn, cfi, op);

	switch (op->dest.type) {

	case OP_DEST_REG:
		switch (op->src.type) {

		case OP_SRC_REG:
			if (op->src.reg == CFI_SP && op->dest.reg == CFI_BP &&
			    cfa->base == CFI_SP &&
			    check_reg_frame_pos(&regs[CFI_BP], -cfa->offset)) {

				/* mov %rsp, %rbp */
				cfa->base = op->dest.reg;
				cfi->bp_scratch = false;
			}

			else if (op->src.reg == CFI_SP &&
				 op->dest.reg == CFI_BP && cfi->drap) {

				/* drap: mov %rsp, %rbp */
				regs[CFI_BP].base = CFI_BP;
				regs[CFI_BP].offset = -cfi->stack_size;
				cfi->bp_scratch = false;
			}

			else if (op->src.reg == CFI_SP && cfa->base == CFI_SP) {

				/*
				 * mov %rsp, %reg
				 *
				 * This is needed for the rare case where GCC
				 * does:
				 *
				 *   mov    %rsp, %rax
				 *   ...
				 *   mov    %rax, %rsp
				 */
				cfi->vals[op->dest.reg].base = CFI_CFA;
				cfi->vals[op->dest.reg].offset = -cfi->stack_size;
			}

			else if (op->src.reg == CFI_BP && op->dest.reg == CFI_SP &&
				 (cfa->base == CFI_BP || cfa->base == cfi->drap_reg)) {

				/*
				 * mov %rbp, %rsp
				 *
				 * Restore the original stack pointer (Clang).
				 */
				cfi->stack_size = -cfi->regs[CFI_BP].offset;
			}

			else if (op->dest.reg == cfa->base) {

				/* mov %reg, %rsp */
				if (cfa->base == CFI_SP &&
				    cfi->vals[op->src.reg].base == CFI_CFA) {

					/*
					 * This is needed for the rare case
					 * where GCC does something dumb like:
					 *
					 *   lea    0x8(%rsp), %rcx
					 *   ...
					 *   mov    %rcx, %rsp
					 */
					cfa->offset = -cfi->vals[op->src.reg].offset;
					cfi->stack_size = cfa->offset;

				} else if (cfa->base == CFI_SP &&
					   cfi->vals[op->src.reg].base == CFI_SP_INDIRECT &&
					   cfi->vals[op->src.reg].offset == cfa->offset) {

					/*
					 * Stack swizzle:
					 *
					 * 1: mov %rsp, (%[tos])
					 * 2: mov %[tos], %rsp
					 *    ...
					 * 3: pop %rsp
					 *
					 * Where:
					 *
					 * 1 - places a pointer to the previous
					 *     stack at the Top-of-Stack of the
					 *     new stack.
					 *
					 * 2 - switches to the new stack.
					 *
					 * 3 - pops the Top-of-Stack to restore
					 *     the original stack.
					 *
					 * Note: we set base to SP_INDIRECT
					 * here and preserve offset. Therefore
					 * when the unwinder reaches ToS it
					 * will dereference SP and then add the
					 * offset to find the next frame, IOW:
					 * (%rsp) + offset.
					 */
					cfa->base = CFI_SP_INDIRECT;

				} else {
					cfa->base = CFI_UNDEFINED;
					cfa->offset = 0;
				}
			}

			else if (op->dest.reg == CFI_SP &&
				 cfi->vals[op->src.reg].base == CFI_SP_INDIRECT &&
				 cfi->vals[op->src.reg].offset == cfa->offset) {

				/*
				 * The same stack swizzle case 2) as above. But
				 * because we can't change cfa->base, case 3)
				 * will become a regular POP. Pretend we're a
				 * PUSH so things don't go unbalanced.
				 */
				cfi->stack_size += 8;
			}


			break;

		case OP_SRC_ADD:
			if (op->dest.reg == CFI_SP && op->src.reg == CFI_SP) {

				/* add imm, %rsp */
				cfi->stack_size -= op->src.offset;
				if (cfa->base == CFI_SP)
					cfa->offset -= op->src.offset;
				break;
			}

			if (op->dest.reg == CFI_BP && op->src.reg == CFI_SP &&
			    insn->sym->frame_pointer) {
				/* addi.d fp,sp,imm on LoongArch */
				if (cfa->base == CFI_SP && cfa->offset == op->src.offset) {
					cfa->base = CFI_BP;
					cfa->offset = 0;
				}
				break;
			}

			if (op->dest.reg == CFI_SP && op->src.reg == CFI_BP) {
				/* addi.d sp,fp,imm on LoongArch */
				if (cfa->base == CFI_BP && cfa->offset == 0) {
					if (insn->sym->frame_pointer) {
						cfa->base = CFI_SP;
						cfa->offset = -op->src.offset;
					}
				} else {
					/* lea disp(%rbp), %rsp */
					cfi->stack_size = -(op->src.offset + regs[CFI_BP].offset);
				}
				break;
			}

			if (op->src.reg == CFI_SP && cfa->base == CFI_SP) {

				/* drap: lea disp(%rsp), %drap */
				cfi->drap_reg = op->dest.reg;

				/*
				 * lea disp(%rsp), %reg
				 *
				 * This is needed for the rare case where GCC
				 * does something dumb like:
				 *
				 *   lea    0x8(%rsp), %rcx
				 *   ...
				 *   mov    %rcx, %rsp
				 */
				cfi->vals[op->dest.reg].base = CFI_CFA;
				cfi->vals[op->dest.reg].offset = \
					-cfi->stack_size + op->src.offset;

				break;
			}

			if (cfi->drap && op->dest.reg == CFI_SP &&
			    op->src.reg == cfi->drap_reg) {

				 /* drap: lea disp(%drap), %rsp */
				cfa->base = CFI_SP;
				cfa->offset = cfi->stack_size = -op->src.offset;
				cfi->drap_reg = CFI_UNDEFINED;
				cfi->drap = false;
				break;
			}

			if (op->dest.reg == cfi->cfa.base && !(next_insn && next_insn->hint)) {
				WARN_INSN(insn, "unsupported stack register modification");
				return -1;
			}

			break;

		case OP_SRC_AND:
			if (op->dest.reg != CFI_SP ||
			    (cfi->drap_reg != CFI_UNDEFINED && cfa->base != CFI_SP) ||
			    (cfi->drap_reg == CFI_UNDEFINED && cfa->base != CFI_BP)) {
				WARN_INSN(insn, "unsupported stack pointer realignment");
				return -1;
			}

			if (cfi->drap_reg != CFI_UNDEFINED) {
				/* drap: and imm, %rsp */
				cfa->base = cfi->drap_reg;
				cfa->offset = cfi->stack_size = 0;
				cfi->drap = true;
			}

			/*
			 * Older versions of GCC (4.8ish) realign the stack
			 * without DRAP, with a frame pointer.
			 */

			break;

		case OP_SRC_POP:
		case OP_SRC_POPF:
			if (op->dest.reg == CFI_SP && cfa->base == CFI_SP_INDIRECT) {

				/* pop %rsp; # restore from a stack swizzle */
				cfa->base = CFI_SP;
				break;
			}

			if (!cfi->drap && op->dest.reg == cfa->base) {

				/* pop %rbp */
				cfa->base = CFI_SP;
			}

			if (cfi->drap && cfa->base == CFI_BP_INDIRECT &&
			    op->dest.reg == cfi->drap_reg &&
			    cfi->drap_offset == -cfi->stack_size) {

				/* drap: pop %drap */
				cfa->base = cfi->drap_reg;
				cfa->offset = 0;
				cfi->drap_offset = -1;

			} else if (cfi->stack_size == -regs[op->dest.reg].offset) {

				/* pop %reg */
				restore_reg(cfi, op->dest.reg);
			}

			cfi->stack_size -= 8;
			if (cfa->base == CFI_SP)
				cfa->offset -= 8;

			break;

		case OP_SRC_REG_INDIRECT:
			if (!cfi->drap && op->dest.reg == cfa->base &&
			    op->dest.reg == CFI_BP) {

				/* mov disp(%rsp), %rbp */
				cfa->base = CFI_SP;
				cfa->offset = cfi->stack_size;
			}

			if (cfi->drap && op->src.reg == CFI_BP &&
			    op->src.offset == cfi->drap_offset) {

				/* drap: mov disp(%rbp), %drap */
				cfa->base = cfi->drap_reg;
				cfa->offset = 0;
				cfi->drap_offset = -1;
			}

			if (cfi->drap && op->src.reg == CFI_BP &&
			    op->src.offset == regs[op->dest.reg].offset) {

				/* drap: mov disp(%rbp), %reg */
				restore_reg(cfi, op->dest.reg);

			} else if (op->src.reg == cfa->base &&
			    op->src.offset == regs[op->dest.reg].offset + cfa->offset) {

				/* mov disp(%rbp), %reg */
				/* mov disp(%rsp), %reg */
				restore_reg(cfi, op->dest.reg);

			} else if (op->src.reg == CFI_SP &&
				   op->src.offset == regs[op->dest.reg].offset + cfi->stack_size) {

				/* mov disp(%rsp), %reg */
				restore_reg(cfi, op->dest.reg);
			}

			break;

		default:
			WARN_INSN(insn, "unknown stack-related instruction");
			return -1;
		}

		break;

	case OP_DEST_PUSH:
	case OP_DEST_PUSHF:
		cfi->stack_size += 8;
		if (cfa->base == CFI_SP)
			cfa->offset += 8;

		if (op->src.type != OP_SRC_REG)
			break;

		if (cfi->drap) {
			if (op->src.reg == cfa->base && op->src.reg == cfi->drap_reg) {

				/* drap: push %drap */
				cfa->base = CFI_BP_INDIRECT;
				cfa->offset = -cfi->stack_size;

				/* save drap so we know when to restore it */
				cfi->drap_offset = -cfi->stack_size;

			} else if (op->src.reg == CFI_BP && cfa->base == cfi->drap_reg) {

				/* drap: push %rbp */
				cfi->stack_size = 0;

			} else {

				/* drap: push %reg */
				save_reg(cfi, op->src.reg, CFI_BP, -cfi->stack_size);
			}

		} else {

			/* push %reg */
			save_reg(cfi, op->src.reg, CFI_CFA, -cfi->stack_size);
		}

		/* detect when asm code uses rbp as a scratch register */
		if (opts.stackval && insn_func(insn) && op->src.reg == CFI_BP &&
		    cfa->base != CFI_BP)
			cfi->bp_scratch = true;
		break;

	case OP_DEST_REG_INDIRECT:

		if (cfi->drap) {
			if (op->src.reg == cfa->base && op->src.reg == cfi->drap_reg) {

				/* drap: mov %drap, disp(%rbp) */
				cfa->base = CFI_BP_INDIRECT;
				cfa->offset = op->dest.offset;

				/* save drap offset so we know when to restore it */
				cfi->drap_offset = op->dest.offset;
			} else {

				/* drap: mov reg, disp(%rbp) */
				save_reg(cfi, op->src.reg, CFI_BP, op->dest.offset);
			}

		} else if (op->dest.reg == cfa->base) {

			/* mov reg, disp(%rbp) */
			/* mov reg, disp(%rsp) */
			save_reg(cfi, op->src.reg, CFI_CFA,
				 op->dest.offset - cfi->cfa.offset);

		} else if (op->dest.reg == CFI_SP) {

			/* mov reg, disp(%rsp) */
			save_reg(cfi, op->src.reg, CFI_CFA,
				 op->dest.offset - cfi->stack_size);

		} else if (op->src.reg == CFI_SP && op->dest.offset == 0) {

			/* mov %rsp, (%reg); # setup a stack swizzle. */
			cfi->vals[op->dest.reg].base = CFI_SP_INDIRECT;
			cfi->vals[op->dest.reg].offset = cfa->offset;
		}

		break;

	case OP_DEST_MEM:
		if (op->src.type != OP_SRC_POP && op->src.type != OP_SRC_POPF) {
			WARN_INSN(insn, "unknown stack-related memory operation");
			return -1;
		}

		/* pop mem */
		cfi->stack_size -= 8;
		if (cfa->base == CFI_SP)
			cfa->offset -= 8;

		break;

	default:
		WARN_INSN(insn, "unknown stack-related instruction");
		return -1;
	}

	return 0;
}

/*
 * The stack layouts of alternatives instructions can sometimes diverge when
 * they have stack modifications.  That's fine as long as the potential stack
 * layouts don't conflict at any given potential instruction boundary.
 *
 * Flatten the CFIs of the different alternative code streams (both original
 * and replacement) into a single shared CFI array which can be used to detect
 * conflicts and nicely feed a linear array of ORC entries to the unwinder.
 */
static int propagate_alt_cfi(struct objtool_file *file, struct instruction *insn)
{
	struct cfi_state **alt_cfi;
	int group_off;

	if (!insn->alt_group)
		return 0;

	if (!insn->cfi) {
		WARN("CFI missing");
		return -1;
	}

	alt_cfi = insn->alt_group->cfi;
	group_off = insn->offset - insn->alt_group->first_insn->offset;

	if (!alt_cfi[group_off]) {
		alt_cfi[group_off] = insn->cfi;
	} else {
		if (cficmp(alt_cfi[group_off], insn->cfi)) {
			struct alt_group *orig_group = insn->alt_group->orig_group ?: insn->alt_group;
			struct instruction *orig = orig_group->first_insn;
			char *where = offstr(insn->sec, insn->offset);
			WARN_INSN(orig, "stack layout conflict in alternatives: %s", where);
			free(where);
			return -1;
		}
	}

	return 0;
}

static int handle_insn_ops(struct instruction *insn,
			   struct instruction *next_insn,
			   struct insn_state *state)
{
	struct stack_op *op;

	for (op = insn->stack_ops; op; op = op->next) {

		if (update_cfi_state(insn, next_insn, &state->cfi, op))
			return 1;

		if (!insn->alt_group)
			continue;

		if (op->dest.type == OP_DEST_PUSHF) {
			if (!state->uaccess_stack) {
				state->uaccess_stack = 1;
			} else if (state->uaccess_stack >> 31) {
				WARN_INSN(insn, "PUSHF stack exhausted");
				return 1;
			}
			state->uaccess_stack <<= 1;
			state->uaccess_stack  |= state->uaccess;
		}

		if (op->src.type == OP_SRC_POPF) {
			if (state->uaccess_stack) {
				state->uaccess = state->uaccess_stack & 1;
				state->uaccess_stack >>= 1;
				if (state->uaccess_stack == 1)
					state->uaccess_stack = 0;
			}
		}
	}

	return 0;
}

static bool insn_cfi_match(struct instruction *insn, struct cfi_state *cfi2)
{
	struct cfi_state *cfi1 = insn->cfi;
	int i;

	if (!cfi1) {
		WARN("CFI missing");
		return false;
	}

	if (memcmp(&cfi1->cfa, &cfi2->cfa, sizeof(cfi1->cfa))) {

		WARN_INSN(insn, "stack state mismatch: cfa1=%d%+d cfa2=%d%+d",
			  cfi1->cfa.base, cfi1->cfa.offset,
			  cfi2->cfa.base, cfi2->cfa.offset);

	} else if (memcmp(&cfi1->regs, &cfi2->regs, sizeof(cfi1->regs))) {
		for (i = 0; i < CFI_NUM_REGS; i++) {
			if (!memcmp(&cfi1->regs[i], &cfi2->regs[i],
				    sizeof(struct cfi_reg)))
				continue;

			WARN_INSN(insn, "stack state mismatch: reg1[%d]=%d%+d reg2[%d]=%d%+d",
				  i, cfi1->regs[i].base, cfi1->regs[i].offset,
				  i, cfi2->regs[i].base, cfi2->regs[i].offset);
			break;
		}

	} else if (cfi1->type != cfi2->type) {

		WARN_INSN(insn, "stack state mismatch: type1=%d type2=%d",
			  cfi1->type, cfi2->type);

	} else if (cfi1->drap != cfi2->drap ||
		   (cfi1->drap && cfi1->drap_reg != cfi2->drap_reg) ||
		   (cfi1->drap && cfi1->drap_offset != cfi2->drap_offset)) {

		WARN_INSN(insn, "stack state mismatch: drap1=%d(%d,%d) drap2=%d(%d,%d)",
			  cfi1->drap, cfi1->drap_reg, cfi1->drap_offset,
			  cfi2->drap, cfi2->drap_reg, cfi2->drap_offset);

	} else
		return true;

	return false;
}

static inline bool func_uaccess_safe(struct symbol *func)
{
	if (func)
		return func->uaccess_safe;

	return false;
}

static inline const char *call_dest_name(struct instruction *insn)
{
	static char pvname[19];
	struct reloc *reloc;
	int idx;

	if (insn_call_dest(insn))
		return insn_call_dest(insn)->name;

	reloc = insn_reloc(NULL, insn);
	if (reloc && !strcmp(reloc->sym->name, "pv_ops")) {
		idx = (reloc_addend(reloc) / sizeof(void *));
		snprintf(pvname, sizeof(pvname), "pv_ops[%d]", idx);
		return pvname;
	}

	return "{dynamic}";
}

static bool pv_call_dest(struct objtool_file *file, struct instruction *insn)
{
	struct symbol *target;
	struct reloc *reloc;
	int idx;

	reloc = insn_reloc(file, insn);
	if (!reloc || strcmp(reloc->sym->name, "pv_ops"))
		return false;

	idx = (arch_dest_reloc_offset(reloc_addend(reloc)) / sizeof(void *));

	if (file->pv_ops[idx].clean)
		return true;

	file->pv_ops[idx].clean = true;

	list_for_each_entry(target, &file->pv_ops[idx].targets, pv_target) {
		if (!target->sec->noinstr) {
			WARN("pv_ops[%d]: %s", idx, target->name);
			file->pv_ops[idx].clean = false;
		}
	}

	return file->pv_ops[idx].clean;
}

static inline bool noinstr_call_dest(struct objtool_file *file,
				     struct instruction *insn,
				     struct symbol *func)
{
	/*
	 * We can't deal with indirect function calls at present;
	 * assume they're instrumented.
	 */
	if (!func) {
		if (file->pv_ops)
			return pv_call_dest(file, insn);

		return false;
	}

	/*
	 * If the symbol is from a noinstr section; we good.
	 */
	if (func->sec->noinstr)
		return true;

	/*
	 * If the symbol is a static_call trampoline, we can't tell.
	 */
	if (func->static_call_tramp)
		return true;

	/*
	 * The __ubsan_handle_*() calls are like WARN(), they only happen when
	 * something 'BAD' happened. At the risk of taking the machine down,
	 * let them proceed to get the message out.
	 */
	if (!strncmp(func->name, "__ubsan_handle_", 15))
		return true;

	return false;
}

static int validate_call(struct objtool_file *file,
			 struct instruction *insn,
			 struct insn_state *state)
{
	if (state->noinstr && state->instr <= 0 &&
	    !noinstr_call_dest(file, insn, insn_call_dest(insn))) {
		WARN_INSN(insn, "call to %s() leaves .noinstr.text section", call_dest_name(insn));
		return 1;
	}

	if (state->uaccess && !func_uaccess_safe(insn_call_dest(insn))) {
		WARN_INSN(insn, "call to %s() with UACCESS enabled", call_dest_name(insn));
		return 1;
	}

	if (state->df) {
		WARN_INSN(insn, "call to %s() with DF set", call_dest_name(insn));
		return 1;
	}

	return 0;
}

static int validate_sibling_call(struct objtool_file *file,
				 struct instruction *insn,
				 struct insn_state *state)
{
	if (insn_func(insn) && has_modified_stack_frame(insn, state)) {
		WARN_INSN(insn, "sibling call from callable instruction with modified stack frame");
		return 1;
	}

	return validate_call(file, insn, state);
}

static int validate_return(struct symbol *func, struct instruction *insn, struct insn_state *state)
{
	if (state->noinstr && state->instr > 0) {
		WARN_INSN(insn, "return with instrumentation enabled");
		return 1;
	}

	if (state->uaccess && !func_uaccess_safe(func)) {
		WARN_INSN(insn, "return with UACCESS enabled");
		return 1;
	}

	if (!state->uaccess && func_uaccess_safe(func)) {
		WARN_INSN(insn, "return with UACCESS disabled from a UACCESS-safe function");
		return 1;
	}

	if (state->df) {
		WARN_INSN(insn, "return with DF set");
		return 1;
	}

	if (func && has_modified_stack_frame(insn, state)) {
		WARN_INSN(insn, "return with modified stack frame");
		return 1;
	}

	if (state->cfi.bp_scratch) {
		WARN_INSN(insn, "BP used as a scratch register");
		return 1;
	}

	return 0;
}

static struct instruction *next_insn_to_validate(struct objtool_file *file,
						 struct instruction *insn)
{
	struct alt_group *alt_group = insn->alt_group;

	/*
	 * Simulate the fact that alternatives are patched in-place.  When the
	 * end of a replacement alt_group is reached, redirect objtool flow to
	 * the end of the original alt_group.
	 *
	 * insn->alts->insn -> alt_group->first_insn
	 *		       ...
	 *		       alt_group->last_insn
	 *		       [alt_group->nop]      -> next(orig_group->last_insn)
	 */
	if (alt_group) {
		if (alt_group->nop) {
			/* ->nop implies ->orig_group */
			if (insn == alt_group->last_insn)
				return alt_group->nop;
			if (insn == alt_group->nop)
				goto next_orig;
		}
		if (insn == alt_group->last_insn && alt_group->orig_group)
			goto next_orig;
	}

	return next_insn_same_sec(file, insn);

next_orig:
	return next_insn_same_sec(file, alt_group->orig_group->last_insn);
}

/*
 * Follow the branch starting at the given instruction, and recursively follow
 * any other branches (jumps).  Meanwhile, track the frame pointer state at
 * each instruction and validate all the rules described in
 * tools/objtool/Documentation/objtool.txt.
 */
static int validate_branch(struct objtool_file *file, struct symbol *func,
			   struct instruction *insn, struct insn_state state)
{
	struct alternative *alt;
	struct instruction *next_insn, *prev_insn = NULL;
	struct section *sec;
	u8 visited;
	int ret;

	sec = insn->sec;

	while (1) {
		next_insn = next_insn_to_validate(file, insn);

		if (func && insn_func(insn) && func != insn_func(insn)->pfunc) {
			/* Ignore KCFI type preambles, which always fall through */
			if (!strncmp(func->name, "__cfi_", 6) ||
			    !strncmp(func->name, "__pfx_", 6))
				return 0;

			WARN("%s() falls through to next function %s()",
			     func->name, insn_func(insn)->name);
			return 1;
		}

		if (func && insn->ignore) {
			WARN_INSN(insn, "BUG: why am I validating an ignored function?");
			return 1;
		}

		visited = VISITED_BRANCH << state.uaccess;
		if (insn->visited & VISITED_BRANCH_MASK) {
			if (!insn->hint && !insn_cfi_match(insn, &state.cfi))
				return 1;

			if (insn->visited & visited)
				return 0;
		} else {
			nr_insns_visited++;
		}

		if (state.noinstr)
			state.instr += insn->instr;

		if (insn->hint) {
			if (insn->restore) {
				struct instruction *save_insn, *i;

				i = insn;
				save_insn = NULL;

				sym_for_each_insn_continue_reverse(file, func, i) {
					if (i->save) {
						save_insn = i;
						break;
					}
				}

				if (!save_insn) {
					WARN_INSN(insn, "no corresponding CFI save for CFI restore");
					return 1;
				}

				if (!save_insn->visited) {
					/*
					 * If the restore hint insn is at the
					 * beginning of a basic block and was
					 * branched to from elsewhere, and the
					 * save insn hasn't been visited yet,
					 * defer following this branch for now.
					 * It will be seen later via the
					 * straight-line path.
					 */
					if (!prev_insn)
						return 0;

					WARN_INSN(insn, "objtool isn't smart enough to handle this CFI save/restore combo");
					return 1;
				}

				insn->cfi = save_insn->cfi;
				nr_cfi_reused++;
			}

			state.cfi = *insn->cfi;
		} else {
			/* XXX track if we actually changed state.cfi */

			if (prev_insn && !cficmp(prev_insn->cfi, &state.cfi)) {
				insn->cfi = prev_insn->cfi;
				nr_cfi_reused++;
			} else {
				insn->cfi = cfi_hash_find_or_add(&state.cfi);
			}
		}

		insn->visited |= visited;

		if (propagate_alt_cfi(file, insn))
			return 1;

		if (!insn->ignore_alts && insn->alts) {
			bool skip_orig = false;

			for (alt = insn->alts; alt; alt = alt->next) {
				if (alt->skip_orig)
					skip_orig = true;

				ret = validate_branch(file, func, alt->insn, state);
				if (ret) {
					BT_INSN(insn, "(alt)");
					return ret;
				}
			}

			if (skip_orig)
				return 0;
		}

		if (handle_insn_ops(insn, next_insn, &state))
			return 1;

		switch (insn->type) {

		case INSN_RETURN:
			return validate_return(func, insn, &state);

		case INSN_CALL:
		case INSN_CALL_DYNAMIC:
			ret = validate_call(file, insn, &state);
			if (ret)
				return ret;

			if (opts.stackval && func && !is_special_call(insn) &&
			    !has_valid_stack_frame(&state)) {
				WARN_INSN(insn, "call without frame pointer save/setup");
				return 1;
			}

			if (insn->dead_end)
				return 0;

			break;

		case INSN_JUMP_CONDITIONAL:
		case INSN_JUMP_UNCONDITIONAL:
			if (is_sibling_call(insn)) {
				ret = validate_sibling_call(file, insn, &state);
				if (ret)
					return ret;

			} else if (insn->jump_dest) {
				ret = validate_branch(file, func,
						      insn->jump_dest, state);
				if (ret) {
					BT_INSN(insn, "(branch)");
					return ret;
				}
			}

			if (insn->type == INSN_JUMP_UNCONDITIONAL)
				return 0;

			break;

		case INSN_JUMP_DYNAMIC:
		case INSN_JUMP_DYNAMIC_CONDITIONAL:
			if (is_sibling_call(insn)) {
				ret = validate_sibling_call(file, insn, &state);
				if (ret)
					return ret;
			}

			if (insn->type == INSN_JUMP_DYNAMIC)
				return 0;

			break;

		case INSN_CONTEXT_SWITCH:
			if (func) {
				if (!next_insn || !next_insn->hint) {
					WARN_INSN(insn, "unsupported instruction in callable function");
					return 1;
				}
				break;
			}
			return 0;

		case INSN_STAC:
			if (state.uaccess) {
				WARN_INSN(insn, "recursive UACCESS enable");
				return 1;
			}

			state.uaccess = true;
			break;

		case INSN_CLAC:
			if (!state.uaccess && func) {
				WARN_INSN(insn, "redundant UACCESS disable");
				return 1;
			}

			if (func_uaccess_safe(func) && !state.uaccess_stack) {
				WARN_INSN(insn, "UACCESS-safe disables UACCESS");
				return 1;
			}

			state.uaccess = false;
			break;

		case INSN_STD:
			if (state.df) {
				WARN_INSN(insn, "recursive STD");
				return 1;
			}

			state.df = true;
			break;

		case INSN_CLD:
			if (!state.df && func) {
				WARN_INSN(insn, "redundant CLD");
				return 1;
			}

			state.df = false;
			break;

		default:
			break;
		}

		if (insn->dead_end)
			return 0;

		if (!next_insn) {
			if (state.cfi.cfa.base == CFI_UNDEFINED)
				return 0;
			WARN("%s: unexpected end of section", sec->name);
			return 1;
		}

		prev_insn = insn;
		insn = next_insn;
	}

	return 0;
}

static int validate_unwind_hint(struct objtool_file *file,
				  struct instruction *insn,
				  struct insn_state *state)
{
	if (insn->hint && !insn->visited && !insn->ignore) {
		int ret = validate_branch(file, insn_func(insn), insn, *state);
		if (ret)
			BT_INSN(insn, "<=== (hint)");
		return ret;
	}

	return 0;
}

static int validate_unwind_hints(struct objtool_file *file, struct section *sec)
{
	struct instruction *insn;
	struct insn_state state;
	int warnings = 0;

	if (!file->hints)
		return 0;

	init_insn_state(file, &state, sec);

	if (sec) {
		sec_for_each_insn(file, sec, insn)
			warnings += validate_unwind_hint(file, insn, &state);
	} else {
		for_each_insn(file, insn)
			warnings += validate_unwind_hint(file, insn, &state);
	}

	return warnings;
}

/*
 * Validate rethunk entry constraint: must untrain RET before the first RET.
 *
 * Follow every branch (intra-function) and ensure VALIDATE_UNRET_END comes
 * before an actual RET instruction.
 */
static int validate_unret(struct objtool_file *file, struct instruction *insn)
{
	struct instruction *next, *dest;
	int ret;

	for (;;) {
		next = next_insn_to_validate(file, insn);

		if (insn->visited & VISITED_UNRET)
			return 0;

		insn->visited |= VISITED_UNRET;

		if (!insn->ignore_alts && insn->alts) {
			struct alternative *alt;
			bool skip_orig = false;

			for (alt = insn->alts; alt; alt = alt->next) {
				if (alt->skip_orig)
					skip_orig = true;

				ret = validate_unret(file, alt->insn);
				if (ret) {
					BT_INSN(insn, "(alt)");
					return ret;
				}
			}

			if (skip_orig)
				return 0;
		}

		switch (insn->type) {

		case INSN_CALL_DYNAMIC:
		case INSN_JUMP_DYNAMIC:
		case INSN_JUMP_DYNAMIC_CONDITIONAL:
			WARN_INSN(insn, "early indirect call");
			return 1;

		case INSN_JUMP_UNCONDITIONAL:
		case INSN_JUMP_CONDITIONAL:
			if (!is_sibling_call(insn)) {
				if (!insn->jump_dest) {
					WARN_INSN(insn, "unresolved jump target after linking?!?");
					return -1;
				}
				ret = validate_unret(file, insn->jump_dest);
				if (ret) {
					BT_INSN(insn, "(branch%s)",
						insn->type == INSN_JUMP_CONDITIONAL ? "-cond" : "");
					return ret;
				}

				if (insn->type == INSN_JUMP_UNCONDITIONAL)
					return 0;

				break;
			}

			/* fallthrough */
		case INSN_CALL:
			dest = find_insn(file, insn_call_dest(insn)->sec,
					 insn_call_dest(insn)->offset);
			if (!dest) {
				WARN("Unresolved function after linking!?: %s",
				     insn_call_dest(insn)->name);
				return -1;
			}

			ret = validate_unret(file, dest);
			if (ret) {
				BT_INSN(insn, "(call)");
				return ret;
			}
			/*
			 * If a call returns without error, it must have seen UNTRAIN_RET.
			 * Therefore any non-error return is a success.
			 */
			return 0;

		case INSN_RETURN:
			WARN_INSN(insn, "RET before UNTRAIN");
			return 1;

		case INSN_NOP:
			if (insn->retpoline_safe)
				return 0;
			break;

		default:
			break;
		}

		if (!next) {
			WARN_INSN(insn, "teh end!");
			return -1;
		}
		insn = next;
	}

	return 0;
}

/*
 * Validate that all branches starting at VALIDATE_UNRET_BEGIN encounter
 * VALIDATE_UNRET_END before RET.
 */
static int validate_unrets(struct objtool_file *file)
{
	struct instruction *insn;
	int ret, warnings = 0;

	for_each_insn(file, insn) {
		if (!insn->unret)
			continue;

		ret = validate_unret(file, insn);
		if (ret < 0) {
			WARN_INSN(insn, "Failed UNRET validation");
			return ret;
		}
		warnings += ret;
	}

	return warnings;
}

static int validate_retpoline(struct objtool_file *file)
{
	struct instruction *insn;
	int warnings = 0;

	for_each_insn(file, insn) {
		if (insn->type != INSN_JUMP_DYNAMIC &&
		    insn->type != INSN_CALL_DYNAMIC &&
		    insn->type != INSN_RETURN)
			continue;

		if (insn->retpoline_safe)
			continue;

		if (insn->sec->init)
			continue;

		if (insn->type == INSN_RETURN) {
			if (opts.rethunk) {
				WARN_INSN(insn, "'naked' return found in MITIGATION_RETHUNK build");
			} else
				continue;
		} else {
			WARN_INSN(insn, "indirect %s found in MITIGATION_RETPOLINE build",
				  insn->type == INSN_JUMP_DYNAMIC ? "jump" : "call");
		}

		warnings++;
	}

	return warnings;
}

static bool is_kasan_insn(struct instruction *insn)
{
	return (insn->type == INSN_CALL &&
		!strcmp(insn_call_dest(insn)->name, "__asan_handle_no_return"));
}

static bool is_ubsan_insn(struct instruction *insn)
{
	return (insn->type == INSN_CALL &&
		!strcmp(insn_call_dest(insn)->name,
			"__ubsan_handle_builtin_unreachable"));
}

static bool ignore_unreachable_insn(struct objtool_file *file, struct instruction *insn)
{
	int i;
	struct instruction *prev_insn;

	if (insn->ignore || insn->type == INSN_NOP || insn->type == INSN_TRAP)
		return true;

	/*
	 * Ignore alternative replacement instructions.  This can happen
	 * when a whitelisted function uses one of the ALTERNATIVE macros.
	 */
	if (!strcmp(insn->sec->name, ".altinstr_replacement") ||
	    !strcmp(insn->sec->name, ".altinstr_aux"))
		return true;

	/*
	 * Whole archive runs might encounter dead code from weak symbols.
	 * This is where the linker will have dropped the weak symbol in
	 * favour of a regular symbol, but leaves the code in place.
	 *
	 * In this case we'll find a piece of code (whole function) that is not
	 * covered by a !section symbol. Ignore them.
	 */
	if (opts.link && !insn_func(insn)) {
		int size = find_symbol_hole_containing(insn->sec, insn->offset);
		unsigned long end = insn->offset + size;

		if (!size) /* not a hole */
			return false;

		if (size < 0) /* hole until the end */
			return true;

		sec_for_each_insn_continue(file, insn) {
			/*
			 * If we reach a visited instruction at or before the
			 * end of the hole, ignore the unreachable.
			 */
			if (insn->visited)
				return true;

			if (insn->offset >= end)
				break;

			/*
			 * If this hole jumps to a .cold function, mark it ignore too.
			 */
			if (insn->jump_dest && insn_func(insn->jump_dest) &&
			    strstr(insn_func(insn->jump_dest)->name, ".cold")) {
				struct instruction *dest = insn->jump_dest;
				func_for_each_insn(file, insn_func(dest), dest)
					dest->ignore = true;
			}
		}

		return false;
	}

	if (!insn_func(insn))
		return false;

	if (insn_func(insn)->static_call_tramp)
		return true;

	/*
	 * CONFIG_UBSAN_TRAP inserts a UD2 when it sees
	 * __builtin_unreachable().  The BUG() macro has an unreachable() after
	 * the UD2, which causes GCC's undefined trap logic to emit another UD2
	 * (or occasionally a JMP to UD2).
	 *
	 * It may also insert a UD2 after calling a __noreturn function.
	 */
	prev_insn = prev_insn_same_sec(file, insn);
	if (prev_insn->dead_end &&
	    (insn->type == INSN_BUG ||
	     (insn->type == INSN_JUMP_UNCONDITIONAL &&
	      insn->jump_dest && insn->jump_dest->type == INSN_BUG)))
		return true;

	/*
	 * Check if this (or a subsequent) instruction is related to
	 * CONFIG_UBSAN or CONFIG_KASAN.
	 *
	 * End the search at 5 instructions to avoid going into the weeds.
	 */
	for (i = 0; i < 5; i++) {

		if (is_kasan_insn(insn) || is_ubsan_insn(insn))
			return true;

		if (insn->type == INSN_JUMP_UNCONDITIONAL) {
			if (insn->jump_dest &&
			    insn_func(insn->jump_dest) == insn_func(insn)) {
				insn = insn->jump_dest;
				continue;
			}

			break;
		}

		if (insn->offset + insn->len >= insn_func(insn)->offset + insn_func(insn)->len)
			break;

		insn = next_insn_same_sec(file, insn);
	}

	return false;
}

static int add_prefix_symbol(struct objtool_file *file, struct symbol *func)
{
	struct instruction *insn, *prev;
	struct cfi_state *cfi;

	insn = find_insn(file, func->sec, func->offset);
	if (!insn)
		return -1;

	for (prev = prev_insn_same_sec(file, insn);
	     prev;
	     prev = prev_insn_same_sec(file, prev)) {
		u64 offset;

		if (prev->type != INSN_NOP)
			return -1;

		offset = func->offset - prev->offset;

		if (offset > opts.prefix)
			return -1;

		if (offset < opts.prefix)
			continue;

		elf_create_prefix_symbol(file->elf, func, opts.prefix);
		break;
	}

	if (!prev)
		return -1;

	if (!insn->cfi) {
		/*
		 * This can happen if stack validation isn't enabled or the
		 * function is annotated with STACK_FRAME_NON_STANDARD.
		 */
		return 0;
	}

	/* Propagate insn->cfi to the prefix code */
	cfi = cfi_hash_find_or_add(insn->cfi);
	for (; prev != insn; prev = next_insn_same_sec(file, prev))
		prev->cfi = cfi;

	return 0;
}

static int add_prefix_symbols(struct objtool_file *file)
{
	struct section *sec;
	struct symbol *func;

	for_each_sec(file, sec) {
		if (!(sec->sh.sh_flags & SHF_EXECINSTR))
			continue;

		sec_for_each_sym(sec, func) {
			if (func->type != STT_FUNC)
				continue;

			add_prefix_symbol(file, func);
		}
	}

	return 0;
}

static int validate_symbol(struct objtool_file *file, struct section *sec,
			   struct symbol *sym, struct insn_state *state)
{
	struct instruction *insn;
	int ret;

	if (!sym->len) {
		WARN("%s() is missing an ELF size annotation", sym->name);
		return 1;
	}

	if (sym->pfunc != sym || sym->alias != sym)
		return 0;

	insn = find_insn(file, sec, sym->offset);
	if (!insn || insn->ignore || insn->visited)
		return 0;

	state->uaccess = sym->uaccess_safe;

	ret = validate_branch(file, insn_func(insn), insn, *state);
	if (ret)
		BT_INSN(insn, "<=== (sym)");
	return ret;
}

static int validate_section(struct objtool_file *file, struct section *sec)
{
	struct insn_state state;
	struct symbol *func;
	int warnings = 0;

	sec_for_each_sym(sec, func) {
		if (func->type != STT_FUNC)
			continue;

		init_insn_state(file, &state, sec);
		set_func_state(&state.cfi);

		warnings += validate_symbol(file, sec, func, &state);
	}

	return warnings;
}

static int validate_noinstr_sections(struct objtool_file *file)
{
	struct section *sec;
	int warnings = 0;

	sec = find_section_by_name(file->elf, ".noinstr.text");
	if (sec) {
		warnings += validate_section(file, sec);
		warnings += validate_unwind_hints(file, sec);
	}

	sec = find_section_by_name(file->elf, ".entry.text");
	if (sec) {
		warnings += validate_section(file, sec);
		warnings += validate_unwind_hints(file, sec);
	}

	sec = find_section_by_name(file->elf, ".cpuidle.text");
	if (sec) {
		warnings += validate_section(file, sec);
		warnings += validate_unwind_hints(file, sec);
	}

	return warnings;
}

static int validate_functions(struct objtool_file *file)
{
	struct section *sec;
	int warnings = 0;

	for_each_sec(file, sec) {
		if (!(sec->sh.sh_flags & SHF_EXECINSTR))
			continue;

		warnings += validate_section(file, sec);
	}

	return warnings;
}

static void mark_endbr_used(struct instruction *insn)
{
	if (!list_empty(&insn->call_node))
		list_del_init(&insn->call_node);
}

static bool noendbr_range(struct objtool_file *file, struct instruction *insn)
{
	struct symbol *sym = find_symbol_containing(insn->sec, insn->offset-1);
	struct instruction *first;

	if (!sym)
		return false;

	first = find_insn(file, sym->sec, sym->offset);
	if (!first)
		return false;

	if (first->type != INSN_ENDBR && !first->noendbr)
		return false;

	return insn->offset == sym->offset + sym->len;
}

static int __validate_ibt_insn(struct objtool_file *file, struct instruction *insn,
			       struct instruction *dest)
{
	if (dest->type == INSN_ENDBR) {
		mark_endbr_used(dest);
		return 0;
	}

	if (insn_func(dest) && insn_func(insn) &&
	    insn_func(dest)->pfunc == insn_func(insn)->pfunc) {
		/*
		 * Anything from->to self is either _THIS_IP_ or
		 * IRET-to-self.
		 *
		 * There is no sane way to annotate _THIS_IP_ since the
		 * compiler treats the relocation as a constant and is
		 * happy to fold in offsets, skewing any annotation we
		 * do, leading to vast amounts of false-positives.
		 *
		 * There's also compiler generated _THIS_IP_ through
		 * KCOV and such which we have no hope of annotating.
		 *
		 * As such, blanket accept self-references without
		 * issue.
		 */
		return 0;
	}

	/*
	 * Accept anything ANNOTATE_NOENDBR.
	 */
	if (dest->noendbr)
		return 0;

	/*
	 * Accept if this is the instruction after a symbol
	 * that is (no)endbr -- typical code-range usage.
	 */
	if (noendbr_range(file, dest))
		return 0;

	WARN_INSN(insn, "relocation to !ENDBR: %s", offstr(dest->sec, dest->offset));
	return 1;
}

static int validate_ibt_insn(struct objtool_file *file, struct instruction *insn)
{
	struct instruction *dest;
	struct reloc *reloc;
	unsigned long off;
	int warnings = 0;

	/*
	 * Looking for function pointer load relocations.  Ignore
	 * direct/indirect branches:
	 */
	switch (insn->type) {

	case INSN_CALL:
	case INSN_CALL_DYNAMIC:
	case INSN_JUMP_CONDITIONAL:
	case INSN_JUMP_UNCONDITIONAL:
	case INSN_JUMP_DYNAMIC:
	case INSN_JUMP_DYNAMIC_CONDITIONAL:
	case INSN_RETURN:
	case INSN_NOP:
		return 0;

	case INSN_LEA_RIP:
		if (!insn_reloc(file, insn)) {
			/* local function pointer reference without reloc */

			off = arch_jump_destination(insn);

			dest = find_insn(file, insn->sec, off);
			if (!dest) {
				WARN_INSN(insn, "corrupt function pointer reference");
				return 1;
			}

			return __validate_ibt_insn(file, insn, dest);
		}
		break;

	default:
		break;
	}

	for (reloc = insn_reloc(file, insn);
	     reloc;
	     reloc = find_reloc_by_dest_range(file->elf, insn->sec,
					      reloc_offset(reloc) + 1,
					      (insn->offset + insn->len) - (reloc_offset(reloc) + 1))) {

		off = reloc->sym->offset;
		if (reloc_type(reloc) == R_X86_64_PC32 ||
		    reloc_type(reloc) == R_X86_64_PLT32)
			off += arch_dest_reloc_offset(reloc_addend(reloc));
		else
			off += reloc_addend(reloc);

		dest = find_insn(file, reloc->sym->sec, off);
		if (!dest)
			continue;

		warnings += __validate_ibt_insn(file, insn, dest);
	}

	return warnings;
}

static int validate_ibt_data_reloc(struct objtool_file *file,
				   struct reloc *reloc)
{
	struct instruction *dest;

	dest = find_insn(file, reloc->sym->sec,
			 reloc->sym->offset + reloc_addend(reloc));
	if (!dest)
		return 0;

	if (dest->type == INSN_ENDBR) {
		mark_endbr_used(dest);
		return 0;
	}

	if (dest->noendbr)
		return 0;

	WARN_FUNC("data relocation to !ENDBR: %s",
		  reloc->sec->base, reloc_offset(reloc),
		  offstr(dest->sec, dest->offset));

	return 1;
}

/*
 * Validate IBT rules and remove used ENDBR instructions from the seal list.
 * Unused ENDBR instructions will be annotated for sealing (i.e., replaced with
 * NOPs) later, in create_ibt_endbr_seal_sections().
 */
static int validate_ibt(struct objtool_file *file)
{
	struct section *sec;
	struct reloc *reloc;
	struct instruction *insn;
	int warnings = 0;

	for_each_insn(file, insn)
		warnings += validate_ibt_insn(file, insn);

	for_each_sec(file, sec) {

		/* Already done by validate_ibt_insn() */
		if (sec->sh.sh_flags & SHF_EXECINSTR)
			continue;

		if (!sec->rsec)
			continue;

		/*
		 * These sections can reference text addresses, but not with
		 * the intent to indirect branch to them.
		 */
		if ((!strncmp(sec->name, ".discard", 8) &&
		     strcmp(sec->name, ".discard.ibt_endbr_noseal"))	||
		    !strncmp(sec->name, ".debug", 6)			||
		    !strcmp(sec->name, ".altinstructions")		||
		    !strcmp(sec->name, ".ibt_endbr_seal")		||
		    !strcmp(sec->name, ".orc_unwind_ip")		||
		    !strcmp(sec->name, ".parainstructions")		||
		    !strcmp(sec->name, ".retpoline_sites")		||
		    !strcmp(sec->name, ".smp_locks")			||
		    !strcmp(sec->name, ".static_call_sites")		||
		    !strcmp(sec->name, "_error_injection_whitelist")	||
		    !strcmp(sec->name, "_kprobe_blacklist")		||
		    !strcmp(sec->name, "__bug_table")			||
		    !strcmp(sec->name, "__ex_table")			||
		    !strcmp(sec->name, "__jump_table")			||
		    !strcmp(sec->name, "__mcount_loc")			||
		    !strcmp(sec->name, ".kcfi_traps")			||
		    !strcmp(sec->name, ".llvm.call-graph-profile")	||
		    !strcmp(sec->name, ".llvm_bb_addr_map")		||
		    !strcmp(sec->name, "__tracepoints")			||
		    strstr(sec->name, "__patchable_function_entries"))
			continue;

		for_each_reloc(sec->rsec, reloc)
			warnings += validate_ibt_data_reloc(file, reloc);
	}

	return warnings;
}

static int validate_sls(struct objtool_file *file)
{
	struct instruction *insn, *next_insn;
	int warnings = 0;

	for_each_insn(file, insn) {
		next_insn = next_insn_same_sec(file, insn);

		if (insn->retpoline_safe)
			continue;

		switch (insn->type) {
		case INSN_RETURN:
			if (!next_insn || next_insn->type != INSN_TRAP) {
				WARN_INSN(insn, "missing int3 after ret");
				warnings++;
			}

			break;
		case INSN_JUMP_DYNAMIC:
			if (!next_insn || next_insn->type != INSN_TRAP) {
				WARN_INSN(insn, "missing int3 after indirect jump");
				warnings++;
			}
			break;
		default:
			break;
		}
	}

	return warnings;
}

static int validate_reachable_instructions(struct objtool_file *file)
{
	struct instruction *insn, *prev_insn;
	struct symbol *call_dest;
	int warnings = 0;

	if (file->ignore_unreachables)
		return 0;

	for_each_insn(file, insn) {
		if (insn->visited || ignore_unreachable_insn(file, insn))
			continue;

		prev_insn = prev_insn_same_sec(file, insn);
		if (prev_insn && prev_insn->dead_end) {
			call_dest = insn_call_dest(prev_insn);
			if (call_dest) {
				WARN_INSN(insn, "%s() missing __noreturn in .c/.h or NORETURN() in noreturns.h",
					  call_dest->name);
				warnings++;
				continue;
			}
		}

		WARN_INSN(insn, "unreachable instruction");
		warnings++;
	}

	return warnings;
}

/* 'funcs' is a space-separated list of function names */
static int disas_funcs(const char *funcs)
{
	const char *objdump_str, *cross_compile;
	int size, ret;
	char *cmd;

	cross_compile = getenv("CROSS_COMPILE");

	objdump_str = "%sobjdump -wdr %s | gawk -M -v _funcs='%s' '"
			"BEGIN { split(_funcs, funcs); }"
			"/^$/ { func_match = 0; }"
			"/<.*>:/ { "
				"f = gensub(/.*<(.*)>:/, \"\\\\1\", 1);"
				"for (i in funcs) {"
					"if (funcs[i] == f) {"
						"func_match = 1;"
						"base = strtonum(\"0x\" $1);"
						"break;"
					"}"
				"}"
			"}"
			"{"
				"if (func_match) {"
					"addr = strtonum(\"0x\" $1);"
					"printf(\"%%04x \", addr - base);"
					"print;"
				"}"
			"}' 1>&2";

	/* fake snprintf() to calculate the size */
	size = snprintf(NULL, 0, objdump_str, cross_compile, objname, funcs) + 1;
	if (size <= 0) {
		WARN("objdump string size calculation failed");
		return -1;
	}

	cmd = malloc(size);

	/* real snprintf() */
	snprintf(cmd, size, objdump_str, cross_compile, objname, funcs);
	ret = system(cmd);
	if (ret) {
		WARN("disassembly failed: %d", ret);
		return -1;
	}

	return 0;
}

static int disas_warned_funcs(struct objtool_file *file)
{
	struct symbol *sym;
	char *funcs = NULL, *tmp;

	for_each_sym(file, sym) {
		if (sym->warnings) {
			if (!funcs) {
				funcs = malloc(strlen(sym->name) + 1);
				strcpy(funcs, sym->name);
			} else {
				tmp = malloc(strlen(funcs) + strlen(sym->name) + 2);
				sprintf(tmp, "%s %s", funcs, sym->name);
				free(funcs);
				funcs = tmp;
			}
		}
	}

	if (funcs)
		disas_funcs(funcs);

	return 0;
}

struct insn_chunk {
	void *addr;
	struct insn_chunk *next;
};

/*
 * Reduce peak RSS usage by freeing insns memory before writing the ELF file,
 * which can trigger more allocations for .debug_* sections whose data hasn't
 * been read yet.
 */
static void free_insns(struct objtool_file *file)
{
	struct instruction *insn;
	struct insn_chunk *chunks = NULL, *chunk;

	for_each_insn(file, insn) {
		if (!insn->idx) {
			chunk = malloc(sizeof(*chunk));
			chunk->addr = insn;
			chunk->next = chunks;
			chunks = chunk;
		}
	}

	for (chunk = chunks; chunk; chunk = chunk->next)
		free(chunk->addr);
}

int check(struct objtool_file *file)
{
	int ret, warnings = 0;

	arch_initial_func_cfi_state(&initial_func_cfi);
	init_cfi_state(&init_cfi);
	init_cfi_state(&func_cfi);
	set_func_state(&func_cfi);
	init_cfi_state(&force_undefined_cfi);
	force_undefined_cfi.force_undefined = true;

	if (!cfi_hash_alloc(1UL << (file->elf->symbol_bits - 3))) {
		ret = -1;
		goto out;
	}

	cfi_hash_add(&init_cfi);
	cfi_hash_add(&func_cfi);

	ret = decode_sections(file);
	if (ret < 0)
		goto out;

	warnings += ret;

	if (!nr_insns)
		goto out;

	if (opts.retpoline) {
		ret = validate_retpoline(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.stackval || opts.orc || opts.uaccess) {
		ret = validate_functions(file);
		if (ret < 0)
			goto out;
		warnings += ret;

		ret = validate_unwind_hints(file, NULL);
		if (ret < 0)
			goto out;
		warnings += ret;

		if (!warnings) {
			ret = validate_reachable_instructions(file);
			if (ret < 0)
				goto out;
			warnings += ret;
		}

	} else if (opts.noinstr) {
		ret = validate_noinstr_sections(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.unret) {
		/*
		 * Must be after validate_branch() and friends, it plays
		 * further games with insn->visited.
		 */
		ret = validate_unrets(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.ibt) {
		ret = validate_ibt(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.sls) {
		ret = validate_sls(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.static_call) {
		ret = create_static_call_sections(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.retpoline) {
		ret = create_retpoline_sites_sections(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.cfi) {
		ret = create_cfi_sections(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.rethunk) {
		ret = create_return_sites_sections(file);
		if (ret < 0)
			goto out;
		warnings += ret;

		if (opts.hack_skylake) {
			ret = create_direct_call_sections(file);
			if (ret < 0)
				goto out;
			warnings += ret;
		}
	}

	if (opts.mcount) {
		ret = create_mcount_loc_sections(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.prefix) {
		ret = add_prefix_symbols(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.ibt) {
		ret = create_ibt_endbr_seal_sections(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	if (opts.orc && nr_insns) {
		ret = orc_create(file);
		if (ret < 0)
			goto out;
		warnings += ret;
	}

	free_insns(file);

	if (opts.verbose)
		disas_warned_funcs(file);

	if (opts.stats) {
		printf("nr_insns_visited: %ld\n", nr_insns_visited);
		printf("nr_cfi: %ld\n", nr_cfi);
		printf("nr_cfi_reused: %ld\n", nr_cfi_reused);
		printf("nr_cfi_cache: %ld\n", nr_cfi_cache);
	}

out:
	/*
	 * CONFIG_OBJTOOL_WERROR upgrades all warnings (and errors) to actual
	 * errors.
	 *
	 * Note that even "fatal" type errors don't actually return an error
	 * without CONFIG_OBJTOOL_WERROR.  That probably needs improved at some
	 * point.
	 */
	if (opts.werror && (ret || warnings)) {
		if (warnings)
			WARN("%d warning(s) upgraded to errors", warnings);
		return 1;
	}

	return 0;
}
