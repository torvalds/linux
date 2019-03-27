/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
__RCSID("$NetBSD: exec_elf32.c,v 1.6 1999/09/20 04:12:16 christos Exp $");
#endif
#endif
__FBSDID("$FreeBSD$");

#ifndef ELFSIZE
#define ELFSIZE         32
#endif

#include <sys/types.h>
#include <sys/endian.h>
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#if (defined(NLIST_ELF32) && (ELFSIZE == 32)) || \
    (defined(NLIST_ELF64) && (ELFSIZE == 64))

#define	__ELF_WORD_SIZE ELFSIZE
#if (ELFSIZE == 32)
#include <sys/elf32.h>
#define	xewtoh(x)	((data == ELFDATA2MSB) ? be32toh(x) : le32toh(x))
#define	htoxew(x)	((data == ELFDATA2MSB) ? htobe32(x) : htole32(x))
#define	wewtoh(x)	((data == ELFDATA2MSB) ? be32toh(x) : le32toh(x))
#define	htowew(x)	((data == ELFDATA2MSB) ? htobe32(x) : htole32(x))
#elif (ELFSIZE == 64)
#include <sys/elf64.h>
#define	xewtoh(x)	((data == ELFDATA2MSB) ? be64toh(x) : le64toh(x))
#define	htoxew(x)	((data == ELFDATA2MSB) ? htobe64(x) : htole64(x))
/* elf64 Elf64_Word are 32 bits */
#define	wewtoh(x)	((data == ELFDATA2MSB) ? be32toh(x) : le32toh(x))
#define	htowew(x)	((data == ELFDATA2MSB) ? htobe32(x) : htole32(x))
#endif
#include <sys/elf_generic.h>

#define CONCAT(x,y)     __CONCAT(x,y)
#define ELFNAME(x)      CONCAT(elf,CONCAT(ELFSIZE,CONCAT(_,x)))
#define ELFNAME2(x,y)   CONCAT(x,CONCAT(_elf,CONCAT(ELFSIZE,CONCAT(_,y))))
#define ELFNAMEEND(x)   CONCAT(x,CONCAT(_elf,ELFSIZE))
#define ELFDEFNNAME(x)  CONCAT(ELF,CONCAT(ELFSIZE,CONCAT(_,x)))
#ifndef ELFCLASS
#define ELFCLASS	CONCAT(ELFCLASS,ELFSIZE)
#endif

#define	xe16toh(x)	((data == ELFDATA2MSB) ? be16toh(x) : le16toh(x))
#define	xe32toh(x)	((data == ELFDATA2MSB) ? be32toh(x) : le32toh(x))
#define	htoxe32(x)	((data == ELFDATA2MSB) ? htobe32(x) : htole32(x))

struct shlayout {
	Elf_Shdr *shdr;
	void *bufp;
};

static ssize_t
xreadatoff(int fd, void *buf, off_t off, size_t size, const char *fn)
{
	ssize_t rv;

	if (lseek(fd, off, SEEK_SET) != off) {
		perror(fn);
		return -1;
	}
	if ((size_t)(rv = read(fd, buf, size)) != size) {
		fprintf(stderr, "%s: read error: %s\n", fn,
		    rv == -1 ? strerror(errno) : "short read");
		return -1;
	}
	return size;
}

static ssize_t
xwriteatoff(int fd, void *buf, off_t off, size_t size, const char *fn)
{
	ssize_t rv;

	if (lseek(fd, off, SEEK_SET) != off) {
		perror(fn);
		return -1;
	}
	if ((size_t)(rv = write(fd, buf, size)) != size) {
		fprintf(stderr, "%s: write error: %s\n", fn,
		    rv == -1 ? strerror(errno) : "short write");
		return -1;
	}
	return size;
}

static void *
xmalloc(size_t size, const char *fn, const char *use)
{
	void *rv;

	rv = malloc(size);
	if (rv == NULL)
		fprintf(stderr, "%s: out of memory (allocating for %s)\n",
		    fn, use);
	return (rv);
}

static void *
xrealloc(void *ptr, size_t size, const char *fn, const char *use)
{
	void *rv;

	rv = realloc(ptr, size);
	if (rv == NULL) {
		free(ptr);
		fprintf(stderr, "%s: out of memory (reallocating for %s)\n",
		    fn, use);
	}
	return (rv);
}

int
ELFNAMEEND(check)(int fd, const char *fn)
{
	Elf_Ehdr eh;
	struct stat sb;
	unsigned char data;

	/*
	 * Check the header to maek sure it's an ELF file (of the
	 * appropriate size).
	 */
	if (fstat(fd, &sb) == -1)
		return 0;
	if (sb.st_size < (off_t)(sizeof eh))
		return 0;
	if (read(fd, &eh, sizeof eh) != sizeof eh)
		return 0;

	if (IS_ELF(eh) == 0 || eh.e_ident[EI_CLASS] != ELFCLASS)
                return 0;

	data = eh.e_ident[EI_DATA];

	switch (xe16toh(eh.e_machine)) {
	case EM_386: break;
	case EM_ALPHA: break;
#ifndef EM_AARCH64
#define	EM_AARCH64	183
#endif
	case EM_AARCH64: break;
	case EM_ARM: break;
	case EM_MIPS: break;
	case /* EM_MIPS_RS3_LE */ EM_MIPS_RS4_BE: break;
	case EM_PPC: break;
	case EM_PPC64: break;
#ifndef EM_RISCV
#define	EM_RISCV	243
#endif
	case EM_RISCV: break;
	case EM_S390: break;
	case EM_SPARCV9: break;
	case EM_X86_64: break;
/*        ELFDEFNNAME(MACHDEP_ID_CASES) */

        default:
                return 0;
        }

	return 1;
}

/*
 * This function 'hides' (some of) ELF executable file's symbols.
 * It hides them by renaming them to "_$$hide$$ <filename> <symbolname>".
 * Symbols in the global keep list, or which are marked as being undefined,
 * are left alone.
 *
 * An old version of this code shuffled various tables around, turning
 * global symbols to be hidden into local symbols.  That lost on the
 * mips, because CALL16 relocs must reference global symbols, and, if
 * those symbols were being hidden, they were no longer global.
 *
 * The new renaming behaviour doesn't take global symbols out of the
 * namespace.  However, it's ... unlikely that there will ever be
 * any collisions in practice because of the new method.
 */
int
ELFNAMEEND(hide)(int fd, const char *fn)
{
	Elf_Ehdr ehdr;
	struct shlayout *layoutp = NULL;
	Elf_Shdr *shdrp = NULL, *symtabshdr, *strtabshdr, *shstrtabshdr;
	Elf_Shdr shdrshdr;
	Elf_Sym *symtabp = NULL;
	char *shstrtabp = NULL, *strtabp = NULL;
	Elf_Size nsyms, ewi;
	Elf_Off off;
	ssize_t shdrsize;
	int rv, i, weird, l, m, r, strtabidx;
	size_t nstrtab_size, nstrtab_nextoff, fn_size, size;
	char *nstrtabp = NULL;
	unsigned char data;
	const char *weirdreason = NULL;
	void *buf;
	Elf_Half shnum;

	rv = 0;
	if (xreadatoff(fd, &ehdr, 0, sizeof ehdr, fn) != sizeof ehdr)
		goto bad;

	data = ehdr.e_ident[EI_DATA];
	shnum = xe16toh(ehdr.e_shnum);

	shdrsize = shnum * xe16toh(ehdr.e_shentsize);
	if ((shdrp = xmalloc(shdrsize, fn, "section header table")) == NULL)
		goto bad;
	if (xreadatoff(fd, shdrp, xewtoh(ehdr.e_shoff), shdrsize, fn) !=
	    shdrsize)
		goto bad;

	symtabshdr = strtabshdr = shstrtabshdr = NULL;
	weird = 0;
	for (i = 0; i < shnum; i++) {
		switch (xe32toh(shdrp[i].sh_type)) {
		case SHT_SYMTAB:
			if (symtabshdr != NULL) {
				weird = 1;
				weirdreason = "multiple symbol tables";
			}
			symtabshdr = &shdrp[i];
			strtabshdr = &shdrp[xe32toh(shdrp[i].sh_link)];
			break;
		case SHT_STRTAB:
			if (i == xe16toh(ehdr.e_shstrndx))
				shstrtabshdr = &shdrp[i];
			break;
		}
	}
	if (symtabshdr == NULL)
		goto out;
	if (strtabshdr == NULL) {
		weird = 1;
		weirdreason = "string table does not exist";
	}
	if (shstrtabshdr == NULL) {
		weird = 1;
		weirdreason = "section header string table does not exist";
	}
	if (strtabshdr == shstrtabshdr) {
		weird = 1;
		weirdreason = "combined strtab and shstrtab not supported";
	}
	if (weirdreason == NULL)
		weirdreason = "unsupported";
	if (weird) {
		fprintf(stderr, "%s: weird executable (%s)\n", fn, weirdreason);
		goto bad;
	}

	/*
	 * sort section layout table by offset
	 */
	layoutp = xmalloc((shnum + 1) * sizeof(struct shlayout),
	    fn, "layout table");
	if (layoutp == NULL)
		goto bad;

	/* add a pseudo entry to represent the section header table */
	shdrshdr.sh_offset = ehdr.e_shoff;
	shdrshdr.sh_size = htoxew(shdrsize);
	shdrshdr.sh_addralign = htoxew(ELFSIZE / 8);
	layoutp[shnum].shdr = &shdrshdr;

	/* insert and sort normal section headers */
	for (i = shnum; i-- != 0;) {
		l = i + 1;
		r = shnum;
		while (l <= r) {
			m = ( l + r) / 2;
			if (xewtoh(shdrp[i].sh_offset) >
			    xewtoh(layoutp[m].shdr->sh_offset))
				l = m + 1;
			else
				r = m - 1;
		}

		if (r != i) {
			memmove(&layoutp[i], &layoutp[i + 1],
			    sizeof(struct shlayout) * (r - i));
		}

		layoutp[r].shdr = &shdrp[i];
		layoutp[r].bufp = NULL;
	}
	++shnum;

	/*
	 * load up everything we need
	 */

	/* load section string table for debug use */
	if ((size = xewtoh(shstrtabshdr->sh_size)) == 0)
		goto bad;
	if ((shstrtabp = xmalloc(size, fn, "section string table")) == NULL)
		goto bad;
	if ((size_t)xreadatoff(fd, shstrtabp, xewtoh(shstrtabshdr->sh_offset),
	    size, fn) != size)
		goto bad;
	if (shstrtabp[size - 1] != '\0')
		goto bad;

	/* we need symtab, strtab, and everything behind strtab */
	strtabidx = INT_MAX;
	for (i = 0; i < shnum; i++) {
		if (layoutp[i].shdr == &shdrshdr) {
			/* not load section header again */
			layoutp[i].bufp = shdrp;
			continue;
		}
		if (layoutp[i].shdr == shstrtabshdr) {
			/* not load section string table again */
			layoutp[i].bufp = shstrtabp;
			continue;
		}

		if (layoutp[i].shdr == strtabshdr)
			strtabidx = i;
		if (layoutp[i].shdr == symtabshdr || i >= strtabidx) {
			off = xewtoh(layoutp[i].shdr->sh_offset);
			if ((size = xewtoh(layoutp[i].shdr->sh_size)) == 0)
				goto bad;
			layoutp[i].bufp = xmalloc(size, fn,
			    shstrtabp + xewtoh(layoutp[i].shdr->sh_name));
			if (layoutp[i].bufp == NULL)
				goto bad;
			if ((size_t)xreadatoff(fd, layoutp[i].bufp, off, size, fn) !=
			    size)
				goto bad;

			/* set symbol table and string table */
			if (layoutp[i].shdr == symtabshdr) {
				symtabp = layoutp[i].bufp;
			} else if (layoutp[i].shdr == strtabshdr) {
				strtabp = layoutp[i].bufp;
				if (strtabp[size - 1] != '\0')
					goto bad;
			}
		}
	}

	nstrtab_size = 256;
	nstrtabp = xmalloc(nstrtab_size, fn, "new string table");
	if (nstrtabp == NULL)
		goto bad;
	nstrtab_nextoff = 0;

	fn_size = strlen(fn);

	/* Prepare data structures for symbol movement. */
	nsyms = xewtoh(symtabshdr->sh_size) / xewtoh(symtabshdr->sh_entsize);

	/* move symbols, making them local */
	for (ewi = 0; ewi < nsyms; ewi++) {
		Elf_Sym *sp = &symtabp[ewi];
		const char *symname = strtabp + xe32toh(sp->st_name);
		size_t newent_len;
		/*
		 * make sure there's size for the next entry, even if it's
		 * as large as it can be.
		 *
		 * "_$$hide$$ <filename> <symname><NUL>" ->
		 *    9 + 3 + sizes of fn and sym name
		 */
		while ((nstrtab_size - nstrtab_nextoff) <
		    strlen(symname) + fn_size + 12) {
			nstrtab_size *= 2;
			nstrtabp = xrealloc(nstrtabp, nstrtab_size, fn,
			    "new string table");
			if (nstrtabp == NULL)
				goto bad;
		}

		sp->st_name = htowew(nstrtab_nextoff);

		/* if it's a keeper or is undefined, don't rename it. */
		if (in_keep_list(symname) ||
		    (xe16toh(sp->st_shndx) == SHN_UNDEF)) {
			newent_len = sprintf(nstrtabp + nstrtab_nextoff,
			    "%s", symname) + 1;
		} else {
			newent_len = sprintf(nstrtabp + nstrtab_nextoff,
			    "_$$hide$$ %s %s", fn, symname) + 1;
		}
		nstrtab_nextoff += newent_len;
	}
	strtabshdr->sh_size = htoxew(nstrtab_nextoff);

	/*
	 * update section header table in ascending order of offset
	 */
	for (i = strtabidx + 1; i < shnum; i++) {
		Elf_Off off, align;
		off = xewtoh(layoutp[i - 1].shdr->sh_offset) +
		    xewtoh(layoutp[i - 1].shdr->sh_size);
		align = xewtoh(layoutp[i].shdr->sh_addralign);
		off = (off + (align - 1)) & ~(align - 1);
		layoutp[i].shdr->sh_offset = htoxew(off);
	}

	/*
	 * write data to the file in descending order of offset
	 */
	for (i = shnum; i-- != 0;) {
		if (layoutp[i].shdr == strtabshdr) {
			/* new string table */
			buf = nstrtabp;
		} else
			buf = layoutp[i].bufp;

		if (layoutp[i].shdr == &shdrshdr ||
		    layoutp[i].shdr == symtabshdr || i >= strtabidx) {
			if (buf == NULL)
				goto bad;

			/*
			 * update the offset of section header table in elf
			 * header if needed.
			 */
			if (layoutp[i].shdr == &shdrshdr &&
			    ehdr.e_shoff != shdrshdr.sh_offset) {
				ehdr.e_shoff = shdrshdr.sh_offset;
				off = offsetof(Elf_Ehdr, e_shoff);
				size = sizeof(Elf_Off);
				if ((size_t)xwriteatoff(fd, &ehdr.e_shoff, off, size,
				    fn) != size)
					goto bad;
			}

			off = xewtoh(layoutp[i].shdr->sh_offset);
			size = xewtoh(layoutp[i].shdr->sh_size);
			if ((size_t)xwriteatoff(fd, buf, off, size, fn) != size)
				goto bad;
		}
	}

out:
	if (layoutp != NULL) {
		for (i = 0; i < shnum; i++) {
			if (layoutp[i].bufp != NULL)
				free(layoutp[i].bufp);
		}
		free(layoutp);
	}
	free(nstrtabp);
	return (rv);

bad:
	rv = 1;
	goto out;
}

#endif /* include this size of ELF */
