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
 *
 * $FreeBSD$
 */

#ifndef _DDB_DB_SYM_H_
#define	_DDB_DB_SYM_H_

/*
 *	Author: Alessandro Forin, Carnegie Mellon University
 *	Date:	8/90
 */

/*
 * This module can handle multiple symbol tables
 */
typedef struct {
	char		*name;		/* symtab name */
	char		*start;		/* symtab location */
	char		*end;
	char		*private;	/* optional machdep pointer */
} db_symtab_t;

/*
 * Symbol representation is specific to the symtab style:
 * BSD compilers use dbx' nlist, other compilers might use
 * a different one
 */
typedef	char *		db_sym_t;	/* opaque handle on symbols */
typedef	const char *	c_db_sym_t;	/* const opaque handle on symbols */
#define	DB_SYM_NULL	((db_sym_t)0)
#define	C_DB_SYM_NULL	((c_db_sym_t)0)

/*
 * Non-stripped symbol tables will have duplicates, for instance
 * the same string could match a parameter name, a local var, a
 * global var, etc.
 * We are most concern with the following matches.
 */
typedef int		db_strategy_t;	/* search strategy */

#define	DB_STGY_ANY	0			/* anything goes */
#define	DB_STGY_XTRN	1			/* only external symbols */
#define	DB_STGY_PROC	2			/* only procedures */

/*
 * Functions exported by the symtable module
 */
void		db_add_symbol_table(char *, char *, char *, char *);
					/* extend the list of symbol tables */

c_db_sym_t	db_search_symbol(db_addr_t, db_strategy_t, db_expr_t *);
					/* find symbol given value */

void		db_symbol_values(c_db_sym_t, const char **, db_expr_t *);
					/* return name and value of symbol */

#define	db_find_sym_and_offset(val,namep,offp)	\
	db_symbol_values(db_search_symbol(val,DB_STGY_ANY,offp),namep,0)
					/* find name&value given approx val */

#define	db_find_xtrn_sym_and_offset(val,namep,offp)	\
	db_symbol_values(db_search_symbol(val,DB_STGY_XTRN,offp),namep,0)
					/* ditto, but no locals */

bool		db_eqname(const char *, const char *, int);
					/* strcmp, modulo leading char */

void		db_printsym(db_expr_t, db_strategy_t);
					/* print closest symbol to a value */

bool		db_sym_numargs(c_db_sym_t, int *, char **);

bool		X_db_line_at_pc(db_symtab_t *symtab, c_db_sym_t cursym,
		    char **filename, int *linenum, db_expr_t off);
c_db_sym_t	X_db_lookup(db_symtab_t *stab, const char *symstr);
c_db_sym_t	X_db_search_symbol(db_symtab_t *symtab, db_addr_t off,
		    db_strategy_t strategy, db_expr_t *diffp);
bool		X_db_sym_numargs(db_symtab_t *, c_db_sym_t, int *, char **);
void		X_db_symbol_values(db_symtab_t *symtab, c_db_sym_t sym,
		    const char **namep, db_expr_t *valuep);

#endif /* !_DDB_DB_SYM_H_ */
