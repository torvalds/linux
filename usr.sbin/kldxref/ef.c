/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000, Boris Popov
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

#define	MAXSEGS 3
struct ef_file {
	char		*ef_name;
	struct elf_file *ef_efile;
	Elf_Phdr	*ef_ph;
	int		ef_fd;
	int		ef_type;
	Elf_Ehdr	ef_hdr;
	void		*ef_fpage;		/* First block of the file */
	int		ef_fplen;		/* length of first block */
	Elf_Dyn		*ef_dyn;		/* Symbol table etc. */
	Elf_Hashelt	ef_nbuckets;
	Elf_Hashelt	ef_nchains;
	Elf_Hashelt	*ef_buckets;
	Elf_Hashelt	*ef_chains;
	Elf_Hashelt	*ef_hashtab;
	Elf_Off		ef_stroff;
	caddr_t		ef_strtab;
	int		ef_strsz;
	Elf_Off		ef_symoff;
	Elf_Sym		*ef_symtab;
	int		ef_nsegs;
	Elf_Phdr	*ef_segs[MAXSEGS];
	int		ef_verbose;
	Elf_Rel		*ef_rel;		/* relocation table */
	int		ef_relsz;		/* number of entries */
	Elf_Rela	*ef_rela;		/* relocation table */
	int		ef_relasz;		/* number of entries */
};

static void	ef_print_phdr(Elf_Phdr *);
static Elf_Off	ef_get_offset(elf_file_t, Elf_Off);
static int	ef_parse_dynamic(elf_file_t);

static int	ef_get_type(elf_file_t ef);
static int	ef_close(elf_file_t ef);
static int	ef_read(elf_file_t ef, Elf_Off offset, size_t len, void *dest);
static int	ef_read_entry(elf_file_t ef, Elf_Off offset, size_t len,
		    void **ptr);

static int	ef_seg_read(elf_file_t ef, Elf_Off offset, size_t len,
		    void *dest);
static int	ef_seg_read_rel(elf_file_t ef, Elf_Off offset, size_t len,
		    void *dest);
static int	ef_seg_read_string(elf_file_t ef, Elf_Off offset, size_t len,
		    char *dest);
static int	ef_seg_read_entry(elf_file_t ef, Elf_Off offset, size_t len,
		    void **ptr);
static int	ef_seg_read_entry_rel(elf_file_t ef, Elf_Off offset, size_t len,
		    void **ptr);

static Elf_Addr	ef_symaddr(elf_file_t ef, Elf_Size symidx);
static int	ef_lookup_set(elf_file_t ef, const char *name, long *startp,
		    long *stopp, long *countp);
static int	ef_lookup_symbol(elf_file_t ef, const char *name,
		    Elf_Sym **sym);

static struct elf_file_ops ef_file_ops = {
	.get_type		= ef_get_type,
	.close			= ef_close,
	.read			= ef_read,
	.read_entry		= ef_read_entry,
	.seg_read		= ef_seg_read,
	.seg_read_rel		= ef_seg_read_rel,
	.seg_read_string	= ef_seg_read_string,
	.seg_read_entry		= ef_seg_read_entry,
	.seg_read_entry_rel	= ef_seg_read_entry_rel,
	.symaddr		= ef_symaddr,
	.lookup_set		= ef_lookup_set,
	.lookup_symbol		= ef_lookup_symbol
};

static void
ef_print_phdr(Elf_Phdr *phdr)
{

	if ((phdr->p_flags & PF_W) == 0) {
		printf("text=0x%jx ", (uintmax_t)phdr->p_filesz);
	} else {
		printf("data=0x%jx", (uintmax_t)phdr->p_filesz);
		if (phdr->p_filesz < phdr->p_memsz)
			printf("+0x%jx",
			    (uintmax_t)(phdr->p_memsz - phdr->p_filesz));
		printf(" ");
	}
}

static Elf_Off
ef_get_offset(elf_file_t ef, Elf_Off off)
{
	Elf_Phdr *ph;
	int i;

	for (i = 0; i < ef->ef_nsegs; i++) {
		ph = ef->ef_segs[i];
		if (off >= ph->p_vaddr && off < ph->p_vaddr + ph->p_memsz) {
			return (ph->p_offset + (off - ph->p_vaddr));
		}
	}
	return (0);
}

static int
ef_get_type(elf_file_t ef)
{

	return (ef->ef_type);
}

/*
 * next three functions copied from link_elf.c
 */
static unsigned long
elf_hash(const char *name)
{
	unsigned long h, g;
	const unsigned char *p;

	h = 0;
	p = (const unsigned char *)name;
	while (*p != '\0') {
		h = (h << 4) + *p++;
		if ((g = h & 0xf0000000) != 0)
			h ^= g >> 24;
		h &= ~g;
	}
	return (h);
}

static int
ef_lookup_symbol(elf_file_t ef, const char *name, Elf_Sym **sym)
{
	unsigned long hash, symnum;
	Elf_Sym *symp;
	char *strp;

	/* First, search hashed global symbols */
	hash = elf_hash(name);
	symnum = ef->ef_buckets[hash % ef->ef_nbuckets];

	while (symnum != STN_UNDEF) {
		if (symnum >= ef->ef_nchains) {
			warnx("ef_lookup_symbol: file %s have corrupted symbol table\n",
			    ef->ef_name);
			return (ENOENT);
		}

		symp = ef->ef_symtab + symnum;
		if (symp->st_name == 0) {
			warnx("ef_lookup_symbol: file %s have corrupted symbol table\n",
			    ef->ef_name);
			return (ENOENT);
		}

		strp = ef->ef_strtab + symp->st_name;

		if (strcmp(name, strp) == 0) {
			if (symp->st_shndx != SHN_UNDEF ||
			    (symp->st_value != 0 &&
				ELF_ST_TYPE(symp->st_info) == STT_FUNC)) {
				*sym = symp;
				return (0);
			} else
				return (ENOENT);
		}

		symnum = ef->ef_chains[symnum];
	}

	return (ENOENT);
}

static int
ef_lookup_set(elf_file_t ef, const char *name, long *startp, long *stopp,
    long *countp)
{
	Elf_Sym *sym;
	char *setsym;
	int error, len;

	len = strlen(name) + sizeof("__start_set_"); /* sizeof includes \0 */
	setsym = malloc(len);
	if (setsym == NULL)
		return (errno);

	/* get address of first entry */
	snprintf(setsym, len, "%s%s", "__start_set_", name);
	error = ef_lookup_symbol(ef, setsym, &sym);
	if (error != 0)
		goto out;
	*startp = sym->st_value;

	/* get address of last entry */
	snprintf(setsym, len, "%s%s", "__stop_set_", name);
	error = ef_lookup_symbol(ef, setsym, &sym);
	if (error != 0)
		goto out;
	*stopp = sym->st_value;

	/* and the number of entries */
	*countp = (*stopp - *startp) / sizeof(void *);

out:
	free(setsym);
	return (error);
}

static Elf_Addr
ef_symaddr(elf_file_t ef, Elf_Size symidx)
{
	const Elf_Sym *sym;

	if (symidx >= ef->ef_nchains)
		return (0);
	sym = ef->ef_symtab + symidx;

	if (ELF_ST_BIND(sym->st_info) == STB_LOCAL &&
	    sym->st_shndx != SHN_UNDEF && sym->st_value != 0)
		return (sym->st_value);
	return (0);
}

static int
ef_parse_dynamic(elf_file_t ef)
{
	Elf_Dyn *dp;
	Elf_Hashelt hashhdr[2];
	int error;
	Elf_Off rel_off;
	Elf_Off rela_off;
	int rel_sz;
	int rela_sz;
	int rel_entry;
	int rela_entry;

	rel_off = rela_off = 0;
	rel_sz = rela_sz = 0;
	rel_entry = rela_entry = 0;
	for (dp = ef->ef_dyn; dp->d_tag != DT_NULL; dp++) {
		switch (dp->d_tag) {
		case DT_HASH:
			error = ef_read(ef, ef_get_offset(ef, dp->d_un.d_ptr),
			    sizeof(hashhdr),  hashhdr);
			if (error != 0) {
				warnx("can't read hash header (%jx)",
				  (uintmax_t)ef_get_offset(ef, dp->d_un.d_ptr));
				return (error);
			}
			ef->ef_nbuckets = hashhdr[0];
			ef->ef_nchains = hashhdr[1];
			error = ef_read_entry(ef, -1, 
			    (hashhdr[0] + hashhdr[1]) * sizeof(Elf_Hashelt),
			    (void **)&ef->ef_hashtab);
			if (error != 0) {
				warnx("can't read hash table");
				return (error);
			}
			ef->ef_buckets = ef->ef_hashtab;
			ef->ef_chains = ef->ef_buckets + ef->ef_nbuckets;
			break;
		case DT_STRTAB:
			ef->ef_stroff = dp->d_un.d_ptr;
			break;
		case DT_STRSZ:
			ef->ef_strsz = dp->d_un.d_val;
			break;
		case DT_SYMTAB:
			ef->ef_symoff = dp->d_un.d_ptr;
			break;
		case DT_SYMENT:
			if (dp->d_un.d_val != sizeof(Elf_Sym))
				return (EFTYPE);
			break;
		case DT_REL:
			if (rel_off != 0)
				warnx("second DT_REL entry ignored");
			rel_off = dp->d_un.d_ptr;
			break;
		case DT_RELSZ:
			if (rel_sz != 0)
				warnx("second DT_RELSZ entry ignored");
			rel_sz = dp->d_un.d_val;
			break;
		case DT_RELENT:
			if (rel_entry != 0)
				warnx("second DT_RELENT entry ignored");
			rel_entry = dp->d_un.d_val;
			break;
		case DT_RELA:
			if (rela_off != 0)
				warnx("second DT_RELA entry ignored");
			rela_off = dp->d_un.d_ptr;
			break;
		case DT_RELASZ:
			if (rela_sz != 0)
				warnx("second DT_RELASZ entry ignored");
			rela_sz = dp->d_un.d_val;
			break;
		case DT_RELAENT:
			if (rela_entry != 0)
				warnx("second DT_RELAENT entry ignored");
			rela_entry = dp->d_un.d_val;
			break;
		}
	}
	if (ef->ef_symoff == 0) {
		warnx("%s: no .dynsym section found\n", ef->ef_name);
		return (EFTYPE);
	}
	if (ef->ef_stroff == 0) {
		warnx("%s: no .dynstr section found\n", ef->ef_name);
		return (EFTYPE);
	}
	if (ef_read_entry(ef, ef_get_offset(ef, ef->ef_symoff),
	    ef->ef_nchains * sizeof(Elf_Sym),
		(void **)&ef->ef_symtab) != 0) {
		if (ef->ef_verbose)
			warnx("%s: can't load .dynsym section (0x%jx)",
			    ef->ef_name, (uintmax_t)ef->ef_symoff);
		return (EIO);
	}
	if (ef_read_entry(ef, ef_get_offset(ef, ef->ef_stroff), ef->ef_strsz,
		(void **)&ef->ef_strtab) != 0) {
		warnx("can't load .dynstr section");
		return (EIO);
	}
	if (rel_off != 0) {
		if (rel_entry == 0) {
			warnx("%s: no DT_RELENT for DT_REL", ef->ef_name);
			return (EFTYPE);
		}
		if (rel_entry != sizeof(Elf_Rel)) {
			warnx("%s: inconsistent DT_RELENT value",
			    ef->ef_name);
			return (EFTYPE);
		}
		if (rel_sz % rel_entry != 0) {
			warnx("%s: inconsistent values for DT_RELSZ and "
			    "DT_RELENT", ef->ef_name);
			return (EFTYPE);
		}
		if (ef_read_entry(ef, ef_get_offset(ef, rel_off), rel_sz,
		    (void **)&ef->ef_rel) != 0) {
			warnx("%s: cannot load DT_REL section", ef->ef_name);
			return (EIO);
		}
		ef->ef_relsz = rel_sz / rel_entry;
		if (ef->ef_verbose)
			warnx("%s: %d REL entries", ef->ef_name,
			    ef->ef_relsz);
	}
	if (rela_off != 0) {
		if (rela_entry == 0) {
			warnx("%s: no DT_RELAENT for DT_RELA", ef->ef_name);
			return (EFTYPE);
		}
		if (rela_entry != sizeof(Elf_Rela)) {
			warnx("%s: inconsistent DT_RELAENT value",
			    ef->ef_name);
			return (EFTYPE);
		}
		if (rela_sz % rela_entry != 0) {
			warnx("%s: inconsistent values for DT_RELASZ and "
			    "DT_RELAENT", ef->ef_name);
			return (EFTYPE);
		}
		if (ef_read_entry(ef, ef_get_offset(ef, rela_off), rela_sz,
		    (void **)&ef->ef_rela) != 0) {
			warnx("%s: cannot load DT_RELA section", ef->ef_name);
			return (EIO);
		}
		ef->ef_relasz = rela_sz / rela_entry;
		if (ef->ef_verbose)
			warnx("%s: %d RELA entries", ef->ef_name,
			    ef->ef_relasz);
	}
	return (0);
}

static int
ef_read(elf_file_t ef, Elf_Off offset, size_t len, void *dest)
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
ef_read_entry(elf_file_t ef, Elf_Off offset, size_t len, void **ptr)
{
	int error;

	*ptr = malloc(len);
	if (*ptr == NULL)
		return (errno);
	error = ef_read(ef, offset, len, *ptr);
	if (error != 0)
		free(*ptr);
	return (error);
}

static int
ef_seg_read(elf_file_t ef, Elf_Off offset, size_t len, void *dest)
{
	Elf_Off ofs;

	ofs = ef_get_offset(ef, offset);
	if (ofs == 0) {
		if (ef->ef_verbose)
			warnx("ef_seg_read(%s): zero offset (%jx:%ju)",
			    ef->ef_name, (uintmax_t)offset, (uintmax_t)ofs);
		return (EFAULT);
	}
	return (ef_read(ef, ofs, len, dest));
}

static int
ef_seg_read_rel(elf_file_t ef, Elf_Off offset, size_t len, void *dest)
{
	Elf_Off ofs;
	const Elf_Rela *a;
	const Elf_Rel *r;
	int error;

	ofs = ef_get_offset(ef, offset);
	if (ofs == 0) {
		if (ef->ef_verbose)
			warnx("ef_seg_read_rel(%s): zero offset (%jx:%ju)",
			    ef->ef_name, (uintmax_t)offset, (uintmax_t)ofs);
		return (EFAULT);
	}
	if ((error = ef_read(ef, ofs, len, dest)) != 0)
		return (error);

	for (r = ef->ef_rel; r < &ef->ef_rel[ef->ef_relsz]; r++) {
		error = ef_reloc(ef->ef_efile, r, EF_RELOC_REL, 0, offset, len,
		    dest);
		if (error != 0)
			return (error);
	}
	for (a = ef->ef_rela; a < &ef->ef_rela[ef->ef_relasz]; a++) {
		error = ef_reloc(ef->ef_efile, a, EF_RELOC_RELA, 0, offset, len,
		    dest);
		if (error != 0)
			return (error);
	}
	return (0);
}

static int
ef_seg_read_string(elf_file_t ef, Elf_Off offset, size_t len, char *dest)
{
	Elf_Off ofs;
	ssize_t r;

	ofs = ef_get_offset(ef, offset);
	if (ofs == 0 || ofs == (Elf_Off)-1) {
		if (ef->ef_verbose)
			warnx("ef_seg_read_string(%s): bad offset (%jx:%ju)",
			    ef->ef_name, (uintmax_t)offset, (uintmax_t)ofs);
		return (EFAULT);
	}

	r = pread(ef->ef_fd, dest, len, ofs);
	if (r < 0)
		return (errno);
	if (strnlen(dest, len) == len)
		return (EFAULT);

	return (0);
}

static int
ef_seg_read_entry(elf_file_t ef, Elf_Off offset, size_t len, void **ptr)
{
	int error;

	*ptr = malloc(len);
	if (*ptr == NULL)
		return (errno);
	error = ef_seg_read(ef, offset, len, *ptr);
	if (error != 0)
		free(*ptr);
	return (error);
}

static int
ef_seg_read_entry_rel(elf_file_t ef, Elf_Off offset, size_t len, void **ptr)
{
	int error;

	*ptr = malloc(len);
	if (*ptr == NULL)
		return (errno);
	error = ef_seg_read_rel(ef, offset, len, *ptr);
	if (error != 0)
		free(*ptr);
	return (error);
}

int
ef_open(const char *filename, struct elf_file *efile, int verbose)
{
	elf_file_t ef;
	Elf_Ehdr *hdr;
	int fd;
	int error;
	int phlen, res;
	int nsegs;
	Elf_Phdr *phdr, *phdyn, *phlimit;

	if (filename == NULL)
		return (EINVAL);
	if ((fd = open(filename, O_RDONLY)) == -1)
		return (errno);

	ef = malloc(sizeof(*ef));
	if (ef == NULL) {
		close(fd);
		return (errno);
	}

	efile->ef_ef = ef;
	efile->ef_ops = &ef_file_ops;

	bzero(ef, sizeof(*ef));
	ef->ef_verbose = verbose;
	ef->ef_fd = fd;
	ef->ef_name = strdup(filename);
	ef->ef_efile = efile;
	hdr = (Elf_Ehdr *)&ef->ef_hdr;
	do {
		res = read(fd, hdr, sizeof(*hdr));
		error = EFTYPE;
		if (res != sizeof(*hdr))
			break;
		if (!IS_ELF(*hdr))
			break;
		if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
		    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
		    hdr->e_ident[EI_VERSION] != EV_CURRENT ||
		    hdr->e_version != EV_CURRENT ||
		    hdr->e_machine != ELF_TARG_MACH ||
		    hdr->e_phentsize != sizeof(Elf_Phdr))
			break;
		phlen = hdr->e_phnum * sizeof(Elf_Phdr);
		if (ef_read_entry(ef, hdr->e_phoff, phlen,
		    (void **)&ef->ef_ph) != 0)
			break;
		phdr = ef->ef_ph;
		phlimit = phdr + hdr->e_phnum;
		nsegs = 0;
		phdyn = NULL;
		while (phdr < phlimit) {
			if (verbose > 1)
				ef_print_phdr(phdr);
			switch (phdr->p_type) {
			case PT_LOAD:
				if (nsegs < MAXSEGS)
					ef->ef_segs[nsegs] = phdr;
				nsegs++;
				break;
			case PT_PHDR:
				break;
			case PT_DYNAMIC:
				phdyn = phdr;
				break;
			}
			phdr++;
		}
		if (verbose > 1)
			printf("\n");
		if (phdyn == NULL) {
			warnx("Skipping %s: not dynamically-linked",
			    filename);
			break;
		} else if (nsegs > MAXSEGS) {
			warnx("%s: too many segments", filename);
			break;
		}
		ef->ef_nsegs = nsegs;
		if (ef_read_entry(ef, phdyn->p_offset,
			phdyn->p_filesz, (void **)&ef->ef_dyn) != 0) {
			printf("ef_read_entry failed\n");
			break;
		}
		error = ef_parse_dynamic(ef);
		if (error != 0)
			break;
		if (hdr->e_type == ET_DYN) {
			ef->ef_type = EFT_KLD;
			error = 0;
		} else if (hdr->e_type == ET_EXEC) {
			ef->ef_type = EFT_KERNEL;
			error = 0;
		} else
			break;
	} while(0);
	if (error != 0)
		ef_close(ef);
	return (error);
}

static int
ef_close(elf_file_t ef)
{

	close(ef->ef_fd);
	if (ef->ef_name)
		free(ef->ef_name);
	ef->ef_efile->ef_ops = NULL;
	ef->ef_efile->ef_ef = NULL;
	free(ef);
	return (0);
}
