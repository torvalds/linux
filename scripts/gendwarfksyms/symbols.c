// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include "gendwarfksyms.h"

#define SYMBOL_HASH_BITS 12

/* struct symbol_addr -> struct symbol */
static HASHTABLE_DEFINE(symbol_addrs, 1 << SYMBOL_HASH_BITS);
/* name -> struct symbol */
static HASHTABLE_DEFINE(symbol_names, 1 << SYMBOL_HASH_BITS);

static inline unsigned int symbol_addr_hash(const struct symbol_addr *addr)
{
	return hash_32(addr->section ^ addr_hash(addr->address));
}

static unsigned int __for_each_addr(struct symbol *sym, symbol_callback_t func,
				    void *data)
{
	struct hlist_node *tmp;
	struct symbol *match = NULL;
	unsigned int processed = 0;

	hash_for_each_possible_safe(symbol_addrs, match, tmp, addr_hash,
				    symbol_addr_hash(&sym->addr)) {
		if (match == sym)
			continue; /* Already processed */

		if (match->addr.section == sym->addr.section &&
		    match->addr.address == sym->addr.address) {
			func(match, data);
			++processed;
		}
	}

	return processed;
}

/*
 * For symbols without debugging information (e.g. symbols defined in other
 * TUs), we also match __gendwarfksyms_ptr_<symbol_name> symbols, which the
 * kernel uses to ensure type information is present in the TU that exports
 * the symbol. A __gendwarfksyms_ptr pointer must have the same type as the
 * exported symbol, e.g.:
 *
 *   typeof(symname) *__gendwarf_ptr_symname = &symname;
 */
bool is_symbol_ptr(const char *name)
{
	return name && !strncmp(name, SYMBOL_PTR_PREFIX, SYMBOL_PTR_PREFIX_LEN);
}

static unsigned int for_each(const char *name, symbol_callback_t func,
			     void *data)
{
	struct hlist_node *tmp;
	struct symbol *match;

	if (!name || !*name)
		return 0;
	if (is_symbol_ptr(name))
		name += SYMBOL_PTR_PREFIX_LEN;

	hash_for_each_possible_safe(symbol_names, match, tmp, name_hash,
				    hash_str(name)) {
		if (strcmp(match->name, name))
			continue;

		/* Call func for the match, and all address matches */
		if (func)
			func(match, data);

		if (match->addr.section != SHN_UNDEF)
			return __for_each_addr(match, func, data) + 1;

		return 1;
	}

	return 0;
}

static void set_crc(struct symbol *sym, void *data)
{
	unsigned long *crc = data;

	if (sym->state == SYMBOL_PROCESSED && sym->crc != *crc)
		warn("overriding version for symbol %s (crc %lx vs. %lx)",
		     sym->name, sym->crc, *crc);

	sym->state = SYMBOL_PROCESSED;
	sym->crc = *crc;
}

void symbol_set_crc(struct symbol *sym, unsigned long crc)
{
	if (for_each(sym->name, set_crc, &crc) == 0)
		error("no matching symbols: '%s'", sym->name);
}

static void set_ptr(struct symbol *sym, void *data)
{
	sym->ptr_die_addr = (uintptr_t)((Dwarf_Die *)data)->addr;
}

void symbol_set_ptr(struct symbol *sym, Dwarf_Die *ptr)
{
	if (for_each(sym->name, set_ptr, ptr) == 0)
		error("no matching symbols: '%s'", sym->name);
}

static void set_die(struct symbol *sym, void *data)
{
	sym->die_addr = (uintptr_t)((Dwarf_Die *)data)->addr;
	sym->state = SYMBOL_MAPPED;
}

void symbol_set_die(struct symbol *sym, Dwarf_Die *die)
{
	if (for_each(sym->name, set_die, die) == 0)
		error("no matching symbols: '%s'", sym->name);
}

static bool is_exported(const char *name)
{
	return for_each(name, NULL, NULL) > 0;
}

int symbol_read_exports(FILE *file)
{
	struct symbol *sym;
	char *line = NULL;
	char *name = NULL;
	size_t size = 0;
	int nsym = 0;

	while (getline(&line, &size, file) > 0) {
		if (sscanf(line, "%ms\n", &name) != 1)
			error("malformed input line: %s", line);

		if (is_exported(name)) {
			/* Ignore duplicates */
			free(name);
			continue;
		}

		sym = xcalloc(1, sizeof(*sym));
		sym->name = name;
		sym->addr.section = SHN_UNDEF;
		sym->state = SYMBOL_UNPROCESSED;

		hash_add(symbol_names, &sym->name_hash, hash_str(sym->name));
		++nsym;

		debug("%s", sym->name);
	}

	free(line);
	debug("%d exported symbols", nsym);

	return nsym;
}

static void get_symbol(struct symbol *sym, void *arg)
{
	struct symbol **res = arg;

	if (sym->state == SYMBOL_UNPROCESSED)
		*res = sym;
}

struct symbol *symbol_get(const char *name)
{
	struct symbol *sym = NULL;

	for_each(name, get_symbol, &sym);
	return sym;
}

void symbol_for_each(symbol_callback_t func, void *arg)
{
	struct hlist_node *tmp;
	struct symbol *sym;

	hash_for_each_safe(symbol_names, sym, tmp, name_hash) {
		func(sym, arg);
	}
}

typedef void (*elf_symbol_callback_t)(const char *name, GElf_Sym *sym,
				      Elf32_Word xndx, void *arg);

static void elf_for_each_global(int fd, elf_symbol_callback_t func, void *arg)
{
	size_t sym_size;
	GElf_Shdr shdr_mem;
	GElf_Shdr *shdr;
	Elf_Data *xndx_data = NULL;
	Elf_Scn *scn;
	Elf *elf;

	if (elf_version(EV_CURRENT) != EV_CURRENT)
		error("elf_version failed: %s", elf_errmsg(-1));

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf)
		error("elf_begin failed: %s", elf_errmsg(-1));

	scn = elf_nextscn(elf, NULL);

	while (scn) {
		shdr = gelf_getshdr(scn, &shdr_mem);
		if (!shdr)
			error("gelf_getshdr failed: %s", elf_errmsg(-1));

		if (shdr->sh_type == SHT_SYMTAB_SHNDX) {
			xndx_data = elf_getdata(scn, NULL);
			if (!xndx_data)
				error("elf_getdata failed: %s", elf_errmsg(-1));
			break;
		}

		scn = elf_nextscn(elf, scn);
	}

	sym_size = gelf_fsize(elf, ELF_T_SYM, 1, EV_CURRENT);
	scn = elf_nextscn(elf, NULL);

	while (scn) {
		shdr = gelf_getshdr(scn, &shdr_mem);
		if (!shdr)
			error("gelf_getshdr failed: %s", elf_errmsg(-1));

		if (shdr->sh_type == SHT_SYMTAB) {
			unsigned int nsyms;
			unsigned int n;
			Elf_Data *data = elf_getdata(scn, NULL);

			if (!data)
				error("elf_getdata failed: %s", elf_errmsg(-1));

			if (shdr->sh_entsize != sym_size)
				error("expected sh_entsize (%lu) to be %zu",
				      shdr->sh_entsize, sym_size);

			nsyms = shdr->sh_size / shdr->sh_entsize;

			for (n = 1; n < nsyms; ++n) {
				const char *name = NULL;
				Elf32_Word xndx = 0;
				GElf_Sym sym_mem;
				GElf_Sym *sym;

				sym = gelf_getsymshndx(data, xndx_data, n,
						       &sym_mem, &xndx);
				if (!sym)
					error("gelf_getsymshndx failed: %s",
					      elf_errmsg(-1));

				if (GELF_ST_BIND(sym->st_info) == STB_LOCAL)
					continue;

				if (sym->st_shndx != SHN_XINDEX)
					xndx = sym->st_shndx;

				name = elf_strptr(elf, shdr->sh_link,
						  sym->st_name);
				if (!name)
					error("elf_strptr failed: %s",
					      elf_errmsg(-1));

				/* Skip empty symbol names */
				if (*name)
					func(name, sym, xndx, arg);
			}
		}

		scn = elf_nextscn(elf, scn);
	}

	check(elf_end(elf));
}

static void set_symbol_addr(struct symbol *sym, void *arg)
{
	struct symbol_addr *addr = arg;

	if (sym->addr.section == SHN_UNDEF) {
		sym->addr = *addr;
		hash_add(symbol_addrs, &sym->addr_hash,
			 symbol_addr_hash(&sym->addr));

		debug("%s -> { %u, %lx }", sym->name, sym->addr.section,
		      sym->addr.address);
	} else if (sym->addr.section != addr->section ||
		   sym->addr.address != addr->address) {
		warn("multiple addresses for symbol %s?", sym->name);
	}
}

static void elf_set_symbol_addr(const char *name, GElf_Sym *sym,
				Elf32_Word xndx, void *arg)
{
	struct symbol_addr addr = { .section = xndx, .address = sym->st_value };

	/* Set addresses for exported symbols */
	if (addr.section != SHN_UNDEF)
		for_each(name, set_symbol_addr, &addr);
}

void symbol_read_symtab(int fd)
{
	elf_for_each_global(fd, elf_set_symbol_addr, NULL);
}

void symbol_print_versions(void)
{
	struct hlist_node *tmp;
	struct symbol *sym;

	hash_for_each_safe(symbol_names, sym, tmp, name_hash) {
		if (sym->state != SYMBOL_PROCESSED)
			warn("no information for symbol %s", sym->name);

		printf("#SYMVER %s 0x%08lx\n", sym->name, sym->crc);
	}
}

void symbol_free(void)
{
	struct hlist_node *tmp;
	struct symbol *sym;

	hash_for_each_safe(symbol_names, sym, tmp, name_hash) {
		free((void *)sym->name);
		free(sym);
	}

	hash_init(symbol_addrs);
	hash_init(symbol_names);
}
