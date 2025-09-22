/* $NetBSD: loadfile.c,v 1.10 2000/12/03 02:53:04 tsutsui Exp $ */
/* $OpenBSD: loadfile.c,v 1.21 2021/10/24 17:49:19 deraadt Exp $ */

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

#ifdef _STANDALONE
#include <lib/libkern/libkern.h>
#include <lib/libsa/stand.h>
#else
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#endif

#include <sys/param.h>
#include <sys/exec.h>

#include "loadfile.h"

#ifdef BOOT_ELF
#include <sys/exec_elf.h>
#if defined(BOOT_ELF32) && defined(BOOT_ELF64)
/*
 * Both defined, so elf32_exec() and elf64_exec() need to be separately
 * created (can't do it by including loadfile_elf.c here).
 */
int elf32_exec(int, Elf32_Ehdr *, uint64_t *, int);
int elf64_exec(int, Elf64_Ehdr *, uint64_t *, int);
#else
#include "loadfile_elf.c"
#endif
#endif

/*
 * Open 'filename', read in program and return -1 on error otherwise fd,
 * with file still open.
 * Also fills in marks.
 */
int
loadfile(const char *fname, uint64_t *marks, int flags)
{
	union {
#if defined(BOOT_ELF32) || (defined(BOOT_ELF) && ELFSIZE == 32)
		Elf32_Ehdr elf32;
#endif
#if defined(BOOT_ELF64) || (defined(BOOT_ELF) && ELFSIZE == 64)
		Elf64_Ehdr elf64;
#endif

	} hdr;
	ssize_t nr;
	int fd, rval;

	/* Open the file. */
	if ((fd = open(fname, O_RDONLY)) < 0) {
		WARN(("open %s", fname ? fname : "<default>"));
		return -1;
	}

	/* Read the exec header. */
	if ((nr = read(fd, &hdr, sizeof(hdr))) != sizeof(hdr)) {
		WARN(("read header"));
		goto err;
	}

#if defined(BOOT_ELF32) || (defined(BOOT_ELF) && ELFSIZE == 32)
	if (memcmp(hdr.elf32.e_ident, ELFMAG, SELFMAG) == 0 &&
	    hdr.elf32.e_ident[EI_CLASS] == ELFCLASS32) {
		rval = elf32_exec(fd, &hdr.elf32, marks, flags);
	} else
#endif
#if defined(BOOT_ELF64) || (defined(BOOT_ELF) && ELFSIZE == 64)
	if (memcmp(hdr.elf64.e_ident, ELFMAG, SELFMAG) == 0 &&
	    hdr.elf64.e_ident[EI_CLASS] == ELFCLASS64) {
		rval = elf64_exec(fd, &hdr.elf64, marks, flags);
	} else
#endif
	{
		rval = 1;
		errno = EFTYPE;
		WARN(("%s", fname ? fname : "<default>"));
	}

	if (rval == 0) {
		PROGRESS(("=0x%lx\n", marks[MARK_END] - marks[MARK_START]));
		return fd;
	}
err:
	(void)close(fd);
	return -1;
}
