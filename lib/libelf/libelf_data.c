/*-
 * Copyright (c) 2006,2008 Joseph Koshy
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

#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_data.c,v 1.2 2020/05/18 06:46:23 jsg Exp $");

int
_libelf_xlate_shtype(uint32_t sht)
{
	/*
	 * Look for known section types.
	 */
	switch (sht) {
	case SHT_DYNAMIC:
		return (ELF_T_DYN);
	case SHT_DYNSYM:
		return (ELF_T_SYM);
	case SHT_FINI_ARRAY:
		return (ELF_T_ADDR);
	case SHT_GNU_HASH:
		return (ELF_T_GNUHASH);
	case SHT_GNU_LIBLIST:
		return (ELF_T_WORD);
	case SHT_GROUP:
		return (ELF_T_WORD);
	case SHT_HASH:
		return (ELF_T_WORD);
	case SHT_INIT_ARRAY:
		return (ELF_T_ADDR);
	case SHT_NOBITS:
		return (ELF_T_BYTE);
	case SHT_NOTE:
		return (ELF_T_NOTE);
	case SHT_PREINIT_ARRAY:
		return (ELF_T_ADDR);
	case SHT_PROGBITS:
		return (ELF_T_BYTE);
	case SHT_REL:
		return (ELF_T_REL);
	case SHT_RELA:
		return (ELF_T_RELA);
	case SHT_STRTAB:
		return (ELF_T_BYTE);
	case SHT_SYMTAB:
		return (ELF_T_SYM);
	case SHT_SYMTAB_SHNDX:
		return (ELF_T_WORD);
	case SHT_SUNW_dof:
		return (ELF_T_BYTE);
	case SHT_SUNW_move:
		return (ELF_T_MOVE);
	case SHT_SUNW_syminfo:
		return (ELF_T_SYMINFO);
	case SHT_SUNW_verdef:	/* == SHT_GNU_verdef */
		return (ELF_T_VDEF);
	case SHT_SUNW_verneed:	/* == SHT_GNU_verneed */
		return (ELF_T_VNEED);
	case SHT_SUNW_versym:	/* == SHT_GNU_versym */
		return (ELF_T_HALF);
	default:
		/*
		 * Values in the range [SHT_LOOS..SHT_HIUSER] (i.e.,
		 * OS, processor and user-defined section types) are
		 * legal, but since we do not know anything more about
		 * their semantics, we return a type of ELF_T_BYTE.
		 *
		 * The ELF specification uses 32 bit unsigned values for
		 * denoting section types, and defines SHT_HIUSER to be
		 * 0xFFFFFFFFUL (i.e., UINT32_MAX). Consequently, we only
		 * need to check that 'sht' is greater than or equal to
		 * SHT_LOOS.
		 */
		if (sht >= SHT_LOOS)
			return (ELF_T_BYTE);

		/*
		 * Other values are unsupported.
		 */
		return (-1);
	}
}
