/*	$OpenBSD: rd.c,v 1.5 2020/12/09 18:10:19 krw Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include "libsa.h"
#include <machine/cpu.h>
#include <sys/exec_elf.h>

static	off_t rdoffs;

/*
 * INITRD I/O
 */

int
rd_iostrategy(void *f, int rw, daddr_t dblk, size_t size, void *buf,
    size_t *rsize)
{
	/* never invoked directly */
	return ENXIO;
}

int
rd_ioopen(struct open_file *f, ...)
{
	return 0;
}

int
rd_ioclose(struct open_file *f)
{
	return 0;
}

int
rd_isvalid()
{
	Elf64_Ehdr *elf64 = (Elf64_Ehdr *)INITRD_BASE;

	if (memcmp(elf64->e_ident, ELFMAG, SELFMAG) != 0 ||
	    elf64->e_ident[EI_CLASS] != ELFCLASS64 ||
	    elf64->e_ident[EI_DATA] != ELFDATA2LSB ||
	    elf64->e_type != ET_EXEC || elf64->e_machine != EM_MIPS)
		return 0;

	return 1;
}

void
rd_invalidate()
{
	bzero((void *)INITRD_BASE, sizeof(Elf64_Ehdr));
}

/*
 * INITRD filesystem
 */
int
rdfs_open(char *path, struct open_file *f)
{
	if (f->f_dev->dv_open == rd_ioopen) {
		if (strcmp(path, kernelfile) == 0) {
			rdoffs = 0;
			return 0;
		} else
			return ENOENT;
	}

	return EINVAL;
}

int
rdfs_close(struct open_file *f)
{
	return 0;
}

int
rdfs_read(struct open_file *f, void *buf, size_t size, size_t *resid)
{
	if (size != 0) {
		bcopy((void *)(INITRD_BASE + rdoffs), buf, size);
		rdoffs += size;
	}
	*resid = 0;

	return 0;
}

int
rdfs_write(struct open_file *f, void *buf, size_t size, size_t *resid)
{
	return EROFS;
}

off_t
rdfs_seek(struct open_file *f, off_t offset, int whence)
{
	switch (whence) {
	case 0:	/* SEEK_SET */
		rdoffs = offset;
		break;
	case 1: /* SEEK_CUR */
		rdoffs += offset;
		break;
	default:
		errno = EIO;
		return -1;
	}

	return rdoffs;
}

int
rdfs_stat(struct open_file *f, struct stat *sb)
{
	return EIO;
}

#ifndef NO_READDIR
int
rdfs_readdir(struct open_file *f, char *path)
{
	return EIO;
}
#endif
