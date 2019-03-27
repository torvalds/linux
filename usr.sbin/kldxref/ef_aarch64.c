/*-
 * Copyright (c) 2005 Peter Grehan.
 * Copyright 1996-1998 John D. Polstra.
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

#include <sys/types.h>
#include <machine/elf.h>

#include <err.h>
#include <errno.h>
#include <string.h>

#include "ef.h"

/*
 * Apply relocations to the values obtained from the file. `relbase' is the
 * target relocation address of the section, and `dataoff/len' is the region
 * that is to be relocated, and has been copied to *dest
 */
int
ef_reloc(struct elf_file *ef, const void *reldata, int reltype, Elf_Off relbase,
    Elf_Off dataoff, size_t len, void *dest)
{
	Elf_Addr *where, addend;
	Elf_Size rtype;
	const Elf_Rela *rela;

	if (reltype != EF_RELOC_RELA)
		return (EINVAL);

	rela = (const Elf_Rela *)reldata;
	where = (Elf_Addr *) ((Elf_Off)dest - dataoff + rela->r_offset);
	addend = rela->r_addend;
	rtype = ELF_R_TYPE(rela->r_info);

	if ((char *)where < (char *)dest || (char *)where >= (char *)dest + len)
		return (0);

	switch(rtype) {
	case R_AARCH64_RELATIVE:
		*where = relbase + addend;
		break;
	case R_AARCH64_ABS64:
		break;
	default:
		warnx("unhandled relocation type %lu", rtype);
		break;
	}
	return (0);
}
