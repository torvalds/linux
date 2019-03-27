/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */
/*
 * 	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/pcpu.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <net/vnet.h>

#include <ddb/ddb.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>

#include "opt_ddb.h"

/*
 * Multiple symbol tables
 */
#ifndef MAXNOSYMTABS
#define	MAXNOSYMTABS	3	/* mach, ux, emulator */
#endif

static db_symtab_t	db_symtabs[MAXNOSYMTABS] = {{0,},};
static int db_nsymtab = 0;

static db_symtab_t	*db_last_symtab; /* where last symbol was found */

static c_db_sym_t	db_lookup( const char *symstr);
static char		*db_qualify(c_db_sym_t sym, char *symtabname);
static bool		db_symbol_is_ambiguous(c_db_sym_t sym);
static bool		db_line_at_pc(c_db_sym_t, char **, int *, db_expr_t);

static int db_cpu = -1;

#ifdef VIMAGE
static void *db_vnet = NULL;
#endif

/*
 * Validate the CPU number used to interpret per-CPU variables so we can
 * avoid later confusion if an invalid CPU is requested.
 */
int
db_var_db_cpu(struct db_variable *vp, db_expr_t *valuep, int op)
{

	switch (op) {
	case DB_VAR_GET:
		*valuep = db_cpu;
		return (1);

	case DB_VAR_SET:
		if (*(int *)valuep < -1 || *(int *)valuep > mp_maxid) {
			db_printf("Invalid value: %d\n", *(int*)valuep);
			return (0);
		}
		db_cpu = *(int *)valuep;
		return (1);

	default:
		db_printf("db_var_db_cpu: unknown operation\n");
		return (0);
	}
}

/*
 * Read-only variable reporting the current CPU, which is what we use when
 * db_cpu is set to -1.
 */
int
db_var_curcpu(struct db_variable *vp, db_expr_t *valuep, int op)
{

	switch (op) {
	case DB_VAR_GET:
		*valuep = curcpu;
		return (1);

	case DB_VAR_SET:
		db_printf("Read-only variable.\n");
		return (0);

	default:
		db_printf("db_var_curcpu: unknown operation\n");
		return (0);
	}
}

#ifdef VIMAGE
/*
 * Validate the virtual network pointer used to interpret per-vnet global
 * variable expansion.  Right now we don't do much here, really we should
 * walk the global vnet list to check it's an OK pointer.
 */
int
db_var_db_vnet(struct db_variable *vp, db_expr_t *valuep, int op)
{

	switch (op) {
	case DB_VAR_GET:
		*valuep = (db_expr_t)db_vnet;
		return (1);

	case DB_VAR_SET:
		db_vnet = *(void **)valuep;
		return (1);

	default:
		db_printf("db_var_db_vnet: unknown operation\n");
		return (0);
	}
}

/*
 * Read-only variable reporting the current vnet, which is what we use when
 * db_vnet is set to NULL.
 */
int
db_var_curvnet(struct db_variable *vp, db_expr_t *valuep, int op)
{

	switch (op) {
	case DB_VAR_GET:
		*valuep = (db_expr_t)curvnet;
		return (1);

	case DB_VAR_SET:
		db_printf("Read-only variable.\n");
		return (0);

	default:
		db_printf("db_var_curvnet: unknown operation\n");
		return (0);
	}
}
#endif

/*
 * Add symbol table, with given name, to list of symbol tables.
 */
void
db_add_symbol_table(char *start, char *end, char *name, char *ref)
{
	if (db_nsymtab >= MAXNOSYMTABS) {
		printf ("No slots left for %s symbol table", name);
		panic ("db_sym.c: db_add_symbol_table");
	}

	db_symtabs[db_nsymtab].start = start;
	db_symtabs[db_nsymtab].end = end;
	db_symtabs[db_nsymtab].name = name;
	db_symtabs[db_nsymtab].private = ref;
	db_nsymtab++;
}

/*
 *  db_qualify("vm_map", "ux") returns "unix:vm_map".
 *
 *  Note: return value points to static data whose content is
 *  overwritten by each call... but in practice this seems okay.
 */
static char *
db_qualify(c_db_sym_t sym, char *symtabname)
{
	const char	*symname;
	static char     tmp[256];

	db_symbol_values(sym, &symname, 0);
	snprintf(tmp, sizeof(tmp), "%s:%s", symtabname, symname);
	return tmp;
}


bool
db_eqname(const char *src, const char *dst, int c)
{
	if (!strcmp(src, dst))
	    return (true);
	if (src[0] == c)
	    return (!strcmp(src+1,dst));
	return (false);
}

bool
db_value_of_name(const char *name, db_expr_t *valuep)
{
	c_db_sym_t	sym;

	sym = db_lookup(name);
	if (sym == C_DB_SYM_NULL)
	    return (false);
	db_symbol_values(sym, &name, valuep);
	return (true);
}

bool
db_value_of_name_pcpu(const char *name, db_expr_t *valuep)
{
	static char     tmp[256];
	db_expr_t	value;
	c_db_sym_t	sym;
	int		cpu;

	if (db_cpu != -1)
		cpu = db_cpu;
	else
		cpu = curcpu;
	snprintf(tmp, sizeof(tmp), "pcpu_entry_%s", name);
	sym = db_lookup(tmp);
	if (sym == C_DB_SYM_NULL)
		return (false);
	db_symbol_values(sym, &name, &value);
	if (value < DPCPU_START || value >= DPCPU_STOP)
		return (false);
	*valuep = (db_expr_t)((uintptr_t)value + dpcpu_off[cpu]);
	return (true);
}

bool
db_value_of_name_vnet(const char *name, db_expr_t *valuep)
{
#ifdef VIMAGE
	static char     tmp[256];
	db_expr_t	value;
	c_db_sym_t	sym;
	struct vnet	*vnet;

	if (db_vnet != NULL)
		vnet = db_vnet;
	else
		vnet = curvnet;
	snprintf(tmp, sizeof(tmp), "vnet_entry_%s", name);
	sym = db_lookup(tmp);
	if (sym == C_DB_SYM_NULL)
		return (false);
	db_symbol_values(sym, &name, &value);
	if (value < VNET_START || value >= VNET_STOP)
		return (false);
	*valuep = (db_expr_t)((uintptr_t)value + vnet->vnet_data_base);
	return (true);
#else
	return (false);
#endif
}

/*
 * Lookup a symbol.
 * If the symbol has a qualifier (e.g., ux:vm_map),
 * then only the specified symbol table will be searched;
 * otherwise, all symbol tables will be searched.
 */
static c_db_sym_t
db_lookup(const char *symstr)
{
	c_db_sym_t sp;
	int i;
	int symtab_start = 0;
	int symtab_end = db_nsymtab;
	const char *cp;

	/*
	 * Look for, remove, and remember any symbol table specifier.
	 */
	for (cp = symstr; *cp; cp++) {
		if (*cp == ':') {
			for (i = 0; i < db_nsymtab; i++) {
				int n = strlen(db_symtabs[i].name);

				if (
				    n == (cp - symstr) &&
				    strncmp(symstr, db_symtabs[i].name, n) == 0
				) {
					symtab_start = i;
					symtab_end = i + 1;
					break;
				}
			}
			if (i == db_nsymtab) {
				db_error("invalid symbol table name");
			}
			symstr = cp+1;
		}
	}

	/*
	 * Look in the specified set of symbol tables.
	 * Return on first match.
	 */
	for (i = symtab_start; i < symtab_end; i++) {
		sp = X_db_lookup(&db_symtabs[i], symstr);
		if (sp) {
			db_last_symtab = &db_symtabs[i];
			return sp;
		}
	}
	return 0;
}

/*
 * If true, check across symbol tables for multiple occurrences
 * of a name.  Might slow things down quite a bit.
 */
static volatile bool db_qualify_ambiguous_names = false;

/*
 * Does this symbol name appear in more than one symbol table?
 * Used by db_symbol_values to decide whether to qualify a symbol.
 */
static bool
db_symbol_is_ambiguous(c_db_sym_t sym)
{
	const char	*sym_name;
	int		i;
	bool		found_once = false;

	if (!db_qualify_ambiguous_names)
		return (false);

	db_symbol_values(sym, &sym_name, 0);
	for (i = 0; i < db_nsymtab; i++) {
		if (X_db_lookup(&db_symtabs[i], sym_name)) {
			if (found_once)
				return (true);
			found_once = true;
		}
	}
	return (false);
}

/*
 * Find the closest symbol to val, and return its name
 * and the difference between val and the symbol found.
 */
c_db_sym_t
db_search_symbol(db_addr_t val, db_strategy_t strategy, db_expr_t *offp)
{
	unsigned int	diff;
	size_t		newdiff;
	int		i;
	c_db_sym_t	ret = C_DB_SYM_NULL, sym;

	newdiff = diff = val;
	for (i = 0; i < db_nsymtab; i++) {
	    sym = X_db_search_symbol(&db_symtabs[i], val, strategy, &newdiff);
	    if ((uintmax_t)newdiff < (uintmax_t)diff) {
		db_last_symtab = &db_symtabs[i];
		diff = newdiff;
		ret = sym;
	    }
	}
	*offp = diff;
	return ret;
}

/*
 * Return name and value of a symbol
 */
void
db_symbol_values(c_db_sym_t sym, const char **namep, db_expr_t *valuep)
{
	db_expr_t	value;

	if (sym == DB_SYM_NULL) {
		*namep = NULL;
		return;
	}

	X_db_symbol_values(db_last_symtab, sym, namep, &value);

	if (db_symbol_is_ambiguous(sym))
		*namep = db_qualify(sym, db_last_symtab->name);
	if (valuep)
		*valuep = value;
}


/*
 * Print a the closest symbol to value
 *
 * After matching the symbol according to the given strategy
 * we print it in the name+offset format, provided the symbol's
 * value is close enough (eg smaller than db_maxoff).
 * We also attempt to print [filename:linenum] when applicable
 * (eg for procedure names).
 *
 * If we could not find a reasonable name+offset representation,
 * then we just print the value in hex.  Small values might get
 * bogus symbol associations, e.g. 3 might get some absolute
 * value like _INCLUDE_VERSION or something, therefore we do
 * not accept symbols whose value is "small" (and use plain hex).
 */

db_expr_t	db_maxoff = 0x10000;

void
db_printsym(db_expr_t off, db_strategy_t strategy)
{
	db_expr_t	d;
	char 		*filename;
	const char	*name;
	int 		linenum;
	c_db_sym_t	cursym;

	if (off < 0 && off >= -db_maxoff) {
		db_printf("%+#lr", (long)off);
		return;
	}
	cursym = db_search_symbol(off, strategy, &d);
	db_symbol_values(cursym, &name, NULL);
	if (name == NULL || d >= (db_addr_t)db_maxoff) {
		db_printf("%#lr", (unsigned long)off);
		return;
	}
#ifdef DDB_NUMSYM
	db_printf("%#lr = %s", (unsigned long)off, name);
#else
	db_printf("%s", name);
#endif
	if (d)
		db_printf("+%+#lr", (long)d);
	if (strategy == DB_STGY_PROC) {
		if (db_line_at_pc(cursym, &filename, &linenum, off))
			db_printf(" [%s:%d]", filename, linenum);
	}
}

static bool
db_line_at_pc(c_db_sym_t sym, char **filename, int *linenum, db_expr_t pc)
{
	return (X_db_line_at_pc(db_last_symtab, sym, filename, linenum, pc));
}

bool
db_sym_numargs(c_db_sym_t sym, int *nargp, char **argnames)
{
	return (X_db_sym_numargs(db_last_symtab, sym, nargp, argnames));
}
