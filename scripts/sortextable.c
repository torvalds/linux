/*
 * sortextable.c: Sort the kernel's exception table
 *
 * Copyright 2011 - 2012 Cavium, Inc.
 *
 * Based on code taken from recortmcount.c which is:
 *
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>.  All rights reserved.
 * Licensed under the GNU General Public License, version 2 (GPLv2).
 *
 * Restructured to fit Linux format, as well as other updates:
 *  Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
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
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int fd_map;	/* File descriptor for file being modified. */
static int mmap_failed; /* Boolean flag. */
static void *ehdr_curr; /* current ElfXX_Ehdr *  for resource cleanup */
static struct stat sb;	/* Remember .st_size, etc. */
static jmp_buf jmpenv;	/* setjmp/longjmp per-file error escape */

/* setjmp() return values */
enum {
	SJ_SETJMP = 0,  /* hardwired first return */
	SJ_FAIL,
	SJ_SUCCEED
};

/* Per-file resource cleanup when multiple files. */
static void
cleanup(void)
{
	if (!mmap_failed)
		munmap(ehdr_curr, sb.st_size);
	close(fd_map);
}

static void __attribute__((noreturn))
fail_file(void)
{
	cleanup();
	longjmp(jmpenv, SJ_FAIL);
}

/*
 * Get the whole file as a programming convenience in order to avoid
 * malloc+lseek+read+free of many pieces.  If successful, then mmap
 * avoids copying unused pieces; else just read the whole file.
 * Open for both read and write.
 */
static void *mmap_file(char const *fname)
{
	void *addr;

	fd_map = open(fname, O_RDWR);
	if (fd_map < 0 || fstat(fd_map, &sb) < 0) {
		perror(fname);
		fail_file();
	}
	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "not a regular file: %s\n", fname);
		fail_file();
	}
	addr = mmap(0, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		    fd_map, 0);
	if (addr == MAP_FAILED) {
		mmap_failed = 1;
		fprintf(stderr, "Could not mmap file: %s\n", fname);
		fail_file();
	}
	return addr;
}

static uint64_t r8be(const uint64_t *x)
{
	return get_unaligned_be64(x);
}
static uint32_t rbe(const uint32_t *x)
{
	return get_unaligned_be32(x);
}
static uint16_t r2be(const uint16_t *x)
{
	return get_unaligned_be16(x);
}
static uint64_t r8le(const uint64_t *x)
{
	return get_unaligned_le64(x);
}
static uint32_t rle(const uint32_t *x)
{
	return get_unaligned_le32(x);
}
static uint16_t r2le(const uint16_t *x)
{
	return get_unaligned_le16(x);
}

static void w8be(uint64_t val, uint64_t *x)
{
	put_unaligned_be64(val, x);
}
static void wbe(uint32_t val, uint32_t *x)
{
	put_unaligned_be32(val, x);
}
static void w2be(uint16_t val, uint16_t *x)
{
	put_unaligned_be16(val, x);
}
static void w8le(uint64_t val, uint64_t *x)
{
	put_unaligned_le64(val, x);
}
static void wle(uint32_t val, uint32_t *x)
{
	put_unaligned_le32(val, x);
}
static void w2le(uint16_t val, uint16_t *x)
{
	put_unaligned_le16(val, x);
}

static uint64_t (*r8)(const uint64_t *);
static uint32_t (*r)(const uint32_t *);
static uint16_t (*r2)(const uint16_t *);
static void (*w8)(uint64_t, uint64_t *);
static void (*w)(uint32_t, uint32_t *);
static void (*w2)(uint16_t, uint16_t *);

typedef void (*table_sort_t)(char *, int);

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

/* 32 bit and 64 bit are very similar */
#include "sortextable.h"
#define SORTEXTABLE_64
#include "sortextable.h"

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
	int i;

	/*
	 * Do the same thing the runtime sort does, first normalize to
	 * being relative to the start of the section.
	 */
	i = 0;
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

static void
do_file(char const *const fname)
{
	table_sort_t custom_sort;
	Elf32_Ehdr *ehdr = mmap_file(fname);

	ehdr_curr = ehdr;
	switch (ehdr->e_ident[EI_DATA]) {
	default:
		fprintf(stderr, "unrecognized ELF data encoding %d: %s\n",
			ehdr->e_ident[EI_DATA], fname);
		fail_file();
		break;
	case ELFDATA2LSB:
		r = rle;
		r2 = r2le;
		r8 = r8le;
		w = wle;
		w2 = w2le;
		w8 = w8le;
		break;
	case ELFDATA2MSB:
		r = rbe;
		r2 = r2be;
		r8 = r8be;
		w = wbe;
		w2 = w2be;
		w8 = w8be;
		break;
	}  /* end switch */
	if (memcmp(ELFMAG, ehdr->e_ident, SELFMAG) != 0
	||  r2(&ehdr->e_type) != ET_EXEC
	||  ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
		fprintf(stderr, "unrecognized ET_EXEC file %s\n", fname);
		fail_file();
	}

	custom_sort = NULL;
	switch (r2(&ehdr->e_machine)) {
	default:
		fprintf(stderr, "unrecognized e_machine %d %s\n",
			r2(&ehdr->e_machine), fname);
		fail_file();
		break;
	case EM_386:
	case EM_X86_64:
	case EM_S390:
		custom_sort = sort_relative_table;
		break;
	case EM_ARCOMPACT:
	case EM_ARM:
	case EM_AARCH64:
	case EM_MICROBLAZE:
	case EM_MIPS:
	case EM_XTENSA:
		break;
	}  /* end switch */

	switch (ehdr->e_ident[EI_CLASS]) {
	default:
		fprintf(stderr, "unrecognized ELF class %d %s\n",
			ehdr->e_ident[EI_CLASS], fname);
		fail_file();
		break;
	case ELFCLASS32:
		if (r2(&ehdr->e_ehsize) != sizeof(Elf32_Ehdr)
		||  r2(&ehdr->e_shentsize) != sizeof(Elf32_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC file: %s\n", fname);
			fail_file();
		}
		do32(ehdr, fname, custom_sort);
		break;
	case ELFCLASS64: {
		Elf64_Ehdr *const ghdr = (Elf64_Ehdr *)ehdr;
		if (r2(&ghdr->e_ehsize) != sizeof(Elf64_Ehdr)
		||  r2(&ghdr->e_shentsize) != sizeof(Elf64_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC file: %s\n", fname);
			fail_file();
		}
		do64(ghdr, fname, custom_sort);
		break;
	}
	}  /* end switch */

	cleanup();
}

int
main(int argc, char *argv[])
{
	int n_error = 0;  /* gcc-4.3.0 false positive complaint */
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: sortextable vmlinux...\n");
		return 0;
	}

	/* Process each file in turn, allowing deep failure. */
	for (i = 1; i < argc; i++) {
		char *file = argv[i];
		int const sjval = setjmp(jmpenv);

		switch (sjval) {
		default:
			fprintf(stderr, "internal error: %s\n", file);
			exit(1);
			break;
		case SJ_SETJMP:    /* normal sequence */
			/* Avoid problems if early cleanup() */
			fd_map = -1;
			ehdr_curr = NULL;
			mmap_failed = 1;
			do_file(file);
			break;
		case SJ_FAIL:    /* error in do_file or below */
			++n_error;
			break;
		case SJ_SUCCEED:    /* premature success */
			/* do nothing */
			break;
		}  /* end switch */
	}
	return !!n_error;
}
