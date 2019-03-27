/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Global Technology Associates, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/endian.h>
#include <stddef.h>
#include "elfhdr.h"

#define KGZ_FIX_NSIZE	0	/* Run-time fixup */

/*
 * Relocatable header template.
 */
const struct kgz_elfhdr elfhdr = {
    /* ELF header */
    {
	{
	    ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, 	/* e_ident */
	    ELFCLASS32, ELFDATA2LSB, EV_CURRENT, 0,
	    'F', 'r', 'e', 'e', 'B', 'S', 'D', 0
	},
	htole16(ET_EXEC),				/* e_type */
	htole16(EM_386),				/* e_machine */
	htole32(EV_CURRENT),				/* e_version */
	0,						/* e_entry */
	0,						/* e_phoff */
	htole32(offsetof(struct kgz_elfhdr, sh)),	/* e_shoff */
	0,						/* e_flags */
	htole16(sizeof(Elf32_Ehdr)),			/* e_ehsize */
	0,						/* e_phentsize */
	0,						/* e_phnum */
	htole16(sizeof(Elf32_Shdr)),			/* e_shentsize */
	htole16(KGZ_SHNUM),				/* e_shnum */
	htole16(KGZ_SH_SHSTRTAB)			/* e_shstrndx */
    },
    /* Section header */
    {
	{
	    0,						/* sh_name */
	    htole32(SHT_NULL),				/* sh_type */
	    0,						/* sh_flags */
	    0,						/* sh_addr */
	    0,						/* sh_offset */
	    0,						/* sh_size */
	    htole32(SHN_UNDEF),				/* sh_link */
	    0,						/* sh_info */
	    0,						/* sh_addralign */
	    0						/* sh_entsize */
	},
	{
	    htole32(offsetof(struct kgz_shstrtab, symtab)), /* sh_name */
	    htole32(SHT_SYMTAB),			/* sh_type */
	    0,						/* sh_flags */
	    0,						/* sh_addr */
	    htole32(offsetof(struct kgz_elfhdr, st)),	/* sh_offset */
	    htole32(sizeof(Elf32_Sym) * KGZ_STNUM),	/* sh_size */
	    htole32(KGZ_SH_STRTAB),			/* sh_link */
	    htole32(1),					/* sh_info */
	    htole32(4),					/* sh_addralign */
	    htole32(sizeof(Elf32_Sym))			/* sh_entsize */
	},
	{
	    htole32(offsetof(struct kgz_shstrtab, shstrtab)), /* sh_name */
	    htole32(SHT_STRTAB),			/* sh_type */
	    0,						/* sh_flags */
	    0,						/* sh_addr */
	    htole32(offsetof(struct kgz_elfhdr, shstrtab)), /* sh_offset */
	    htole32(sizeof(struct kgz_shstrtab)),	/* sh_size */
	    htole32(SHN_UNDEF),				/* sh_link */
	    0,						/* sh_info */
	    htole32(1),					/* sh_addralign */
	    0						/* sh_entsize */
	},
	{
	    htole32(offsetof(struct kgz_shstrtab, strtab)), /* sh_name */
	    htole32(SHT_STRTAB),			/* sh_type */
	    0,						/* sh_flags */
	    0,						/* sh_addr */
	    htole32(offsetof(struct kgz_elfhdr, strtab)), /* sh_offset */
	    htole32(sizeof(struct kgz_strtab)),		/* sh_size */
	    htole32(SHN_UNDEF),				/* sh_link */
	    0,						/* sh_info */
	    htole32(1),					/* sh_addralign */
	    0						/* sh_entsize */
	},
	{
	    htole32(offsetof(struct kgz_shstrtab, data)), /* sh_name */
	    htole32(SHT_PROGBITS),			/* sh_type */
	    htole32(SHF_ALLOC | SHF_WRITE),		/* sh_flags */
	    0,						/* sh_addr */
	    htole32(sizeof(struct kgz_elfhdr)),		/* sh_offset */
	    htole32(sizeof(struct kgz_hdr) + KGZ_FIX_NSIZE), /* sh_size */
	    htole32(SHN_UNDEF),				/* sh_link */
	    0,						/* sh_info */
	    htole32(4),					/* sh_addralign */
	    0						/* sh_entsize */
	}
    },
    /* Symbol table */
    {
	{
	    0,						/* st_name */
	    0,						/* st_value */
	    0,						/* st_size */
	    0,						/* st_info */
	    0,						/* st_other */
	    htole16(SHN_UNDEF)				/* st_shndx */
	},
	{
	    htole32(offsetof(struct kgz_strtab, kgz)),	/* st_name */
	    0,						/* st_value */
	    htole32(sizeof(struct kgz_hdr)),		/* st_size */
	    ELF32_ST_INFO(STB_GLOBAL, STT_OBJECT),	/* st_info */
	    0,						/* st_other */
	    htole16(KGZ_SH_DATA)			/* st_shndx */
	},
	{
	    htole32(offsetof(struct kgz_strtab, kgz_ndata)), /* st_name */
	    htole32(sizeof(struct kgz_hdr)),		/* st_value */
	    htole32(KGZ_FIX_NSIZE),			/* st_size */
	    ELF32_ST_INFO(STB_GLOBAL, STT_OBJECT),	/* st_info */
	    0,						/* st_other */
	    htole16(KGZ_SH_DATA)			/* st_shndx */
	}
    },
    /* Section header string table */
    {
	KGZ_SHSTR_ZERO, 				/* zero */
	KGZ_SHSTR_SYMTAB,				/* symtab */
	KGZ_SHSTR_SHSTRTAB,				/* shstrtab */
	KGZ_SHSTR_STRTAB,				/* strtab */
	KGZ_SHSTR_DATA					/* data */
    },
    /* String table */
    {
	KGZ_STR_ZERO,					/* zero */
	KGZ_STR_KGZ,					/* kgz */
	KGZ_STR_KGZ_NDATA				/* kgz_ndata */
    }
};
