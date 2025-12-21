// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE /* memmem() */
#include <subcmd/parse-options.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <stdio.h>
#include <ctype.h>

#include <objtool/objtool.h>
#include <objtool/warn.h>
#include <objtool/arch.h>
#include <objtool/klp.h>
#include <objtool/util.h>
#include <arch/special.h>

#include <linux/objtool_types.h>
#include <linux/livepatch_external.h>
#include <linux/stringify.h>
#include <linux/string.h>
#include <linux/jhash.h>

#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

struct elfs {
	struct elf *orig, *patched, *out;
	const char *modname;
};

struct export {
	struct hlist_node hash;
	char *mod, *sym;
};

static const char * const klp_diff_usage[] = {
	"objtool klp diff [<options>] <in1.o> <in2.o> <out.o>",
	NULL,
};

static const struct option klp_diff_options[] = {
	OPT_GROUP("Options:"),
	OPT_BOOLEAN('d', "debug", &debug, "enable debug output"),
	OPT_END(),
};

static DEFINE_HASHTABLE(exports, 15);

static inline u32 str_hash(const char *str)
{
	return jhash(str, strlen(str), 0);
}

static char *escape_str(const char *orig)
{
	size_t len = 0;
	const char *a;
	char *b, *new;

	for (a = orig; *a; a++) {
		switch (*a) {
		case '\001': len += 5; break;
		case '\n':
		case '\t':   len += 2; break;
		default: len++;
		}
	}

	new = malloc(len + 1);
	if (!new)
		return NULL;

	for (a = orig, b = new; *a; a++) {
		switch (*a) {
		case '\001': memcpy(b, "<SOH>", 5); b += 5; break;
		case '\n': *b++ = '\\'; *b++ = 'n'; break;
		case '\t': *b++ = '\\'; *b++ = 't'; break;
		default:   *b++ = *a;
		}
	}

	*b = '\0';
	return new;
}

static int read_exports(void)
{
	const char *symvers = "Module.symvers";
	char line[1024], *path = NULL;
	unsigned int line_num = 1;
	FILE *file;

	file = fopen(symvers, "r");
	if (!file) {
		path = top_level_dir(symvers);
		if (!path) {
			ERROR("can't open '%s', \"objtool diff\" should be run from the kernel tree", symvers);
			return -1;
		}

		file = fopen(path, "r");
		if (!file) {
			ERROR_GLIBC("fopen");
			return -1;
		}
	}

	while (fgets(line, 1024, file)) {
		char *sym, *mod, *type;
		struct export *export;

		sym = strchr(line, '\t');
		if (!sym) {
			ERROR("malformed Module.symvers (sym) at line %d", line_num);
			return -1;
		}

		*sym++ = '\0';

		mod = strchr(sym, '\t');
		if (!mod) {
			ERROR("malformed Module.symvers (mod) at line %d", line_num);
			return -1;
		}

		*mod++ = '\0';

		type = strchr(mod, '\t');
		if (!type) {
			ERROR("malformed Module.symvers (type) at line %d", line_num);
			return -1;
		}

		*type++ = '\0';

		if (*sym == '\0' || *mod == '\0') {
			ERROR("malformed Module.symvers at line %d", line_num);
			return -1;
		}

		export = calloc(1, sizeof(*export));
		if (!export) {
			ERROR_GLIBC("calloc");
			return -1;
		}

		export->mod = strdup(mod);
		if (!export->mod) {
			ERROR_GLIBC("strdup");
			return -1;
		}

		export->sym = strdup(sym);
		if (!export->sym) {
			ERROR_GLIBC("strdup");
			return -1;
		}

		hash_add(exports, &export->hash, str_hash(sym));
	}

	free(path);
	fclose(file);

	return 0;
}

static int read_sym_checksums(struct elf *elf)
{
	struct section *sec;

	sec = find_section_by_name(elf, ".discard.sym_checksum");
	if (!sec) {
		ERROR("'%s' missing .discard.sym_checksum section, file not processed by 'objtool --checksum'?",
		      elf->name);
		return -1;
	}

	if (!sec->rsec) {
		ERROR("missing reloc section for .discard.sym_checksum");
		return -1;
	}

	if (sec_size(sec) % sizeof(struct sym_checksum)) {
		ERROR("struct sym_checksum size mismatch");
		return -1;
	}

	for (int i = 0; i < sec_size(sec) / sizeof(struct sym_checksum); i++) {
		struct sym_checksum *sym_checksum;
		struct reloc *reloc;
		struct symbol *sym;

		sym_checksum = (struct sym_checksum *)sec->data->d_buf + i;

		reloc = find_reloc_by_dest(elf, sec, i * sizeof(*sym_checksum));
		if (!reloc) {
			ERROR("can't find reloc for sym_checksum[%d]", i);
			return -1;
		}

		sym = reloc->sym;

		if (is_sec_sym(sym)) {
			ERROR("not sure how to handle section %s", sym->name);
			return -1;
		}

		if (is_func_sym(sym))
			sym->csum.checksum = sym_checksum->checksum;
	}

	return 0;
}

static struct symbol *first_file_symbol(struct elf *elf)
{
	struct symbol *sym;

	for_each_sym(elf, sym) {
		if (is_file_sym(sym))
			return sym;
	}

	return NULL;
}

static struct symbol *next_file_symbol(struct elf *elf, struct symbol *sym)
{
	for_each_sym_continue(elf, sym) {
		if (is_file_sym(sym))
			return sym;
	}

	return NULL;
}

/*
 * Certain static local variables should never be correlated.  They will be
 * used in place rather than referencing the originals.
 */
static bool is_uncorrelated_static_local(struct symbol *sym)
{
	static const char * const vars[] = {
		"__already_done.",
		"__func__.",
		"__key.",
		"__warned.",
		"_entry.",
		"_entry_ptr.",
		"_rs.",
		"descriptor.",
		"CSWTCH.",
	};

	if (!is_object_sym(sym) || !is_local_sym(sym))
		return false;

	if (!strcmp(sym->sec->name, ".data.once"))
		return true;

	for (int i = 0; i < ARRAY_SIZE(vars); i++) {
		if (strstarts(sym->name, vars[i]))
			return true;
	}

	return false;
}

/*
 * Clang emits several useless .Ltmp_* code labels.
 */
static bool is_clang_tmp_label(struct symbol *sym)
{
	return sym->type == STT_NOTYPE &&
	       is_text_sec(sym->sec) &&
	       strstarts(sym->name, ".Ltmp") &&
	       isdigit(sym->name[5]);
}

static bool is_special_section(struct section *sec)
{
	static const char * const specials[] = {
		".altinstructions",
		".smp_locks",
		"__bug_table",
		"__ex_table",
		"__jump_table",
		"__mcount_loc",

		/*
		 * Extract .static_call_sites here to inherit non-module
		 * preferential treatment.  The later static call processing
		 * during klp module build will be skipped when it sees this
		 * section already exists.
		 */
		".static_call_sites",
	};

	static const char * const non_special_discards[] = {
		".discard.addressable",
		".discard.sym_checksum",
	};

	if (is_text_sec(sec))
		return false;

	for (int i = 0; i < ARRAY_SIZE(specials); i++) {
		if (!strcmp(sec->name, specials[i]))
			return true;
	}

	/* Most .discard data sections are special */
	for (int i = 0; i < ARRAY_SIZE(non_special_discards); i++) {
		if (!strcmp(sec->name, non_special_discards[i]))
			return false;
	}

	return strstarts(sec->name, ".discard.");
}

/*
 * These sections are referenced by special sections but aren't considered
 * special sections themselves.
 */
static bool is_special_section_aux(struct section *sec)
{
	static const char * const specials_aux[] = {
		".altinstr_replacement",
		".altinstr_aux",
	};

	for (int i = 0; i < ARRAY_SIZE(specials_aux); i++) {
		if (!strcmp(sec->name, specials_aux[i]))
			return true;
	}

	return false;
}

/*
 * These symbols should never be correlated, so their local patched versions
 * are used instead of linking to the originals.
 */
static bool dont_correlate(struct symbol *sym)
{
	return is_file_sym(sym) ||
	       is_null_sym(sym) ||
	       is_sec_sym(sym) ||
	       is_prefix_func(sym) ||
	       is_uncorrelated_static_local(sym) ||
	       is_clang_tmp_label(sym) ||
	       is_string_sec(sym->sec) ||
	       is_special_section(sym->sec) ||
	       is_special_section_aux(sym->sec) ||
	       strstarts(sym->name, "__initcall__");
}

/*
 * For each symbol in the original kernel, find its corresponding "twin" in the
 * patched kernel.
 */
static int correlate_symbols(struct elfs *e)
{
	struct symbol *file1_sym, *file2_sym;
	struct symbol *sym1, *sym2;

	/* Correlate locals */
	for (file1_sym = first_file_symbol(e->orig),
	     file2_sym = first_file_symbol(e->patched); ;
	     file1_sym = next_file_symbol(e->orig, file1_sym),
	     file2_sym = next_file_symbol(e->patched, file2_sym)) {

		if (!file1_sym && file2_sym) {
			ERROR("FILE symbol mismatch: NULL != %s", file2_sym->name);
			return -1;
		}

		if (file1_sym && !file2_sym) {
			ERROR("FILE symbol mismatch: %s != NULL", file1_sym->name);
			return -1;
		}

		if (!file1_sym)
			break;

		if (strcmp(file1_sym->name, file2_sym->name)) {
			ERROR("FILE symbol mismatch: %s != %s", file1_sym->name, file2_sym->name);
			return -1;
		}

		file1_sym->twin = file2_sym;
		file2_sym->twin = file1_sym;

		sym1 = file1_sym;

		for_each_sym_continue(e->orig, sym1) {
			if (is_file_sym(sym1) || !is_local_sym(sym1))
				break;

			if (dont_correlate(sym1))
				continue;

			sym2 = file2_sym;
			for_each_sym_continue(e->patched, sym2) {
				if (is_file_sym(sym2) || !is_local_sym(sym2))
					break;

				if (sym2->twin || dont_correlate(sym2))
					continue;

				if (strcmp(sym1->demangled_name, sym2->demangled_name))
					continue;

				sym1->twin = sym2;
				sym2->twin = sym1;
				break;
			}
		}
	}

	/* Correlate globals */
	for_each_sym(e->orig, sym1) {
		if (sym1->bind == STB_LOCAL)
			continue;

		sym2 = find_global_symbol_by_name(e->patched, sym1->name);

		if (sym2 && !sym2->twin && !strcmp(sym1->name, sym2->name)) {
			sym1->twin = sym2;
			sym2->twin = sym1;
		}
	}

	for_each_sym(e->orig, sym1) {
		if (sym1->twin || dont_correlate(sym1))
			continue;
		WARN("no correlation: %s", sym1->name);
	}

	return 0;
}

/* "sympos" is used by livepatch to disambiguate duplicate symbol names */
static unsigned long find_sympos(struct elf *elf, struct symbol *sym)
{
	bool vmlinux = str_ends_with(objname, "vmlinux.o");
	unsigned long sympos = 0, nr_matches = 0;
	bool has_dup = false;
	struct symbol *s;

	if (sym->bind != STB_LOCAL)
		return 0;

	if (vmlinux && sym->type == STT_FUNC) {
		/*
		 * HACK: Unfortunately, symbol ordering can differ between
		 * vmlinux.o and vmlinux due to the linker script emitting
		 * .text.unlikely* before .text*.  Count .text.unlikely* first.
		 *
		 * TODO: Disambiguate symbols more reliably (checksums?)
		 */
		for_each_sym(elf, s) {
			if (strstarts(s->sec->name, ".text.unlikely") &&
			    !strcmp(s->name, sym->name)) {
				nr_matches++;
				if (s == sym)
					sympos = nr_matches;
				else
					has_dup = true;
			}
		}
		for_each_sym(elf, s) {
			if (!strstarts(s->sec->name, ".text.unlikely") &&
			    !strcmp(s->name, sym->name)) {
				nr_matches++;
				if (s == sym)
					sympos = nr_matches;
				else
					has_dup = true;
			}
		}
	} else {
		for_each_sym(elf, s) {
			if (!strcmp(s->name, sym->name)) {
				nr_matches++;
				if (s == sym)
					sympos = nr_matches;
				else
					has_dup = true;
			}
		}
	}

	if (!sympos) {
		ERROR("can't find sympos for %s", sym->name);
		return ULONG_MAX;
	}

	return has_dup ? sympos : 0;
}

static int clone_sym_relocs(struct elfs *e, struct symbol *patched_sym);

static struct symbol *__clone_symbol(struct elf *elf, struct symbol *patched_sym,
				     bool data_too)
{
	struct section *out_sec = NULL;
	unsigned long offset = 0;
	struct symbol *out_sym;

	if (data_too && !is_undef_sym(patched_sym)) {
		struct section *patched_sec = patched_sym->sec;

		out_sec = find_section_by_name(elf, patched_sec->name);
		if (!out_sec) {
			out_sec = elf_create_section(elf, patched_sec->name, 0,
						     patched_sec->sh.sh_entsize,
						     patched_sec->sh.sh_type,
						     patched_sec->sh.sh_addralign,
						     patched_sec->sh.sh_flags);
			if (!out_sec)
				return NULL;
		}

		if (is_string_sec(patched_sym->sec)) {
			out_sym = elf_create_section_symbol(elf, out_sec);
			if (!out_sym)
				return NULL;

			goto sym_created;
		}

		if (!is_sec_sym(patched_sym))
			offset = sec_size(out_sec);

		if (patched_sym->len || is_sec_sym(patched_sym)) {
			void *data = NULL;
			size_t size;

			/* bss doesn't have data */
			if (patched_sym->sec->data->d_buf)
				data = patched_sym->sec->data->d_buf + patched_sym->offset;

			if (is_sec_sym(patched_sym))
				size = sec_size(patched_sym->sec);
			else
				size = patched_sym->len;

			if (!elf_add_data(elf, out_sec, data, size))
				return NULL;
		}
	}

	out_sym = elf_create_symbol(elf, patched_sym->name, out_sec,
				    patched_sym->bind, patched_sym->type,
				    offset, patched_sym->len);
	if (!out_sym)
		return NULL;

sym_created:
	patched_sym->clone = out_sym;
	out_sym->clone = patched_sym;

	return out_sym;
}

static const char *sym_type(struct symbol *sym)
{
	switch (sym->type) {
	case STT_NOTYPE:  return "NOTYPE";
	case STT_OBJECT:  return "OBJECT";
	case STT_FUNC:    return "FUNC";
	case STT_SECTION: return "SECTION";
	case STT_FILE:    return "FILE";
	default:	  return "UNKNOWN";
	}
}

static const char *sym_bind(struct symbol *sym)
{
	switch (sym->bind) {
	case STB_LOCAL:   return "LOCAL";
	case STB_GLOBAL:  return "GLOBAL";
	case STB_WEAK:    return "WEAK";
	default:	  return "UNKNOWN";
	}
}

/*
 * Copy a symbol to the output object, optionally including its data and
 * relocations.
 */
static struct symbol *clone_symbol(struct elfs *e, struct symbol *patched_sym,
				   bool data_too)
{
	struct symbol *pfx;

	if (patched_sym->clone)
		return patched_sym->clone;

	dbg_indent("%s%s", patched_sym->name, data_too ? " [+DATA]" : "");

	/* Make sure the prefix gets cloned first */
	if (is_func_sym(patched_sym) && data_too) {
		pfx = get_func_prefix(patched_sym);
		if (pfx)
			clone_symbol(e, pfx, true);
	}

	if (!__clone_symbol(e->out, patched_sym, data_too))
		return NULL;

	if (data_too && clone_sym_relocs(e, patched_sym))
		return NULL;

	return patched_sym->clone;
}

static void mark_included_function(struct symbol *func)
{
	struct symbol *pfx;

	func->included = 1;

	/* Include prefix function */
	pfx = get_func_prefix(func);
	if (pfx)
		pfx->included = 1;

	/* Make sure .cold parent+child always stay together */
	if (func->cfunc && func->cfunc != func)
		func->cfunc->included = 1;
	if (func->pfunc && func->pfunc != func)
		func->pfunc->included = 1;
}

/*
 * Copy all changed functions (and their dependencies) from the patched object
 * to the output object.
 */
static int mark_changed_functions(struct elfs *e)
{
	struct symbol *sym_orig, *patched_sym;
	bool changed = false;

	/* Find changed functions */
	for_each_sym(e->orig, sym_orig) {
		if (!is_func_sym(sym_orig) || is_prefix_func(sym_orig))
			continue;

		patched_sym = sym_orig->twin;
		if (!patched_sym)
			continue;

		if (sym_orig->csum.checksum != patched_sym->csum.checksum) {
			patched_sym->changed = 1;
			mark_included_function(patched_sym);
			changed = true;
		}
	}

	/* Find added functions and print them */
	for_each_sym(e->patched, patched_sym) {
		if (!is_func_sym(patched_sym) || is_prefix_func(patched_sym))
			continue;

		if (!patched_sym->twin) {
			printf("%s: new function: %s\n", objname, patched_sym->name);
			mark_included_function(patched_sym);
			changed = true;
		}
	}

	/* Print changed functions */
	for_each_sym(e->patched, patched_sym) {
		if (patched_sym->changed)
			printf("%s: changed function: %s\n", objname, patched_sym->name);
	}

	return !changed ? -1 : 0;
}

static int clone_included_functions(struct elfs *e)
{
	struct symbol *patched_sym;

	for_each_sym(e->patched, patched_sym) {
		if (patched_sym->included) {
			if (!clone_symbol(e, patched_sym, true))
				return -1;
		}
	}

	return 0;
}

/*
 * Determine whether a relocation should reference the section rather than the
 * underlying symbol.
 */
static bool section_reference_needed(struct section *sec)
{
	/*
	 * String symbols are zero-length and uncorrelated.  It's easier to
	 * deal with them as section symbols.
	 */
	if (is_string_sec(sec))
		return true;

	/*
	 * .rodata has mostly anonymous data so there's no way to determine the
	 * length of a needed reference.  just copy the whole section if needed.
	 */
	if (strstarts(sec->name, ".rodata"))
		return true;

	/* UBSAN anonymous data */
	if (strstarts(sec->name, ".data..Lubsan") ||	/* GCC */
	    strstarts(sec->name, ".data..L__unnamed_"))	/* Clang */
		return true;

	return false;
}

static bool is_reloc_allowed(struct reloc *reloc)
{
	return section_reference_needed(reloc->sym->sec) == is_sec_sym(reloc->sym);
}

static struct export *find_export(struct symbol *sym)
{
	struct export *export;

	hash_for_each_possible(exports, export, hash, str_hash(sym->name)) {
		if (!strcmp(export->sym, sym->name))
			return export;
	}

	return NULL;
}

static const char *__find_modname(struct elfs *e)
{
	struct section *sec;
	char *name;

	sec = find_section_by_name(e->orig, ".modinfo");
	if (!sec) {
		ERROR("missing .modinfo section");
		return NULL;
	}

	name = memmem(sec->data->d_buf, sec_size(sec), "\0name=", 6);
	if (name)
		return name + 6;

	name = strdup(e->orig->name);
	if (!name) {
		ERROR_GLIBC("strdup");
		return NULL;
	}

	for (char *c = name; *c; c++) {
		if (*c == '/')
			name = c + 1;
		else if (*c == '-')
			*c = '_';
		else if (*c == '.') {
			*c = '\0';
			break;
		}
	}

	return name;
}

/* Get the object's module name as defined by the kernel (and klp_object) */
static const char *find_modname(struct elfs *e)
{
	const char *modname;

	if (e->modname)
		return e->modname;

	modname = __find_modname(e);
	e->modname = modname;
	return modname;
}

/*
 * Copying a function from its native compiled environment to a kernel module
 * removes its natural access to local functions/variables and unexported
 * globals.  References to such symbols need to be converted to KLP relocs so
 * the kernel arch relocation code knows to apply them and where to find the
 * symbols.  Particularly, duplicate static symbols need to be disambiguated.
 */
static bool klp_reloc_needed(struct reloc *patched_reloc)
{
	struct symbol *patched_sym = patched_reloc->sym;
	struct export *export;

	/* no external symbol to reference */
	if (dont_correlate(patched_sym))
		return false;

	/* For included functions, a regular reloc will do. */
	if (patched_sym->included)
		return false;

	/*
	 * If exported by a module, it has to be a klp reloc.  Thanks to the
	 * clusterfunk that is late module patching, the patch module is
	 * allowed to be loaded before any modules it depends on.
	 *
	 * If exported by vmlinux, a normal reloc will do.
	 */
	export = find_export(patched_sym);
	if (export)
		return strcmp(export->mod, "vmlinux");

	if (!patched_sym->twin) {
		/*
		 * Presumably the symbol and its reference were added by the
		 * patch.  The symbol could be defined in this .o or in another
		 * .o in the patch module.
		 *
		 * This check needs to be *after* the export check due to the
		 * possibility of the patch adding a new UNDEF reference to an
		 * exported symbol.
		 */
		return false;
	}

	/* Unexported symbol which lives in the original vmlinux or module. */
	return true;
}

static int convert_reloc_sym_to_secsym(struct elf *elf, struct reloc *reloc)
{
	struct symbol *sym = reloc->sym;
	struct section *sec = sym->sec;

	if (!sec->sym && !elf_create_section_symbol(elf, sec))
		return -1;

	reloc->sym = sec->sym;
	set_reloc_sym(elf, reloc, sym->idx);
	set_reloc_addend(elf, reloc, sym->offset + reloc_addend(reloc));
	return 0;
}

static int convert_reloc_secsym_to_sym(struct elf *elf, struct reloc *reloc)
{
	struct symbol *sym = reloc->sym;
	struct section *sec = sym->sec;

	/* If the symbol has a dedicated section, it's easy to find */
	sym = find_symbol_by_offset(sec, 0);
	if (sym && sym->len == sec_size(sec))
		goto found_sym;

	/* No dedicated section; find the symbol manually */
	sym = find_symbol_containing(sec, arch_adjusted_addend(reloc));
	if (!sym) {
		/*
		 * This can happen for special section references to weak code
		 * whose symbol has been stripped by the linker.
		 */
		return -1;
	}

found_sym:
	reloc->sym = sym;
	set_reloc_sym(elf, reloc, sym->idx);
	set_reloc_addend(elf, reloc, reloc_addend(reloc) - sym->offset);
	return 0;
}

/*
 * Convert a relocation symbol reference to the needed format: either a section
 * symbol or the underlying symbol itself.
 */
static int convert_reloc_sym(struct elf *elf, struct reloc *reloc)
{
	if (is_reloc_allowed(reloc))
		return 0;

	if (section_reference_needed(reloc->sym->sec))
		return convert_reloc_sym_to_secsym(elf, reloc);
	else
		return convert_reloc_secsym_to_sym(elf, reloc);
}

/*
 * Convert a regular relocation to a klp relocation (sort of).
 */
static int clone_reloc_klp(struct elfs *e, struct reloc *patched_reloc,
			   struct section *sec, unsigned long offset,
			   struct export *export)
{
	struct symbol *patched_sym = patched_reloc->sym;
	s64 addend = reloc_addend(patched_reloc);
	const char *sym_modname, *sym_orig_name;
	static struct section *klp_relocs;
	struct symbol *sym, *klp_sym;
	unsigned long klp_reloc_off;
	char sym_name[SYM_NAME_LEN];
	struct klp_reloc klp_reloc;
	unsigned long sympos;

	if (!patched_sym->twin) {
		ERROR("unexpected klp reloc for new symbol %s", patched_sym->name);
		return -1;
	}

	/*
	 * Keep the original reloc intact for now to avoid breaking objtool run
	 * which relies on proper relocations for many of its features.  This
	 * will be disabled later by "objtool klp post-link".
	 *
	 * Convert it to UNDEF (and WEAK to avoid modpost warnings).
	 */

	sym = patched_sym->clone;
	if (!sym) {
		/* STB_WEAK: avoid modpost undefined symbol warnings */
		sym = elf_create_symbol(e->out, patched_sym->name, NULL,
					STB_WEAK, patched_sym->type, 0, 0);
		if (!sym)
			return -1;

		patched_sym->clone = sym;
		sym->clone = patched_sym;
	}

	if (!elf_create_reloc(e->out, sec, offset, sym, addend, reloc_type(patched_reloc)))
		return -1;

	/*
	 * Create the KLP symbol.
	 */

	if (export) {
		sym_modname = export->mod;
		sym_orig_name = export->sym;
		sympos = 0;
	} else {
		sym_modname = find_modname(e);
		if (!sym_modname)
			return -1;

		sym_orig_name = patched_sym->twin->name;
		sympos = find_sympos(e->orig, patched_sym->twin);
		if (sympos == ULONG_MAX)
			return -1;
	}

	/* symbol format: .klp.sym.modname.sym_name,sympos */
	if (snprintf_check(sym_name, SYM_NAME_LEN, KLP_SYM_PREFIX "%s.%s,%ld",
		      sym_modname, sym_orig_name, sympos))
		return -1;

	klp_sym = find_symbol_by_name(e->out, sym_name);
	if (!klp_sym) {
		__dbg_indent("%s", sym_name);

		/* STB_WEAK: avoid modpost undefined symbol warnings */
		klp_sym = elf_create_symbol(e->out, sym_name, NULL,
					    STB_WEAK, patched_sym->type, 0, 0);
		if (!klp_sym)
			return -1;
	}

	/*
	 * Create the __klp_relocs entry.  This will be converted to an actual
	 * KLP rela by "objtool klp post-link".
	 *
	 * This intermediate step is necessary to prevent corruption by the
	 * linker, which doesn't know how to properly handle two rela sections
	 * applying to the same base section.
	 */

	if (!klp_relocs) {
		klp_relocs = elf_create_section(e->out, KLP_RELOCS_SEC, 0,
						0, SHT_PROGBITS, 8, SHF_ALLOC);
		if (!klp_relocs)
			return -1;
	}

	klp_reloc_off = sec_size(klp_relocs);
	memset(&klp_reloc, 0, sizeof(klp_reloc));

	klp_reloc.type = reloc_type(patched_reloc);
	if (!elf_add_data(e->out, klp_relocs, &klp_reloc, sizeof(klp_reloc)))
		return -1;

	/* klp_reloc.offset */
	if (!sec->sym && !elf_create_section_symbol(e->out, sec))
		return -1;

	if (!elf_create_reloc(e->out, klp_relocs,
			      klp_reloc_off + offsetof(struct klp_reloc, offset),
			      sec->sym, offset, R_ABS64))
		return -1;

	/* klp_reloc.sym */
	if (!elf_create_reloc(e->out, klp_relocs,
			      klp_reloc_off + offsetof(struct klp_reloc, sym),
			      klp_sym, addend, R_ABS64))
		return -1;

	return 0;
}

#define dbg_clone_reloc(sec, offset, patched_sym, addend, export, klp)			\
	dbg_indent("%s+0x%lx: %s%s0x%lx [%s%s%s%s%s%s]",				\
		   sec->name, offset, patched_sym->name,				\
		   addend >= 0 ? "+" : "-", labs(addend),				\
		   sym_type(patched_sym),						\
		   patched_sym->type == STT_SECTION ? "" : " ",				\
		   patched_sym->type == STT_SECTION ? "" : sym_bind(patched_sym),	\
		   is_undef_sym(patched_sym) ? " UNDEF" : "",				\
		   export ? " EXPORTED" : "",						\
		   klp ? " KLP" : "")

/* Copy a reloc and its symbol to the output object */
static int clone_reloc(struct elfs *e, struct reloc *patched_reloc,
			struct section *sec, unsigned long offset)
{
	struct symbol *patched_sym = patched_reloc->sym;
	struct export *export = find_export(patched_sym);
	long addend = reloc_addend(patched_reloc);
	struct symbol *out_sym;
	bool klp;

	if (!is_reloc_allowed(patched_reloc)) {
		ERROR_FUNC(patched_reloc->sec->base, reloc_offset(patched_reloc),
			   "missing symbol for reference to %s+%ld",
			   patched_sym->name, addend);
		return -1;
	}

	klp = klp_reloc_needed(patched_reloc);

	dbg_clone_reloc(sec, offset, patched_sym, addend, export, klp);

	if (klp) {
		if (clone_reloc_klp(e, patched_reloc, sec, offset, export))
			return -1;

		return 0;
	}

	/*
	 * Why !export sets 'data_too':
	 *
	 * Unexported non-klp symbols need to live in the patch module,
	 * otherwise there will be unresolved symbols.  Notably, this includes:
	 *
	 *   - New functions/data
	 *   - String sections
	 *   - Special section entries
	 *   - Uncorrelated static local variables
	 *   - UBSAN sections
	 */
	out_sym = clone_symbol(e, patched_sym, patched_sym->included || !export);
	if (!out_sym)
		return -1;

	/*
	 * For strings, all references use section symbols, thanks to
	 * section_reference_needed().  clone_symbol() has cloned an empty
	 * version of the string section.  Now copy the string itself.
	 */
	if (is_string_sec(patched_sym->sec)) {
		const char *str = patched_sym->sec->data->d_buf + addend;

		__dbg_indent("\"%s\"", escape_str(str));

		addend = elf_add_string(e->out, out_sym->sec, str);
		if (addend == -1)
			return -1;
	}

	if (!elf_create_reloc(e->out, sec, offset, out_sym, addend,
			      reloc_type(patched_reloc)))
		return -1;

	return 0;
}

/* Copy all relocs needed for a symbol's contents */
static int clone_sym_relocs(struct elfs *e, struct symbol *patched_sym)
{
	struct section *patched_rsec = patched_sym->sec->rsec;
	struct reloc *patched_reloc;
	unsigned long start, end;
	struct symbol *out_sym;

	out_sym = patched_sym->clone;
	if (!out_sym) {
		ERROR("no clone for %s", patched_sym->name);
		return -1;
	}

	if (!patched_rsec)
		return 0;

	if (!is_sec_sym(patched_sym) && !patched_sym->len)
		return 0;

	if (is_string_sec(patched_sym->sec))
		return 0;

	if (is_sec_sym(patched_sym)) {
		start = 0;
		end = sec_size(patched_sym->sec);
	} else {
		start = patched_sym->offset;
		end = start + patched_sym->len;
	}

	for_each_reloc(patched_rsec, patched_reloc) {
		unsigned long offset;

		if (reloc_offset(patched_reloc) < start ||
		    reloc_offset(patched_reloc) >= end)
			continue;

		/*
		 * Skip any reloc referencing .altinstr_aux.  Its code is
		 * always patched by alternatives.  See ALTERNATIVE_TERNARY().
		 */
		if (patched_reloc->sym->sec &&
		    !strcmp(patched_reloc->sym->sec->name, ".altinstr_aux"))
			continue;

		if (convert_reloc_sym(e->patched, patched_reloc)) {
			ERROR_FUNC(patched_rsec->base, reloc_offset(patched_reloc),
				   "failed to convert reloc sym '%s' to its proper format",
				   patched_reloc->sym->name);
			return -1;
		}

		offset = out_sym->offset + (reloc_offset(patched_reloc) - patched_sym->offset);

		if (clone_reloc(e, patched_reloc, out_sym->sec, offset))
			return -1;
	}
	return 0;

}

static int create_fake_symbol(struct elf *elf, struct section *sec,
			      unsigned long offset, size_t size)
{
	char name[SYM_NAME_LEN];
	unsigned int type;
	static int ctr;
	char *c;

	if (snprintf_check(name, SYM_NAME_LEN, "%s_%d", sec->name, ctr++))
		return -1;

	for (c = name; *c; c++)
		if (*c == '.')
			*c = '_';

	/*
	 * STT_NOTYPE: Prevent objtool from validating .altinstr_replacement
	 *	       while still allowing objdump to disassemble it.
	 */
	type = is_text_sec(sec) ? STT_NOTYPE : STT_OBJECT;
	return elf_create_symbol(elf, name, sec, STB_LOCAL, type, offset, size) ? 0 : -1;
}

/*
 * Special sections (alternatives, etc) are basically arrays of structs.
 * For all the special sections, create a symbol for each struct entry.  This
 * is a bit cumbersome, but it makes the extracting of the individual entries
 * much more straightforward.
 *
 * There are three ways to identify the entry sizes for a special section:
 *
 * 1) ELF section header sh_entsize: Ideally this would be used almost
 *    everywhere.  But unfortunately the toolchains make it difficult.  The
 *    assembler .[push]section directive syntax only takes entsize when
 *    combined with SHF_MERGE.  But Clang disallows combining SHF_MERGE with
 *    SHF_WRITE.  And some special sections do need to be writable.
 *
 *    Another place this wouldn't work is .altinstr_replacement, whose entries
 *    don't have a fixed size.
 *
 * 2) ANNOTATE_DATA_SPECIAL: This is a lightweight objtool annotation which
 *    points to the beginning of each entry.  The size of the entry is then
 *    inferred by the location of the subsequent annotation (or end of
 *    section).
 *
 * 3) Simple array of pointers: If the special section is just a basic array of
 *    pointers, the entry size can be inferred by the number of relocations.
 *    No annotations needed.
 *
 * Note I also tried to create per-entry symbols at the time of creation, in
 * the original [inline] asm.  Unfortunately, creating uniquely named symbols
 * is trickier than one might think, especially with Clang inline asm.  I
 * eventually just gave up trying to make that work, in favor of using
 * ANNOTATE_DATA_SPECIAL and creating the symbols here after the fact.
 */
static int create_fake_symbols(struct elf *elf)
{
	struct section *sec;
	struct reloc *reloc;

	/*
	 * 1) Make symbols for all the ANNOTATE_DATA_SPECIAL entries:
	 */

	sec = find_section_by_name(elf, ".discard.annotate_data");
	if (!sec || !sec->rsec)
		return 0;

	for_each_reloc(sec->rsec, reloc) {
		unsigned long offset, size;
		struct reloc *next_reloc;

		if (annotype(elf, sec, reloc) != ANNOTYPE_DATA_SPECIAL)
			continue;

		offset = reloc_addend(reloc);

		size = 0;
		next_reloc = reloc;
		for_each_reloc_continue(sec->rsec, next_reloc) {
			if (annotype(elf, sec, next_reloc) != ANNOTYPE_DATA_SPECIAL ||
			    next_reloc->sym->sec != reloc->sym->sec)
				continue;

			size = reloc_addend(next_reloc) - offset;
			break;
		}

		if (!size)
			size = sec_size(reloc->sym->sec) - offset;

		if (create_fake_symbol(elf, reloc->sym->sec, offset, size))
			return -1;
	}

	/*
	 * 2) Make symbols for sh_entsize, and simple arrays of pointers:
	 */

	for_each_sec(elf, sec) {
		unsigned int entry_size;
		unsigned long offset;

		if (!is_special_section(sec) || find_symbol_by_offset(sec, 0))
			continue;

		if (!sec->rsec) {
			ERROR("%s: missing special section relocations", sec->name);
			return -1;
		}

		entry_size = sec->sh.sh_entsize;
		if (!entry_size) {
			entry_size = arch_reloc_size(sec->rsec->relocs);
			if (sec_size(sec) != entry_size * sec_num_entries(sec->rsec)) {
				ERROR("%s: missing special section entsize or annotations", sec->name);
				return -1;
			}
		}

		for (offset = 0; offset < sec_size(sec); offset += entry_size) {
			if (create_fake_symbol(elf, sec, offset, entry_size))
				return -1;
		}
	}

	return 0;
}

/* Keep a special section entry if it references an included function */
static bool should_keep_special_sym(struct elf *elf, struct symbol *sym)
{
	struct reloc *reloc;

	if (is_sec_sym(sym) || !sym->sec->rsec)
		return false;

	sym_for_each_reloc(elf, sym, reloc) {
		if (convert_reloc_sym(elf, reloc))
			continue;

		if (is_func_sym(reloc->sym) && reloc->sym->included)
			return true;
	}

	return false;
}

/*
 * Klp relocations aren't allowed for __jump_table and .static_call_sites if
 * the referenced symbol lives in a kernel module, because such klp relocs may
 * be applied after static branch/call init, resulting in code corruption.
 *
 * Validate a special section entry to avoid that.  Note that an inert
 * tracepoint is harmless enough, in that case just skip the entry and print a
 * warning.  Otherwise, return an error.
 *
 * This is only a temporary limitation which will be fixed when livepatch adds
 * support for submodules: fully self-contained modules which are embedded in
 * the top-level livepatch module's data and which can be loaded on demand when
 * their corresponding to-be-patched module gets loaded.  Then klp relocs can
 * be retired.
 *
 * Return:
 *   -1: error: validation failed
 *    1: warning: tracepoint skipped
 *    0: success
 */
static int validate_special_section_klp_reloc(struct elfs *e, struct symbol *sym)
{
	bool static_branch = !strcmp(sym->sec->name, "__jump_table");
	bool static_call   = !strcmp(sym->sec->name, ".static_call_sites");
	struct symbol *code_sym = NULL;
	unsigned long code_offset = 0;
	struct reloc *reloc;
	int ret = 0;

	if (!static_branch && !static_call)
		return 0;

	sym_for_each_reloc(e->patched, sym, reloc) {
		const char *sym_modname;
		struct export *export;

		/* Static branch/call keys are always STT_OBJECT */
		if (reloc->sym->type != STT_OBJECT) {

			/* Save code location which can be printed below */
			if (reloc->sym->type == STT_FUNC && !code_sym) {
				code_sym = reloc->sym;
				code_offset = reloc_addend(reloc);
			}

			continue;
		}

		if (!klp_reloc_needed(reloc))
			continue;

		export = find_export(reloc->sym);
		if (export) {
			sym_modname = export->mod;
		} else {
			sym_modname = find_modname(e);
			if (!sym_modname)
				return -1;
		}

		/* vmlinux keys are ok */
		if (!strcmp(sym_modname, "vmlinux"))
			continue;

		if (static_branch) {
			if (strstarts(reloc->sym->name, "__tracepoint_")) {
				WARN("%s: disabling unsupported tracepoint %s",
				     code_sym->name, reloc->sym->name + 13);
				ret = 1;
				continue;
			}

			ERROR("%s+0x%lx: unsupported static branch key %s.  Use static_key_enabled() instead",
			      code_sym->name, code_offset, reloc->sym->name);
			return -1;
		}

		/* static call */
		if (strstarts(reloc->sym->name, "__SCK__tp_func_")) {
			ret = 1;
			continue;
		}

		ERROR("%s()+0x%lx: unsupported static call key %s.  Use KLP_STATIC_CALL() instead",
		      code_sym->name, code_offset, reloc->sym->name);
		return -1;
	}

	return ret;
}

static int clone_special_section(struct elfs *e, struct section *patched_sec)
{
	struct symbol *patched_sym;

	/*
	 * Extract all special section symbols (and their dependencies) which
	 * reference included functions.
	 */
	sec_for_each_sym(patched_sec, patched_sym) {
		int ret;

		if (!is_object_sym(patched_sym))
			continue;

		if (!should_keep_special_sym(e->patched, patched_sym))
			continue;

		ret = validate_special_section_klp_reloc(e, patched_sym);
		if (ret < 0)
			return -1;
		if (ret > 0)
			continue;

		if (!clone_symbol(e, patched_sym, true))
			return -1;
	}

	return 0;
}

/* Extract only the needed bits from special sections */
static int clone_special_sections(struct elfs *e)
{
	struct section *patched_sec;

	if (create_fake_symbols(e->patched))
		return -1;

	for_each_sec(e->patched, patched_sec) {
		if (is_special_section(patched_sec)) {
			if (clone_special_section(e, patched_sec))
				return -1;
		}
	}

	return 0;
}

/*
 * Create __klp_objects and __klp_funcs sections which are intermediate
 * sections provided as input to the patch module's init code for building the
 * klp_patch, klp_object and klp_func structs for the livepatch API.
 */
static int create_klp_sections(struct elfs *e)
{
	size_t obj_size  = sizeof(struct klp_object_ext);
	size_t func_size = sizeof(struct klp_func_ext);
	struct section *obj_sec, *funcs_sec, *str_sec;
	struct symbol *funcs_sym, *str_sym, *sym;
	char sym_name[SYM_NAME_LEN];
	unsigned int nr_funcs = 0;
	const char *modname;
	void *obj_data;
	s64 addend;

	obj_sec  = elf_create_section_pair(e->out, KLP_OBJECTS_SEC, obj_size, 0, 0);
	if (!obj_sec)
		return -1;

	funcs_sec = elf_create_section_pair(e->out, KLP_FUNCS_SEC, func_size, 0, 0);
	if (!funcs_sec)
		return -1;

	funcs_sym = elf_create_section_symbol(e->out, funcs_sec);
	if (!funcs_sym)
		return -1;

	str_sec = elf_create_section(e->out, KLP_STRINGS_SEC, 0, 0,
				     SHT_PROGBITS, 1,
				     SHF_ALLOC | SHF_STRINGS | SHF_MERGE);
	if (!str_sec)
		return -1;

	if (elf_add_string(e->out, str_sec, "") == -1)
		return -1;

	str_sym = elf_create_section_symbol(e->out, str_sec);
	if (!str_sym)
		return -1;

	/* allocate klp_object_ext */
	obj_data = elf_add_data(e->out, obj_sec, NULL, obj_size);
	if (!obj_data)
		return -1;

	modname = find_modname(e);
	if (!modname)
		return -1;

	/* klp_object_ext.name */
	if (strcmp(modname, "vmlinux")) {
		addend = elf_add_string(e->out, str_sec, modname);
		if (addend == -1)
			return -1;

		if (!elf_create_reloc(e->out, obj_sec,
				      offsetof(struct klp_object_ext, name),
				      str_sym, addend, R_ABS64))
			return -1;
	}

	/* klp_object_ext.funcs */
	if (!elf_create_reloc(e->out, obj_sec, offsetof(struct klp_object_ext, funcs),
			      funcs_sym, 0, R_ABS64))
		return -1;

	for_each_sym(e->out, sym) {
		unsigned long offset = nr_funcs * func_size;
		unsigned long sympos;
		void *func_data;

		if (!is_func_sym(sym) || sym->cold || !sym->clone || !sym->clone->changed)
			continue;

		/* allocate klp_func_ext */
		func_data = elf_add_data(e->out, funcs_sec, NULL, func_size);
		if (!func_data)
			return -1;

		/* klp_func_ext.old_name */
		addend = elf_add_string(e->out, str_sec, sym->clone->twin->name);
		if (addend == -1)
			return -1;

		if (!elf_create_reloc(e->out, funcs_sec,
				      offset + offsetof(struct klp_func_ext, old_name),
				      str_sym, addend, R_ABS64))
			return -1;

		/* klp_func_ext.new_func */
		if (!elf_create_reloc(e->out, funcs_sec,
				      offset + offsetof(struct klp_func_ext, new_func),
				      sym, 0, R_ABS64))
			return -1;

		/* klp_func_ext.sympos */
		BUILD_BUG_ON(sizeof(sympos) != sizeof_field(struct klp_func_ext, sympos));
		sympos = find_sympos(e->orig, sym->clone->twin);
		if (sympos == ULONG_MAX)
			return -1;
		memcpy(func_data + offsetof(struct klp_func_ext, sympos), &sympos,
		       sizeof_field(struct klp_func_ext, sympos));

		nr_funcs++;
	}

	/* klp_object_ext.nr_funcs */
	BUILD_BUG_ON(sizeof(nr_funcs) != sizeof_field(struct klp_object_ext, nr_funcs));
	memcpy(obj_data + offsetof(struct klp_object_ext, nr_funcs), &nr_funcs,
	       sizeof_field(struct klp_object_ext, nr_funcs));

	/*
	 * Find callback pointers created by KLP_PRE_PATCH_CALLBACK() and
	 * friends, and add them to the klp object.
	 */

	if (snprintf_check(sym_name, SYM_NAME_LEN, KLP_PRE_PATCH_PREFIX "%s", modname))
		return -1;

	sym = find_symbol_by_name(e->out, sym_name);
	if (sym) {
		struct reloc *reloc;

		reloc = find_reloc_by_dest(e->out, sym->sec, sym->offset);

		if (!elf_create_reloc(e->out, obj_sec,
				      offsetof(struct klp_object_ext, callbacks) +
				      offsetof(struct klp_callbacks, pre_patch),
				      reloc->sym, reloc_addend(reloc), R_ABS64))
			return -1;
	}

	if (snprintf_check(sym_name, SYM_NAME_LEN, KLP_POST_PATCH_PREFIX "%s", modname))
		return -1;

	sym = find_symbol_by_name(e->out, sym_name);
	if (sym) {
		struct reloc *reloc;

		reloc = find_reloc_by_dest(e->out, sym->sec, sym->offset);

		if (!elf_create_reloc(e->out, obj_sec,
				      offsetof(struct klp_object_ext, callbacks) +
				      offsetof(struct klp_callbacks, post_patch),
				      reloc->sym, reloc_addend(reloc), R_ABS64))
			return -1;
	}

	if (snprintf_check(sym_name, SYM_NAME_LEN, KLP_PRE_UNPATCH_PREFIX "%s", modname))
		return -1;

	sym = find_symbol_by_name(e->out, sym_name);
	if (sym) {
		struct reloc *reloc;

		reloc = find_reloc_by_dest(e->out, sym->sec, sym->offset);

		if (!elf_create_reloc(e->out, obj_sec,
				      offsetof(struct klp_object_ext, callbacks) +
				      offsetof(struct klp_callbacks, pre_unpatch),
				      reloc->sym, reloc_addend(reloc), R_ABS64))
			return -1;
	}

	if (snprintf_check(sym_name, SYM_NAME_LEN, KLP_POST_UNPATCH_PREFIX "%s", modname))
		return -1;

	sym = find_symbol_by_name(e->out, sym_name);
	if (sym) {
		struct reloc *reloc;

		reloc = find_reloc_by_dest(e->out, sym->sec, sym->offset);

		if (!elf_create_reloc(e->out, obj_sec,
				      offsetof(struct klp_object_ext, callbacks) +
				      offsetof(struct klp_callbacks, post_unpatch),
				      reloc->sym, reloc_addend(reloc), R_ABS64))
			return -1;
	}

	return 0;
}

/*
 * Copy all .modinfo import_ns= tags to ensure all namespaced exported symbols
 * can be accessed via normal relocs.
 */
static int copy_import_ns(struct elfs *e)
{
	struct section *patched_sec, *out_sec = NULL;
	char *import_ns, *data_end;

	patched_sec = find_section_by_name(e->patched, ".modinfo");
	if (!patched_sec)
		return 0;

	import_ns = patched_sec->data->d_buf;
	if (!import_ns)
		return 0;

	for (data_end = import_ns + sec_size(patched_sec);
	     import_ns < data_end;
	     import_ns += strlen(import_ns) + 1) {

		import_ns = memmem(import_ns, data_end - import_ns, "import_ns=", 10);
		if (!import_ns)
			return 0;

		if (!out_sec) {
			out_sec = find_section_by_name(e->out, ".modinfo");
			if (!out_sec) {
				out_sec = elf_create_section(e->out, ".modinfo", 0,
							     patched_sec->sh.sh_entsize,
							     patched_sec->sh.sh_type,
							     patched_sec->sh.sh_addralign,
							     patched_sec->sh.sh_flags);
				if (!out_sec)
					return -1;
			}
		}

		if (!elf_add_data(e->out, out_sec, import_ns, strlen(import_ns) + 1))
			return -1;
	}

	return 0;
}

int cmd_klp_diff(int argc, const char **argv)
{
	struct elfs e = {0};

	argc = parse_options(argc, argv, klp_diff_options, klp_diff_usage, 0);
	if (argc != 3)
		usage_with_options(klp_diff_usage, klp_diff_options);

	objname = argv[0];

	e.orig = elf_open_read(argv[0], O_RDONLY);
	e.patched = elf_open_read(argv[1], O_RDONLY);
	e.out = NULL;

	if (!e.orig || !e.patched)
		return -1;

	if (read_exports())
		return -1;

	if (read_sym_checksums(e.orig))
		return -1;

	if (read_sym_checksums(e.patched))
		return -1;

	if (correlate_symbols(&e))
		return -1;

	if (mark_changed_functions(&e))
		return 0;

	e.out = elf_create_file(&e.orig->ehdr, argv[2]);
	if (!e.out)
		return -1;

	if (clone_included_functions(&e))
		return -1;

	if (clone_special_sections(&e))
		return -1;

	if (create_klp_sections(&e))
		return -1;

	if (copy_import_ns(&e))
		return -1;

	if  (elf_write(e.out))
		return -1;

	return elf_close(e.out);
}
