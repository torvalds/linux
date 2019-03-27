/*-
 * Copyright (c) 2004 Ian Dowse <iedowse@freebsd.org>
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 1998 Peter Wemm <peter@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <stdint.h>
#include <string.h>
#include <machine/elf.h>
#include <stand.h>
#define FREEBSD_ELF
#include <sys/link_elf.h>

#include "bootstrap.h"

#define COPYOUT(s,d,l)	archsw.arch_copyout((vm_offset_t)(s), d, l)

#if defined(__i386__) && __ELF_WORD_SIZE == 64
#undef ELF_TARG_CLASS
#undef ELF_TARG_MACH
#define ELF_TARG_CLASS  ELFCLASS64
#define ELF_TARG_MACH   EM_X86_64
#endif

typedef struct elf_file {
	Elf_Ehdr	hdr;
	Elf_Shdr	*e_shdr;

	int		symtabindex;	/* Index of symbol table */
	int		shstrindex;	/* Index of section name string table */

	int		fd;
	vm_offset_t	off;
} *elf_file_t;

static int __elfN(obj_loadimage)(struct preloaded_file *mp, elf_file_t ef,
    uint64_t loadaddr);
static int __elfN(obj_lookup_set)(struct preloaded_file *mp, elf_file_t ef,
    const char *name, Elf_Addr *startp, Elf_Addr *stopp, int *countp);
static int __elfN(obj_reloc_ptr)(struct preloaded_file *mp, elf_file_t ef,
    Elf_Addr p, void *val, size_t len);
static int __elfN(obj_parse_modmetadata)(struct preloaded_file *mp,
    elf_file_t ef);
static Elf_Addr __elfN(obj_symaddr)(struct elf_file *ef, Elf_Size symidx);

const char	*__elfN(obj_kerneltype) = "elf kernel";
const char	*__elfN(obj_moduletype) = "elf obj module";

/*
 * Attempt to load the file (file) as an ELF module.  It will be stored at
 * (dest), and a pointer to a module structure describing the loaded object
 * will be saved in (result).
 */
int
__elfN(obj_loadfile)(char *filename, uint64_t dest,
    struct preloaded_file **result)
{
	struct preloaded_file *fp, *kfp;
	struct elf_file	ef;
	Elf_Ehdr *hdr;
	int err;
	ssize_t bytes_read;

	fp = NULL;
	bzero(&ef, sizeof(struct elf_file));

	/*
	 * Open the image, read and validate the ELF header
	 */
	if (filename == NULL)	/* can't handle nameless */
		return(EFTYPE);
	if ((ef.fd = open(filename, O_RDONLY)) == -1)
		return(errno);

	hdr = &ef.hdr;
	bytes_read = read(ef.fd, hdr, sizeof(*hdr));
	if (bytes_read != sizeof(*hdr)) {
		err = EFTYPE;	/* could be EIO, but may be small file */
		goto oerr;
	}

	/* Is it ELF? */
	if (!IS_ELF(*hdr)) {
		err = EFTYPE;
		goto oerr;
	}
	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||	/* Layout ? */
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT ||	/* Version ? */
	    hdr->e_version != EV_CURRENT ||
	    hdr->e_machine != ELF_TARG_MACH ||		/* Machine ? */
	    hdr->e_type != ET_REL) {
		err = EFTYPE;
		goto oerr;
	}

	if (hdr->e_shnum * hdr->e_shentsize == 0 || hdr->e_shoff == 0 ||
	    hdr->e_shentsize != sizeof(Elf_Shdr)) {
		err = EFTYPE;
		goto oerr;
	}

#ifdef LOADER_VERIEXEC
	if (verify_file(ef.fd, filename, bytes_read, VE_MUST) < 0) {
	    err = EAUTH;
	    goto oerr;
	}
#endif

	kfp = file_findfile(NULL, __elfN(obj_kerneltype));
	if (kfp == NULL) {
		printf("elf" __XSTRING(__ELF_WORD_SIZE)
		    "_obj_loadfile: can't load module before kernel\n");
		err = EPERM;
		goto oerr;
	}

	if (archsw.arch_loadaddr != NULL)
		dest = archsw.arch_loadaddr(LOAD_ELF, hdr, dest);
	else
		dest = roundup(dest, PAGE_SIZE);

	/*
	 * Ok, we think we should handle this.
	 */
	fp = file_alloc();
	if (fp == NULL) {
		printf("elf" __XSTRING(__ELF_WORD_SIZE)
		    "_obj_loadfile: cannot allocate module info\n");
		err = EPERM;
		goto out;
	}
	fp->f_name = strdup(filename);
	fp->f_type = strdup(__elfN(obj_moduletype));

	printf("%s ", filename);

	fp->f_size = __elfN(obj_loadimage)(fp, &ef, dest);
	if (fp->f_size == 0 || fp->f_addr == 0)
		goto ioerr;

	/* save exec header as metadata */
	file_addmetadata(fp, MODINFOMD_ELFHDR, sizeof(*hdr), hdr);

	/* Load OK, return module pointer */
	*result = (struct preloaded_file *)fp;
	err = 0;
	goto out;

ioerr:
	err = EIO;
oerr:
	file_discard(fp);
out:
	close(ef.fd);
	if (ef.e_shdr != NULL)
		free(ef.e_shdr);

	return(err);
}

/*
 * With the file (fd) open on the image, and (ehdr) containing
 * the Elf header, load the image at (off)
 */
static int
__elfN(obj_loadimage)(struct preloaded_file *fp, elf_file_t ef, uint64_t off)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *shdr, *cshdr, *lshdr;
	vm_offset_t firstaddr, lastaddr;
	int i, nsym, res, ret, shdrbytes, symstrindex;

	ret = 0;
	firstaddr = lastaddr = (vm_offset_t)off;
	hdr = &ef->hdr;
	ef->off = (vm_offset_t)off;

	/* Read in the section headers. */
	shdrbytes = hdr->e_shnum * hdr->e_shentsize;
	shdr = alloc_pread(ef->fd, (off_t)hdr->e_shoff, shdrbytes);
	if (shdr == NULL) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "_obj_loadimage: read section headers failed\n");
		goto out;
	}
	ef->e_shdr = shdr;

	/*
	 * Decide where to load everything, but don't read it yet.
	 * We store the load address as a non-zero sh_addr value.
	 * Start with the code/data and bss.
	 */
	for (i = 0; i < hdr->e_shnum; i++)
		shdr[i].sh_addr = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#if defined(__i386__) || defined(__amd64__)
		case SHT_X86_64_UNWIND:
#endif
			if ((shdr[i].sh_flags & SHF_ALLOC) == 0)
				break;
			lastaddr = roundup(lastaddr, shdr[i].sh_addralign);
			shdr[i].sh_addr = (Elf_Addr)lastaddr;
			lastaddr += shdr[i].sh_size;
			break;
		}
	}

	/* Symbols. */
	nsym = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_SYMTAB:
			nsym++;
			ef->symtabindex = i;
			shdr[i].sh_addr = (Elf_Addr)lastaddr;
			lastaddr += shdr[i].sh_size;
			break;
		}
	}
	if (nsym != 1) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "_obj_loadimage: file has no valid symbol table\n");
		goto out;
	}
	lastaddr = roundup(lastaddr, shdr[ef->symtabindex].sh_addralign);
	shdr[ef->symtabindex].sh_addr = (Elf_Addr)lastaddr;
	lastaddr += shdr[ef->symtabindex].sh_size;

	symstrindex = shdr[ef->symtabindex].sh_link;
	if (symstrindex < 0 || symstrindex >= hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "_obj_loadimage: file has invalid symbol strings\n");
		goto out;
	}
	lastaddr = roundup(lastaddr, shdr[symstrindex].sh_addralign);
	shdr[symstrindex].sh_addr = (Elf_Addr)lastaddr;
	lastaddr += shdr[symstrindex].sh_size;

	/* Section names. */
	if (hdr->e_shstrndx == 0 || hdr->e_shstrndx >= hdr->e_shnum ||
	    shdr[hdr->e_shstrndx].sh_type != SHT_STRTAB) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "_obj_loadimage: file has no section names\n");
		goto out;
	}
	ef->shstrindex = hdr->e_shstrndx;
	lastaddr = roundup(lastaddr, shdr[ef->shstrindex].sh_addralign);
	shdr[ef->shstrindex].sh_addr = (Elf_Addr)lastaddr;
	lastaddr += shdr[ef->shstrindex].sh_size;

	/* Relocation tables. */
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_REL:
		case SHT_RELA:
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			lastaddr = roundup(lastaddr, shdr[i].sh_addralign);
			shdr[i].sh_addr = (Elf_Addr)lastaddr;
			lastaddr += shdr[i].sh_size;
			break;
		}
	}

	/* Clear the whole area, including bss regions. */
	kern_bzero(firstaddr, lastaddr - firstaddr);

	/* Figure section with the lowest file offset we haven't loaded yet. */
	for (cshdr = NULL; /* none */; /* none */)
	{
		/*
		 * Find next section to load. The complexity of this loop is
		 * O(n^2), but with  the number of sections being typically
		 * small, we do not care.
		 */
		lshdr = cshdr;

		for (i = 0; i < hdr->e_shnum; i++) {
			if (shdr[i].sh_addr == 0 ||
			    shdr[i].sh_type == SHT_NOBITS)
				continue;
			/* Skip sections that were loaded already. */
			if (lshdr != NULL &&
			    lshdr->sh_offset >= shdr[i].sh_offset)
				continue;
			/* Find section with smallest offset. */
			if (cshdr == lshdr ||
			    cshdr->sh_offset > shdr[i].sh_offset)
				cshdr = &shdr[i];
		}

		if (cshdr == lshdr)
			break;

		if (kern_pread(ef->fd, (vm_offset_t)cshdr->sh_addr,
		    cshdr->sh_size, (off_t)cshdr->sh_offset) != 0) {
			printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
			    "_obj_loadimage: read failed\n");
			goto out;
		}
	}

	file_addmetadata(fp, MODINFOMD_SHDR, shdrbytes, shdr);

	res = __elfN(obj_parse_modmetadata)(fp, ef);
	if (res != 0)
		goto out;

	ret = lastaddr - firstaddr;
	fp->f_addr = firstaddr;

	printf("size 0x%lx at 0x%lx", (u_long)ret, (u_long)firstaddr);

out:
	printf("\n");
	return ret;
}

#if defined(__i386__) && __ELF_WORD_SIZE == 64
struct mod_metadata64 {
	int		md_version;	/* structure version MDTV_* */
	int		md_type;	/* type of entry MDT_* */
	uint64_t	md_data;	/* specific data */
	uint64_t	md_cval;	/* common string label */
};
#endif

int
__elfN(obj_parse_modmetadata)(struct preloaded_file *fp, elf_file_t ef)
{
	struct mod_metadata md;
#if defined(__i386__) && __ELF_WORD_SIZE == 64
	struct mod_metadata64 md64;
#endif
	struct mod_depend *mdepend;
	struct mod_version mver;
	char *s;
	int error, modcnt, minfolen;
	Elf_Addr v, p, p_stop;

	if (__elfN(obj_lookup_set)(fp, ef, "modmetadata_set", &p, &p_stop,
	    &modcnt) != 0)
		return 0;

	modcnt = 0;
	while (p < p_stop) {
		COPYOUT(p, &v, sizeof(v));
		error = __elfN(obj_reloc_ptr)(fp, ef, p, &v, sizeof(v));
		if (error != 0)
			return (error);
#if defined(__i386__) && __ELF_WORD_SIZE == 64
		COPYOUT(v, &md64, sizeof(md64));
		error = __elfN(obj_reloc_ptr)(fp, ef, v, &md64, sizeof(md64));
		if (error != 0)
			return (error);
		md.md_version = md64.md_version;
		md.md_type = md64.md_type;
		md.md_cval = (const char *)(uintptr_t)md64.md_cval;
		md.md_data = (void *)(uintptr_t)md64.md_data;
#else
		COPYOUT(v, &md, sizeof(md));
		error = __elfN(obj_reloc_ptr)(fp, ef, v, &md, sizeof(md));
		if (error != 0)
			return (error);
#endif
		p += sizeof(Elf_Addr);
		switch(md.md_type) {
		case MDT_DEPEND:
			s = strdupout((vm_offset_t)md.md_cval);
			minfolen = sizeof(*mdepend) + strlen(s) + 1;
			mdepend = malloc(minfolen);
			if (mdepend == NULL)
				return ENOMEM;
			COPYOUT((vm_offset_t)md.md_data, mdepend,
			    sizeof(*mdepend));
			strcpy((char*)(mdepend + 1), s);
			free(s);
			file_addmetadata(fp, MODINFOMD_DEPLIST, minfolen,
			    mdepend);
			free(mdepend);
			break;
		case MDT_VERSION:
			s = strdupout((vm_offset_t)md.md_cval);
			COPYOUT((vm_offset_t)md.md_data, &mver, sizeof(mver));
			file_addmodule(fp, s, mver.mv_version, NULL);
			free(s);
			modcnt++;
			break;
		case MDT_MODULE:
		case MDT_PNP_INFO:
			break;
		default:
			printf("unknown type %d\n", md.md_type);
			break;
		}
	}
	return 0;
}

static int
__elfN(obj_lookup_set)(struct preloaded_file *fp, elf_file_t ef,
    const char* name, Elf_Addr *startp, Elf_Addr *stopp, int *countp)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *shdr;
	char *p;
	vm_offset_t shstrtab;
	int i;

	hdr = &ef->hdr;
	shdr = ef->e_shdr;
	shstrtab = shdr[ef->shstrindex].sh_addr;

	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_type != SHT_PROGBITS)
			continue;
		if (shdr[i].sh_name == 0)
			continue;
		p = strdupout(shstrtab + shdr[i].sh_name);
		if (strncmp(p, "set_", 4) == 0 && strcmp(p + 4, name) == 0) {
			*startp = shdr[i].sh_addr;
			*stopp = shdr[i].sh_addr +  shdr[i].sh_size;
			*countp = (*stopp - *startp) / sizeof(Elf_Addr);
			free(p);
			return (0);
		}
		free(p);
	}

	return (ESRCH);
}

/*
 * Apply any intra-module relocations to the value. p is the load address
 * of the value and val/len is the value to be modified. This does NOT modify
 * the image in-place, because this is done by kern_linker later on.
 */
static int
__elfN(obj_reloc_ptr)(struct preloaded_file *mp, elf_file_t ef, Elf_Addr p,
    void *val, size_t len)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *shdr;
	Elf_Addr off = p;
	Elf_Addr base;
	Elf_Rela a, *abase;
	Elf_Rel r, *rbase;
	int error, i, j, nrel, nrela;

	hdr = &ef->hdr;
	shdr = ef->e_shdr;

	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_type != SHT_RELA && shdr[i].sh_type != SHT_REL)
			continue;
		base = shdr[shdr[i].sh_info].sh_addr;
		if (base == 0 || shdr[i].sh_addr == 0)
			continue;
		if (off < base || off + len > base +
		    shdr[shdr[i].sh_info].sh_size)
			continue;

		switch (shdr[i].sh_type) {
		case SHT_RELA:
			abase = (Elf_Rela *)(intptr_t)shdr[i].sh_addr;

			nrela = shdr[i].sh_size / sizeof(Elf_Rela);
			for (j = 0; j < nrela; j++) {
				COPYOUT(abase + j, &a, sizeof(a));

				error = __elfN(reloc)(ef, __elfN(obj_symaddr),
				    &a, ELF_RELOC_RELA, base, off, val, len);
				if (error != 0)
					return (error);
			}
			break;
		case SHT_REL:
			rbase = (Elf_Rel *)(intptr_t)shdr[i].sh_addr;

			nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			for (j = 0; j < nrel; j++) {
				COPYOUT(rbase + j, &r, sizeof(r));

				error = __elfN(reloc)(ef, __elfN(obj_symaddr),
				    &r, ELF_RELOC_REL, base, off, val, len);
				if (error != 0)
					return (error);
			}
			break;
		}
	}
	return (0);
}

/* Look up the address of a specified symbol. */
static Elf_Addr
__elfN(obj_symaddr)(struct elf_file *ef, Elf_Size symidx)
{
	Elf_Sym sym;
	Elf_Addr base;

	if (symidx >= ef->e_shdr[ef->symtabindex].sh_size / sizeof(Elf_Sym))
		return (0);
	COPYOUT(ef->e_shdr[ef->symtabindex].sh_addr + symidx * sizeof(Elf_Sym),
	    &sym, sizeof(sym));
	if (sym.st_shndx == SHN_UNDEF || sym.st_shndx >= ef->hdr.e_shnum)
		return (0);
	base = ef->e_shdr[sym.st_shndx].sh_addr;
	if (base == 0)
		return (0);
	return (base + sym.st_value);
}
