/*	$OpenBSD: ksyms.c,v 1.34 2022/01/08 22:54:49 guenther Exp $	*/
/*
 * Copyright (c) 1998 Todd C. Miller <millert@openbsd.org>
 * Copyright (c) 2001 Artur Grabowski <art@openbsd.org>
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
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/exec_elf.h>

extern char *esym;				/* end of symbol table */
#if defined(__sparc64__) || defined(__mips__) || defined(__amd64__) || \
    defined(__i386__) || defined(__powerpc64__)
extern char *ssym;				/* end of kernel */
#else
extern long end;				/* end of kernel */
#endif

static caddr_t ksym_head;
static caddr_t ksym_syms;
static size_t ksym_head_size;
static size_t ksym_syms_size;

void	ksymsattach(int);

void
ksymsattach(int num)
{

#if defined(__sparc64__) || defined(__mips__) || defined(__amd64__) || \
    defined(__i386__) || defined(__powerpc64__)
	if (esym <= ssym) {
		printf("/dev/ksyms: Symbol table not valid.\n");
		return;
	}
#else
	if (esym <= (char *)&end) {
		printf("/dev/ksyms: Symbol table not valid.\n");
		return;
	}
#endif

	do {
#if defined(__sparc64__) || defined(__mips__) || defined(__amd64__) || \
    defined(__i386__) || defined(__powerpc64__)
		caddr_t symtab = ssym;
#else
		caddr_t symtab = (caddr_t)&end;
#endif
		Elf_Ehdr *elf;
		Elf_Shdr *shdr;
		int i;

		elf = (Elf_Ehdr *)symtab;
		if (memcmp(elf->e_ident, ELFMAG, SELFMAG) != 0 ||
		    elf->e_ident[EI_CLASS] != ELFCLASS ||
		    elf->e_machine != ELF_TARG_MACH)
			break;

		shdr = (Elf_Shdr *)&symtab[elf->e_shoff];
		for (i = 0; i < elf->e_shnum; i++) {
			if (shdr[i].sh_type == SHT_SYMTAB) {
				break;
			}
		}

		/*
		 * No symbol table found.
		 */
		if (i == elf->e_shnum)
			break;

		/*
		 * No additional header.
		 */
		ksym_head_size = 0;
		ksym_syms = symtab;
		ksym_syms_size = (size_t)(esym - symtab);

		return;
	} while (0);
}

int
ksymsopen(dev_t dev, int flag, int mode, struct proc *p)
{

	/* There are no non-zero minor devices */
	if (minor(dev) != 0)
		return (ENXIO);

	/* This device is read-only */
	if ((flag & FWRITE))
		return (EPERM);

	/* ksym_syms must be initialized */
	if (ksym_syms == NULL)
		return (ENXIO);

	return (0);
}

int
ksymsclose(dev_t dev, int flag, int mode, struct proc *p)
{

	return (0);
}

int
ksymsread(dev_t dev, struct uio *uio, int flags)
{
	int error;
	size_t len;
	caddr_t v;
	size_t off;

	if (uio->uio_offset < 0)
		return (EINVAL);

	while (uio->uio_resid > 0) {
		if (uio->uio_offset >= ksym_head_size + ksym_syms_size)
			break;

		if (uio->uio_offset < ksym_head_size) {
			v = ksym_head + uio->uio_offset;
			len = ksym_head_size - uio->uio_offset;
		} else {
			off = uio->uio_offset - ksym_head_size;
			v = ksym_syms + off;
			len = ksym_syms_size - off;
		}

		if (len > uio->uio_resid)
			len = uio->uio_resid;

		if ((error = uiomove(v, len, uio)) != 0)
			return (error);
	}

	return (0);
}
