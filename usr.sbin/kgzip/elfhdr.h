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

#include <sys/types.h>
#define	__ELF_WORD_SIZE	32
#include <sys/elf32.h>
#include <sys/elf_generic.h>
#include "kgz.h"

/* Section header indices */
#define KGZ_SH_SYMTAB		1
#define KGZ_SH_SHSTRTAB 	2
#define KGZ_SH_STRTAB		3
#define KGZ_SH_DATA		4
#define KGZ_SHNUM		5

/* Section header strings */
#define KGZ_SHSTR_ZERO		""
#define KGZ_SHSTR_SYMTAB	".symtab"
#define KGZ_SHSTR_SHSTRTAB	".shstrtab"
#define KGZ_SHSTR_STRTAB	".strtab"
#define KGZ_SHSTR_DATA		".data"

/* Section header string table */
struct kgz_shstrtab {
    char zero[sizeof(KGZ_SHSTR_ZERO)];
    char symtab[sizeof(KGZ_SHSTR_SYMTAB)];
    char shstrtab[sizeof(KGZ_SHSTR_SHSTRTAB)];
    char strtab[sizeof(KGZ_SHSTR_STRTAB)];
    char data[sizeof(KGZ_SHSTR_DATA)];
};

/* Symbol table indices */
#define KGZ_ST_KGZ		1
#define KGZ_ST_KGZ_NDATA	2
#define KGZ_STNUM		3

/* Symbol table strings */
#define KGZ_STR_ZERO		""
#define KGZ_STR_KGZ		"kgz"
#define KGZ_STR_KGZ_NDATA	"kgz_ndata"

/* String table */
struct kgz_strtab {
    char zero[sizeof(KGZ_STR_ZERO)];
    char kgz[sizeof(KGZ_STR_KGZ)];
    char kgz_ndata[sizeof(KGZ_STR_KGZ_NDATA)];
};

/* Relocatable header format */
struct kgz_elfhdr {
    Elf32_Ehdr e;
    Elf32_Shdr sh[KGZ_SHNUM];
    Elf32_Sym st[KGZ_STNUM];
    struct kgz_shstrtab shstrtab;
    struct kgz_strtab strtab;
};

extern const struct kgz_elfhdr elfhdr;
