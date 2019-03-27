/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996-1997 John D. Polstra.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN D. POLSTRA AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JOHN D. POLSTRA OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <machine/elf.h>

#include <arpa/inet.h>

#include <a.out.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <sys/link_aout.h>
#include <stab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#define PAGE_SIZE	4096	/* i386 specific */

#ifndef N_SETA
#define	N_SETA	0x14		/* Absolute set element symbol */
#endif				/* This is input to LD, in a .o file.  */

#ifndef N_SETT
#define	N_SETT	0x16		/* Text set element symbol */
#endif				/* This is input to LD, in a .o file.  */

#ifndef N_SETD
#define	N_SETD	0x18		/* Data set element symbol */
#endif				/* This is input to LD, in a .o file. */

#ifndef N_SETB
#define	N_SETB	0x1A		/* Bss set element symbol */
#endif				/* This is input to LD, in a .o file. */

#ifndef N_SETV
#define N_SETV	0x1C		/* Pointer to set vector in data area. */
#endif				/* This is output from LD. */

#ifdef STANDALONE
static
#endif

static void dump_rels(const char *, const struct relocation_info *,
    unsigned long, const char *(*)(unsigned long), unsigned char *);
static void dump_segs(void);
static void dump_sods(void);
static void dump_sym(const struct nlist *);
static void dump_syms(void);

static void dump_rtsyms(void);

static const char *rtsym_name(unsigned long);
static const char *sym_name(unsigned long);

#ifdef STANDALONE
static
#endif
int error_count;

/*
 * Variables ending in _base are pointers to things in our address space,
 * i.e., in the file itself.
 *
 * Variables ending in _addr are adjusted according to where things would
 * actually appear in memory if the file were loaded.
 */
static const char *file_base;
static const char *text_base;
static const char *data_base;
static const struct relocation_info *rel_base;
static const struct nlist *sym_base;
static const char *str_base;

static const struct relocation_info *rtrel_base;
static const struct nzlist *rtsym_base;
static const char *rtstr_base;

static const struct exec *ex;
static const struct _dynamic *dyn;
static const struct section_dispatch_table *sdt;

static const char *text_addr;
static const char *data_addr;

static unsigned long rel_count;
static unsigned long sym_count;

static unsigned long rtrel_count;
static unsigned long rtsym_count;

/* Dynamically allocated flags, 1 byte per symbol, to record whether each
   symbol was referenced by a relocation entry. */
static unsigned char *sym_used;
static unsigned char *rtsym_used;

static unsigned long origin;	/* What values are relocated relative to */

#ifdef STANDALONE
int
main(int argc, char *argv[])
{
    int i;

    for (i = 1;  i < argc;  ++i)
	dump_file(argv[i]);

    return error_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif

static inline const void *align_struct(const void *expr)
{
  assert(!(((int)expr) & 3));
  return expr;
}

static inline const void *align_long(const void *expr)
{
  assert(!(((int)expr) & 3));
  return expr;
}

static inline const void *align_short(const void *expr)
{
  assert(!(((int)expr) & 1));
  return expr;
}

#ifdef STANDALONE
static
#endif
void
dump_file(const char *fname)
{
    int fd;
    struct stat sb;
    caddr_t objbase;

    if (stat(fname, &sb) == -1) {
	warnx("cannot stat \"%s\"", fname);
	++error_count;
	return;
    }

    if ((sb.st_mode & S_IFMT) != S_IFREG) {
	warnx("\"%s\" is not a regular file", fname);
	++error_count;
	return;
    }

    if ((fd = open(fname, O_RDONLY, 0)) == -1) {
	warnx("cannot open \"%s\"", fname);
	++error_count;
	return;
    }

    objbase = mmap(0, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (objbase == (caddr_t) -1) {
	warnx("cannot mmap \"%s\"", fname);
	++error_count;
	close(fd);
	return;
    }

    close(fd);

    file_base = (const char *) objbase;	/* Makes address arithmetic easier */

    if (IS_ELF(*(const Elf32_Ehdr*) align_struct(file_base))) {
	warnx("%s: this is an ELF program; use readelf to examine", fname);
	++error_count;
	munmap(objbase, sb.st_size);
	return;
    }

    ex = (const struct exec *) align_struct(file_base);

    printf("%s: a_midmag = 0x%lx\n", fname, (long)ex->a_midmag);
    printf("  magic = 0x%lx = 0%lo, netmagic = 0x%lx = 0%lo\n",
	(long)N_GETMAGIC(*ex), (long)N_GETMAGIC(*ex),
	(long)N_GETMAGIC_NET(*ex), (long)N_GETMAGIC_NET(*ex));

    if (N_BADMAG(*ex)) {
	warnx("%s: bad magic number", fname);
	++error_count;
	munmap(objbase, sb.st_size);
	return;
    }

    printf("  a_text   = 0x%lx\n", (long)ex->a_text);
    printf("  a_data   = 0x%lx\n", (long)ex->a_data);
    printf("  a_bss    = 0x%lx\n", (long)ex->a_bss);
    printf("  a_syms   = 0x%lx\n", (long)ex->a_syms);
    printf("  a_entry  = 0x%lx\n", (long)ex->a_entry);
    printf("  a_trsize = 0x%lx\n", (long)ex->a_trsize);
    printf("  a_drsize = 0x%lx\n", (long)ex->a_drsize);

    text_base = file_base + N_TXTOFF(*ex);
    data_base = file_base + N_DATOFF(*ex);
    rel_base = (const struct relocation_info *)
	align_struct(file_base + N_RELOFF(*ex));
    sym_base = (const struct nlist *) align_struct(file_base + N_SYMOFF(*ex));
    str_base = file_base + N_STROFF(*ex);

    rel_count = (ex->a_trsize + ex->a_drsize) / sizeof rel_base[0];
    assert(rel_count * sizeof rel_base[0] == ex->a_trsize + ex->a_drsize);
    sym_count = ex->a_syms / sizeof sym_base[0];
    assert(sym_count * sizeof sym_base[0] == ex->a_syms);

    if (sym_count != 0) {
	sym_used = (unsigned char *) calloc(sym_count, sizeof(unsigned char));
	assert(sym_used != NULL);
    }

    printf("  Entry = 0x%lx\n", (long)ex->a_entry);
    printf("  Text offset = %x, address = %lx\n", N_TXTOFF(*ex),
	(long)N_TXTADDR(*ex));
    printf("  Data offset = %lx, address = %lx\n", (long)N_DATOFF(*ex),
	(long)N_DATADDR(*ex));

    /*
     * In an executable program file, everything is relocated relative to
     * the assumed run-time load address, i.e., N_TXTADDR(*ex), i.e., 0x1000.
     *
     * In a shared library file, everything is relocated relative to the
     * start of the file, i.e., N_TXTOFF(*ex), i.e., 0.
     *
     * The way to tell the difference is by looking at ex->a_entry.   If it
     * is >= 0x1000, then we have an executable program.  Otherwise, we
     * have a shared library.
     *
     * When a program is executed, the entire file is mapped into memory,
     * including the a.out header and so forth.  But it is not mapped at
     * address 0; rather it is mapped at address 0x1000.  The first page
     * of the user's address space is left unmapped in order to catch null
     * pointer dereferences.
     *
     * In this program, when we map in an executable program, we have to
     * simulate the empty page by decrementing our assumed base address by
     * a pagesize.
     */

    text_addr = text_base;
    data_addr = data_base;
    origin = 0;

    if (ex->a_entry >= PAGE_SIZE) {	/* Executable, not a shared library */
	/*
	 * The fields in the object have already been relocated on the
	 * assumption that the object will be loaded at N_TXTADDR(*ex).
	 * We have to compensate for that.
	 */
	text_addr -= PAGE_SIZE;
	data_addr -= PAGE_SIZE;
	origin = PAGE_SIZE;
	printf("  Program, origin = %lx\n", origin);
    } else if (N_GETFLAG(*ex) & EX_DYNAMIC)
	printf("  Shared library, origin = %lx\n", origin);
    else
	printf("  Object file, origin = %lx\n", origin);

    if (N_GETFLAG(*ex) & EX_DYNAMIC) {
	dyn = (const struct _dynamic *) align_struct(data_base);
	printf("  Dynamic version = %d\n", dyn->d_version);

	sdt = (const struct section_dispatch_table *)
	    align_struct(text_addr + (unsigned long) dyn->d_un.d_sdt);

	rtrel_base = (const struct relocation_info *)
	    align_struct(text_addr + sdt->sdt_rel);
	rtrel_count = (sdt->sdt_hash - sdt->sdt_rel) / sizeof rtrel_base[0];
	assert(rtrel_count * sizeof rtrel_base[0] ==
	    (size_t)(sdt->sdt_hash - sdt->sdt_rel));

	rtsym_base = (const struct nzlist *)
	    align_struct(text_addr + sdt->sdt_nzlist);
	rtsym_count = (sdt->sdt_strings - sdt->sdt_nzlist) /
	    sizeof rtsym_base[0];
	assert(rtsym_count * sizeof rtsym_base[0] ==
	    (size_t)(sdt->sdt_strings - sdt->sdt_nzlist));

	if (rtsym_count != 0) {
	    rtsym_used = (unsigned char *) calloc(rtsym_count,
		sizeof(unsigned char));
	    assert(rtsym_used != NULL);
	}

	rtstr_base = text_addr + sdt->sdt_strings;
    }

    dump_segs();
    dump_sods();
    dump_rels("Relocations", rel_base, rel_count, sym_name, sym_used);
    dump_syms();

    dump_rels("Run-time relocations", rtrel_base, rtrel_count, rtsym_name,
	rtsym_used);
    dump_rtsyms();

    if (rtsym_used != NULL) {
	free(rtsym_used);
	rtsym_used = NULL;
    }
    if (sym_used != NULL) {
	free(sym_used);
	sym_used = NULL;
    }
    munmap(objbase, sb.st_size);
}

static void
dump_rels(const char *label, const struct relocation_info *base,
    unsigned long count, const char *(*name)(unsigned long),
    unsigned char *sym_used_flags)
{
    unsigned long i;

    printf("  %s:\n", label);
    for (i = 0;  i < count;  ++i) {
	const struct relocation_info *r = &base[i];
	unsigned int size;
	char contents[16];

	size = 1u << r->r_length;

	if (origin <= (unsigned long)r->r_address
	  && (unsigned long)r->r_address < origin + ex->a_text + ex->a_data
	  && 1 <= size && size <= 4) {
	    /*
	     * XXX - This can cause unaligned accesses.  OK for the
	     * i386, not so for other architectures.
	     */
	    switch (size) {
	    case 1:
		snprintf(contents, sizeof contents, "      [%02x]",
		  *(unsigned const char *)(text_addr + r->r_address));
		break;
	    case 2:
		snprintf(contents, sizeof contents, "    [%04x]",
			 *(unsigned const short *)
			 align_short(text_addr + r->r_address));
		break;
	    case 4:
		snprintf(contents, sizeof contents, "[%08lx]",
			 *(unsigned const long *)
			 align_long(text_addr + r->r_address));
		break;
	    }
	} else
	    snprintf(contents, sizeof contents, "          ");

	printf("    %6lu %8x/%u %s %c%c%c%c%c%c", i,
	    r->r_address, size,
	    contents,
	    r->r_extern   ? 'e' : '-',
	    r->r_jmptable ? 'j' : '-',
	    r->r_relative ? 'r' : '-',
	    r->r_baserel  ? 'b' : '-',
	    r->r_pcrel    ? 'p' : '-',
	    r->r_copy     ? 'c' : '-');

	if (r->r_extern || r->r_baserel || r->r_jmptable || r->r_copy) {
	    printf(" %4u %s", r->r_symbolnum, name(r->r_symbolnum));
	    sym_used_flags[r->r_symbolnum] = 1;
	}

	printf("\n");
    }
}

static void
dump_rtsyms(void)
{
    unsigned long i;

    printf("  Run-time symbols:\n");
    for (i = 0;  i < rtsym_count;  ++i) {
	printf("    %6lu%c ", i, rtsym_used[i] ? '*' : ' ');
	dump_sym(&rtsym_base[i].nlist);
	printf("/%-5ld %s\n", rtsym_base[i].nz_size, rtsym_name(i));
    }
}

static void
dump_segs(void)
{
    printf("  Text segment starts at address %lx\n", origin + N_TXTOFF(*ex));
    if (N_GETFLAG(*ex) & EX_DYNAMIC) {
	printf("    rel starts at %lx\n", sdt->sdt_rel);
	printf("    hash starts at %lx\n", sdt->sdt_hash);
	printf("    nzlist starts at %lx\n", sdt->sdt_nzlist);
	printf("    strings starts at %lx\n", sdt->sdt_strings);
    }

    printf("  Data segment starts at address %lx\n", origin + N_DATOFF(*ex));
    if (N_GETFLAG(*ex) & EX_DYNAMIC) {
	printf("    _dynamic starts at %lx\n", origin + N_DATOFF(*ex));
	printf("    so_debug starts at %lx\n", (unsigned long) dyn->d_debug);
	printf("    sdt starts at %lx\n", (unsigned long) dyn->d_un.d_sdt);
	printf("    got starts at %lx\n", sdt->sdt_got);
	printf("    plt starts at %lx\n", sdt->sdt_plt);
	printf("    rest of stuff starts at %lx\n",
	    sdt->sdt_plt + sdt->sdt_plt_sz);
    }
}

static void
dump_sods(void)
{
    long sod_offset;
    long paths_offset;

    if (dyn == NULL)		/* Not a shared object */
	return;

    sod_offset = sdt->sdt_sods;
    printf("  Shared object dependencies:\n");
    while (sod_offset != 0) {
      const struct sod *sodp = (const struct sod *) align_struct((text_addr + sod_offset));
	const char *name = (const char *) (text_addr + sodp->sod_name);

	if (sodp->sod_library)
	    printf("    -l%-16s version %d.%d\n", name, sodp->sod_major,
		sodp->sod_minor);
	else
	    printf("    %s\n", name);
	sod_offset = sodp->sod_next;
    }
    paths_offset = sdt->sdt_paths;
    printf("  Shared object additional paths:\n");
    if (paths_offset != 0) {
	printf("    %s\n", (const char *)(text_addr + paths_offset));
    } else {
	printf("    (none)\n");
    }
}

static void
dump_sym(const struct nlist *np)
{
    char type[8];
    char aux[8];
    char weak;
    char *p;

    switch (np->n_type & ~N_EXT) {
    case N_UNDF:	strcpy(type, "undf");  break;
    case N_ABS:		strcpy(type, "abs");  break;
    case N_TEXT:	strcpy(type, "text");  break;
    case N_DATA:	strcpy(type, "data");  break;
    case N_BSS:		strcpy(type, "bss");  break;
    case N_INDR:	strcpy(type, "indr");  break;
    case N_SIZE:	strcpy(type, "size");  break;
    case N_COMM:	strcpy(type, "comm");  break;
    case N_SETA:	strcpy(type, "seta");  break;
    case N_SETT:	strcpy(type, "sett");  break;
    case N_SETD:	strcpy(type, "setd");  break;
    case N_SETB:	strcpy(type, "setb");  break;
    case N_SETV:	strcpy(type, "setv");  break;
    case N_FN:		strcpy(type, np->n_type&N_EXT ? "fn" : "warn");  break;
    case N_GSYM:	strcpy(type, "gsym");  break;
    case N_FNAME:	strcpy(type, "fname");  break;
    case N_FUN:		strcpy(type, "fun");  break;
    case N_STSYM:	strcpy(type, "stsym");  break;
    case N_LCSYM:	strcpy(type, "lcsym");  break;
    case N_MAIN:	strcpy(type, "main");  break;
    case N_PC:		strcpy(type, "pc");  break;
    case N_RSYM:	strcpy(type, "rsym");  break;
    case N_SLINE:	strcpy(type, "sline");  break;
    case N_DSLINE:	strcpy(type, "dsline");  break;
    case N_BSLINE:	strcpy(type, "bsline");  break;
    case N_SSYM:	strcpy(type, "ssym");  break;
    case N_SO:		strcpy(type, "so");  break;
    case N_LSYM:	strcpy(type, "lsym");  break;
    case N_BINCL:	strcpy(type, "bincl");  break;
    case N_SOL:		strcpy(type, "sol");  break;
    case N_PSYM:	strcpy(type, "psym");  break;
    case N_EINCL:	strcpy(type, "eincl");  break;
    case N_ENTRY:	strcpy(type, "entry");  break;
    case N_LBRAC:	strcpy(type, "lbrac");  break;
    case N_EXCL:	strcpy(type, "excl");  break;
    case N_RBRAC:	strcpy(type, "rbrac");  break;
    case N_BCOMM:	strcpy(type, "bcomm");  break;
    case N_ECOMM:	strcpy(type, "ecomm");  break;
    case N_ECOML:	strcpy(type, "ecoml");  break;
    case N_LENG:	strcpy(type, "leng");  break;
    default:
	snprintf(type, sizeof type, "%#02x", np->n_type);
	break;
    }

    if (np->n_type & N_EXT && type[0] != '0')
	for (p = type;  *p != '\0';  ++p)
	    *p = toupper(*p);

    switch (N_AUX(np)) {
    case 0:		strcpy(aux, "");  break;
    case AUX_OBJECT:	strcpy(aux, "objt");  break;
    case AUX_FUNC:	strcpy(aux, "func");  break;
    default:		snprintf(aux, sizeof aux, "%#01x", N_AUX(np));  break;
    }

    weak = N_BIND(np) == BIND_WEAK ? 'w' : ' ';

    printf("%c%-6s %-4s %8lx", weak, type, aux, np->n_value);
}

static void
dump_syms(void)
{
    unsigned long i;

    printf("  Symbols:\n");
    for (i = 0;  i < sym_count;  ++i) {
	printf("    %6lu%c ", i, sym_used[i] ? '*' : ' ');
	dump_sym(&sym_base[i]);
	printf(" %s\n", sym_name(i));
    }
}

static const char *
rtsym_name(unsigned long n)
{
    assert(n < rtsym_count);
    if (rtsym_base[n].nz_strx == 0)
	return "";
    return rtstr_base + rtsym_base[n].nz_strx;
}

static const char *
sym_name(unsigned long n)
{
    assert(n < sym_count);
    if (sym_base[n].n_un.n_strx == 0)
	return "";
    return str_base + sym_base[n].n_un.n_strx;
}
