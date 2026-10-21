// SPDX-License-Identifier: GPL-2.0
/*
 * Obtain symbols from vmlinux for usage by kallsyms. Replaces mksysmap.
 *
 * To retain compatibility, it provides the same output as nm, only faster.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xalloc.h>

#include "elf-parse.h"
#include "kallsyms.h"

/*
 * What mksysmap dropped from System.map, by name: symbols that are not needed
 * there, or not suitable for kallsyms.
 */
static const char *const sysmap_omit_prefixes[] = {
	/* local symbols for ARM, MIPS, etc. */
	"$",
	/* local labels, .LBB, .Ltmpxxx, .L__unnamed_xx, .LASANPC, etc. */
	".L",
	/* arm64 EFI stub namespace */
	"__efistub_",
	/* arm64 local symbols in PIE namespace */
	"__pi_$",
	"__pi_.L",
	/* arm64 local symbols in non-VHE KVM namespace */
	"__kvm_nvhe_$",
	"__kvm_nvhe_.L",
	/* CFI type identifiers */
	"__kcfi_typeid_",
	"__kvm_nvhe___kcfi_typeid_",
	"__pi___kcfi_typeid_",
	/* CRC from modversions */
	"__crc_",
	/* EXPORT_SYMBOL (symbol name) */
	"__kstrtab_",
	/* EXPORT_SYMBOL (namespace) */
	"__kstrtabns_",
	/* MODULE_DEVICE_TABLE (symbol name) */
	"__mod_device_table__",
};

static const char *const sysmap_omit_suffixes[] = {
	/* arm */
	"_from_arm",
	"_from_thumb",
	"_veneer",
};

static const char *const sysmap_omit_names[] = {
	/* for LoongArch? */
	"L0",
	/* ppc */
	"_SDA_BASE_",
	"_SDA2_BASE_",
};

/* Symbols that contain the pattern anywhere are dropped. */
static const char *const sysmap_omit_patterns[] = {
	/* ppc stub */
	".long_branch.",
	".plt_branch.",
};

/* __<alnum>*Thunk_: the linker's range extension thunks on arm. */
static bool is_range_thunk(const char *name)
{
	const char *p;

	if (!string_starts_with(name, "__"))
		return false;
	for (p = name + 2; isalnum((unsigned char)*p); p++)
		;
	return p - name >= 7 && *p == '_' && strncmp(p - 5, "Thunk", 5) == 0;
}

/* __UNIQUE_ID_modinfo_<n>: the MODULE_INFO() strings of built-in code. */
static bool is_modinfo_id(const char *name)
{
	static const char prefix[] = "__UNIQUE_ID_modinfo_";
	const char *p;

	if (!string_starts_with(name, prefix))
		return false;
	for (p = name + strlen(prefix); isdigit((unsigned char)*p); p++)
		;
	return !*p;
}

static bool sysmap_omits(const char *name, char type)
{
	size_t i;

	/* Absolute, undefined and debugging symbols. */
	if (type == 'a' || type == 'N' || type == 'U' || type == 'w')
		return true;

	for (i = 0; i < ARRAY_SIZE(sysmap_omit_prefixes); i++)
		if (string_starts_with(name, sysmap_omit_prefixes[i]))
			return true;
	for (i = 0; i < ARRAY_SIZE(sysmap_omit_suffixes); i++)
		if (string_ends_with(name, sysmap_omit_suffixes[i]))
			return true;
	for (i = 0; i < ARRAY_SIZE(sysmap_omit_names); i++)
		if (strcmp(name, sysmap_omit_names[i]) == 0)
			return true;
	for (i = 0; i < ARRAY_SIZE(sysmap_omit_patterns); i++)
		if (strstr(name, sysmap_omit_patterns[i]))
			return true;

	return is_range_thunk(name) || is_modinfo_id(name);
}

/* nm's letter for a symbol defined in a section, as BFD classifies it. */
static char section_symbol_type(Elf_Shdr *shdr, const char *secname)
{
	static const char *const debug_prefixes[] = {
		".debug", ".zdebug", ".gnu.debuglto_.debug_",
		".gnu.linkonce.wi.", ".line", ".stab",
	};
	const uint64_t flags = shdr_flags(shdr);
	size_t i;

	if (flags & SHF_EXECINSTR)
		return 't';
	if (flags & SHF_ALLOC) {
		if (shdr_type(shdr) == SHT_NOBITS)
			return 'b';
		return flags & SHF_WRITE ? 'd' : 'r';
	}
	for (i = 0; i < ARRAY_SIZE(debug_prefixes); i++)
		if (string_starts_with(secname, debug_prefixes[i]))
			return 'N';
	if (shdr_type(shdr) != SHT_NOBITS && !(flags & SHF_WRITE))
		return 'n';
	return '?';
}

/* The letter nm prints for a symbol, or 0 for one it leaves out. */
static char elf_symbol_type(const struct elf_file *elf, Elf_Sym *sym)
{
	unsigned int bind = sym_bind(sym), type = sym_type(sym);
	unsigned int shndx = sym_shndx(sym);
	Elf_Shdr *shdr;
	char c;

	if (type == STT_SECTION || type == STT_FILE)
		return 0;
	if (shndx == SHN_COMMON)
		return 'C';
	if (shndx == SHN_UNDEF) {
		if (bind == STB_WEAK)
			return type == STT_OBJECT ? 'v' : 'w';
		return 'U';
	}
	if (type == STT_GNU_IFUNC)
		return 'i';
	if (bind == STB_WEAK)
		return type == STT_OBJECT ? 'V' : 'W';
	if (bind == STB_GNU_UNIQUE)
		return 'u';
	if (bind != STB_GLOBAL && bind != STB_LOCAL)
		return '?';

	if (shndx == SHN_ABS) {
		c = 'a';
	} else if (shndx < elf->shnum) {
		shdr = elf_section(elf, shndx);
		c = section_symbol_type(shdr, elf_section_name(elf, shdr));
	} else {
		return '?';
	}

	return bind == STB_GLOBAL ? toupper(c) : c;
}

/* nm -n order: by address, then by name. */
static int compare_symbols(const void *a, const void *b)
{
	const struct sysmap_symbol *sa = a, *sb = b;

	if (sa->addr != sb->addr)
		return sa->addr < sb->addr ? -1 : 1;
	return strcmp(sa->name, sb->name);
}

/* The symbols "nm -n | mksysmap" would list, in that order. */
static struct sysmap_symbol *elf_read_symbols(const struct elf_file *elf,
					   size_t *nr_kept)
{
	struct sysmap_symbol *syms = xmalloc(elf->nr_syms * sizeof(*syms));
	size_t i, n = 0;

	for (i = 1; i < elf->nr_syms; i++) {
		Elf_Sym *sym = elf_symbol(elf, i);
		const char *name = elf->strtab + sym_name(sym);
		char type = elf_symbol_type(elf, sym);

		if (!type || sysmap_omits(name, type))
			continue;

		syms[n].addr = sym_value(sym);
		syms[n].name = name;
		syms[n].type = type;
		n++;
	}

	qsort(syms, n, sizeof(*syms), compare_symbols);
	*nr_kept = n;
	return syms;
}

struct sysmap *sysmap_read(const char *path)
{
	struct sysmap *map = xcalloc(1, sizeof(*map));

	/* The names point into the mapping; keep it until the map is freed. */
	if (elf_open_ro(path, (1 << ET_EXEC) | (1 << ET_DYN), &map->elf))
		exit(EXIT_FAILURE);
	map->syms = elf_read_symbols(&map->elf, &map->nr_syms);
	map->addr_width = elf_map_long_size(map->elf.base) * 2;

	return map;
}

void sysmap_write(const struct sysmap *map, FILE *out)
{
	size_t i;

	for (i = 0; i < map->nr_syms; i++) {
		const struct sysmap_symbol *s = &map->syms[i];

		fprintf(out, "%0*llx %c %s\n", map->addr_width, s->addr, s->type,
			s->name);
	}
}

void sysmap_free(struct sysmap *map)
{
	free(map->syms);
	elf_close(&map->elf);
	free(map);
}
