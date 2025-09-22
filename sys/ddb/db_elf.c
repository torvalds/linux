/*	$OpenBSD: db_elf.c,v 1.34 2024/11/07 16:02:29 miod Exp $	*/
/*	$NetBSD: db_elf.c,v 1.13 2000/07/07 21:55:18 jhawk Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

#include <sys/param.h>
#include <sys/stdint.h>
#include <sys/systm.h>
#include <sys/exec.h>

#include <machine/db_machdep.h>

#include <ddb/db_elf.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>

#include <sys/exec_elf.h>

db_symtab_t db_symtab;

Elf_Sym		*db_elf_sym_lookup(const char *);

/*
 * Find the symbol table and strings; tell ddb about them.
 *
 * symsize:	size of symbol table
 * symtab:	pointer to start of symbol table
 * esymtab:	pointer to end of string table, for checking - rounded up to
 *		    integer boundary
 */
int
db_elf_sym_init(int symsize, void *symtab, void *esymtab, const char *name)
{
	Elf_Ehdr *elf;
	Elf_Shdr *shp;
	Elf_Sym *symp, *symtab_start, *symtab_end;
	char *shstrtab, *strtab_start, *strtab_end;
	int i;
	char *errstr = "";

	if (ALIGNED_POINTER(symtab, long) == 0) {
		db_printf("[ %s symbol table has bad start address %p ]\n",
		    name, symtab);
		return (0);
	}

	symtab_start = symtab_end = NULL;
	strtab_start = strtab_end = NULL;

	/*
	 * The format of the symbols loaded by the boot program is:
	 *
	 *	Elf exec header
	 *	first section header
	 *	. . .
	 *	. . .
	 *	last section header
	 *	first symbol, string, or line table section
	 *	. . .
	 *	. . .
	 *	last symbol, string, or line table section
	 */

	/*
	 * Validate the Elf header.
	 */
	elf = (Elf_Ehdr *)symtab;
	if (memcmp(elf->e_ident, ELFMAG, SELFMAG) != 0 ||
	    elf->e_ident[EI_CLASS] != ELFCLASS) {
		errstr = "bad magic";
		goto badheader;
	}

	if (elf->e_machine != ELF_TARG_MACH) {
		errstr = "bad e_machine";
		goto badheader;
	}

	/*
	 * Find the section header string table (.shstrtab), and look up
	 * the symbol table (.symtab) and string table (.strtab) via their
	 * names in shstrtab, rather than by table type.
	 * This works in the presence of multiple string tables, such as
	 * stabs data found when booting bsd.gdb.
	 */
	shp = (Elf_Shdr *)((char *)symtab + elf->e_shoff);
	shstrtab = (char *)symtab + shp[elf->e_shstrndx].sh_offset;
	for (i = 0; i < elf->e_shnum; i++) {
		if (shp[i].sh_type == SHT_SYMTAB) {
			int j;

			if (shp[i].sh_offset == 0)
				continue;
			symtab_start = (Elf_Sym *)((char *)symtab +
			    shp[i].sh_offset);
			symtab_end = (Elf_Sym *)((char *)symtab +
			    shp[i].sh_offset + shp[i].sh_size);
			j = shp[i].sh_link;
			if (shp[j].sh_offset == 0)
				continue;
			strtab_start = (char *)symtab + shp[j].sh_offset;
			strtab_end = (char *)symtab + shp[j].sh_offset +
			    shp[j].sh_size;
			break;
		}

		/*
		 * This is the old way of doing things.
		 * XXX - verify that it's not needed.
		 */
		if (strcmp(".strtab", shstrtab+shp[i].sh_name) == 0) {
			strtab_start = (char *)symtab + shp[i].sh_offset;
			strtab_end = (char *)symtab + shp[i].sh_offset +
			    shp[i].sh_size;
		} else if (strcmp(".symtab", shstrtab+shp[i].sh_name) == 0) {
			symtab_start = (Elf_Sym *)((char *)symtab +
			    shp[i].sh_offset);
			symtab_end = (Elf_Sym *)((char *)symtab +
			    shp[i].sh_offset + shp[i].sh_size);
		}
	}

	/*
	 * Now, sanity check the symbols against the string table.
	 */
	if (symtab_start == NULL || strtab_start == NULL ||
	    ALIGNED_POINTER(symtab_start, long) == 0) {
		errstr = "symtab unaligned";
		goto badheader;
	}
	for (symp = symtab_start; symp < symtab_end; symp++)
		if (symp->st_name + strtab_start > strtab_end) {
			errstr = "symtab corrupted";
			goto badheader;
		}

	/*
	 * Link the symbol table into the debugger.
	 */
	db_symtab.start = (char *)symtab_start;
	db_symtab.end = (char *)symtab_end;
	db_symtab.name = name;
	db_symtab.private = (char *)symtab;

	db_printf("[ using %lu bytes of %s ELF symbol table ]\n",
	    (u_long)roundup(((char *)esymtab - (char *)symtab), sizeof(u_long)),
	    name);

	return (1);

 badheader:
	db_printf("[ %s ELF symbol table not valid: %s ]\n", name, errstr);
	return (0);
}

/*
 * Internal helper function - return a pointer to the string table
 * for the current symbol table.
 */
const char *
db_elf_find_strtab(db_symtab_t *stab)
{
	Elf_Ehdr *elf = STAB_TO_EHDR(stab);
	Elf_Shdr *shp = STAB_TO_SHDR(stab, elf);
	const char *shstrtab;
	int i;

	shstrtab = (const char *)elf + shp[elf->e_shstrndx].sh_offset;
	for (i = 0; i < elf->e_shnum; i++) {
		if (shp[i].sh_type == SHT_SYMTAB)
			return ((const char *)elf +
			    shp[shp[i].sh_link].sh_offset);
		if (strcmp(".strtab", shstrtab+shp[i].sh_name) == 0)
			return ((const char *)elf + shp[i].sh_offset);
	}

	return (NULL);
}

/*
 * Internal helper function - return a pointer to the section
 * named ``sname''.
 */
const char *
db_elf_find_section(db_symtab_t *stab, size_t *size, const char *sname)
{
	Elf_Ehdr *elf = STAB_TO_EHDR(stab);
	Elf_Shdr *shp = STAB_TO_SHDR(stab, elf);
	char *shstrtab;
	int i;

	shstrtab = (char *)elf + shp[elf->e_shstrndx].sh_offset;
	for (i = 0; i < elf->e_shnum; i++) {
		if ((shp[i].sh_flags & SHF_ALLOC) != 0 &&
		    strcmp(sname, shstrtab+shp[i].sh_name) == 0) {
			*size = shp[i].sh_size;
			return ((char *)elf + shp[i].sh_offset);
		}
	}

	return (NULL);
}

/*
 * Lookup the symbol with the given name.
 */
Elf_Sym *
db_elf_sym_lookup(const char *symstr)
{
	db_symtab_t *stab = &db_symtab;
	Elf_Sym *symp, *symtab_start, *symtab_end;
	const char *strtab;

	if (stab->private == NULL)
		return (NULL);

	symtab_start = STAB_TO_SYMSTART(stab);
	symtab_end = STAB_TO_SYMEND(stab);

	strtab = db_elf_find_strtab(stab);
	if (strtab == NULL)
		return (NULL);

	for (symp = symtab_start; symp < symtab_end; symp++) {
		if (symp->st_name != 0 &&
		    db_eqname(strtab + symp->st_name, symstr, 0))
			return (symp);
	}

	return (NULL);
}

/*
 * Search for the symbol with the given address (matching within the
 * provided threshold).
 */
Elf_Sym *
db_elf_sym_search(vaddr_t off, db_strategy_t strategy, db_expr_t *diffp)
{
	db_symtab_t *stab = &db_symtab;
	Elf_Sym *rsymp, *symp, *symtab_start, *symtab_end;
	db_expr_t diff = *diffp;

	if (stab->private == NULL)
		return (NULL);

	symtab_start = STAB_TO_SYMSTART(stab);
	symtab_end = STAB_TO_SYMEND(stab);

	rsymp = NULL;

	for (symp = symtab_start; symp < symtab_end; symp++) {
		if (symp->st_name == 0)
			continue;
#if 0
		/* This prevents me from seeing anything in locore.s -- eeh */
		if (ELF_SYM_TYPE(symp->st_info) != Elf_estt_object &&
		    ELF_SYM_TYPE(symp->st_info) != Elf_estt_func)
			continue;
#endif

		if (off >= symp->st_value) {
			if ((off - symp->st_value) < diff) {
				diff = off - symp->st_value;
				rsymp = symp;
				if (diff == 0) {
					if (strategy == DB_STGY_PROC &&
					    ELF_ST_TYPE(symp->st_info)
					      == STT_FUNC &&
					    ELF_ST_BIND(symp->st_info)
					      != STB_LOCAL)
						break;
					if (strategy == DB_STGY_ANY &&
					    ELF_ST_BIND(symp->st_info)
					      != STB_LOCAL)
						break;
				}
			} else if ((off - symp->st_value) == diff) {
				if (rsymp == NULL)
					rsymp = symp;
				else if (ELF_ST_BIND(rsymp->st_info)
				      == STB_LOCAL &&
				    ELF_ST_BIND(symp->st_info)
				      != STB_LOCAL) {
					/* pick the external symbol */
					rsymp = symp;
				}
			}
		}
	}

	if (rsymp == NULL)
		*diffp = off;
	else
		*diffp = diff;

	return (rsymp);
}

/*
 * Return the name and value for a symbol.
 */
void
db_symbol_values(Elf_Sym *sym, const char **namep, db_expr_t *valuep)
{
	db_symtab_t *stab = &db_symtab;
	Elf_Sym *symp = (Elf_Sym *)sym;
	const char *strtab;

	if (sym == NULL) {
		*namep = NULL;
		return;
	}

	if (stab->private == NULL)
		return;

	if (namep) {
		strtab = db_elf_find_strtab(stab);
		if (strtab == NULL)
			*namep = NULL;
		else
			*namep = strtab + symp->st_name;
	}

	if (valuep)
		*valuep = symp->st_value;
}

/*
 * Return the file and line number of the current program counter
 * if we can find the appropriate debugging symbol.
 */
int
db_elf_line_at_pc(Elf_Sym *cursym, const char **filename,
    int *linenum, db_expr_t off)
{
	db_symtab_t *stab = &db_symtab;
	static char path[PATH_MAX];
	const char *linetab, *dirname, *basename;
	size_t linetab_size;

	if (stab->private == NULL)
		return (0);

	linetab = db_elf_find_section(stab, &linetab_size, ".debug_line");
	if (linetab == NULL)
		return (0);

	if (!db_dwarf_line_at_pc(linetab, linetab_size, off,
	    &dirname, &basename, linenum))
		return (0);

	if (dirname == NULL)
		strlcpy(path, basename, sizeof(path));
	else
		snprintf(path, sizeof(path), "%s/%s", dirname, basename);
	*filename = path;
	return (1);
}

void
db_elf_sym_forall(db_forall_func_t db_forall_func, void *arg)
{
	db_symtab_t *stab = &db_symtab;
	const char *strtab;
	static char suffix[2];
	Elf_Sym *symp, *symtab_start, *symtab_end;

	if (stab->private == NULL)
		return;

	symtab_start = STAB_TO_SYMSTART(stab);
	symtab_end = STAB_TO_SYMEND(stab);

	strtab = db_elf_find_strtab(stab);
	if (strtab == NULL)
		return;

	for (symp = symtab_start; symp < symtab_end; symp++)
		if (symp->st_name != 0) {
			suffix[1] = '\0';
			switch (ELF_ST_TYPE(symp->st_info)) {
			case STT_OBJECT:
				suffix[0] = '+';
				break;
			case STT_FUNC:
				suffix[0] = '*';
				break;
			case STT_SECTION:
				suffix[0] = '&';
				break;
			case STT_FILE:
				suffix[0] = '/';
				break;
			default:
				suffix[0] = '\0';
			}
			(*db_forall_func)(symp,
			    strtab + symp->st_name, suffix, arg);
		}
}

Elf_Sym *
db_symbol_by_name(const char *name, db_expr_t *valuep)
{
	Elf_Sym		*sym;

	sym = db_elf_sym_lookup(name);
	if (sym == NULL)
		return (NULL);
	db_symbol_values(sym, &name, valuep);
	return (sym);
}
