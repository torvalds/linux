/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009, Stacey Son <sson@freebsd.org>
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <sys/conf.h>
#include <sys/elf.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/uio.h>

#include <machine/elf.h>

#include <vm/pmap.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>

#include "linker_if.h"

#define SHDR_NULL	0
#define SHDR_SYMTAB	1
#define SHDR_STRTAB	2
#define SHDR_SHSTRTAB	3

#define SHDR_NUM	4

#define STR_SYMTAB	".symtab"
#define STR_STRTAB	".strtab"
#define STR_SHSTRTAB	".shstrtab"

#define KSYMS_DNAME	"ksyms"

static d_open_t ksyms_open;
static d_read_t ksyms_read;
static d_mmap_single_t ksyms_mmap_single;

static struct cdevsw ksyms_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	ksyms_open,
	.d_read =	ksyms_read,
	.d_mmap_single = ksyms_mmap_single,
	.d_name =	KSYMS_DNAME
};

struct ksyms_softc {
	LIST_ENTRY(ksyms_softc)	sc_list;
	vm_offset_t		sc_uaddr;
	size_t			sc_usize;
	vm_object_t		sc_obj;
	vm_size_t		sc_objsz;
	struct proc	       *sc_proc;
};

static struct sx		 ksyms_mtx;
static struct cdev		*ksyms_dev;
static LIST_HEAD(, ksyms_softc)	 ksyms_list = LIST_HEAD_INITIALIZER(ksyms_list);

static const char	ksyms_shstrtab[] =
	"\0" STR_SYMTAB "\0" STR_STRTAB "\0" STR_SHSTRTAB "\0";

struct ksyms_hdr {
	Elf_Ehdr	kh_ehdr;
	Elf_Phdr	kh_txtphdr;
	Elf_Phdr	kh_datphdr;
	Elf_Shdr	kh_shdr[SHDR_NUM];
	char		kh_shstrtab[sizeof(ksyms_shstrtab)];
};

struct tsizes {
	size_t		ts_symsz;
	size_t		ts_strsz;
};

struct toffsets {
	struct ksyms_softc *to_sc;
	vm_offset_t	to_symoff;
	vm_offset_t	to_stroff;
	unsigned	to_stridx;
	size_t		to_resid;
};

static MALLOC_DEFINE(M_KSYMS, "KSYMS", "Kernel Symbol Table");

/*
 * Get the symbol and string table sizes for a kernel module. Add it to the
 * running total.
 */
static int
ksyms_size_permod(linker_file_t lf, void *arg)
{
	struct tsizes *ts;
	const Elf_Sym *symtab;
	caddr_t strtab;
	long syms;

	ts = arg;

	syms = LINKER_SYMTAB_GET(lf, &symtab);
	ts->ts_symsz += syms * sizeof(Elf_Sym);
	ts->ts_strsz += LINKER_STRTAB_GET(lf, &strtab);

	return (0);
}

/*
 * For kernel module get the symbol and string table sizes, returning the
 * totals in *ts.
 */
static void
ksyms_size_calc(struct tsizes *ts)
{

	ts->ts_symsz = 0;
	ts->ts_strsz = 0;

	(void)linker_file_foreach(ksyms_size_permod, ts);
}

static int
ksyms_emit(struct ksyms_softc *sc, void *buf, off_t off, size_t sz)
{
	struct iovec iov;
	struct uio uio;

	iov.iov_base = buf;
	iov.iov_len = sz;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = off;
	uio.uio_resid = (ssize_t)sz;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_td = curthread;

	return (uiomove_object(sc->sc_obj, sc->sc_objsz, &uio));
}

#define SYMBLKSZ	(256 * sizeof(Elf_Sym))

/*
 * For a kernel module, add the symbol and string tables into the
 * snapshot buffer.  Fix up the offsets in the tables.
 */
static int
ksyms_add(linker_file_t lf, void *arg)
{
	char *buf;
	struct ksyms_softc *sc;
	struct toffsets *to;
	const Elf_Sym *symtab;
	Elf_Sym *symp;
	caddr_t strtab;
	size_t len, numsyms, strsz, symsz;
	linker_symval_t symval;
	int error, i, nsyms;

	buf = malloc(SYMBLKSZ, M_KSYMS, M_WAITOK);
	to = arg;
	sc = to->to_sc;

	MOD_SLOCK;
	numsyms =  LINKER_SYMTAB_GET(lf, &symtab);
	strsz = LINKER_STRTAB_GET(lf, &strtab);
	symsz = numsyms * sizeof(Elf_Sym);

	while (symsz > 0) {
		len = min(SYMBLKSZ, symsz);
		bcopy(symtab, buf, len);

		/*
		 * Fix up symbol table for kernel modules:
		 *   string offsets need adjusted
		 *   symbol values made absolute
		 */
		symp = (Elf_Sym *) buf;
		nsyms = len / sizeof(Elf_Sym);
		for (i = 0; i < nsyms; i++) {
			symp[i].st_name += to->to_stridx;
			if (lf->id > 1 && LINKER_SYMBOL_VALUES(lf,
			    (c_linker_sym_t)&symtab[i], &symval) == 0) {
				symp[i].st_value = (uintptr_t)symval.value;
			}
		}

		if (len > to->to_resid) {
			MOD_SUNLOCK;
			free(buf, M_KSYMS);
			return (ENXIO);
		}
		to->to_resid -= len;
		error = ksyms_emit(sc, buf, to->to_symoff, len);
		to->to_symoff += len;
		if (error != 0) {
			MOD_SUNLOCK;
			free(buf, M_KSYMS);
			return (error);
		}

		symtab += nsyms;
		symsz -= len;
	}
	free(buf, M_KSYMS);
	MOD_SUNLOCK;

	if (strsz > to->to_resid)
		return (ENXIO);
	to->to_resid -= strsz;
	error = ksyms_emit(sc, strtab, to->to_stroff, strsz);
	to->to_stroff += strsz;
	to->to_stridx += strsz;

	return (error);
}

/*
 * Create a single ELF symbol table for the kernel and kernel modules loaded
 * at this time. Write this snapshot out in the process address space. Return
 * 0 on success, otherwise error.
 */
static int
ksyms_snapshot(struct ksyms_softc *sc, struct tsizes *ts)
{
	struct toffsets	to;
	struct ksyms_hdr *hdr;
	int error;

	hdr = malloc(sizeof(*hdr), M_KSYMS, M_WAITOK | M_ZERO);

	/*
	 * Create the ELF header.
	 */
	hdr->kh_ehdr.e_ident[EI_PAD] = 0;
	hdr->kh_ehdr.e_ident[EI_MAG0] = ELFMAG0;
	hdr->kh_ehdr.e_ident[EI_MAG1] = ELFMAG1;
	hdr->kh_ehdr.e_ident[EI_MAG2] = ELFMAG2;
	hdr->kh_ehdr.e_ident[EI_MAG3] = ELFMAG3;
	hdr->kh_ehdr.e_ident[EI_DATA] = ELF_DATA;
	hdr->kh_ehdr.e_ident[EI_OSABI] = ELFOSABI_FREEBSD;
	hdr->kh_ehdr.e_ident[EI_CLASS] = ELF_CLASS;
	hdr->kh_ehdr.e_ident[EI_VERSION] = EV_CURRENT;
	hdr->kh_ehdr.e_ident[EI_ABIVERSION] = 0;
	hdr->kh_ehdr.e_type = ET_EXEC;
	hdr->kh_ehdr.e_machine = ELF_ARCH;
	hdr->kh_ehdr.e_version = EV_CURRENT;
	hdr->kh_ehdr.e_entry = 0;
	hdr->kh_ehdr.e_phoff = offsetof(struct ksyms_hdr, kh_txtphdr);
	hdr->kh_ehdr.e_shoff = offsetof(struct ksyms_hdr, kh_shdr);
	hdr->kh_ehdr.e_flags = 0;
	hdr->kh_ehdr.e_ehsize = sizeof(Elf_Ehdr);
	hdr->kh_ehdr.e_phentsize = sizeof(Elf_Phdr);
	hdr->kh_ehdr.e_phnum = 2;	/* Text and Data */
	hdr->kh_ehdr.e_shentsize = sizeof(Elf_Shdr);
	hdr->kh_ehdr.e_shnum = SHDR_NUM;
	hdr->kh_ehdr.e_shstrndx = SHDR_SHSTRTAB;

	/*
	 * Add both the text and data program headers.
	 */
	hdr->kh_txtphdr.p_type = PT_LOAD;
	/* XXX - is there a way to put the actual .text addr/size here? */
	hdr->kh_txtphdr.p_vaddr = 0;
	hdr->kh_txtphdr.p_memsz = 0;
	hdr->kh_txtphdr.p_flags = PF_R | PF_X;

	hdr->kh_datphdr.p_type = PT_LOAD;
	/* XXX - is there a way to put the actual .data addr/size here? */
	hdr->kh_datphdr.p_vaddr = 0;
	hdr->kh_datphdr.p_memsz = 0;
	hdr->kh_datphdr.p_flags = PF_R | PF_W | PF_X;

	/*
	 * Add the section headers: null, symtab, strtab, shstrtab.
	 */

	/* First section header - null */

	/* Second section header - symtab */
	hdr->kh_shdr[SHDR_SYMTAB].sh_name = 1; /* String offset (skip null) */
	hdr->kh_shdr[SHDR_SYMTAB].sh_type = SHT_SYMTAB;
	hdr->kh_shdr[SHDR_SYMTAB].sh_flags = 0;
	hdr->kh_shdr[SHDR_SYMTAB].sh_addr = 0;
	hdr->kh_shdr[SHDR_SYMTAB].sh_offset = sizeof(*hdr);
	hdr->kh_shdr[SHDR_SYMTAB].sh_size = ts->ts_symsz;
	hdr->kh_shdr[SHDR_SYMTAB].sh_link = SHDR_STRTAB;
	hdr->kh_shdr[SHDR_SYMTAB].sh_info = ts->ts_symsz / sizeof(Elf_Sym);
	hdr->kh_shdr[SHDR_SYMTAB].sh_addralign = sizeof(long);
	hdr->kh_shdr[SHDR_SYMTAB].sh_entsize = sizeof(Elf_Sym);

	/* Third section header - strtab */
	hdr->kh_shdr[SHDR_STRTAB].sh_name = 1 + sizeof(STR_SYMTAB);
	hdr->kh_shdr[SHDR_STRTAB].sh_type = SHT_STRTAB;
	hdr->kh_shdr[SHDR_STRTAB].sh_flags = 0;
	hdr->kh_shdr[SHDR_STRTAB].sh_addr = 0;
	hdr->kh_shdr[SHDR_STRTAB].sh_offset =
	    hdr->kh_shdr[SHDR_SYMTAB].sh_offset + ts->ts_symsz;
	hdr->kh_shdr[SHDR_STRTAB].sh_size = ts->ts_strsz;
	hdr->kh_shdr[SHDR_STRTAB].sh_link = 0;
	hdr->kh_shdr[SHDR_STRTAB].sh_info = 0;
	hdr->kh_shdr[SHDR_STRTAB].sh_addralign = sizeof(char);
	hdr->kh_shdr[SHDR_STRTAB].sh_entsize = 0;

	/* Fourth section - shstrtab */
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_name = 1 + sizeof(STR_SYMTAB) +
	    sizeof(STR_STRTAB);
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_type = SHT_STRTAB;
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_flags = 0;
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_addr = 0;
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_offset =
	    offsetof(struct ksyms_hdr, kh_shstrtab);
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_size = sizeof(ksyms_shstrtab);
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_link = 0;
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_info = 0;
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_addralign = 0 /* sizeof(char) */;
	hdr->kh_shdr[SHDR_SHSTRTAB].sh_entsize = 0;

	/* Copy shstrtab into the header. */
	bcopy(ksyms_shstrtab, hdr->kh_shstrtab, sizeof(ksyms_shstrtab));

	to.to_sc = sc;
	to.to_symoff = hdr->kh_shdr[SHDR_SYMTAB].sh_offset;
	to.to_stroff = hdr->kh_shdr[SHDR_STRTAB].sh_offset;
	to.to_stridx = 0;
	to.to_resid = sc->sc_objsz - sizeof(struct ksyms_hdr);

	/* emit header */
	error = ksyms_emit(sc, hdr, 0, sizeof(*hdr));
	free(hdr, M_KSYMS);
	if (error != 0)
		return (error);

	/* Add symbol and string tables for each kernel module. */
	error = linker_file_foreach(ksyms_add, &to);
	if (error != 0)
		return (error);
	if (to.to_resid != 0)
		return (ENXIO);
	return (0);
}

static void
ksyms_cdevpriv_dtr(void *data)
{
	struct ksyms_softc *sc;
	vm_object_t obj;

	sc = (struct ksyms_softc *)data;

	sx_xlock(&ksyms_mtx);
	LIST_REMOVE(sc, sc_list);
	sx_xunlock(&ksyms_mtx);
	obj = sc->sc_obj;
	if (obj != NULL)
		vm_object_deallocate(obj);
	free(sc, M_KSYMS);
}

static int
ksyms_open(struct cdev *dev, int flags, int fmt __unused, struct thread *td)
{
	struct tsizes ts;
	struct ksyms_softc *sc;
	vm_size_t elfsz;
	int error, try;

	/*
	 * Limit one open() per process. The process must close()
	 * before open()'ing again.
	 */
	sx_xlock(&ksyms_mtx);
	LIST_FOREACH(sc, &ksyms_list, sc_list) {
		if (sc->sc_proc == td->td_proc) {
			sx_xunlock(&ksyms_mtx);
			return (EBUSY);
		}
	}

	sc = malloc(sizeof(*sc), M_KSYMS, M_WAITOK | M_ZERO);
	sc->sc_proc = td->td_proc;
	LIST_INSERT_HEAD(&ksyms_list, sc, sc_list);
	sx_xunlock(&ksyms_mtx);

	error = devfs_set_cdevpriv(sc, ksyms_cdevpriv_dtr);
	if (error != 0) {
		ksyms_cdevpriv_dtr(sc);
		return (error);
	}

	/*
	 * MOD_SLOCK doesn't work here (because of a lock reversal with
	 * KLD_SLOCK).  Therefore, simply try up to 3 times to get a "clean"
	 * snapshot of the kernel symbol table.  This should work fine in the
	 * rare case of a kernel module being loaded/unloaded at the same
	 * time.
	 */
	for (try = 0; try < 3; try++) {
		ksyms_size_calc(&ts);
		elfsz = sizeof(struct ksyms_hdr) + ts.ts_symsz + ts.ts_strsz;

		sc->sc_obj = vm_object_allocate(OBJT_DEFAULT,
		    OFF_TO_IDX(round_page(elfsz)));
		sc->sc_objsz = elfsz;

		error = ksyms_snapshot(sc, &ts);
		if (error == 0)
			break;

		vm_object_deallocate(sc->sc_obj);
		sc->sc_obj = NULL;
	}
	return (error);
}

static int
ksyms_read(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct ksyms_softc *sc;
	int error;

	error = devfs_get_cdevpriv((void **)&sc);
	if (error != 0)
		return (error);
	return (uiomove_object(sc->sc_obj, sc->sc_objsz, uio));
}

static int
ksyms_mmap_single(struct cdev *dev, vm_ooffset_t *offset, vm_size_t size,
    vm_object_t *objp, int nprot)
{
	struct ksyms_softc *sc;
	vm_object_t obj;
	int error;

	error = devfs_get_cdevpriv((void **)&sc);
	if (error != 0)
		return (error);

	if (*offset < 0 || *offset >= round_page(sc->sc_objsz) ||
	    size > round_page(sc->sc_objsz) - *offset ||
	    (nprot & ~PROT_READ) != 0)
		return (EINVAL);

	obj = sc->sc_obj;
	vm_object_reference(obj);
	*objp = obj;
	return (0);
}

static int
ksyms_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error;

	error = 0;
	switch (type) {
	case MOD_LOAD:
		sx_init(&ksyms_mtx, "KSyms mtx");
		ksyms_dev = make_dev(&ksyms_cdevsw, 0, UID_ROOT, GID_WHEEL,
		    0400, KSYMS_DNAME);
		break;
	case MOD_UNLOAD:
		if (!LIST_EMPTY(&ksyms_list))
			return (EBUSY);
		destroy_dev(ksyms_dev);
		sx_destroy(&ksyms_mtx);
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

DEV_MODULE(ksyms, ksyms_modevent, NULL);
MODULE_VERSION(ksyms, 1);
