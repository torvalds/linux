/*
 * sortextable.h
 *
 * Copyright 2011 - 2012 Cavium, Inc.
 *
 * Some of this code was taken out of recordmcount.h written by:
 *
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>.  All rights reserved.
 * Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
 *
 *
 * Licensed under the GNU General Public License, version 2 (GPLv2).
 */

#undef extable_ent_size
#undef compare_extable
#undef do_func
#undef Elf_Addr
#undef Elf_Ehdr
#undef Elf_Shdr
#undef Elf_Rel
#undef Elf_Rela
#undef Elf_Sym
#undef ELF_R_SYM
#undef Elf_r_sym
#undef ELF_R_INFO
#undef Elf_r_info
#undef ELF_ST_BIND
#undef ELF_ST_TYPE
#undef fn_ELF_R_SYM
#undef fn_ELF_R_INFO
#undef uint_t
#undef _r
#undef _w

#ifdef SORTEXTABLE_64
# define extable_ent_size	16
# define compare_extable	compare_extable_64
# define do_func		do64
# define Elf_Addr		Elf64_Addr
# define Elf_Ehdr		Elf64_Ehdr
# define Elf_Shdr		Elf64_Shdr
# define Elf_Rel		Elf64_Rel
# define Elf_Rela		Elf64_Rela
# define Elf_Sym		Elf64_Sym
# define ELF_R_SYM		ELF64_R_SYM
# define Elf_r_sym		Elf64_r_sym
# define ELF_R_INFO		ELF64_R_INFO
# define Elf_r_info		Elf64_r_info
# define ELF_ST_BIND		ELF64_ST_BIND
# define ELF_ST_TYPE		ELF64_ST_TYPE
# define fn_ELF_R_SYM		fn_ELF64_R_SYM
# define fn_ELF_R_INFO		fn_ELF64_R_INFO
# define uint_t			uint64_t
# define _r			r8
# define _w			w8
#else
# define extable_ent_size	8
# define compare_extable	compare_extable_32
# define do_func		do32
# define Elf_Addr		Elf32_Addr
# define Elf_Ehdr		Elf32_Ehdr
# define Elf_Shdr		Elf32_Shdr
# define Elf_Rel		Elf32_Rel
# define Elf_Rela		Elf32_Rela
# define Elf_Sym		Elf32_Sym
# define ELF_R_SYM		ELF32_R_SYM
# define Elf_r_sym		Elf32_r_sym
# define ELF_R_INFO		ELF32_R_INFO
# define Elf_r_info		Elf32_r_info
# define ELF_ST_BIND		ELF32_ST_BIND
# define ELF_ST_TYPE		ELF32_ST_TYPE
# define fn_ELF_R_SYM		fn_ELF32_R_SYM
# define fn_ELF_R_INFO		fn_ELF32_R_INFO
# define uint_t			uint32_t
# define _r			r
# define _w			w
#endif

static int compare_extable(const void *a, const void *b)
{
	Elf_Addr av = _r(a);
	Elf_Addr bv = _r(b);

	if (av < bv)
		return -1;
	if (av > bv)
		return 1;
	return 0;
}

static void
do_func(Elf_Ehdr *ehdr, char const *const fname, table_sort_t custom_sort)
{
	Elf_Shdr *shdr;
	Elf_Shdr *shstrtab_sec;
	Elf_Shdr *strtab_sec = NULL;
	Elf_Shdr *symtab_sec = NULL;
	Elf_Shdr *extab_sec = NULL;
	Elf_Sym *sym;
	const Elf_Sym *symtab;
	Elf32_Word *symtab_shndx_start = NULL;
	Elf_Sym *sort_needed_sym;
	Elf_Shdr *sort_needed_sec;
	Elf_Rel *relocs = NULL;
	int relocs_size = 0;
	uint32_t *sort_done_location;
	const char *secstrtab;
	const char *strtab;
	char *extab_image;
	int extab_index = 0;
	int i;
	int idx;
	unsigned int num_sections;
	unsigned int secindex_strings;

	shdr = (Elf_Shdr *)((char *)ehdr + _r(&ehdr->e_shoff));

	num_sections = r2(&ehdr->e_shnum);
	if (num_sections == SHN_UNDEF)
		num_sections = _r(&shdr[0].sh_size);

	secindex_strings = r2(&ehdr->e_shstrndx);
	if (secindex_strings == SHN_XINDEX)
		secindex_strings = r(&shdr[0].sh_link);

	shstrtab_sec = shdr + secindex_strings;
	secstrtab = (const char *)ehdr + _r(&shstrtab_sec->sh_offset);
	for (i = 0; i < num_sections; i++) {
		idx = r(&shdr[i].sh_name);
		if (strcmp(secstrtab + idx, "__ex_table") == 0) {
			extab_sec = shdr + i;
			extab_index = i;
		}
		if ((r(&shdr[i].sh_type) == SHT_REL ||
		     r(&shdr[i].sh_type) == SHT_RELA) &&
		    r(&shdr[i].sh_info) == extab_index) {
			relocs = (void *)ehdr + _r(&shdr[i].sh_offset);
			relocs_size = _r(&shdr[i].sh_size);
		}
		if (strcmp(secstrtab + idx, ".symtab") == 0)
			symtab_sec = shdr + i;
		if (strcmp(secstrtab + idx, ".strtab") == 0)
			strtab_sec = shdr + i;
		if (r(&shdr[i].sh_type) == SHT_SYMTAB_SHNDX)
			symtab_shndx_start = (Elf32_Word *)(
				(const char *)ehdr + _r(&shdr[i].sh_offset));
	}
	if (strtab_sec == NULL) {
		fprintf(stderr,	"no .strtab in  file: %s\n", fname);
		fail_file();
	}
	if (symtab_sec == NULL) {
		fprintf(stderr,	"no .symtab in  file: %s\n", fname);
		fail_file();
	}
	symtab = (const Elf_Sym *)((const char *)ehdr +
				   _r(&symtab_sec->sh_offset));
	if (extab_sec == NULL) {
		fprintf(stderr,	"no __ex_table in  file: %s\n", fname);
		fail_file();
	}
	strtab = (const char *)ehdr + _r(&strtab_sec->sh_offset);

	extab_image = (void *)ehdr + _r(&extab_sec->sh_offset);

	if (custom_sort) {
		custom_sort(extab_image, _r(&extab_sec->sh_size));
	} else {
		int num_entries = _r(&extab_sec->sh_size) / extable_ent_size;
		qsort(extab_image, num_entries,
		      extable_ent_size, compare_extable);
	}
	/* If there were relocations, we no longer need them. */
	if (relocs)
		memset(relocs, 0, relocs_size);

	/* find main_extable_sort_needed */
	sort_needed_sym = NULL;
	for (i = 0; i < _r(&symtab_sec->sh_size) / sizeof(Elf_Sym); i++) {
		sym = (void *)ehdr + _r(&symtab_sec->sh_offset);
		sym += i;
		if (ELF_ST_TYPE(sym->st_info) != STT_OBJECT)
			continue;
		idx = r(&sym->st_name);
		if (strcmp(strtab + idx, "main_extable_sort_needed") == 0) {
			sort_needed_sym = sym;
			break;
		}
	}
	if (sort_needed_sym == NULL) {
		fprintf(stderr,
			"no main_extable_sort_needed symbol in  file: %s\n",
			fname);
		fail_file();
	}
	sort_needed_sec = &shdr[get_secindex(r2(&sym->st_shndx),
					     sort_needed_sym - symtab,
					     symtab_shndx_start)];
	sort_done_location = (void *)ehdr +
		_r(&sort_needed_sec->sh_offset) +
		_r(&sort_needed_sym->st_value) -
		_r(&sort_needed_sec->sh_addr);

#if 0
	printf("sort done marker at %lx\n",
	       (unsigned long)((char *)sort_done_location - (char *)ehdr));
#endif
	/* We sorted it, clear the flag. */
	w(0, sort_done_location);
}
