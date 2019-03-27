/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *
 * $FreeBSD$
 */

/*
 * Support for printing debugging messages.
 */

#include <stdarg.h>
#include <stdio.h>

#include "debug.h"
#include "rtld.h"
#include "rtld_printf.h"

static const char rel_header[] =
    " symbol name               r_info r_offset st_value st_size    address    value\n"
    " ------------------------------------------------------------------------------\n";
static const char rel_format[] =  " %-25s %6lx %08lx %08lx %7d %10p %08lx\n";

int debug = 0;

void
debug_printf(const char *format, ...)
{
    if (debug) {
	va_list ap;
	va_start(ap, format);

	rtld_vfdprintf(STDERR_FILENO, format, ap);
	rtld_fdputchar(STDERR_FILENO, '\n');

	va_end(ap);
    }
}

void
dump_relocations (Obj_Entry *obj0)
{
    Obj_Entry *obj;

    for (obj = globallist_curr(obj0); obj != NULL;
      obj = globallist_next(obj)) {
        dump_obj_relocations(obj);
    }
}

void
dump_obj_relocations (Obj_Entry *obj)
{

    rtld_printf("Object \"%s\", relocbase %p\n", obj->path, obj->relocbase);

    if (obj->relsize) {
        rtld_printf("Non-PLT Relocations: %ld\n",
            (obj->relsize / sizeof(Elf_Rel)));
        dump_Elf_Rel(obj, obj->rel, obj->relsize);
    }

    if (obj->relasize) {
        rtld_printf("Non-PLT Relocations with Addend: %ld\n",
            (obj->relasize / sizeof(Elf_Rela)));
        dump_Elf_Rela(obj, obj->rela, obj->relasize);
    }

    if (obj->pltrelsize) {
        rtld_printf("PLT Relocations: %ld\n",
            (obj->pltrelsize / sizeof(Elf_Rel)));
        dump_Elf_Rel(obj, obj->pltrel, obj->pltrelsize);
    }

    if (obj->pltrelasize) {
        rtld_printf("PLT Relocations with Addend: %ld\n",
            (obj->pltrelasize / sizeof(Elf_Rela)));
        dump_Elf_Rela(obj, obj->pltrela, obj->pltrelasize);
    }
}

void
dump_Elf_Rel (Obj_Entry *obj, const Elf_Rel *rel0, u_long relsize)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    const Elf_Sym *sym;
    Elf_Addr *dstaddr;

    rtld_putstr(rel_header);
    rellim = (const Elf_Rel *)((const char *)rel0 + relsize);
    for (rel = rel0; rel < rellim; rel++) {
	dstaddr = (Elf_Addr *)(obj->relocbase + rel->r_offset);
        sym = obj->symtab + ELF_R_SYM(rel->r_info);
        rtld_printf(rel_format,
		obj->strtab + sym->st_name,
		(u_long)rel->r_info, (u_long)rel->r_offset,
		(u_long)sym->st_value, (int)sym->st_size,
		dstaddr, (u_long)*dstaddr);
    }
    return;
}

void
dump_Elf_Rela (Obj_Entry *obj, const Elf_Rela *rela0, u_long relasize)
{
    const Elf_Rela *rela;
    const Elf_Rela *relalim;
    const Elf_Sym *sym;
    Elf_Addr *dstaddr;

    rtld_putstr(rel_header);
    relalim = (const Elf_Rela *)((const char *)rela0 + relasize);
    for (rela = rela0; rela < relalim; rela++) {
	dstaddr = (Elf_Addr *)(obj->relocbase + rela->r_offset);
        sym = obj->symtab + ELF_R_SYM(rela->r_info);
        rtld_printf(rel_format,
		obj->strtab + sym->st_name,
		(u_long)rela->r_info, (u_long)rela->r_offset,
		(u_long)sym->st_value, (int)sym->st_size,
		dstaddr, (u_long)*dstaddr);
    }
    return;
}
