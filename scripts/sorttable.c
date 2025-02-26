// SPDX-License-Identifier: GPL-2.0-only
/*
 * sorttable.c: Sort the kernel's table
 *
 * Added ORC unwind tables sort support and other updates:
 * Copyright (C) 1999-2019 Alibaba Group Holding Limited. by:
 * Shile Zhang <shile.zhang@linux.alibaba.com>
 *
 * Copyright 2011 - 2012 Cavium, Inc.
 *
 * Based on code taken from recortmcount.c which is:
 *
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>.  All rights reserved.
 *
 * Restructured to fit Linux format, as well as other updates:
 * Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
 */

/*
 * Strategy: alter the vmlinux file in-place.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <getopt.h>
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <tools/be_byteshift.h>
#include <tools/le_byteshift.h>

#ifndef EM_ARCOMPACT
#define EM_ARCOMPACT	93
#endif

#ifndef EM_XTENSA
#define EM_XTENSA	94
#endif

#ifndef EM_AARCH64
#define EM_AARCH64	183
#endif

#ifndef EM_MICROBLAZE
#define EM_MICROBLAZE	189
#endif

#ifndef EM_ARCV2
#define EM_ARCV2	195
#endif

#ifndef EM_RISCV
#define EM_RISCV	243
#endif

#ifndef EM_LOONGARCH
#define EM_LOONGARCH	258
#endif

typedef union {
	Elf32_Ehdr	e32;
	Elf64_Ehdr	e64;
} Elf_Ehdr;

typedef union {
	Elf32_Shdr	e32;
	Elf64_Shdr	e64;
} Elf_Shdr;

typedef union {
	Elf32_Sym	e32;
	Elf64_Sym	e64;
} Elf_Sym;

static uint32_t (*r)(const uint32_t *);
static uint16_t (*r2)(const uint16_t *);
static uint64_t (*r8)(const uint64_t *);
static void (*w)(uint32_t, uint32_t *);
typedef void (*table_sort_t)(char *, int);

static struct elf_funcs {
	int (*compare_extable)(const void *a, const void *b);
	uint64_t (*ehdr_shoff)(Elf_Ehdr *ehdr);
	uint16_t (*ehdr_shstrndx)(Elf_Ehdr *ehdr);
	uint16_t (*ehdr_shentsize)(Elf_Ehdr *ehdr);
	uint16_t (*ehdr_shnum)(Elf_Ehdr *ehdr);
	uint64_t (*shdr_addr)(Elf_Shdr *shdr);
	uint64_t (*shdr_offset)(Elf_Shdr *shdr);
	uint64_t (*shdr_size)(Elf_Shdr *shdr);
	uint64_t (*shdr_entsize)(Elf_Shdr *shdr);
	uint32_t (*shdr_link)(Elf_Shdr *shdr);
	uint32_t (*shdr_name)(Elf_Shdr *shdr);
	uint32_t (*shdr_type)(Elf_Shdr *shdr);
	uint8_t (*sym_type)(Elf_Sym *sym);
	uint32_t (*sym_name)(Elf_Sym *sym);
	uint64_t (*sym_value)(Elf_Sym *sym);
	uint16_t (*sym_shndx)(Elf_Sym *sym);
} e;

static uint64_t ehdr64_shoff(Elf_Ehdr *ehdr)
{
	return r8(&ehdr->e64.e_shoff);
}

static uint64_t ehdr32_shoff(Elf_Ehdr *ehdr)
{
	return r(&ehdr->e32.e_shoff);
}

static uint64_t ehdr_shoff(Elf_Ehdr *ehdr)
{
	return e.ehdr_shoff(ehdr);
}

#define EHDR_HALF(fn_name)				\
static uint16_t ehdr64_##fn_name(Elf_Ehdr *ehdr)	\
{							\
	return r2(&ehdr->e64.e_##fn_name);		\
}							\
							\
static uint16_t ehdr32_##fn_name(Elf_Ehdr *ehdr)	\
{							\
	return r2(&ehdr->e32.e_##fn_name);		\
}							\
							\
static uint16_t ehdr_##fn_name(Elf_Ehdr *ehdr)		\
{							\
	return e.ehdr_##fn_name(ehdr);			\
}

EHDR_HALF(shentsize)
EHDR_HALF(shstrndx)
EHDR_HALF(shnum)

#define SHDR_WORD(fn_name)				\
static uint32_t shdr64_##fn_name(Elf_Shdr *shdr)	\
{							\
	return r(&shdr->e64.sh_##fn_name);		\
}							\
							\
static uint32_t shdr32_##fn_name(Elf_Shdr *shdr)	\
{							\
	return r(&shdr->e32.sh_##fn_name);		\
}							\
							\
static uint32_t shdr_##fn_name(Elf_Shdr *shdr)		\
{							\
	return e.shdr_##fn_name(shdr);			\
}

#define SHDR_ADDR(fn_name)				\
static uint64_t shdr64_##fn_name(Elf_Shdr *shdr)	\
{							\
	return r8(&shdr->e64.sh_##fn_name);		\
}							\
							\
static uint64_t shdr32_##fn_name(Elf_Shdr *shdr)	\
{							\
	return r(&shdr->e32.sh_##fn_name);		\
}							\
							\
static uint64_t shdr_##fn_name(Elf_Shdr *shdr)		\
{							\
	return e.shdr_##fn_name(shdr);			\
}

#define SHDR_WORD(fn_name)				\
static uint32_t shdr64_##fn_name(Elf_Shdr *shdr)	\
{							\
	return r(&shdr->e64.sh_##fn_name);		\
}							\
							\
static uint32_t shdr32_##fn_name(Elf_Shdr *shdr)	\
{							\
	return r(&shdr->e32.sh_##fn_name);		\
}							\
static uint32_t shdr_##fn_name(Elf_Shdr *shdr)		\
{							\
	return e.shdr_##fn_name(shdr);			\
}

SHDR_ADDR(addr)
SHDR_ADDR(offset)
SHDR_ADDR(size)
SHDR_ADDR(entsize)

SHDR_WORD(link)
SHDR_WORD(name)
SHDR_WORD(type)

#define SYM_ADDR(fn_name)			\
static uint64_t sym64_##fn_name(Elf_Sym *sym)	\
{						\
	return r8(&sym->e64.st_##fn_name);	\
}						\
						\
static uint64_t sym32_##fn_name(Elf_Sym *sym)	\
{						\
	return r(&sym->e32.st_##fn_name);	\
}						\
						\
static uint64_t sym_##fn_name(Elf_Sym *sym)	\
{						\
	return e.sym_##fn_name(sym);		\
}

#define SYM_WORD(fn_name)			\
static uint32_t sym64_##fn_name(Elf_Sym *sym)	\
{						\
	return r(&sym->e64.st_##fn_name);	\
}						\
						\
static uint32_t sym32_##fn_name(Elf_Sym *sym)	\
{						\
	return r(&sym->e32.st_##fn_name);	\
}						\
						\
static uint32_t sym_##fn_name(Elf_Sym *sym)	\
{						\
	return e.sym_##fn_name(sym);		\
}

#define SYM_HALF(fn_name)			\
static uint16_t sym64_##fn_name(Elf_Sym *sym)	\
{						\
	return r2(&sym->e64.st_##fn_name);	\
}						\
						\
static uint16_t sym32_##fn_name(Elf_Sym *sym)	\
{						\
	return r2(&sym->e32.st_##fn_name);	\
}						\
						\
static uint16_t sym_##fn_name(Elf_Sym *sym)	\
{						\
	return e.sym_##fn_name(sym);		\
}

static uint8_t sym64_type(Elf_Sym *sym)
{
	return ELF64_ST_TYPE(sym->e64.st_info);
}

static uint8_t sym32_type(Elf_Sym *sym)
{
	return ELF32_ST_TYPE(sym->e32.st_info);
}

static uint8_t sym_type(Elf_Sym *sym)
{
	return e.sym_type(sym);
}

SYM_ADDR(value)
SYM_WORD(name)
SYM_HALF(shndx)

/*
 * Get the whole file as a programming convenience in order to avoid
 * malloc+lseek+read+free of many pieces.  If successful, then mmap
 * avoids copying unused pieces; else just read the whole file.
 * Open for both read and write.
 */
static void *mmap_file(char const *fname, size_t *size)
{
	int fd;
	struct stat sb;
	void *addr = NULL;

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		perror(fname);
		return NULL;
	}
	if (fstat(fd, &sb) < 0) {
		perror(fname);
		goto out;
	}
	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "not a regular file: %s\n", fname);
		goto out;
	}

	addr = mmap(0, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Could not mmap file: %s\n", fname);
		goto out;
	}

	*size = sb.st_size;

out:
	close(fd);
	return addr;
}

static uint32_t rbe(const uint32_t *x)
{
	return get_unaligned_be32(x);
}

static uint16_t r2be(const uint16_t *x)
{
	return get_unaligned_be16(x);
}

static uint64_t r8be(const uint64_t *x)
{
	return get_unaligned_be64(x);
}

static uint32_t rle(const uint32_t *x)
{
	return get_unaligned_le32(x);
}

static uint16_t r2le(const uint16_t *x)
{
	return get_unaligned_le16(x);
}

static uint64_t r8le(const uint64_t *x)
{
	return get_unaligned_le64(x);
}

static void wbe(uint32_t val, uint32_t *x)
{
	put_unaligned_be32(val, x);
}

static void wle(uint32_t val, uint32_t *x)
{
	put_unaligned_le32(val, x);
}

/*
 * Move reserved section indices SHN_LORESERVE..SHN_HIRESERVE out of
 * the way to -256..-1, to avoid conflicting with real section
 * indices.
 */
#define SPECIAL(i) ((i) - (SHN_HIRESERVE + 1))

static inline int is_shndx_special(unsigned int i)
{
	return i != SHN_XINDEX && i >= SHN_LORESERVE && i <= SHN_HIRESERVE;
}

/* Accessor for sym->st_shndx, hides ugliness of "64k sections" */
static inline unsigned int get_secindex(unsigned int shndx,
					unsigned int sym_offs,
					const Elf32_Word *symtab_shndx_start)
{
	if (is_shndx_special(shndx))
		return SPECIAL(shndx);
	if (shndx != SHN_XINDEX)
		return shndx;
	return r(&symtab_shndx_start[sym_offs]);
}

static int compare_extable_32(const void *a, const void *b)
{
	Elf32_Addr av = r(a);
	Elf32_Addr bv = r(b);

	if (av < bv)
		return -1;
	return av > bv;
}

static int compare_extable_64(const void *a, const void *b)
{
	Elf64_Addr av = r8(a);
	Elf64_Addr bv = r8(b);

	if (av < bv)
		return -1;
	return av > bv;
}

static int compare_extable(const void *a, const void *b)
{
	return e.compare_extable(a, b);
}

static inline void *get_index(void *start, int entsize, int index)
{
	return start + (entsize * index);
}

static int extable_ent_size;
static int long_size;


#ifdef UNWINDER_ORC_ENABLED
/* ORC unwinder only support X86_64 */
#include <asm/orc_types.h>

#define ERRSTR_MAXSZ	256

static char g_err[ERRSTR_MAXSZ];
static int *g_orc_ip_table;
static struct orc_entry *g_orc_table;

static pthread_t orc_sort_thread;

static inline unsigned long orc_ip(const int *ip)
{
	return (unsigned long)ip + *ip;
}

static int orc_sort_cmp(const void *_a, const void *_b)
{
	struct orc_entry *orc_a, *orc_b;
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
	orc_b = g_orc_table + (b - g_orc_ip_table);
	if (orc_a->type == ORC_TYPE_UNDEFINED && orc_b->type == ORC_TYPE_UNDEFINED)
		return 0;
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

#ifdef MCOUNT_SORT_ENABLED
static pthread_t mcount_sort_thread;

struct elf_mcount_loc {
	Elf_Ehdr *ehdr;
	Elf_Shdr *init_data_sec;
	uint64_t start_mcount_loc;
	uint64_t stop_mcount_loc;
};

/* Sort the addresses stored between __start_mcount_loc to __stop_mcount_loc in vmlinux */
static void *sort_mcount_loc(void *arg)
{
	struct elf_mcount_loc *emloc = (struct elf_mcount_loc *)arg;
	uint64_t offset = emloc->start_mcount_loc - shdr_addr(emloc->init_data_sec)
					+ shdr_offset(emloc->init_data_sec);
	uint64_t count = emloc->stop_mcount_loc - emloc->start_mcount_loc;
	unsigned char *start_loc = (void *)emloc->ehdr + offset;

	qsort(start_loc, count/long_size, long_size, compare_extable);
	return NULL;
}

/* Get the address of __start_mcount_loc and __stop_mcount_loc in System.map */
static void get_mcount_loc(struct elf_mcount_loc *emloc, Elf_Shdr *symtab_sec,
			   const char *strtab)
{
	Elf_Sym *sym, *end_sym;
	int symentsize = shdr_entsize(symtab_sec);
	int found = 0;

	sym = (void *)emloc->ehdr + shdr_offset(symtab_sec);
	end_sym = (void *)sym + shdr_size(symtab_sec);

	while (sym < end_sym) {
		if (!strcmp(strtab + sym_name(sym), "__start_mcount_loc")) {
			emloc->start_mcount_loc = sym_value(sym);
			if (++found == 2)
				break;
		} else if (!strcmp(strtab + sym_name(sym), "__stop_mcount_loc")) {
			emloc->stop_mcount_loc = sym_value(sym);
			if (++found == 2)
				break;
		}
		sym = (void *)sym + symentsize;
	}

	if (!emloc->start_mcount_loc) {
		fprintf(stderr, "get start_mcount_loc error!");
		return;
	}

	if (!emloc->stop_mcount_loc) {
		fprintf(stderr, "get stop_mcount_loc error!");
		return;
	}
}
#endif

static int do_sort(Elf_Ehdr *ehdr,
		   char const *const fname,
		   table_sort_t custom_sort)
{
	int rc = -1;
	Elf_Shdr *shdr_start;
	Elf_Shdr *strtab_sec = NULL;
	Elf_Shdr *symtab_sec = NULL;
	Elf_Shdr *extab_sec = NULL;
	Elf_Shdr *string_sec;
	Elf_Sym *sym;
	const Elf_Sym *symtab;
	Elf32_Word *symtab_shndx = NULL;
	Elf_Sym *sort_needed_sym = NULL;
	Elf_Shdr *sort_needed_sec;
	uint32_t *sort_needed_loc;
	void *sym_start;
	void *sym_end;
	const char *secstrings;
	const char *strtab;
	char *extab_image;
	int sort_need_index;
	int symentsize;
	int shentsize;
	int idx;
	int i;
	unsigned int shnum;
	unsigned int shstrndx;
#ifdef MCOUNT_SORT_ENABLED
	struct elf_mcount_loc mstruct = {0};
#endif
#ifdef UNWINDER_ORC_ENABLED
	unsigned int orc_ip_size = 0;
	unsigned int orc_size = 0;
	unsigned int orc_num_entries = 0;
#endif

	shdr_start = (Elf_Shdr *)((char *)ehdr + ehdr_shoff(ehdr));
	shentsize = ehdr_shentsize(ehdr);

	shstrndx = ehdr_shstrndx(ehdr);
	if (shstrndx == SHN_XINDEX)
		shstrndx = shdr_link(shdr_start);
	string_sec = get_index(shdr_start, shentsize, shstrndx);
	secstrings = (const char *)ehdr + shdr_offset(string_sec);

	shnum = ehdr_shnum(ehdr);
	if (shnum == SHN_UNDEF)
		shnum = shdr_size(shdr_start);

	for (i = 0; i < shnum; i++) {
		Elf_Shdr *shdr = get_index(shdr_start, shentsize, i);

		idx = shdr_name(shdr);
		if (!strcmp(secstrings + idx, "__ex_table"))
			extab_sec = shdr;
		if (!strcmp(secstrings + idx, ".symtab"))
			symtab_sec = shdr;
		if (!strcmp(secstrings + idx, ".strtab"))
			strtab_sec = shdr;

		if (shdr_type(shdr) == SHT_SYMTAB_SHNDX)
			symtab_shndx = (Elf32_Word *)((const char *)ehdr +
						      shdr_offset(shdr));

#ifdef MCOUNT_SORT_ENABLED
		/* locate the .init.data section in vmlinux */
		if (!strcmp(secstrings + idx, ".init.data"))
			mstruct.init_data_sec = shdr;
#endif

#ifdef UNWINDER_ORC_ENABLED
		/* locate the ORC unwind tables */
		if (!strcmp(secstrings + idx, ".orc_unwind_ip")) {
			orc_ip_size = shdr_size(shdr);
			g_orc_ip_table = (int *)((void *)ehdr +
						   shdr_offset(shdr));
		}
		if (!strcmp(secstrings + idx, ".orc_unwind")) {
			orc_size = shdr_size(shdr);
			g_orc_table = (struct orc_entry *)((void *)ehdr +
							     shdr_offset(shdr));
		}
#endif
	} /* for loop */

#ifdef UNWINDER_ORC_ENABLED
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

	extab_image = (void *)ehdr + shdr_offset(extab_sec);
	strtab = (const char *)ehdr + shdr_offset(strtab_sec);
	symtab = (const Elf_Sym *)((const char *)ehdr + shdr_offset(symtab_sec));

#ifdef MCOUNT_SORT_ENABLED
	mstruct.ehdr = ehdr;
	get_mcount_loc(&mstruct, symtab_sec, strtab);

	if (!mstruct.init_data_sec || !mstruct.start_mcount_loc || !mstruct.stop_mcount_loc) {
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

	if (custom_sort) {
		custom_sort(extab_image, shdr_size(extab_sec));
	} else {
		int num_entries = shdr_size(extab_sec) / extable_ent_size;
		qsort(extab_image, num_entries,
		      extable_ent_size, compare_extable);
	}

	/* find the flag main_extable_sort_needed */
	sym_start = (void *)ehdr + shdr_offset(symtab_sec);
	sym_end = sym_start + shdr_size(symtab_sec);
	symentsize = shdr_entsize(symtab_sec);

	for (sym = sym_start; (void *)sym + symentsize < sym_end;
	     sym = (void *)sym + symentsize) {
		if (sym_type(sym) != STT_OBJECT)
			continue;
		if (!strcmp(strtab + sym_name(sym),
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

	sort_need_index = get_secindex(sym_shndx(sym),
				       ((void *)sort_needed_sym - (void *)symtab) / symentsize,
				       symtab_shndx);
	sort_needed_sec = get_index(shdr_start, shentsize, sort_need_index);
	sort_needed_loc = (void *)ehdr +
		shdr_offset(sort_needed_sec) +
		sym_value(sort_needed_sym) - shdr_addr(sort_needed_sec);

	/* extable has been sorted, clear the flag */
	w(0, sort_needed_loc);
	rc = 0;

out:
#ifdef UNWINDER_ORC_ENABLED
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

static int compare_relative_table(const void *a, const void *b)
{
	int32_t av = (int32_t)r(a);
	int32_t bv = (int32_t)r(b);

	if (av < bv)
		return -1;
	if (av > bv)
		return 1;
	return 0;
}

static void sort_relative_table(char *extab_image, int image_size)
{
	int i = 0;

	/*
	 * Do the same thing the runtime sort does, first normalize to
	 * being relative to the start of the section.
	 */
	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(extab_image + i);
		w(r(loc) + i, loc);
		i += 4;
	}

	qsort(extab_image, image_size / 8, 8, compare_relative_table);

	/* Now denormalize. */
	i = 0;
	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(extab_image + i);
		w(r(loc) - i, loc);
		i += 4;
	}
}

static void sort_relative_table_with_data(char *extab_image, int image_size)
{
	int i = 0;

	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(extab_image + i);

		w(r(loc) + i, loc);
		w(r(loc + 1) + i + 4, loc + 1);
		/* Don't touch the fixup type or data */

		i += sizeof(uint32_t) * 3;
	}

	qsort(extab_image, image_size / 12, 12, compare_relative_table);

	i = 0;
	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(extab_image + i);

		w(r(loc) - i, loc);
		w(r(loc + 1) - (i + 4), loc + 1);
		/* Don't touch the fixup type or data */

		i += sizeof(uint32_t) * 3;
	}
}

static int do_file(char const *const fname, void *addr)
{
	Elf_Ehdr *ehdr = addr;
	table_sort_t custom_sort = NULL;

	switch (ehdr->e32.e_ident[EI_DATA]) {
	case ELFDATA2LSB:
		r	= rle;
		r2	= r2le;
		r8	= r8le;
		w	= wle;
		break;
	case ELFDATA2MSB:
		r	= rbe;
		r2	= r2be;
		r8	= r8be;
		w	= wbe;
		break;
	default:
		fprintf(stderr, "unrecognized ELF data encoding %d: %s\n",
			ehdr->e32.e_ident[EI_DATA], fname);
		return -1;
	}

	if (memcmp(ELFMAG, ehdr->e32.e_ident, SELFMAG) != 0 ||
	    (r2(&ehdr->e32.e_type) != ET_EXEC && r2(&ehdr->e32.e_type) != ET_DYN) ||
	    ehdr->e32.e_ident[EI_VERSION] != EV_CURRENT) {
		fprintf(stderr, "unrecognized ET_EXEC/ET_DYN file %s\n", fname);
		return -1;
	}

	switch (r2(&ehdr->e32.e_machine)) {
	case EM_386:
	case EM_AARCH64:
	case EM_LOONGARCH:
	case EM_RISCV:
	case EM_S390:
	case EM_X86_64:
		custom_sort = sort_relative_table_with_data;
		break;
	case EM_PARISC:
	case EM_PPC:
	case EM_PPC64:
		custom_sort = sort_relative_table;
		break;
	case EM_ARCOMPACT:
	case EM_ARCV2:
	case EM_ARM:
	case EM_MICROBLAZE:
	case EM_MIPS:
	case EM_XTENSA:
		break;
	default:
		fprintf(stderr, "unrecognized e_machine %d %s\n",
			r2(&ehdr->e32.e_machine), fname);
		return -1;
	}

	switch (ehdr->e32.e_ident[EI_CLASS]) {
	case ELFCLASS32: {
		struct elf_funcs efuncs = {
			.compare_extable	= compare_extable_32,
			.ehdr_shoff		= ehdr32_shoff,
			.ehdr_shentsize		= ehdr32_shentsize,
			.ehdr_shstrndx		= ehdr32_shstrndx,
			.ehdr_shnum		= ehdr32_shnum,
			.shdr_addr		= shdr32_addr,
			.shdr_offset		= shdr32_offset,
			.shdr_link		= shdr32_link,
			.shdr_size		= shdr32_size,
			.shdr_name		= shdr32_name,
			.shdr_type		= shdr32_type,
			.shdr_entsize		= shdr32_entsize,
			.sym_type		= sym32_type,
			.sym_name		= sym32_name,
			.sym_value		= sym32_value,
			.sym_shndx		= sym32_shndx,
		};

		e = efuncs;
		long_size		= 4;
		extable_ent_size	= 8;

		if (r2(&ehdr->e32.e_ehsize) != sizeof(Elf32_Ehdr) ||
		    r2(&ehdr->e32.e_shentsize) != sizeof(Elf32_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n", fname);
			return -1;
		}

		}
		break;
	case ELFCLASS64: {
		struct elf_funcs efuncs = {
			.compare_extable	= compare_extable_64,
			.ehdr_shoff		= ehdr64_shoff,
			.ehdr_shentsize		= ehdr64_shentsize,
			.ehdr_shstrndx		= ehdr64_shstrndx,
			.ehdr_shnum		= ehdr64_shnum,
			.shdr_addr		= shdr64_addr,
			.shdr_offset		= shdr64_offset,
			.shdr_link		= shdr64_link,
			.shdr_size		= shdr64_size,
			.shdr_name		= shdr64_name,
			.shdr_type		= shdr64_type,
			.shdr_entsize		= shdr64_entsize,
			.sym_type		= sym64_type,
			.sym_name		= sym64_name,
			.sym_value		= sym64_value,
			.sym_shndx		= sym64_shndx,
		};

		e = efuncs;
		long_size		= 8;
		extable_ent_size	= 16;

		if (r2(&ehdr->e64.e_ehsize) != sizeof(Elf64_Ehdr) ||
		    r2(&ehdr->e64.e_shentsize) != sizeof(Elf64_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n",
				fname);
			return -1;
		}

		}
		break;
	default:
		fprintf(stderr, "unrecognized ELF class %d %s\n",
			ehdr->e32.e_ident[EI_CLASS], fname);
		return -1;
	}

	return do_sort(ehdr, fname, custom_sort);
}

int main(int argc, char *argv[])
{
	int i, n_error = 0;  /* gcc-4.3.0 false positive complaint */
	size_t size = 0;
	void *addr = NULL;

	if (argc < 2) {
		fprintf(stderr, "usage: sorttable vmlinux...\n");
		return 0;
	}

	/* Process each file in turn, allowing deep failure. */
	for (i = 1; i < argc; i++) {
		addr = mmap_file(argv[i], &size);
		if (!addr) {
			++n_error;
			continue;
		}

		if (do_file(argv[i], addr))
			++n_error;

		munmap(addr, size);
	}

	return !!n_error;
}
