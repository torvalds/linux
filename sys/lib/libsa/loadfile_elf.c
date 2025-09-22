/* $NetBSD: loadfile.c,v 1.10 2000/12/03 02:53:04 tsutsui Exp $ */
/* $OpenBSD: loadfile_elf.c,v 1.17 2020/10/26 04:04:31 visa Exp $ */

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center and by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)boot.c	8.1 (Berkeley) 6/10/93
 */

#include <lib/libsa/arc4.h>

int ELFNAME(exec)(int, Elf_Ehdr *, uint64_t *, int);

int
ELFNAME(exec)(int fd, Elf_Ehdr *elf, uint64_t *marks, int flags)
{
	Elf_Shdr *shp;
	Elf_Phdr *phdr;
	Elf_Off off;
	int i;
	size_t sz;
	int first;
	int havesyms;
	paddr_t minp = ~0, maxp = 0, pos = 0;
	paddr_t offset = marks[MARK_START], shpp, elfp;

	sz = elf->e_phnum * sizeof(Elf_Phdr);
	phdr = ALLOC(sz);

	if (lseek(fd, (off_t)elf->e_phoff, SEEK_SET) == -1)  {
		WARN(("lseek phdr"));
		FREE(phdr, sz);
		return 1;
	}
	if (read(fd, phdr, sz) != sz) {
		WARN(("read program headers"));
		FREE(phdr, sz);
		return 1;
	}

	for (first = 1, i = 0; i < elf->e_phnum; i++) {
		if (phdr[i].p_type == PT_OPENBSD_RANDOMIZE) {

			/* Fill segment if asked for. */
			if (flags & LOAD_RANDOM) {
				extern struct rc4_ctx randomctx;

				rc4_getbytes(&randomctx,
				    (void *)LOADADDR(phdr[i].p_paddr),
				    phdr[i].p_filesz);
			}
			if (flags & (LOAD_RANDOM | COUNT_RANDOM)) {
				marks[MARK_RANDOM] = LOADADDR(phdr[i].p_paddr);
				marks[MARK_ERANDOM] =
				    marks[MARK_RANDOM] + phdr[i].p_filesz;
			}
			continue;
		}

		if (phdr[i].p_type != PT_LOAD ||
		    (phdr[i].p_flags & (PF_W|PF_R|PF_X)) == 0)
			continue;

#ifdef CHECK_PHDR
		if (CHECK_PHDR(ELFSIZE, &phdr[i])) {
			FREE(phdr, sz);
			return 1;
		}
#endif

#define IS_TEXT(p)	(p.p_flags & PF_X)
#define IS_DATA(p)	((p.p_flags & PF_X) == 0)
#define IS_BSS(p)	(p.p_filesz < p.p_memsz)
		/*
		 * XXX: Assume first address is lowest
		 */
		if ((IS_TEXT(phdr[i]) && (flags & LOAD_TEXT)) ||
		    (IS_DATA(phdr[i]) && (flags & LOAD_DATA))) {

			/* Read in segment. */
			PROGRESS(("%s%lu", first ? "" : "+",
			    (u_long)phdr[i].p_filesz));

			if (lseek(fd, (off_t)phdr[i].p_offset, SEEK_SET) == -1) {
				WARN(("lseek text"));
				FREE(phdr, sz);
				return 1;
			}
			if (READ(fd, phdr[i].p_paddr, phdr[i].p_filesz) !=
			    phdr[i].p_filesz) {
				WARN(("read text"));
				FREE(phdr, sz);
				return 1;
			}

			first = 0;
		}

		if ((IS_TEXT(phdr[i]) && (flags & (LOAD_TEXT | COUNT_TEXT))) ||
		    (IS_DATA(phdr[i]) && (flags & (LOAD_DATA | COUNT_TEXT)))) {
			pos = phdr[i].p_paddr;
			if (minp > pos)
				minp = pos;
			pos += phdr[i].p_filesz;
			if (maxp < pos)
				maxp = pos;
		}

		/* Zero out BSS. */
		if (IS_BSS(phdr[i]) && (flags & LOAD_BSS)) {
			PROGRESS(("+%lu",
			    (u_long)(phdr[i].p_memsz - phdr[i].p_filesz)));
			BZERO((phdr[i].p_paddr + phdr[i].p_filesz),
			    phdr[i].p_memsz - phdr[i].p_filesz);
		}
		if (IS_BSS(phdr[i]) && (flags & (LOAD_BSS|COUNT_BSS))) {
			pos += phdr[i].p_memsz - phdr[i].p_filesz;
			if (maxp < pos)
				maxp = pos;
		}
	}
	FREE(phdr, sz);

	/*
	 * Copy the ELF and section headers.
	 */
	elfp = maxp = roundup(maxp, sizeof(Elf_Addr));
	if (flags & (LOAD_HDR | COUNT_HDR))
		maxp += sizeof(Elf_Ehdr);

	if (flags & (LOAD_SYM | COUNT_SYM)) {
		if (lseek(fd, (off_t)elf->e_shoff, SEEK_SET) == -1)  {
			WARN(("lseek section headers"));
			return 1;
		}
		sz = elf->e_shnum * sizeof(Elf_Shdr);
		shp = ALLOC(sz);

		if (read(fd, shp, sz) != sz) {
			WARN(("read section headers"));
			FREE(shp, sz);
			return 1;
		}

		shpp = maxp;
		maxp += roundup(sz, sizeof(Elf_Addr));

		size_t shstrsz = shp[elf->e_shstrndx].sh_size;
		char *shstr = ALLOC(shstrsz);
		if (lseek(fd, (off_t)shp[elf->e_shstrndx].sh_offset, SEEK_SET) == -1) {
			WARN(("lseek section header string table"));
			FREE(shstr, shstrsz);
			FREE(shp, sz);
			return 1;
		}
		if (read(fd, shstr, shstrsz) != shstrsz) {
			WARN(("read section header string table"));
			FREE(shstr, shstrsz);
			FREE(shp, sz);
			return 1;
		}

		/*
		 * Now load the symbol sections themselves. Make sure the
		 * sections are aligned. Don't bother with string tables if
		 * there are no symbol sections.
		 */
		off = roundup((sizeof(Elf_Ehdr) + sz), sizeof(Elf_Addr));

		for (havesyms = i = 0; i < elf->e_shnum; i++)
			if (shp[i].sh_type == SHT_SYMTAB)
				havesyms = 1;

		for (first = 1, i = 0; i < elf->e_shnum; i++) {
			if (shp[i].sh_type == SHT_SYMTAB ||
			    shp[i].sh_type == SHT_STRTAB ||
			    !strcmp(shstr + shp[i].sh_name, ".debug_line") ||
			    !strcmp(shstr + shp[i].sh_name, ELF_CTF)) {
				if (havesyms && (flags & LOAD_SYM)) {
					PROGRESS(("%s%ld", first ? " [" : "+",
					    (u_long)shp[i].sh_size));
					if (lseek(fd, (off_t)shp[i].sh_offset,
					    SEEK_SET) == -1) {
						WARN(("lseek symbols"));
						FREE(shstr, shstrsz);
						FREE(shp, sz);
						return 1;
					}
					if (READ(fd, maxp, shp[i].sh_size) !=
					    shp[i].sh_size) {
						WARN(("read symbols"));
						FREE(shstr, shstrsz);
						FREE(shp, sz);
						return 1;
					}
				}
				maxp += roundup(shp[i].sh_size,
				    sizeof(Elf_Addr));
				shp[i].sh_offset = off;
				shp[i].sh_flags |= SHF_ALLOC;
				off += roundup(shp[i].sh_size, sizeof(Elf_Addr));
				first = 0;
			}
		}
		if (flags & LOAD_SYM) {
			BCOPY(shp, shpp, sz);

			if (havesyms && first == 0)
				PROGRESS(("]"));
		}
		FREE(shstr, shstrsz);
		FREE(shp, sz);
	}

	/*
	 * Frob the copied ELF header to give information relative
	 * to elfp.
	 */
	if (flags & LOAD_HDR) {
		elf->e_phoff = 0;
		elf->e_shoff = sizeof(Elf_Ehdr);
		elf->e_phentsize = 0;
		elf->e_phnum = 0;
		BCOPY(elf, elfp, sizeof(*elf));
	}

	marks[MARK_START] = LOADADDR(minp);
	marks[MARK_ENTRY] = LOADADDR(elf->e_entry);
	marks[MARK_VENTRY] = elf->e_entry;
	marks[MARK_NSYM] = 1;	/* XXX: Kernel needs >= 0 */
	marks[MARK_SYM] = LOADADDR(elfp);
	marks[MARK_END] = LOADADDR(maxp);

	return 0;
}
