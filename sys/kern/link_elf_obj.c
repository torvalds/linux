/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/linker.h>

#include <machine/elf.h>

#include <net/vnet.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/link_elf.h>

#ifdef DDB_CTF
#include <sys/zlib.h>
#endif

#include "linker_if.h"

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


typedef struct elf_file {
	struct linker_file lf;		/* Common fields */

	int		preloaded;
	caddr_t		address;	/* Relocation address */
	vm_object_t	object;		/* VM object to hold file pages */
	Elf_Shdr	*e_shdr;

	Elf_progent	*progtab;
	u_int		nprogtab;

	Elf_relaent	*relatab;
	u_int		nrelatab;

	Elf_relent	*reltab;
	int		nreltab;

	Elf_Sym		*ddbsymtab;	/* The symbol table we are using */
	long		ddbsymcnt;	/* Number of symbols */
	caddr_t		ddbstrtab;	/* String table */
	long		ddbstrcnt;	/* number of bytes in string table */

	caddr_t		shstrtab;	/* Section name string table */
	long		shstrcnt;	/* number of bytes in string table */

	caddr_t		ctftab;		/* CTF table */
	long		ctfcnt;		/* number of bytes in CTF table */
	caddr_t		ctfoff;		/* CTF offset table */
	caddr_t		typoff;		/* Type offset table */
	long		typlen;		/* Number of type entries. */

} *elf_file_t;

#include <kern/kern_ctf.c>

static int	link_elf_link_preload(linker_class_t cls,
		    const char *, linker_file_t *);
static int	link_elf_link_preload_finish(linker_file_t);
static int	link_elf_load_file(linker_class_t, const char *, linker_file_t *);
static int	link_elf_lookup_symbol(linker_file_t, const char *,
		    c_linker_sym_t *);
static int	link_elf_symbol_values(linker_file_t, c_linker_sym_t,
		    linker_symval_t *);
static int	link_elf_search_symbol(linker_file_t, caddr_t value,
		    c_linker_sym_t *sym, long *diffp);

static void	link_elf_unload_file(linker_file_t);
static int	link_elf_lookup_set(linker_file_t, const char *,
		    void ***, void ***, int *);
static int	link_elf_each_function_name(linker_file_t,
		    int (*)(const char *, void *), void *);
static int	link_elf_each_function_nameval(linker_file_t,
				linker_function_nameval_callback_t,
				void *);
static int	link_elf_reloc_local(linker_file_t, bool);
static long	link_elf_symtab_get(linker_file_t, const Elf_Sym **);
static long	link_elf_strtab_get(linker_file_t, caddr_t *);

static int	elf_obj_lookup(linker_file_t lf, Elf_Size symidx, int deps,
		    Elf_Addr *);

static kobj_method_t link_elf_methods[] = {
	KOBJMETHOD(linker_lookup_symbol,	link_elf_lookup_symbol),
	KOBJMETHOD(linker_symbol_values,	link_elf_symbol_values),
	KOBJMETHOD(linker_search_symbol,	link_elf_search_symbol),
	KOBJMETHOD(linker_unload,		link_elf_unload_file),
	KOBJMETHOD(linker_load_file,		link_elf_load_file),
	KOBJMETHOD(linker_link_preload,		link_elf_link_preload),
	KOBJMETHOD(linker_link_preload_finish,	link_elf_link_preload_finish),
	KOBJMETHOD(linker_lookup_set,		link_elf_lookup_set),
	KOBJMETHOD(linker_each_function_name,	link_elf_each_function_name),
	KOBJMETHOD(linker_each_function_nameval, link_elf_each_function_nameval),
	KOBJMETHOD(linker_ctf_get,		link_elf_ctf_get),
	KOBJMETHOD(linker_symtab_get, 		link_elf_symtab_get),
	KOBJMETHOD(linker_strtab_get, 		link_elf_strtab_get),
	{ 0, 0 }
};

static struct linker_class link_elf_class = {
#if ELF_TARG_CLASS == ELFCLASS32
	"elf32_obj",
#else
	"elf64_obj",
#endif
	link_elf_methods, sizeof(struct elf_file)
};

static int	relocate_file(elf_file_t ef);
static void	elf_obj_cleanup_globals_cache(elf_file_t);

static void
link_elf_error(const char *filename, const char *s)
{
	if (filename == NULL)
		printf("kldload: %s\n", s);
	else
		printf("kldload: %s: %s\n", filename, s);
}

static void
link_elf_init(void *arg)
{

	linker_add_class(&link_elf_class);
}

SYSINIT(link_elf_obj, SI_SUB_KLD, SI_ORDER_SECOND, link_elf_init, NULL);

static int
link_elf_link_preload(linker_class_t cls, const char *filename,
    linker_file_t *result)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *shdr;
	Elf_Sym *es;
	void *modptr, *baseptr, *sizeptr;
	char *type;
	elf_file_t ef;
	linker_file_t lf;
	Elf_Addr off;
	int error, i, j, pb, ra, rl, shstrindex, symstrindex, symtabindex;

	/* Look to see if we have the file preloaded */
	modptr = preload_search_by_name(filename);
	if (modptr == NULL)
		return ENOENT;

	type = (char *)preload_search_info(modptr, MODINFO_TYPE);
	baseptr = preload_search_info(modptr, MODINFO_ADDR);
	sizeptr = preload_search_info(modptr, MODINFO_SIZE);
	hdr = (Elf_Ehdr *)preload_search_info(modptr, MODINFO_METADATA |
	    MODINFOMD_ELFHDR);
	shdr = (Elf_Shdr *)preload_search_info(modptr, MODINFO_METADATA |
	    MODINFOMD_SHDR);
	if (type == NULL || (strcmp(type, "elf" __XSTRING(__ELF_WORD_SIZE)
	    " obj module") != 0 &&
	    strcmp(type, "elf obj module") != 0)) {
		return (EFTYPE);
	}
	if (baseptr == NULL || sizeptr == NULL || hdr == NULL ||
	    shdr == NULL)
		return (EINVAL);

	lf = linker_make_file(filename, &link_elf_class);
	if (lf == NULL)
		return (ENOMEM);

	ef = (elf_file_t)lf;
	ef->preloaded = 1;
	ef->address = *(caddr_t *)baseptr;
	lf->address = *(caddr_t *)baseptr;
	lf->size = *(size_t *)sizeptr;

	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    hdr->e_version != EV_CURRENT ||
	    hdr->e_type != ET_REL ||
	    hdr->e_machine != ELF_TARG_MACH) {
		error = EFTYPE;
		goto out;
	}
	ef->e_shdr = shdr;

	/* Scan the section header for information and table sizing. */
	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			/* Ignore sections not loaded by the loader. */
			if (shdr[i].sh_addr == 0)
				break;
			ef->nprogtab++;
			break;
		case SHT_SYMTAB:
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
			break;
		case SHT_REL:
			/*
			 * Ignore relocation tables for sections not
			 * loaded by the loader.
			 */
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->nreltab++;
			break;
		case SHT_RELA:
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->nrelatab++;
			break;
		}
	}

	shstrindex = hdr->e_shstrndx;
	if (ef->nprogtab == 0 || symstrindex < 0 ||
	    symstrindex >= hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB || shstrindex == 0 ||
	    shstrindex >= hdr->e_shnum ||
	    shdr[shstrindex].sh_type != SHT_STRTAB) {
		printf("%s: bad/missing section headers\n", filename);
		error = ENOEXEC;
		goto out;
	}

	/* Allocate space for tracking the load chunks */
	if (ef->nprogtab != 0)
		ef->progtab = malloc(ef->nprogtab * sizeof(*ef->progtab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nreltab != 0)
		ef->reltab = malloc(ef->nreltab * sizeof(*ef->reltab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nrelatab != 0)
		ef->relatab = malloc(ef->nrelatab * sizeof(*ef->relatab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if ((ef->nprogtab != 0 && ef->progtab == NULL) ||
	    (ef->nreltab != 0 && ef->reltab == NULL) ||
	    (ef->nrelatab != 0 && ef->relatab == NULL)) {
		error = ENOMEM;
		goto out;
	}

	/* XXX, relocate the sh_addr fields saved by the loader. */
	off = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_addr != 0 && (off == 0 || shdr[i].sh_addr < off))
			off = shdr[i].sh_addr;
	}
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_addr != 0)
			shdr[i].sh_addr = shdr[i].sh_addr - off +
			    (Elf_Addr)ef->address;
	}

	ef->ddbsymcnt = shdr[symtabindex].sh_size / sizeof(Elf_Sym);
	ef->ddbsymtab = (Elf_Sym *)shdr[symtabindex].sh_addr;
	ef->ddbstrcnt = shdr[symstrindex].sh_size;
	ef->ddbstrtab = (char *)shdr[symstrindex].sh_addr;
	ef->shstrcnt = shdr[shstrindex].sh_size;
	ef->shstrtab = (char *)shdr[shstrindex].sh_addr;

	/* Now fill out progtab and the relocation tables. */
	pb = 0;
	rl = 0;
	ra = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if (shdr[i].sh_addr == 0)
				break;
			ef->progtab[pb].addr = (void *)shdr[i].sh_addr;
			if (shdr[i].sh_type == SHT_PROGBITS)
				ef->progtab[pb].name = "<<PROGBITS>>";
#ifdef __amd64__
			else if (shdr[i].sh_type == SHT_X86_64_UNWIND)
				ef->progtab[pb].name = "<<UNWIND>>";
#endif
			else
				ef->progtab[pb].name = "<<NOBITS>>";
			ef->progtab[pb].size = shdr[i].sh_size;
			ef->progtab[pb].sec = i;
			if (ef->shstrtab && shdr[i].sh_name != 0)
				ef->progtab[pb].name =
				    ef->shstrtab + shdr[i].sh_name;
			if (ef->progtab[pb].name != NULL && 
			    !strcmp(ef->progtab[pb].name, DPCPU_SETNAME)) {
				void *dpcpu;

				dpcpu = dpcpu_alloc(shdr[i].sh_size);
				if (dpcpu == NULL) {
					printf("%s: pcpu module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
					error = ENOSPC;
					goto out;
				}
				memcpy(dpcpu, ef->progtab[pb].addr,
				    ef->progtab[pb].size);
				dpcpu_copy(dpcpu, shdr[i].sh_size);
				ef->progtab[pb].addr = dpcpu;
#ifdef VIMAGE
			} else if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, VNET_SETNAME)) {
				void *vnet_data;

				vnet_data = vnet_data_alloc(shdr[i].sh_size);
				if (vnet_data == NULL) {
					printf("%s: vnet module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
					error = ENOSPC;
					goto out;
				}
				memcpy(vnet_data, ef->progtab[pb].addr,
				    ef->progtab[pb].size);
				vnet_data_copy(vnet_data, shdr[i].sh_size);
				ef->progtab[pb].addr = vnet_data;
#endif
			} else if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, ".ctors")) {
				lf->ctors_addr = ef->progtab[pb].addr;
				lf->ctors_size = shdr[i].sh_size;
			}

			/* Update all symbol values with the offset. */
			for (j = 0; j < ef->ddbsymcnt; j++) {
				es = &ef->ddbsymtab[j];
				if (es->st_shndx != i)
					continue;
				es->st_value += (Elf_Addr)ef->progtab[pb].addr;
			}
			pb++;
			break;
		case SHT_REL:
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->reltab[rl].rel = (Elf_Rel *)shdr[i].sh_addr;
			ef->reltab[rl].nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			ef->reltab[rl].sec = shdr[i].sh_info;
			rl++;
			break;
		case SHT_RELA:
			if (shdr[shdr[i].sh_info].sh_addr == 0)
				break;
			ef->relatab[ra].rela = (Elf_Rela *)shdr[i].sh_addr;
			ef->relatab[ra].nrela =
			    shdr[i].sh_size / sizeof(Elf_Rela);
			ef->relatab[ra].sec = shdr[i].sh_info;
			ra++;
			break;
		}
	}
	if (pb != ef->nprogtab) {
		printf("%s: lost progbits\n", filename);
		error = ENOEXEC;
		goto out;
	}
	if (rl != ef->nreltab) {
		printf("%s: lost reltab\n", filename);
		error = ENOEXEC;
		goto out;
	}
	if (ra != ef->nrelatab) {
		printf("%s: lost relatab\n", filename);
		error = ENOEXEC;
		goto out;
	}

	/* Local intra-module relocations */
	error = link_elf_reloc_local(lf, false);
	if (error != 0)
		goto out;
	*result = lf;
	return (0);

out:
	/* preload not done this way */
	linker_file_unload(lf, LINKER_UNLOAD_FORCE);
	return (error);
}

static void
link_elf_invoke_ctors(caddr_t addr, size_t size)
{
	void (**ctor)(void);
	size_t i, cnt;

	if (addr == NULL || size == 0)
		return;
	cnt = size / sizeof(*ctor);
	ctor = (void *)addr;
	for (i = 0; i < cnt; i++) {
		if (ctor[i] != NULL)
			(*ctor[i])();
	}
}

static int
link_elf_link_preload_finish(linker_file_t lf)
{
	elf_file_t ef;
	int error;

	ef = (elf_file_t)lf;
	error = relocate_file(ef);
	if (error)
		return (error);

	/* Notify MD code that a module is being loaded. */
	error = elf_cpu_load_file(lf);
	if (error)
		return (error);

#if defined(__i386__) || defined(__amd64__)
	/* Now ifuncs. */
	error = link_elf_reloc_local(lf, true);
	if (error != 0)
		return (error);
#endif

	/* Invoke .ctors */
	link_elf_invoke_ctors(lf->ctors_addr, lf->ctors_size);
	return (0);
}

static int
link_elf_load_file(linker_class_t cls, const char *filename,
    linker_file_t *result)
{
	struct nameidata *nd;
	struct thread *td = curthread;	/* XXX */
	Elf_Ehdr *hdr;
	Elf_Shdr *shdr;
	Elf_Sym *es;
	int nbytes, i, j;
	vm_offset_t mapbase;
	size_t mapsize;
	int error = 0;
	ssize_t resid;
	int flags;
	elf_file_t ef;
	linker_file_t lf;
	int symtabindex;
	int symstrindex;
	int shstrindex;
	int nsym;
	int pb, rl, ra;
	int alignmask;

	shdr = NULL;
	lf = NULL;
	mapsize = 0;
	hdr = NULL;

	nd = malloc(sizeof(struct nameidata), M_TEMP, M_WAITOK);
	NDINIT(nd, LOOKUP, FOLLOW, UIO_SYSSPACE, filename, td);
	flags = FREAD;
	error = vn_open(nd, &flags, 0, NULL);
	if (error) {
		free(nd, M_TEMP);
		return error;
	}
	NDFREE(nd, NDF_ONLY_PNBUF);
	if (nd->ni_vp->v_type != VREG) {
		error = ENOEXEC;
		goto out;
	}
#ifdef MAC
	error = mac_kld_check_load(td->td_ucred, nd->ni_vp);
	if (error) {
		goto out;
	}
#endif

	/* Read the elf header from the file. */
	hdr = malloc(sizeof(*hdr), M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, nd->ni_vp, (void *)hdr, sizeof(*hdr), 0,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error)
		goto out;
	if (resid != 0){
		error = ENOEXEC;
		goto out;
	}

	if (!IS_ELF(*hdr)) {
		error = ENOEXEC;
		goto out;
	}

	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS
	    || hdr->e_ident[EI_DATA] != ELF_TARG_DATA) {
		link_elf_error(filename, "Unsupported file layout");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_ident[EI_VERSION] != EV_CURRENT
	    || hdr->e_version != EV_CURRENT) {
		link_elf_error(filename, "Unsupported file version");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_type != ET_REL) {
		error = ENOSYS;
		goto out;
	}
	if (hdr->e_machine != ELF_TARG_MACH) {
		link_elf_error(filename, "Unsupported machine");
		error = ENOEXEC;
		goto out;
	}

	lf = linker_make_file(filename, &link_elf_class);
	if (!lf) {
		error = ENOMEM;
		goto out;
	}
	ef = (elf_file_t) lf;
	ef->nprogtab = 0;
	ef->e_shdr = 0;
	ef->nreltab = 0;
	ef->nrelatab = 0;

	/* Allocate and read in the section header */
	nbytes = hdr->e_shnum * hdr->e_shentsize;
	if (nbytes == 0 || hdr->e_shoff == 0 ||
	    hdr->e_shentsize != sizeof(Elf_Shdr)) {
		error = ENOEXEC;
		goto out;
	}
	shdr = malloc(nbytes, M_LINKER, M_WAITOK);
	ef->e_shdr = shdr;
	error = vn_rdwr(UIO_READ, nd->ni_vp, (caddr_t)shdr, nbytes,
	    hdr->e_shoff, UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred,
	    NOCRED, &resid, td);
	if (error)
		goto out;
	if (resid) {
		error = ENOEXEC;
		goto out;
	}

	/* Scan the section header for information and table sizing. */
	nsym = 0;
	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if ((shdr[i].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->nprogtab++;
			break;
		case SHT_SYMTAB:
			nsym++;
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
			break;
		case SHT_REL:
			/*
			 * Ignore relocation tables for unallocated
			 * sections.
			 */
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->nreltab++;
			break;
		case SHT_RELA:
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->nrelatab++;
			break;
		case SHT_STRTAB:
			break;
		}
	}
	if (ef->nprogtab == 0) {
		link_elf_error(filename, "file has no contents");
		error = ENOEXEC;
		goto out;
	}
	if (nsym != 1) {
		/* Only allow one symbol table for now */
		link_elf_error(filename,
		    "file must have exactly one symbol table");
		error = ENOEXEC;
		goto out;
	}
	if (symstrindex < 0 || symstrindex > hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB) {
		link_elf_error(filename, "file has invalid symbol strings");
		error = ENOEXEC;
		goto out;
	}

	/* Allocate space for tracking the load chunks */
	if (ef->nprogtab != 0)
		ef->progtab = malloc(ef->nprogtab * sizeof(*ef->progtab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nreltab != 0)
		ef->reltab = malloc(ef->nreltab * sizeof(*ef->reltab),
		    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nrelatab != 0)
		ef->relatab = malloc(ef->nrelatab * sizeof(*ef->relatab),
		    M_LINKER, M_WAITOK | M_ZERO);

	if (symtabindex == -1) {
		link_elf_error(filename, "lost symbol table index");
		error = ENOEXEC;
		goto out;
	}
	/* Allocate space for and load the symbol table */
	ef->ddbsymcnt = shdr[symtabindex].sh_size / sizeof(Elf_Sym);
	ef->ddbsymtab = malloc(shdr[symtabindex].sh_size, M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, nd->ni_vp, (void *)ef->ddbsymtab,
	    shdr[symtabindex].sh_size, shdr[symtabindex].sh_offset,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error)
		goto out;
	if (resid != 0){
		error = EINVAL;
		goto out;
	}

	if (symstrindex == -1) {
		link_elf_error(filename, "lost symbol string index");
		error = ENOEXEC;
		goto out;
	}
	/* Allocate space for and load the symbol strings */
	ef->ddbstrcnt = shdr[symstrindex].sh_size;
	ef->ddbstrtab = malloc(shdr[symstrindex].sh_size, M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, nd->ni_vp, ef->ddbstrtab,
	    shdr[symstrindex].sh_size, shdr[symstrindex].sh_offset,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error)
		goto out;
	if (resid != 0){
		error = EINVAL;
		goto out;
	}

	/* Do we have a string table for the section names?  */
	shstrindex = -1;
	if (hdr->e_shstrndx != 0 &&
	    shdr[hdr->e_shstrndx].sh_type == SHT_STRTAB) {
		shstrindex = hdr->e_shstrndx;
		ef->shstrcnt = shdr[shstrindex].sh_size;
		ef->shstrtab = malloc(shdr[shstrindex].sh_size, M_LINKER,
		    M_WAITOK);
		error = vn_rdwr(UIO_READ, nd->ni_vp, ef->shstrtab,
		    shdr[shstrindex].sh_size, shdr[shstrindex].sh_offset,
		    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
		    &resid, td);
		if (error)
			goto out;
		if (resid != 0){
			error = EINVAL;
			goto out;
		}
	}

	/* Size up code/data(progbits) and bss(nobits). */
	alignmask = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if ((shdr[i].sh_flags & SHF_ALLOC) == 0)
				break;
			alignmask = shdr[i].sh_addralign - 1;
			mapsize += alignmask;
			mapsize &= ~alignmask;
			mapsize += shdr[i].sh_size;
			break;
		}
	}

	/*
	 * We know how much space we need for the text/data/bss/etc.
	 * This stuff needs to be in a single chunk so that profiling etc
	 * can get the bounds and gdb can associate offsets with modules
	 */
	ef->object = vm_object_allocate(OBJT_DEFAULT,
	    round_page(mapsize) >> PAGE_SHIFT);
	if (ef->object == NULL) {
		error = ENOMEM;
		goto out;
	}
	ef->address = (caddr_t) vm_map_min(kernel_map);

	/*
	 * In order to satisfy amd64's architectural requirements on the
	 * location of code and data in the kernel's address space, request a
	 * mapping that is above the kernel.  
	 */
#ifdef __amd64__
	mapbase = KERNBASE;
#else
	mapbase = VM_MIN_KERNEL_ADDRESS;
#endif
	error = vm_map_find(kernel_map, ef->object, 0, &mapbase,
	    round_page(mapsize), 0, VMFS_OPTIMAL_SPACE, VM_PROT_ALL,
	    VM_PROT_ALL, 0);
	if (error) {
		vm_object_deallocate(ef->object);
		ef->object = 0;
		goto out;
	}

	/* Wire the pages */
	error = vm_map_wire(kernel_map, mapbase,
	    mapbase + round_page(mapsize),
	    VM_MAP_WIRE_SYSTEM|VM_MAP_WIRE_NOHOLES);
	if (error != KERN_SUCCESS) {
		error = ENOMEM;
		goto out;
	}

	/* Inform the kld system about the situation */
	lf->address = ef->address = (caddr_t)mapbase;
	lf->size = mapsize;

	/*
	 * Now load code/data(progbits), zero bss(nobits), allocate space for
	 * and load relocs
	 */
	pb = 0;
	rl = 0;
	ra = 0;
	alignmask = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_size == 0)
			continue;
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
#ifdef __amd64__
		case SHT_X86_64_UNWIND:
#endif
			if ((shdr[i].sh_flags & SHF_ALLOC) == 0)
				break;
			alignmask = shdr[i].sh_addralign - 1;
			mapbase += alignmask;
			mapbase &= ~alignmask;
			if (ef->shstrtab != NULL && shdr[i].sh_name != 0) {
				ef->progtab[pb].name =
				    ef->shstrtab + shdr[i].sh_name;
				if (!strcmp(ef->progtab[pb].name, ".ctors")) {
					lf->ctors_addr = (caddr_t)mapbase;
					lf->ctors_size = shdr[i].sh_size;
				}
			} else if (shdr[i].sh_type == SHT_PROGBITS)
				ef->progtab[pb].name = "<<PROGBITS>>";
#ifdef __amd64__
			else if (shdr[i].sh_type == SHT_X86_64_UNWIND)
				ef->progtab[pb].name = "<<UNWIND>>";
#endif
			else
				ef->progtab[pb].name = "<<NOBITS>>";
			if (ef->progtab[pb].name != NULL && 
			    !strcmp(ef->progtab[pb].name, DPCPU_SETNAME)) {
				ef->progtab[pb].addr =
				    dpcpu_alloc(shdr[i].sh_size);
				if (ef->progtab[pb].addr == NULL) {
					printf("%s: pcpu module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
				}
			}
#ifdef VIMAGE
			else if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, VNET_SETNAME)) {
				ef->progtab[pb].addr =
				    vnet_data_alloc(shdr[i].sh_size);
				if (ef->progtab[pb].addr == NULL) {
					printf("%s: vnet module space is out "
					    "of space; cannot allocate %#jx "
					    "for %s\n", __func__,
					    (uintmax_t)shdr[i].sh_size,
					    filename);
				}
			}
#endif
			else
				ef->progtab[pb].addr =
				    (void *)(uintptr_t)mapbase;
			if (ef->progtab[pb].addr == NULL) {
				error = ENOSPC;
				goto out;
			}
			ef->progtab[pb].size = shdr[i].sh_size;
			ef->progtab[pb].sec = i;
			if (shdr[i].sh_type == SHT_PROGBITS
#ifdef __amd64__
			    || shdr[i].sh_type == SHT_X86_64_UNWIND
#endif
			    ) {
				error = vn_rdwr(UIO_READ, nd->ni_vp,
				    ef->progtab[pb].addr,
				    shdr[i].sh_size, shdr[i].sh_offset,
				    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred,
				    NOCRED, &resid, td);
				if (error)
					goto out;
				if (resid != 0){
					error = EINVAL;
					goto out;
				}
				/* Initialize the per-cpu or vnet area. */
				if (ef->progtab[pb].addr != (void *)mapbase &&
				    !strcmp(ef->progtab[pb].name, DPCPU_SETNAME))
					dpcpu_copy(ef->progtab[pb].addr,
					    shdr[i].sh_size);
#ifdef VIMAGE
				else if (ef->progtab[pb].addr !=
				    (void *)mapbase &&
				    !strcmp(ef->progtab[pb].name, VNET_SETNAME))
					vnet_data_copy(ef->progtab[pb].addr,
					    shdr[i].sh_size);
#endif
			} else
				bzero(ef->progtab[pb].addr, shdr[i].sh_size);

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
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->reltab[rl].rel = malloc(shdr[i].sh_size, M_LINKER,
			    M_WAITOK);
			ef->reltab[rl].nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			ef->reltab[rl].sec = shdr[i].sh_info;
			error = vn_rdwr(UIO_READ, nd->ni_vp,
			    (void *)ef->reltab[rl].rel,
			    shdr[i].sh_size, shdr[i].sh_offset,
			    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
			    &resid, td);
			if (error)
				goto out;
			if (resid != 0){
				error = EINVAL;
				goto out;
			}
			rl++;
			break;
		case SHT_RELA:
			if ((shdr[shdr[i].sh_info].sh_flags & SHF_ALLOC) == 0)
				break;
			ef->relatab[ra].rela = malloc(shdr[i].sh_size, M_LINKER,
			    M_WAITOK);
			ef->relatab[ra].nrela =
			    shdr[i].sh_size / sizeof(Elf_Rela);
			ef->relatab[ra].sec = shdr[i].sh_info;
			error = vn_rdwr(UIO_READ, nd->ni_vp,
			    (void *)ef->relatab[ra].rela,
			    shdr[i].sh_size, shdr[i].sh_offset,
			    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
			    &resid, td);
			if (error)
				goto out;
			if (resid != 0){
				error = EINVAL;
				goto out;
			}
			ra++;
			break;
		}
	}
	if (pb != ef->nprogtab) {
		link_elf_error(filename, "lost progbits");
		error = ENOEXEC;
		goto out;
	}
	if (rl != ef->nreltab) {
		link_elf_error(filename, "lost reltab");
		error = ENOEXEC;
		goto out;
	}
	if (ra != ef->nrelatab) {
		link_elf_error(filename, "lost relatab");
		error = ENOEXEC;
		goto out;
	}
	if (mapbase != (vm_offset_t)ef->address + mapsize) {
		printf(
		    "%s: mapbase 0x%lx != address %p + mapsize 0x%lx (0x%lx)\n",
		    filename != NULL ? filename : "<none>",
		    (u_long)mapbase, ef->address, (u_long)mapsize,
		    (u_long)(vm_offset_t)ef->address + mapsize);
		error = ENOMEM;
		goto out;
	}

	/* Local intra-module relocations */
	error = link_elf_reloc_local(lf, false);
	if (error != 0)
		goto out;

	/* Pull in dependencies */
	VOP_UNLOCK(nd->ni_vp, 0);
	error = linker_load_dependencies(lf);
	vn_lock(nd->ni_vp, LK_EXCLUSIVE | LK_RETRY);
	if (error)
		goto out;

	/* External relocations */
	error = relocate_file(ef);
	if (error)
		goto out;

	/* Notify MD code that a module is being loaded. */
	error = elf_cpu_load_file(lf);
	if (error)
		goto out;

#if defined(__i386__) || defined(__amd64__)
	/* Now ifuncs. */
	error = link_elf_reloc_local(lf, true);
	if (error != 0)
		goto out;
#endif

	/* Invoke .ctors */
	link_elf_invoke_ctors(lf->ctors_addr, lf->ctors_size);

	*result = lf;

out:
	VOP_UNLOCK(nd->ni_vp, 0);
	vn_close(nd->ni_vp, FREAD, td->td_ucred, td);
	free(nd, M_TEMP);
	if (error && lf)
		linker_file_unload(lf, LINKER_UNLOAD_FORCE);
	free(hdr, M_LINKER);

	return error;
}

static void
link_elf_unload_file(linker_file_t file)
{
	elf_file_t ef = (elf_file_t) file;
	u_int i;

	/* Notify MD code that a module is being unloaded. */
	elf_cpu_unload_file(file);

	if (ef->progtab) {
		for (i = 0; i < ef->nprogtab; i++) {
			if (ef->progtab[i].size == 0)
				continue;
			if (ef->progtab[i].name == NULL)
				continue;
			if (!strcmp(ef->progtab[i].name, DPCPU_SETNAME))
				dpcpu_free(ef->progtab[i].addr,
				    ef->progtab[i].size);
#ifdef VIMAGE
			else if (!strcmp(ef->progtab[i].name, VNET_SETNAME))
				vnet_data_free(ef->progtab[i].addr,
				    ef->progtab[i].size);
#endif
		}
	}
	if (ef->preloaded) {
		free(ef->reltab, M_LINKER);
		free(ef->relatab, M_LINKER);
		free(ef->progtab, M_LINKER);
		free(ef->ctftab, M_LINKER);
		free(ef->ctfoff, M_LINKER);
		free(ef->typoff, M_LINKER);
		if (file->pathname != NULL)
			preload_delete_name(file->pathname);
		return;
	}

	for (i = 0; i < ef->nreltab; i++)
		free(ef->reltab[i].rel, M_LINKER);
	for (i = 0; i < ef->nrelatab; i++)
		free(ef->relatab[i].rela, M_LINKER);
	free(ef->reltab, M_LINKER);
	free(ef->relatab, M_LINKER);
	free(ef->progtab, M_LINKER);

	if (ef->object) {
		vm_map_remove(kernel_map, (vm_offset_t) ef->address,
		    (vm_offset_t) ef->address +
		    (ef->object->size << PAGE_SHIFT));
	}
	free(ef->e_shdr, M_LINKER);
	free(ef->ddbsymtab, M_LINKER);
	free(ef->ddbstrtab, M_LINKER);
	free(ef->shstrtab, M_LINKER);
	free(ef->ctftab, M_LINKER);
	free(ef->ctfoff, M_LINKER);
	free(ef->typoff, M_LINKER);
}

static const char *
symbol_name(elf_file_t ef, Elf_Size r_info)
{
	const Elf_Sym *ref;

	if (ELF_R_SYM(r_info)) {
		ref = ef->ddbsymtab + ELF_R_SYM(r_info);
		return ef->ddbstrtab + ref->st_name;
	} else
		return NULL;
}

static Elf_Addr
findbase(elf_file_t ef, int sec)
{
	int i;
	Elf_Addr base = 0;

	for (i = 0; i < ef->nprogtab; i++) {
		if (sec == ef->progtab[i].sec) {
			base = (Elf_Addr)ef->progtab[i].addr;
			break;
		}
	}
	return base;
}

static int
relocate_file(elf_file_t ef)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const char *symname;
	const Elf_Sym *sym;
	int i;
	Elf_Size symidx;
	Elf_Addr base;


	/* Perform relocations without addend if there are any: */
	for (i = 0; i < ef->nreltab; i++) {
		rel = ef->reltab[i].rel;
		if (rel == NULL) {
			link_elf_error(ef->lf.filename, "lost a reltab!");
			return (ENOEXEC);
		}
		rellim = rel + ef->reltab[i].nrel;
		base = findbase(ef, ef->reltab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename, "lost base for reltab");
			return (ENOEXEC);
		}
		for ( ; rel < rellim; rel++) {
			symidx = ELF_R_SYM(rel->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Local relocs are already done */
			if (ELF_ST_BIND(sym->st_info) == STB_LOCAL)
				continue;
			if (elf_reloc(&ef->lf, base, rel, ELF_RELOC_REL,
			    elf_obj_lookup)) {
				symname = symbol_name(ef, rel->r_info);
				printf("link_elf_obj: symbol %s undefined\n",
				    symname);
				return (ENOENT);
			}
		}
	}

	/* Perform relocations with addend if there are any: */
	for (i = 0; i < ef->nrelatab; i++) {
		rela = ef->relatab[i].rela;
		if (rela == NULL) {
			link_elf_error(ef->lf.filename, "lost a relatab!");
			return (ENOEXEC);
		}
		relalim = rela + ef->relatab[i].nrela;
		base = findbase(ef, ef->relatab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename,
			    "lost base for relatab");
			return (ENOEXEC);
		}
		for ( ; rela < relalim; rela++) {
			symidx = ELF_R_SYM(rela->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Local relocs are already done */
			if (ELF_ST_BIND(sym->st_info) == STB_LOCAL)
				continue;
			if (elf_reloc(&ef->lf, base, rela, ELF_RELOC_RELA,
			    elf_obj_lookup)) {
				symname = symbol_name(ef, rela->r_info);
				printf("link_elf_obj: symbol %s undefined\n",
				    symname);
				return (ENOENT);
			}
		}
	}

	/*
	 * Only clean SHN_FBSD_CACHED for successful return.  If we
	 * modified symbol table for the object but found an
	 * unresolved symbol, there is no reason to roll back.
	 */
	elf_obj_cleanup_globals_cache(ef);

	return (0);
}

static int
link_elf_lookup_symbol(linker_file_t lf, const char *name, c_linker_sym_t *sym)
{
	elf_file_t ef = (elf_file_t) lf;
	const Elf_Sym *symp;
	const char *strp;
	int i;

	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		strp = ef->ddbstrtab + symp->st_name;
		if (symp->st_shndx != SHN_UNDEF && strcmp(name, strp) == 0) {
			*sym = (c_linker_sym_t) symp;
			return 0;
		}
	}
	return ENOENT;
}

static int
link_elf_symbol_values(linker_file_t lf, c_linker_sym_t sym,
    linker_symval_t *symval)
{
	elf_file_t ef;
	const Elf_Sym *es;
	caddr_t val;

	ef = (elf_file_t) lf;
	es = (const Elf_Sym*) sym;
	val = (caddr_t)es->st_value;
	if (es >= ef->ddbsymtab && es < (ef->ddbsymtab + ef->ddbsymcnt)) {
		symval->name = ef->ddbstrtab + es->st_name;
		val = (caddr_t)es->st_value;
		if (ELF_ST_TYPE(es->st_info) == STT_GNU_IFUNC)
			val = ((caddr_t (*)(void))val)();
		symval->value = val;
		symval->size = es->st_size;
		return 0;
	}
	return ENOENT;
}

static int
link_elf_search_symbol(linker_file_t lf, caddr_t value,
    c_linker_sym_t *sym, long *diffp)
{
	elf_file_t ef = (elf_file_t) lf;
	u_long off = (uintptr_t) (void *) value;
	u_long diff = off;
	u_long st_value;
	const Elf_Sym *es;
	const Elf_Sym *best = NULL;
	int i;

	for (i = 0, es = ef->ddbsymtab; i < ef->ddbsymcnt; i++, es++) {
		if (es->st_name == 0)
			continue;
		st_value = es->st_value;
		if (off >= st_value) {
			if (off - st_value < diff) {
				diff = off - st_value;
				best = es;
				if (diff == 0)
					break;
			} else if (off - st_value == diff) {
				best = es;
			}
		}
	}
	if (best == NULL)
		*diffp = off;
	else
		*diffp = diff;
	*sym = (c_linker_sym_t) best;

	return 0;
}

/*
 * Look up a linker set on an ELF system.
 */
static int
link_elf_lookup_set(linker_file_t lf, const char *name,
    void ***startp, void ***stopp, int *countp)
{
	elf_file_t ef = (elf_file_t)lf;
	void **start, **stop;
	int i, count;

	/* Relative to section number */
	for (i = 0; i < ef->nprogtab; i++) {
		if ((strncmp(ef->progtab[i].name, "set_", 4) == 0) &&
		    strcmp(ef->progtab[i].name + 4, name) == 0) {
			start  = (void **)ef->progtab[i].addr;
			stop = (void **)((char *)ef->progtab[i].addr +
			    ef->progtab[i].size);
			count = stop - start;
			if (startp)
				*startp = start;
			if (stopp)
				*stopp = stop;
			if (countp)
				*countp = count;
			return (0);
		}
	}
	return (ESRCH);
}

static int
link_elf_each_function_name(linker_file_t file,
    int (*callback)(const char *, void *), void *opaque)
{
	elf_file_t ef = (elf_file_t)file;
	const Elf_Sym *symp;
	int i, error;
	
	/* Exhaustive search */
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		if (symp->st_value != 0 &&
		    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
		    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC)) {
			error = callback(ef->ddbstrtab + symp->st_name, opaque);
			if (error)
				return (error);
		}
	}
	return (0);
}

static int
link_elf_each_function_nameval(linker_file_t file,
    linker_function_nameval_callback_t callback, void *opaque)
{
	linker_symval_t symval;
	elf_file_t ef = (elf_file_t)file;
	const Elf_Sym* symp;
	int i, error;

	/* Exhaustive search */
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		if (symp->st_value != 0 &&
		    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
		    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC)) {
			error = link_elf_symbol_values(file,
			    (c_linker_sym_t)symp, &symval);
			if (error)
				return (error);
			error = callback(file, i, &symval, opaque);
			if (error)
				return (error);
		}
	}
	return (0);
}

static void
elf_obj_cleanup_globals_cache(elf_file_t ef)
{
	Elf_Sym *sym;
	Elf_Size i;

	for (i = 0; i < ef->ddbsymcnt; i++) {
		sym = ef->ddbsymtab + i;
		if (sym->st_shndx == SHN_FBSD_CACHED) {
			sym->st_shndx = SHN_UNDEF;
			sym->st_value = 0;
		}
	}
}

/*
 * Symbol lookup function that can be used when the symbol index is known (ie
 * in relocations). It uses the symbol index instead of doing a fully fledged
 * hash table based lookup when such is valid. For example for local symbols.
 * This is not only more efficient, it's also more correct. It's not always
 * the case that the symbol can be found through the hash table.
 */
static int
elf_obj_lookup(linker_file_t lf, Elf_Size symidx, int deps, Elf_Addr *res)
{
	elf_file_t ef = (elf_file_t)lf;
	Elf_Sym *sym;
	const char *symbol;
	Elf_Addr res1;

	/* Don't even try to lookup the symbol if the index is bogus. */
	if (symidx >= ef->ddbsymcnt) {
		*res = 0;
		return (EINVAL);
	}

	sym = ef->ddbsymtab + symidx;

	/* Quick answer if there is a definition included. */
	if (sym->st_shndx != SHN_UNDEF) {
		res1 = (Elf_Addr)sym->st_value;
		if (ELF_ST_TYPE(sym->st_info) == STT_GNU_IFUNC)
			res1 = ((Elf_Addr (*)(void))res1)();
		*res = res1;
		return (0);
	}

	/* If we get here, then it is undefined and needs a lookup. */
	switch (ELF_ST_BIND(sym->st_info)) {
	case STB_LOCAL:
		/* Local, but undefined? huh? */
		*res = 0;
		return (EINVAL);

	case STB_GLOBAL:
	case STB_WEAK:
		/* Relative to Data or Function name */
		symbol = ef->ddbstrtab + sym->st_name;

		/* Force a lookup failure if the symbol name is bogus. */
		if (*symbol == 0) {
			*res = 0;
			return (EINVAL);
		}
		res1 = (Elf_Addr)linker_file_lookup_symbol(lf, symbol, deps);

		/*
		 * Cache global lookups during module relocation. The failure
		 * case is particularly expensive for callers, who must scan
		 * through the entire globals table doing strcmp(). Cache to
		 * avoid doing such work repeatedly.
		 *
		 * After relocation is complete, undefined globals will be
		 * restored to SHN_UNDEF in elf_obj_cleanup_globals_cache(),
		 * above.
		 */
		if (res1 != 0) {
			sym->st_shndx = SHN_FBSD_CACHED;
			sym->st_value = res1;
			*res = res1;
			return (0);
		} else if (ELF_ST_BIND(sym->st_info) == STB_WEAK) {
			sym->st_value = 0;
			*res = 0;
			return (0);
		}
		return (EINVAL);

	default:
		return (EINVAL);
	}
}

static void
link_elf_fix_link_set(elf_file_t ef)
{
	static const char startn[] = "__start_";
	static const char stopn[] = "__stop_";
	Elf_Sym *sym;
	const char *sym_name, *linkset_name;
	Elf_Addr startp, stopp;
	Elf_Size symidx;
	int start, i;

	startp = stopp = 0;
	for (symidx = 1 /* zero entry is special */;
		symidx < ef->ddbsymcnt; symidx++) {
		sym = ef->ddbsymtab + symidx;
		if (sym->st_shndx != SHN_UNDEF)
			continue;

		sym_name = ef->ddbstrtab + sym->st_name;
		if (strncmp(sym_name, startn, sizeof(startn) - 1) == 0) {
			start = 1;
			linkset_name = sym_name + sizeof(startn) - 1;
		}
		else if (strncmp(sym_name, stopn, sizeof(stopn) - 1) == 0) {
			start = 0;
			linkset_name = sym_name + sizeof(stopn) - 1;
		}
		else
			continue;

		for (i = 0; i < ef->nprogtab; i++) {
			if (strcmp(ef->progtab[i].name, linkset_name) == 0) {
				startp = (Elf_Addr)ef->progtab[i].addr;
				stopp = (Elf_Addr)(startp + ef->progtab[i].size);
				break;
			}
		}
		if (i == ef->nprogtab)
			continue;

		sym->st_value = start ? startp : stopp;
		sym->st_shndx = i;
	}
}

static int
link_elf_reloc_local(linker_file_t lf, bool ifuncs)
{
	elf_file_t ef = (elf_file_t)lf;
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Sym *sym;
	Elf_Addr base;
	int i;
	Elf_Size symidx;

	link_elf_fix_link_set(ef);

	/* Perform relocations without addend if there are any: */
	for (i = 0; i < ef->nreltab; i++) {
		rel = ef->reltab[i].rel;
		if (rel == NULL) {
			link_elf_error(ef->lf.filename, "lost a reltab");
			return (ENOEXEC);
		}
		rellim = rel + ef->reltab[i].nrel;
		base = findbase(ef, ef->reltab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename, "lost base for reltab");
			return (ENOEXEC);
		}
		for ( ; rel < rellim; rel++) {
			symidx = ELF_R_SYM(rel->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Only do local relocs */
			if (ELF_ST_BIND(sym->st_info) != STB_LOCAL)
				continue;
			if ((ELF_ST_TYPE(sym->st_info) == STT_GNU_IFUNC ||
			    elf_is_ifunc_reloc(rel->r_info)) == ifuncs)
				elf_reloc_local(lf, base, rel, ELF_RELOC_REL,
				    elf_obj_lookup);
		}
	}

	/* Perform relocations with addend if there are any: */
	for (i = 0; i < ef->nrelatab; i++) {
		rela = ef->relatab[i].rela;
		if (rela == NULL) {
			link_elf_error(ef->lf.filename, "lost a relatab!");
			return (ENOEXEC);
		}
		relalim = rela + ef->relatab[i].nrela;
		base = findbase(ef, ef->relatab[i].sec);
		if (base == 0) {
			link_elf_error(ef->lf.filename, "lost base for reltab");
			return (ENOEXEC);
		}
		for ( ; rela < relalim; rela++) {
			symidx = ELF_R_SYM(rela->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Only do local relocs */
			if (ELF_ST_BIND(sym->st_info) != STB_LOCAL)
				continue;
			if ((ELF_ST_TYPE(sym->st_info) == STT_GNU_IFUNC ||
			    elf_is_ifunc_reloc(rela->r_info)) == ifuncs)
				elf_reloc_local(lf, base, rela, ELF_RELOC_RELA,
				    elf_obj_lookup);
		}
	}
	return (0);
}

static long
link_elf_symtab_get(linker_file_t lf, const Elf_Sym **symtab)
{
    elf_file_t ef = (elf_file_t)lf;
    
    *symtab = ef->ddbsymtab;
    
    if (*symtab == NULL)
        return (0);

    return (ef->ddbsymcnt);
}
    
static long
link_elf_strtab_get(linker_file_t lf, caddr_t *strtab)
{
    elf_file_t ef = (elf_file_t)lf;

    *strtab = ef->ddbstrtab;

    if (*strtab == NULL)
        return (0);

    return (ef->ddbstrcnt);
}
