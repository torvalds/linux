/*	$OpenBSD: nlist.c,v 1.53 2019/06/28 13:32:48 deraadt Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

#include <db.h>
#include <elf.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

typedef struct nlist NLIST;
#define	_strx	n_un.n_strx
#define	_name	n_un.n_name

static char *kfile;
static char *fmterr;

int	__elf_knlist(int fd, DB *db, int ksyms);

int
__elf_knlist(int fd, DB *db, int ksyms)
{
	caddr_t strtab = NULL;
	off_t symstroff, symoff;
	u_long symsize, symstrsize;
	u_long kernvma, kernoffs;
	int i, error = 0;
	Elf32_Word j;
	Elf_Sym sbuf;
	char buf[1024];
	Elf_Ehdr eh;
	Elf_Shdr *sh = NULL;
	DBT data, key;
	NLIST nbuf;
	FILE *fp;
	int usemalloc = 0;

	if ((fp = fdopen(fd, "r")) == NULL)
		err(1, "%s", kfile);

	if (fseek(fp, (off_t)0, SEEK_SET) == -1 ||
	    fread(&eh, sizeof(eh), 1, fp) != 1 ||
	    !IS_ELF(eh)) {
		fclose(fp);
		return (1);
	}

	sh = calloc(sizeof(Elf_Shdr), eh.e_shnum);
	if (sh == NULL)
		errx(1, "cannot allocate %zu bytes for symbol header",
		    sizeof(Elf_Shdr) * eh.e_shnum);

	if (fseek(fp, eh.e_shoff, SEEK_SET) == -1) {
		fmterr = "no exec header";
		error = -1;
		goto done;
	}

	if (fread(sh, sizeof(Elf_Shdr) * eh.e_shnum, 1, fp) != 1) {
		fmterr = "no exec header";
		error = -1;
		goto done;
	}

	symstrsize = symsize = 0;
	kernvma = (u_long)-1;	/* 0 is a valid value (at least on hp300) */
	for (i = 0; i < eh.e_shnum; i++) {
		if (sh[i].sh_type == SHT_STRTAB) {
			for (j = 0; j < eh.e_shnum; j++)
				if (sh[j].sh_type == SHT_SYMTAB &&
				    sh[j].sh_link == (unsigned)i) {
					symstroff = sh[i].sh_offset;
					symstrsize = sh[i].sh_size;
			}
		} else if (sh[i].sh_type == SHT_SYMTAB) {
			symoff = sh[i].sh_offset;
			symsize = sh[i].sh_size;
		} else if (sh[i].sh_type == SHT_PROGBITS &&
		    (sh[i].sh_flags & SHF_EXECINSTR)) {
			kernvma = sh[i].sh_addr;
			kernoffs = sh[i].sh_offset;
		}
	}

	if (symstrsize == 0 || symsize == 0 || kernvma == (u_long)-1) {
		fmterr = "corrupt file";
		error = -1;
		goto done;
	}

	/*
	 * Map string table into our address space.  This gives us
	 * an easy way to randomly access all the strings, without
	 * making the memory allocation permanent as with malloc/free
	 * (i.e., munmap will return it to the system).
	 *
	 * XXX - we really want to check if this is a regular file.
	 *	 then we probably want a MAP_PRIVATE here.
	 */
	strtab = mmap(NULL, (size_t)symstrsize, PROT_READ,
	    MAP_SHARED|MAP_FILE, fileno(fp), symstroff);
	if (strtab == MAP_FAILED) {
		usemalloc = 1;
		if ((strtab = malloc(symstrsize)) == NULL) {
			fmterr = "out of memory";
			error = -1;
			goto done;
		}
		if (fseek(fp, symstroff, SEEK_SET) == -1) {
			fmterr = "corrupt file";
			error = -1;
			goto done;
		}
		if (fread(strtab, symstrsize, 1, fp) != 1) {
			fmterr = "corrupt file";
			error = -1;
			goto done;
		}
	}

	if (fseek(fp, symoff, SEEK_SET) == -1) {
		fmterr = "corrupt file";
		error = -1;
		goto done;
	}

	data.data = (u_char *)&nbuf;
	data.size = sizeof(NLIST);

	/* Read each symbol and enter it into the database. */
	while (symsize > 0) {
		symsize -= sizeof(Elf_Sym);
		if (fread((char *)&sbuf, sizeof(sbuf), 1, fp) != 1) {
			if (feof(fp))
				fmterr = "corrupted symbol table";
			else
				warn("%s", kfile);
			error = -1;
			goto done;
		}
		if (!sbuf.st_name)
			continue;

		nbuf.n_value = sbuf.st_value;

		/* XXX type conversion is pretty rude... */
		switch(ELF_ST_TYPE(sbuf.st_info)) {
		case STT_NOTYPE:
			switch (sbuf.st_shndx) {
			case SHN_UNDEF:
				nbuf.n_type = N_UNDF;
				break;
			case SHN_ABS:
				nbuf.n_type = N_ABS;
				break;
			case SHN_COMMON:
				nbuf.n_type = N_COMM;
				break;
			default:
				nbuf.n_type = N_COMM | N_EXT;
				break;
			}
			break;
		case STT_FUNC:
			nbuf.n_type = N_TEXT;
			break;
		case STT_OBJECT:
			nbuf.n_type = N_DATA;
			break;
		case STT_FILE:
			nbuf.n_type = N_FN;
			break;
		}
		if (ELF_ST_BIND(sbuf.st_info) == STB_LOCAL)
			nbuf.n_type = N_EXT;

		*buf = '_';
		strlcpy(buf + 1, strtab + sbuf.st_name, sizeof buf - 1);
		key.data = (u_char *)buf;
		key.size = strlen((char *)key.data);
		if (db->put(db, &key, &data, 0))
			err(1, "record enter");

		if (strcmp((char *)key.data, VRS_SYM) == 0) {
			long cur_off;
			if (!ksyms) {
				/*
				 * Calculate offset to the version string in
				 * the file. kernvma is where the kernel is
				 * really loaded; kernoffs is where in the
				 * file it starts.
				 */
				long voff;
				voff = nbuf.n_value - kernvma + kernoffs;
				cur_off = ftell(fp);
				if (fseek(fp, voff, SEEK_SET) == -1) {
					fmterr = "corrupted string table";
					error = -1;
					goto done;
				}

				/*
				 * Read version string up to, and including
				 * newline. This code assumes that a newline
				 * terminates the version line.
				 */
				if (fgets(buf, sizeof(buf), fp) == NULL) {
					fmterr = "corrupted string table";
					error = -1;
					goto done;
				}
			} else {
				/*
				 * This is /dev/ksyms or a look alike.
				 * Use sysctl() to get version since we
				 * don't have real text or data.
				 */
				int mib[2];
				size_t len;
				char *p;

				mib[0] = CTL_KERN;
				mib[1] = KERN_VERSION;
				len = sizeof(buf);
				if (sysctl(mib, 2, buf, &len, NULL, 0) == -1) {
					err(1, "sysctl can't find kernel "
					    "version string");
				}
				if ((p = strchr(buf, '\n')) != NULL)
					*(p+1) = '\0';
			}

			key.data = (u_char *)VRS_KEY;
			key.size = sizeof(VRS_KEY) - 1;
			data.data = (u_char *)buf;
			data.size = strlen(buf);
			if (db->put(db, &key, &data, 0))
				err(1, "record enter");

			/* Restore to original values. */
			data.data = (u_char *)&nbuf;
			data.size = sizeof(NLIST);
			if (!ksyms && fseek(fp, cur_off, SEEK_SET) == -1) {
				fmterr = "corrupted string table";
				error = -1;
				goto done;
			}
		}
	}
done:
	if (strtab) {
		if (usemalloc)
			free(strtab);
		else
			munmap(strtab, symstrsize);
	}
	(void)fclose(fp);
	free(sh);
	return (error);
}

int
create_knlist(char *name, int fd, DB *db)
{
	int error, ksyms;

	if (strcmp(name, _PATH_KSYMS) == 0) {
		ksyms = 1;
	} else {
		ksyms = 0;
	}

	fmterr = NULL;
	kfile = name;
	/* rval of 1 means wrong executable type */
	error = __elf_knlist(fd, db, ksyms);

	if (fmterr != NULL)
		warnc(EFTYPE, "%s: %s", kfile, fmterr);

	return(error);
}
