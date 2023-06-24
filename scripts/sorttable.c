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

static uint32_t (*r)(const uint32_t *);
static uint16_t (*r2)(const uint16_t *);
static uint64_t (*r8)(const uint64_t *);
static void (*w)(uint32_t, uint32_t *);
static void (*w2)(uint16_t, uint16_t *);
static void (*w8)(uint64_t, uint64_t *);
typedef void (*table_sort_t)(char *, int);

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

static void w2be(uint16_t val, uint16_t *x)
{
	put_unaligned_be16(val, x);
}

static void w8be(uint64_t val, uint64_t *x)
{
	put_unaligned_be64(val, x);
}

static void wle(uint32_t val, uint32_t *x)
{
	put_unaligned_le32(val, x);
}

static void w2le(uint16_t val, uint16_t *x)
{
	put_unaligned_le16(val, x);
}

static void w8le(uint64_t val, uint64_t *x)
{
	put_unaligned_le64(val, x);
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

/* 32 bit and 64 bit are very similar */
#include "sorttable.h"
#define SORTTABLE_64
#include "sorttable.h"

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
	int rc = -1;
	Elf32_Ehdr *ehdr = addr;
	table_sort_t custom_sort = NULL;

	switch (ehdr->e_ident[EI_DATA]) {
	case ELFDATA2LSB:
		r	= rle;
		r2	= r2le;
		r8	= r8le;
		w	= wle;
		w2	= w2le;
		w8	= w8le;
		break;
	case ELFDATA2MSB:
		r	= rbe;
		r2	= r2be;
		r8	= r8be;
		w	= wbe;
		w2	= w2be;
		w8	= w8be;
		break;
	default:
		fprintf(stderr, "unrecognized ELF data encoding %d: %s\n",
			ehdr->e_ident[EI_DATA], fname);
		return -1;
	}

	if (memcmp(ELFMAG, ehdr->e_ident, SELFMAG) != 0 ||
	    (r2(&ehdr->e_type) != ET_EXEC && r2(&ehdr->e_type) != ET_DYN) ||
	    ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
		fprintf(stderr, "unrecognized ET_EXEC/ET_DYN file %s\n", fname);
		return -1;
	}

	switch (r2(&ehdr->e_machine)) {
	case EM_386:
	case EM_AARCH64:
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
	case EM_LOONGARCH:
	case EM_MICROBLAZE:
	case EM_MIPS:
	case EM_XTENSA:
		break;
	default:
		fprintf(stderr, "unrecognized e_machine %d %s\n",
			r2(&ehdr->e_machine), fname);
		return -1;
	}

	switch (ehdr->e_ident[EI_CLASS]) {
	case ELFCLASS32:
		if (r2(&ehdr->e_ehsize) != sizeof(Elf32_Ehdr) ||
		    r2(&ehdr->e_shentsize) != sizeof(Elf32_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n", fname);
			break;
		}
		rc = do_sort_32(ehdr, fname, custom_sort);
		break;
	case ELFCLASS64:
		{
		Elf64_Ehdr *const ghdr = (Elf64_Ehdr *)ehdr;
		if (r2(&ghdr->e_ehsize) != sizeof(Elf64_Ehdr) ||
		    r2(&ghdr->e_shentsize) != sizeof(Elf64_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n",
				fname);
			break;
		}
		rc = do_sort_64(ghdr, fname, custom_sort);
		}
		break;
	default:
		fprintf(stderr, "unrecognized ELF class %d %s\n",
			ehdr->e_ident[EI_CLASS], fname);
		break;
	}

	return rc;
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
