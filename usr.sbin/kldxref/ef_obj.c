/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000, Boris Popov
 * Copyright (c) 1998-2000 Doug Rabson
 * Copyright (c) 2004 Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/linker.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <machine/elf.h>
#define FREEBSD_ELF

#include "ef.h"

typedef struct {
	void		*addr;
	Elf_Off		size;
	int		flags;
	int		sec;	/* Original section */
	char		*name;
} Elf_progent;

typedef struct {
	Elf_Rel		*rel;
	int		nrel;
	int		sec;
} Elf_relent;

typedef struct {
	Elf_Rela	*rela;
	int		nrela;
	int		sec;
} Elf_relaent;

struct ef_file {
	char		*ef_name;
	int		ef_fd;
	Elf_Ehdr	ef_hdr;
	struct elf_file *ef_efile;

	caddr_t		address;
	Elf_Off		size;
	Elf_Shdr	*e_shdr;

	Elf_progent	*progtab;
	int		nprogtab;

	Elf_relaent	*relatab;
	int		nrela;

	Elf_relent	*reltab;
	int		nrel;

	Elf_Sym		*ddbsymtab;	/* The symbol table we are using */
	long		ddbsymcnt;	/* Number of symbols */
	caddr_t		ddbstrtab;	/* String table */
	long		ddbstrcnt;	/* number of bytes in string table */

	caddr_t		shstrtab;	/* Section name string table */
	long		shstrcnt;	/* number of bytes in string table */

	int		ef_verbose;
};

static int	ef_obj_get_type(elf_file_t ef);
static int	ef_obj_close(elf_file_t ef);
static int	ef_obj_read(elf_file_t ef, Elf_Off offset, size_t len,
		    void* dest);
static int	ef_obj_read_entry(elf_file_t ef, Elf_Off offset, size_t len,
		    void **ptr);
static int	ef_obj_seg_read(elf_file_t ef, Elf_Off offset, size_t len,
		    void *dest);
static int	ef_obj_seg_read_rel(elf_file_t ef, Elf_Off offset, size_t len,
		    void *dest);
static int	ef_obj_seg_read_string(elf_file_t ef, Elf_Off offset,
		    size_t len, char *dest);
static int	ef_obj_seg_read_entry(elf_file_t ef, Elf_Off offset, size_t len,
		    void **ptr);
static int	ef_obj_seg_read_entry_rel(elf_file_t ef, Elf_Off offset,
		    size_t len, void **ptr);
static Elf_Addr	ef_obj_symaddr(elf_file_t ef, Elf_Size symidx);
static int	ef_obj_lookup_set(elf_file_t ef, const char *name, long *startp,
		    long *stopp, long *countp);
static int	ef_obj_lookup_symbol(elf_file_t ef, const char* name,
		    Elf_Sym** sym);

static struct elf_file_ops ef_obj_file_ops = {
	.get_type		= ef_obj_get_type,
	.close			= ef_obj_close,
	.read			= ef_obj_read,
	.read_entry		= ef_obj_read_entry,
	.seg_read		= ef_obj_seg_read,
	.seg_read_rel		= ef_obj_seg_read_rel,
	.seg_read_string	= ef_obj_seg_read_string,
	.seg_read_entry		= ef_obj_seg_read_entry,
	.seg_read_entry_rel	= ef_obj_seg_read_entry_rel,
	.symaddr		= ef_obj_symaddr,
	.lookup_set		= ef_obj_lookup_set,
	.lookup_symbol		= ef_obj_lookup_symbol
};

static int
ef_obj_get_type(elf_file_t __unused ef)
{

	return (EFT_KLD);
}

static int
ef_obj_lookup_symbol(elf_file_t ef, const char* name, Elf_Sym** sym)
{
	Elf_Sym *symp;
	const char *strp;
	int i;

	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		strp = ef->ddbstrtab + symp->st_name;
		if (symp->st_shndx != SHN_UNDEF && strcmp(name, strp) == 0) {
			*sym = symp;
			return (0);
		}
	}
	return (ENOENT);
}

static int
ef_obj_lookup_set(elf_file_t ef, const char *name, long *startp, long *stopp,
    long *countp)
{
	int i;

	for (i = 0; i < ef->nprogtab; i++) {
		if ((strncmp(ef->progtab[i].name, "set_", 4) == 0) &&
		    strcmp(ef->progtab[i].name + 4, name) == 0) {
			*startp = (char *)ef->progtab[i].addr - ef->address;
			*stopp = (char *)ef->progtab[i].addr +
			    ef->progtab[i].size - ef->address;
			*countp = (*stopp - *startp) / sizeof(void *);
			return (0);
		}
	}
	return (ESRCH);
}

static Elf_Addr
ef_obj_symaddr(elf_file_t ef, Elf_Size symidx)
{
	const Elf_Sym *sym;

	if (symidx >= (size_t) ef->ddbsymcnt)
		return (0);
	sym = ef->ddbsymtab + symidx;

	if (sym->st_shndx != SHN_UNDEF)
		return (sym->st_value - (Elf_Addr)ef->address);
	return (0);
}

static int
ef_obj_read(elf_file_t ef, Elf_Off offset, size_t len, void *dest)
{
	ssize_t r;

	if (offset != (Elf_Off)-1) {
		if (lseek(ef->ef_fd, offset, SEEK_SET) == -1)
			return (EIO);
	}

	r = read(ef->ef_fd, dest, len);
	if (r != -1 && (size_t)r == len)
		return (0);
	else
		return (EIO);
}

static int
ef_obj_read_entry(elf_file_t ef, Elf_Off offset, size_t len, void **ptr)
{
	int error;

	*ptr = malloc(len);
	if (*ptr == NULL)
		return (errno);
	error = ef_obj_read(ef, offset, len, *ptr);
	if (error != 0)
		free(*ptr);
	return (error);
}

static int
ef_obj_seg_read(elf_file_t ef, Elf_Off offset, size_t len, void *dest)
{

	if (offset + len > ef->size) {
		if (ef->ef_verbose)
			warnx("ef_obj_seg_read(%s): bad offset/len (%lx:%ld)",
			    ef->ef_name, (long)offset, (long)len);
		return (EFAULT);
	}
	bcopy(ef->address + offset, dest, len);
	return (0);
}

static int
ef_obj_seg_read_rel(elf_file_t ef, Elf_Off offset, size_t len, void *dest)
{
	char *memaddr;
	Elf_Rel *r;
	Elf_Rela *a;
	Elf_Off secbase, dataoff;
	int error, i, sec;

	if (offset + len > ef->size) {
		if (ef->ef_verbose)
			warnx("ef_obj_seg_read_rel(%s): bad offset/len (%lx:%ld)",
			    ef->ef_name, (long)offset, (long)len);
		return (EFAULT);
	}
	bcopy(ef->address + offset, dest, len);

	/* Find out which section contains the data. */
	memaddr = ef->address + offset;
	sec = -1;
	secbase = dataoff = 0;
	for (i = 0; i < ef->nprogtab; i++) {
		if (ef->progtab[i].addr == NULL)
			continue;
		if (memaddr < (char *)ef->progtab[i].addr || memaddr + len >
		     (char *)ef->progtab[i].addr + ef->progtab[i].size)
			continue;
		sec = ef->progtab[i].sec;
		/* We relocate to address 0. */
		secbase = (char *)ef->progtab[i].addr - ef->address;
		dataoff = memaddr - ef->address;
		break;
	}

	if (sec == -1)
		return (EFAULT);

	/* Now do the relocations. */
	for (i = 0; i < ef->nrel; i++) {
		if (ef->reltab[i].sec != sec)
			continue;
		for (r = ef->reltab[i].rel;
		     r < &ef->reltab[i].rel[ef->reltab[i].nrel]; r++) {
			error = ef_reloc(ef->ef_efile, r, EF_RELOC_REL, secbase,
			    dataoff, len, dest);
			if (error != 0)
				return (error);
		}
	}
	for (i = 0; i < ef->nrela; i++) {
		if (ef->relatab[i].sec != sec)
			continue;
		for (a = ef->relatab[i].rela;
		     a < &ef->relatab[i].rela[ef->relatab[i].nrela]; a++) {
			error = ef_reloc(ef->ef_efile, a, EF_RELOC_RELA,
			    secbase, dataoff, len, dest);
			if (error != 0)
				return (error);
		}
	}
	return (0);
}

static int
ef_obj_seg_read_string(elf_file_t ef, Elf_Off offset, size_t len, char *dest)
{

	if (offset >= ef->size) {
		if (ef->ef_verbose)
			warnx("ef_obj_seg_read_string(%s): bad offset (%lx)",
			    ef->ef_name, (long)offset);
		return (EFAULT);
	}

	if (ef->size - offset < len)
		len = ef->size - offset;

	if (strnlen(ef->address + offset, len) == len)
		return (EFAULT);

	memcpy(dest, ef->address + offset, len);
	return (0);
}

static int
ef_obj_seg_read_entry(elf_file_t ef, Elf_Off offset, size_t len, void **ptr)
{
	int error;

	*ptr = malloc(len);
	if (*ptr == NULL)
		return (errno);
	error = ef_obj_seg_read(ef, offset, len, *ptr);
	if (error != 0)
		free(*ptr);
	return (error);
}

static int
ef_obj_seg_read_entry_rel(elf_file_t ef, Elf_Off offset, size_t len,
    void **ptr)
{
	int error;

	*ptr = malloc(len);
	if (*ptr == NULL)
		return (errno);
	error = ef_obj_seg_read_rel(ef, offset, len, *ptr);
	if (error != 0)
		free(*ptr);
	return (error);
}

int
ef_obj_open(const char *filename, struct elf_file *efile, int verbose)
{
	elf_file_t ef;
	Elf_Ehdr *hdr;
	Elf_Shdr *shdr;
	Elf_Sym *es;
	char *mapbase;
	void *vtmp;
	size_t mapsize, alignmask, max_addralign;
	int error, fd, pb, ra, res, rl;
	int i, j, nbytes, nsym, shstrindex, symstrindex, symtabindex;

	if (filename == NULL)
		return (EINVAL);
	if ((fd = open(filename, O_RDONLY)) == -1)
		return (errno);

	ef = calloc(1, sizeof(*ef));
	if (ef == NULL) {
		close(fd);
		return (errno);
	}

	efile->ef_ef = ef;
	efile->ef_ops = &ef_obj_file_ops;

	ef->ef_verbose = verbose;
	ef->ef_fd = fd;
	ef->ef_name = strdup(filename);
	ef->ef_efile = efile;
	hdr = (Elf_Ehdr *)&ef->ef_hdr;

	res = read(fd, hdr, sizeof(*hdr));
	error = EFTYPE;
	if (res != sizeof(*hdr))
		goto out;
	if (!IS_ELF(*hdr))
		goto out;
	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    hdr->e_version != EV_CURRENT || hdr->e_machine != ELF_TARG_MACH ||
	    hdr->e_type != ET_REL)
		goto out;

	nbytes = hdr->e_shnum * hdr->e_shentsize;
	if (nbytes == 0 || hdr->e_shoff == 0 ||
	    hdr->e_shentsize != sizeof(Elf_Shdr))
		goto out;

	if (ef_obj_read_entry(ef, hdr->e_shoff, nbytes, &vtmp) != 0) {
		printf("ef_read_entry failed\n");
		goto out;
	}
	ef->e_shdr = shdr = vtmp;

	/* Scan the section header for information and table sizing. */
	nsym = 0;
	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			ef->nprogtab++;
			break;
		case SHT_SYMTAB:
			nsym++;
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
			break;
		case SHT_REL:
			ef->nrel++;
			break;
		case SHT_RELA:
			ef->nrela++;
			break;
		case SHT_STRTAB:
			break;
		}
	}

	if (ef->nprogtab == 0) {
		warnx("%s: file has no contents", filename);
		goto out;
	}
	if (nsym != 1) {
		warnx("%s: file has no valid symbol table", filename);
		goto out;
	}
	if (symstrindex < 0 || symstrindex > hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB) {
		warnx("%s: file has invalid symbol strings", filename);
		goto out;
	}

	/* Allocate space for tracking the load chunks */
	if (ef->nprogtab != 0)
		ef->progtab = calloc(ef->nprogtab, sizeof(*ef->progtab));
	if (ef->nrel != 0)
		ef->reltab = calloc(ef->nrel, sizeof(*ef->reltab));
	if (ef->nrela != 0)
		ef->relatab = calloc(ef->nrela, sizeof(*ef->relatab));
	if ((ef->nprogtab != 0 && ef->progtab == NULL) ||
	    (ef->nrel != 0 && ef->reltab == NULL) ||
	    (ef->nrela != 0 && ef->relatab == NULL)) {
		printf("malloc failed\n");
		error = ENOMEM;
		goto out;
	}

	ef->ddbsymcnt = shdr[symtabindex].sh_size / sizeof(Elf_Sym);
	if (ef_obj_read_entry(ef, shdr[symtabindex].sh_offset,
	    shdr[symtabindex].sh_size, (void**)&ef->ddbsymtab) != 0) {
		printf("ef_read_entry failed\n");
		goto out;
	}

	ef->ddbstrcnt = shdr[symstrindex].sh_size;
	if (ef_obj_read_entry(ef, shdr[symstrindex].sh_offset,
	    shdr[symstrindex].sh_size, (void**)&ef->ddbstrtab) != 0) {
		printf("ef_read_entry failed\n");
		goto out;
	}

	/* Do we have a string table for the section names?  */
	shstrindex = -1;
	if (hdr->e_shstrndx != 0 &&
	    shdr[hdr->e_shstrndx].sh_type == SHT_STRTAB) {
		shstrindex = hdr->e_shstrndx;
		ef->shstrcnt = shdr[shstrindex].sh_size;
		if (ef_obj_read_entry(ef, shdr[shstrindex].sh_offset,
		    shdr[shstrindex].sh_size, (void**)&ef->shstrtab) != 0) {
			printf("ef_read_entry failed\n");
			goto out;
		}
	}

	/* Size up code/data(progbits) and bss(nobits). */
	alignmask = 0;
	max_addralign = 0;
	mapsize = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			alignmask = shdr[i].sh_addralign - 1;
			if (shdr[i].sh_addralign > max_addralign)
				max_addralign = shdr[i].sh_addralign;
			mapsize += alignmask;
			mapsize &= ~alignmask;
			mapsize += shdr[i].sh_size;
			break;
		}
	}

	/* We know how much space we need for the text/data/bss/etc. */
	ef->size = mapsize;
	if (posix_memalign((void **)&ef->address, max_addralign, mapsize)) {
		printf("posix_memalign failed\n");
		goto out;
	}
	mapbase = ef->address;

	/*
	 * Now load code/data(progbits), zero bss(nobits), allocate
	 * space for and load relocs
	 */
	pb = 0;
	rl = 0;
	ra = 0;
	alignmask = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			alignmask = shdr[i].sh_addralign - 1;
			mapbase += alignmask;
			mapbase  = (char *)((uintptr_t)mapbase & ~alignmask);
			ef->progtab[pb].addr = (void *)(uintptr_t)mapbase;
			if (shdr[i].sh_type == SHT_PROGBITS) {
				ef->progtab[pb].name = "<<PROGBITS>>";
				if (ef_obj_read(ef, shdr[i].sh_offset,
				    shdr[i].sh_size,
				    ef->progtab[pb].addr) != 0) {
					printf("failed to read progbits\n");
					goto out;
				}
			} else {
				ef->progtab[pb].name = "<<NOBITS>>";
				bzero(ef->progtab[pb].addr, shdr[i].sh_size);
			}
			ef->progtab[pb].size = shdr[i].sh_size;
			ef->progtab[pb].sec = i;
			if (ef->shstrtab && shdr[i].sh_name != 0)
				ef->progtab[pb].name =
				    ef->shstrtab + shdr[i].sh_name;

			/* Update all symbol values with the offset. */
			for (j = 0; j < ef->ddbsymcnt; j++) {
				es = &ef->ddbsymtab[j];
				if (es->st_shndx != i)
					continue;
				es->st_value += (Elf_Addr)ef->progtab[pb].addr;
			}
			mapbase += shdr[i].sh_size;
			pb++;
			break;
		case SHT_REL:
			ef->reltab[rl].nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			ef->reltab[rl].sec = shdr[i].sh_info;
			if (ef_obj_read_entry(ef, shdr[i].sh_offset,
			    shdr[i].sh_size, (void**)&ef->reltab[rl].rel) !=
			    0) {
				printf("ef_read_entry failed\n");
				goto out;
			}
			rl++;
			break;
		case SHT_RELA:
			ef->relatab[ra].nrela =
			    shdr[i].sh_size / sizeof(Elf_Rela);
			ef->relatab[ra].sec = shdr[i].sh_info;
			if (ef_obj_read_entry(ef, shdr[i].sh_offset,
			    shdr[i].sh_size, (void**)&ef->relatab[ra].rela) !=
			    0) {
				printf("ef_read_entry failed\n");
				goto out;
			}
			ra++;
			break;
		}
	}
	error = 0;
out:
	if (error != 0)
		ef_obj_close(ef);
	return (error);
}

static int
ef_obj_close(elf_file_t ef)
{
	int i;

	close(ef->ef_fd);
	if (ef->ef_name)
		free(ef->ef_name);
	if (ef->e_shdr != NULL)
		free(ef->e_shdr);
	if (ef->size != 0)
		free(ef->address);
	if (ef->nprogtab != 0)
		free(ef->progtab);
	if (ef->nrel != 0) {
		for (i = 0; i < ef->nrel; i++)
			if (ef->reltab[i].rel != NULL)
				free(ef->reltab[i].rel);
		free(ef->reltab);
	}
	if (ef->nrela != 0) {
		for (i = 0; i < ef->nrela; i++)
			if (ef->relatab[i].rela != NULL)
				free(ef->relatab[i].rela);
		free(ef->relatab);
	}
	if (ef->ddbsymtab != NULL)
		free(ef->ddbsymtab);
	if (ef->ddbstrtab != NULL)
		free(ef->ddbstrtab);
	if (ef->shstrtab != NULL)
		free(ef->shstrtab);
	ef->ef_efile->ef_ops = NULL;
	ef->ef_efile->ef_ef = NULL;
	free(ef);

	return (0);
}
