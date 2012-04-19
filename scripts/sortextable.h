/*
 * sortextable.h
 *
 * Copyright 2011 Cavium, Inc.
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
# define _w			w
#endif

static int compare_extable(const void *a, const void *b)
{
	const uint_t *aa = a;
	const uint_t *bb = b;

	if (_w(*aa) < _w(*bb))
		return -1;
	if (_w(*aa) > _w(*bb))
		return 1;
	return 0;
}

static void
do_func(Elf_Ehdr *const ehdr, char const *const fname)
{
	Elf_Shdr *shdr;
	Elf_Shdr *shstrtab_sec;
	Elf_Shdr *strtab_sec = NULL;
	Elf_Shdr *symtab_sec = NULL;
	Elf_Shdr *extab_sec = NULL;
	Elf_Sym *sym;
	Elf_Sym *sort_needed_sym;
	Elf_Shdr *sort_needed_sec;
	uint32_t *sort_done_location;
	const char *secstrtab;
	const char *strtab;
	int i;
	int idx;

	shdr = (Elf_Shdr *)((void *)ehdr + _w(ehdr->e_shoff));
	shstrtab_sec = shdr + w2(ehdr->e_shstrndx);
	secstrtab = (const char *)ehdr + _w(shstrtab_sec->sh_offset);
	for (i = 0; i < w2(ehdr->e_shnum); i++) {
		idx = w(shdr[i].sh_name);
		if (strcmp(secstrtab + idx, "__ex_table") == 0)
			extab_sec = shdr + i;
		if (strcmp(secstrtab + idx, ".symtab") == 0)
			symtab_sec = shdr + i;
		if (strcmp(secstrtab + idx, ".strtab") == 0)
			strtab_sec = shdr + i;
	}
	if (strtab_sec == NULL) {
		fprintf(stderr,	"no .strtab in  file: %s\n", fname);
		fail_file();
	}
	if (symtab_sec == NULL) {
		fprintf(stderr,	"no .symtab in  file: %s\n", fname);
		fail_file();
	}
	if (extab_sec == NULL) {
		fprintf(stderr,	"no __ex_table in  file: %s\n", fname);
		fail_file();
	}
	strtab = (const char *)ehdr + _w(strtab_sec->sh_offset);

	/* Sort the table in place */
	qsort((void *)ehdr + _w(extab_sec->sh_offset),
	      (_w(extab_sec->sh_size) / extable_ent_size),
	      extable_ent_size, compare_extable);

	/* find main_extable_sort_needed */
	sort_needed_sym = NULL;
	for (i = 0; i < _w(symtab_sec->sh_size) / sizeof(Elf_Sym); i++) {
		sym = (void *)ehdr + _w(symtab_sec->sh_offset);
		sym += i;
		if (ELF_ST_TYPE(sym->st_info) != STT_OBJECT)
			continue;
		idx = w(sym->st_name);
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
	sort_needed_sec = &shdr[w2(sort_needed_sym->st_shndx)];
	sort_done_location = (void *)ehdr +
		_w(sort_needed_sec->sh_offset) +
		_w(sort_needed_sym->st_value) -
		_w(sort_needed_sec->sh_addr);

	printf("sort done marker at %lx\n",
		(unsigned long) (_w(sort_needed_sec->sh_offset) +
				 _w(sort_needed_sym->st_value) -
				 _w(sort_needed_sec->sh_addr)));
	/* We sorted it, clear the flag. */
	*sort_done_location = 0;
}
