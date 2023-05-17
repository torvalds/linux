/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sorttable.h
 *
 * Added ORC unwind tables sort support and other updates:
 * Copyright (C) 1999-2019 Alibaba Group Holding Limited. by:
 * Shile Zhang <shile.zhang@linux.alibaba.com>
 *
 * Copyright 2011 - 2012 Cavium, Inc.
 *
 * Some of code was taken out of arch/x86/kernel/unwind_orc.c, written by:
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * Some of this code was taken out of recordmcount.h written by:
 *
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>. All rights reserved.
 * Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
 */

#undef extable_ent_size
#undef compare_extable
#undef get_mcount_loc
#undef sort_mcount_loc
#undef elf_mcount_loc
#undef do_sort
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

#ifdef SORTTABLE_64
# define extable_ent_size	16
# define compare_extable	compare_extable_64
# define get_mcount_loc		get_mcount_loc_64
# define sort_mcount_loc	sort_mcount_loc_64
# define elf_mcount_loc		elf_mcount_loc_64
# define do_sort		do_sort_64
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
# define get_mcount_loc		get_mcount_loc_32
# define sort_mcount_loc	sort_mcount_loc_32
# define elf_mcount_loc		elf_mcount_loc_32
# define do_sort		do_sort_32
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

#if defined(SORTTABLE_64) && defined(UNWINDER_ORC_ENABLED)
/* ORC unwinder only support X86_64 */
#include <asm/orc_types.h>

#define ERRSTR_MAXSZ	256

char g_err[ERRSTR_MAXSZ];
int *g_orc_ip_table;
struct orc_entry *g_orc_table;

pthread_t orc_sort_thread;

static inline unsigned long orc_ip(const int *ip)
{
	return (unsigned long)ip + *ip;
}

static int orc_sort_cmp(const void *_a, const void *_b)
{
	struct orc_entry *orc_a;
	const int *a = g_orc_ip_table + *(int *)_a;
	const int *b = g_orc_ip_table + *(int *)_b;
	unsigned long a_val = orc_ip(a);
	unsigned long b_val = orc_ip(b);

	if (a_val > b_val)
		return 1;
	if (a_val < b_val)
		return -1;

	/*
	 * The "weak" section terminator entries need to always be on the left
	 * to ensure the lookup code skips them in favor of real entries.
	 * These terminator entries exist to handle any gaps created by
	 * whitelisted .o files which didn't get objtool generation.
	 */
	orc_a = g_orc_table + (a - g_orc_ip_table);
	return orc_a->type == ORC_TYPE_UNDEFINED ? -1 : 1;
}

static void *sort_orctable(void *arg)
{
	int i;
	int *idxs = NULL;
	int *tmp_orc_ip_table = NULL;
	struct orc_entry *tmp_orc_table = NULL;
	unsigned int *orc_ip_size = (unsigned int *)arg;
	unsigned int num_entries = *orc_ip_size / sizeof(int);
	unsigned int orc_size = num_entries * sizeof(struct orc_entry);

	idxs = (int *)malloc(*orc_ip_size);
	if (!idxs) {
		snprintf(g_err, ERRSTR_MAXSZ, "malloc idxs: %s",
			 strerror(errno));
		pthread_exit(g_err);
	}

	tmp_orc_ip_table = (int *)malloc(*orc_ip_size);
	if (!tmp_orc_ip_table) {
		snprintf(g_err, ERRSTR_MAXSZ, "malloc tmp_orc_ip_table: %s",
			 strerror(errno));
		pthread_exit(g_err);
	}

	tmp_orc_table = (struct orc_entry *)malloc(orc_size);
	if (!tmp_orc_table) {
		snprintf(g_err, ERRSTR_MAXSZ, "malloc tmp_orc_table: %s",
			 strerror(errno));
		pthread_exit(g_err);
	}

	/* initialize indices array, convert ip_table to absolute address */
	for (i = 0; i < num_entries; i++) {
		idxs[i] = i;
		tmp_orc_ip_table[i] = g_orc_ip_table[i] + i * sizeof(int);
	}
	memcpy(tmp_orc_table, g_orc_table, orc_size);

	qsort(idxs, num_entries, sizeof(int), orc_sort_cmp);

	for (i = 0; i < num_entries; i++) {
		if (idxs[i] == i)
			continue;

		/* convert back to relative address */
		g_orc_ip_table[i] = tmp_orc_ip_table[idxs[i]] - i * sizeof(int);
		g_orc_table[i] = tmp_orc_table[idxs[i]];
	}

	free(idxs);
	free(tmp_orc_ip_table);
	free(tmp_orc_table);
	pthread_exit(NULL);
}
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
#ifdef MCOUNT_SORT_ENABLED
pthread_t mcount_sort_thread;

struct elf_mcount_loc {
	Elf_Ehdr *ehdr;
	Elf_Shdr *init_data_sec;
	uint_t start_mcount_loc;
	uint_t stop_mcount_loc;
};

/* Sort the addresses stored between __start_mcount_loc to __stop_mcount_loc in vmlinux */
static void *sort_mcount_loc(void *arg)
{
	struct elf_mcount_loc *emloc = (struct elf_mcount_loc *)arg;
	uint_t offset = emloc->start_mcount_loc - _r(&(emloc->init_data_sec)->sh_addr)
					+ _r(&(emloc->init_data_sec)->sh_offset);
	uint_t count = emloc->stop_mcount_loc - emloc->start_mcount_loc;
	unsigned char *start_loc = (void *)emloc->ehdr + offset;

	qsort(start_loc, count/sizeof(uint_t), sizeof(uint_t), compare_extable);
	return NULL;
}

/* Get the address of __start_mcount_loc and __stop_mcount_loc in System.map */
static void get_mcount_loc(uint_t *_start, uint_t *_stop)
{
	FILE *file_start, *file_stop;
	char start_buff[20];
	char stop_buff[20];
	int len = 0;

	file_start = popen(" grep start_mcount System.map | awk '{print $1}' ", "r");
	if (!file_start) {
		fprintf(stderr, "get start_mcount_loc error!");
		return;
	}

	file_stop = popen(" grep stop_mcount System.map | awk '{print $1}' ", "r");
	if (!file_stop) {
		fprintf(stderr, "get stop_mcount_loc error!");
		pclose(file_start);
		return;
	}

	while (fgets(start_buff, sizeof(start_buff), file_start) != NULL) {
		len = strlen(start_buff);
		start_buff[len - 1] = '\0';
	}
	*_start = strtoul(start_buff, NULL, 16);

	while (fgets(stop_buff, sizeof(stop_buff), file_stop) != NULL) {
		len = strlen(stop_buff);
		stop_buff[len - 1] = '\0';
	}
	*_stop = strtoul(stop_buff, NULL, 16);

	pclose(file_start);
	pclose(file_stop);
}
#endif
static int do_sort(Elf_Ehdr *ehdr,
		   char const *const fname,
		   table_sort_t custom_sort)
{
	int rc = -1;
	Elf_Shdr *s, *shdr = (Elf_Shdr *)((char *)ehdr + _r(&ehdr->e_shoff));
	Elf_Shdr *strtab_sec = NULL;
	Elf_Shdr *symtab_sec = NULL;
	Elf_Shdr *extab_sec = NULL;
	Elf_Sym *sym;
	const Elf_Sym *symtab;
	Elf32_Word *symtab_shndx = NULL;
	Elf_Sym *sort_needed_sym = NULL;
	Elf_Shdr *sort_needed_sec;
	Elf_Rel *relocs = NULL;
	int relocs_size = 0;
	uint32_t *sort_needed_loc;
	const char *secstrings;
	const char *strtab;
	char *extab_image;
	int extab_index = 0;
	int i;
	int idx;
	unsigned int shnum;
	unsigned int shstrndx;
#ifdef MCOUNT_SORT_ENABLED
	struct elf_mcount_loc mstruct = {0};
	uint_t _start_mcount_loc = 0;
	uint_t _stop_mcount_loc = 0;
#endif
#if defined(SORTTABLE_64) && defined(UNWINDER_ORC_ENABLED)
	unsigned int orc_ip_size = 0;
	unsigned int orc_size = 0;
	unsigned int orc_num_entries = 0;
#endif

	shstrndx = r2(&ehdr->e_shstrndx);
	if (shstrndx == SHN_XINDEX)
		shstrndx = r(&shdr[0].sh_link);
	secstrings = (const char *)ehdr + _r(&shdr[shstrndx].sh_offset);

	shnum = r2(&ehdr->e_shnum);
	if (shnum == SHN_UNDEF)
		shnum = _r(&shdr[0].sh_size);

	for (i = 0, s = shdr; s < shdr + shnum; i++, s++) {
		idx = r(&s->sh_name);
		if (!strcmp(secstrings + idx, "__ex_table")) {
			extab_sec = s;
			extab_index = i;
		}
		if (!strcmp(secstrings + idx, ".symtab"))
			symtab_sec = s;
		if (!strcmp(secstrings + idx, ".strtab"))
			strtab_sec = s;

		if ((r(&s->sh_type) == SHT_REL ||
		     r(&s->sh_type) == SHT_RELA) &&
		    r(&s->sh_info) == extab_index) {
			relocs = (void *)ehdr + _r(&s->sh_offset);
			relocs_size = _r(&s->sh_size);
		}
		if (r(&s->sh_type) == SHT_SYMTAB_SHNDX)
			symtab_shndx = (Elf32_Word *)((const char *)ehdr +
						      _r(&s->sh_offset));

#ifdef MCOUNT_SORT_ENABLED
		/* locate the .init.data section in vmlinux */
		if (!strcmp(secstrings + idx, ".init.data")) {
			get_mcount_loc(&_start_mcount_loc, &_stop_mcount_loc);
			mstruct.ehdr = ehdr;
			mstruct.init_data_sec = s;
			mstruct.start_mcount_loc = _start_mcount_loc;
			mstruct.stop_mcount_loc = _stop_mcount_loc;
		}
#endif

#if defined(SORTTABLE_64) && defined(UNWINDER_ORC_ENABLED)
		/* locate the ORC unwind tables */
		if (!strcmp(secstrings + idx, ".orc_unwind_ip")) {
			orc_ip_size = s->sh_size;
			g_orc_ip_table = (int *)((void *)ehdr +
						   s->sh_offset);
		}
		if (!strcmp(secstrings + idx, ".orc_unwind")) {
			orc_size = s->sh_size;
			g_orc_table = (struct orc_entry *)((void *)ehdr +
							     s->sh_offset);
		}
#endif
	} /* for loop */

#if defined(SORTTABLE_64) && defined(UNWINDER_ORC_ENABLED)
	if (!g_orc_ip_table || !g_orc_table) {
		fprintf(stderr,
			"incomplete ORC unwind tables in file: %s\n", fname);
		goto out;
	}

	orc_num_entries = orc_ip_size / sizeof(int);
	if (orc_ip_size % sizeof(int) != 0 ||
	    orc_size % sizeof(struct orc_entry) != 0 ||
	    orc_num_entries != orc_size / sizeof(struct orc_entry)) {
		fprintf(stderr,
			"inconsistent ORC unwind table entries in file: %s\n",
			fname);
		goto out;
	}

	/* create thread to sort ORC unwind tables concurrently */
	if (pthread_create(&orc_sort_thread, NULL,
			   sort_orctable, &orc_ip_size)) {
		fprintf(stderr,
			"pthread_create orc_sort_thread failed '%s': %s\n",
			strerror(errno), fname);
		goto out;
	}
#endif

#ifdef MCOUNT_SORT_ENABLED
	if (!mstruct.init_data_sec || !_start_mcount_loc || !_stop_mcount_loc) {
		fprintf(stderr,
			"incomplete mcount's sort in file: %s\n",
			fname);
		goto out;
	}

	/* create thread to sort mcount_loc concurrently */
	if (pthread_create(&mcount_sort_thread, NULL, &sort_mcount_loc, &mstruct)) {
		fprintf(stderr,
			"pthread_create mcount_sort_thread failed '%s': %s\n",
			strerror(errno), fname);
		goto out;
	}
#endif
	if (!extab_sec) {
		fprintf(stderr,	"no __ex_table in file: %s\n", fname);
		goto out;
	}

	if (!symtab_sec) {
		fprintf(stderr,	"no .symtab in file: %s\n", fname);
		goto out;
	}

	if (!strtab_sec) {
		fprintf(stderr,	"no .strtab in file: %s\n", fname);
		goto out;
	}

	extab_image = (void *)ehdr + _r(&extab_sec->sh_offset);
	strtab = (const char *)ehdr + _r(&strtab_sec->sh_offset);
	symtab = (const Elf_Sym *)((const char *)ehdr +
						  _r(&symtab_sec->sh_offset));

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

	/* find the flag main_extable_sort_needed */
	for (sym = (void *)ehdr + _r(&symtab_sec->sh_offset);
	     sym < sym + _r(&symtab_sec->sh_size) / sizeof(Elf_Sym);
	     sym++) {
		if (ELF_ST_TYPE(sym->st_info) != STT_OBJECT)
			continue;
		if (!strcmp(strtab + r(&sym->st_name),
			    "main_extable_sort_needed")) {
			sort_needed_sym = sym;
			break;
		}
	}

	if (!sort_needed_sym) {
		fprintf(stderr,
			"no main_extable_sort_needed symbol in file: %s\n",
			fname);
		goto out;
	}

	sort_needed_sec = &shdr[get_secindex(r2(&sym->st_shndx),
					     sort_needed_sym - symtab,
					     symtab_shndx)];
	sort_needed_loc = (void *)ehdr +
		_r(&sort_needed_sec->sh_offset) +
		_r(&sort_needed_sym->st_value) -
		_r(&sort_needed_sec->sh_addr);

	/* extable has been sorted, clear the flag */
	w(0, sort_needed_loc);
	rc = 0;

out:
#if defined(SORTTABLE_64) && defined(UNWINDER_ORC_ENABLED)
	if (orc_sort_thread) {
		void *retval = NULL;
		/* wait for ORC tables sort done */
		rc = pthread_join(orc_sort_thread, &retval);
		if (rc) {
			fprintf(stderr,
				"pthread_join failed '%s': %s\n",
				strerror(errno), fname);
		} else if (retval) {
			rc = -1;
			fprintf(stderr,
				"failed to sort ORC tables '%s': %s\n",
				(char *)retval, fname);
		}
	}
#endif

#ifdef MCOUNT_SORT_ENABLED
	if (mcount_sort_thread) {
		void *retval = NULL;
		/* wait for mcount sort done */
		rc = pthread_join(mcount_sort_thread, &retval);
		if (rc) {
			fprintf(stderr,
				"pthread_join failed '%s': %s\n",
				strerror(errno), fname);
		} else if (retval) {
			rc = -1;
			fprintf(stderr,
				"failed to sort mcount '%s': %s\n",
				(char *)retval, fname);
		}
	}
#endif
	return rc;
}
