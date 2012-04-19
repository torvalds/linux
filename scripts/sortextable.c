/*
 * sortextable.c: Sort the kernel's exception table
 *
 * Copyright 2011 Cavium, Inc.
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

static void __attribute__((noreturn))
succeed_file(void)
{
	cleanup();
	longjmp(jmpenv, SJ_SUCCEED);
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

/* w8rev, w8nat, ...: Handle endianness. */

static uint64_t w8rev(uint64_t const x)
{
	return   ((0xff & (x >> (0 * 8))) << (7 * 8))
	       | ((0xff & (x >> (1 * 8))) << (6 * 8))
	       | ((0xff & (x >> (2 * 8))) << (5 * 8))
	       | ((0xff & (x >> (3 * 8))) << (4 * 8))
	       | ((0xff & (x >> (4 * 8))) << (3 * 8))
	       | ((0xff & (x >> (5 * 8))) << (2 * 8))
	       | ((0xff & (x >> (6 * 8))) << (1 * 8))
	       | ((0xff & (x >> (7 * 8))) << (0 * 8));
}

static uint32_t w4rev(uint32_t const x)
{
	return   ((0xff & (x >> (0 * 8))) << (3 * 8))
	       | ((0xff & (x >> (1 * 8))) << (2 * 8))
	       | ((0xff & (x >> (2 * 8))) << (1 * 8))
	       | ((0xff & (x >> (3 * 8))) << (0 * 8));
}

static uint32_t w2rev(uint16_t const x)
{
	return   ((0xff & (x >> (0 * 8))) << (1 * 8))
	       | ((0xff & (x >> (1 * 8))) << (0 * 8));
}

static uint64_t w8nat(uint64_t const x)
{
	return x;
}

static uint32_t w4nat(uint32_t const x)
{
	return x;
}

static uint32_t w2nat(uint16_t const x)
{
	return x;
}

static uint64_t (*w8)(uint64_t);
static uint32_t (*w)(uint32_t);
static uint32_t (*w2)(uint16_t);


/* 32 bit and 64 bit are very similar */
#include "sortextable.h"
#define SORTEXTABLE_64
#include "sortextable.h"


static void
do_file(char const *const fname)
{
	Elf32_Ehdr *const ehdr = mmap_file(fname);

	ehdr_curr = ehdr;
	w = w4nat;
	w2 = w2nat;
	w8 = w8nat;
	switch (ehdr->e_ident[EI_DATA]) {
		static unsigned int const endian = 1;
	default:
		fprintf(stderr, "unrecognized ELF data encoding %d: %s\n",
			ehdr->e_ident[EI_DATA], fname);
		fail_file();
		break;
	case ELFDATA2LSB:
		if (*(unsigned char const *)&endian != 1) {
			/* main() is big endian, file.o is little endian. */
			w = w4rev;
			w2 = w2rev;
			w8 = w8rev;
		}
		break;
	case ELFDATA2MSB:
		if (*(unsigned char const *)&endian != 0) {
			/* main() is little endian, file.o is big endian. */
			w = w4rev;
			w2 = w2rev;
			w8 = w8rev;
		}
		break;
	}  /* end switch */
	if (memcmp(ELFMAG, ehdr->e_ident, SELFMAG) != 0
	||  w2(ehdr->e_type) != ET_EXEC
	||  ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
		fprintf(stderr, "unrecognized ET_EXEC file %s\n", fname);
		fail_file();
	}

	switch (w2(ehdr->e_machine)) {
	default:
		fprintf(stderr, "unrecognized e_machine %d %s\n",
			w2(ehdr->e_machine), fname);
		fail_file();
		break;
	case EM_386:
	case EM_MIPS:
	case EM_X86_64:
		break;
	}  /* end switch */

	switch (ehdr->e_ident[EI_CLASS]) {
	default:
		fprintf(stderr, "unrecognized ELF class %d %s\n",
			ehdr->e_ident[EI_CLASS], fname);
		fail_file();
		break;
	case ELFCLASS32:
		if (w2(ehdr->e_ehsize) != sizeof(Elf32_Ehdr)
		||  w2(ehdr->e_shentsize) != sizeof(Elf32_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC file: %s\n", fname);
			fail_file();
		}
		do32(ehdr, fname);
		break;
	case ELFCLASS64: {
		Elf64_Ehdr *const ghdr = (Elf64_Ehdr *)ehdr;
		if (w2(ghdr->e_ehsize) != sizeof(Elf64_Ehdr)
		||  w2(ghdr->e_shentsize) != sizeof(Elf64_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC file: %s\n", fname);
			fail_file();
		}
		do64(ghdr, fname);
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
