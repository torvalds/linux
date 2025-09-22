/*	$OpenBSD: db_variables.c,v 1.22 2023/03/08 04:43:07 guenther Exp $	*/
/*	$NetBSD: db_variables.c,v 1.8 1996/02/05 01:57:19 christos Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1993,1992,1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
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
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_lex.h>
#include <ddb/db_variables.h>
#include <ddb/db_command.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#include <ddb/db_var.h>

struct db_variable db_vars[] = {
	{ "radix",	(long *)&db_radix, db_var_rw_int },
	{ "maxoff",	(long *)&db_maxoff, db_var_rw_int },
	{ "maxwidth",	(long *)&db_max_width, db_var_rw_int },
	{ "tabstops",	(long *)&db_tab_stop_width, db_var_rw_int },
	{ "lines",	(long *)&db_max_line, db_var_rw_int },
	{ "log",	(long *)&db_log, db_var_rw_int }
};
struct db_variable *db_evars = db_vars + nitems(db_vars);

int
db_find_variable(struct db_variable **varp)
{
	int	t;
	struct db_variable *vp;

	t = db_read_token();
	if (t == tIDENT) {
		for (vp = db_vars; vp < db_evars; vp++) {
			if (!strcmp(db_tok_string, vp->name)) {
				*varp = vp;
				return (1);
			}
		}
		for (vp = db_regs; vp < db_eregs; vp++) {
			if (!strcmp(db_tok_string, vp->name)) {
				*varp = vp;
				return (1);
			}
		}
	}
	db_error("Unknown variable\n");
	/*NOTREACHED*/
	return 0;
}

int
db_get_variable(db_expr_t *valuep)
{
	struct db_variable *vp;

	if (!db_find_variable(&vp))
		return (0);

	db_read_variable(vp, valuep);

	return (1);
}

int
db_set_variable(db_expr_t value)
{
	struct db_variable *vp;

	if (!db_find_variable(&vp))
		return (0);

	db_write_variable(vp, &value);

	return (1);
}


void
db_read_variable(struct db_variable *vp, db_expr_t *valuep)
{
	int	(*func)(struct db_variable *, db_expr_t *, int) = vp->fcn;

	if (func == FCN_NULL)
		*valuep = *(vp->valuep);
	else
		(*func)(vp, valuep, DB_VAR_GET);
}

void
db_write_variable(struct db_variable *vp, db_expr_t *valuep)
{
	int	(*func)(struct db_variable *, db_expr_t *, int) = vp->fcn;

	if (func == FCN_NULL)
		*(vp->valuep) = *valuep;
	else
		(*func)(vp, valuep, DB_VAR_SET);
}

void
db_set_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_expr_t	value;
	struct db_variable *vp;
	int	t;

	t = db_read_token();
	if (t != tDOLLAR) {
		db_error("Unknown variable\n");
		/*NOTREACHED*/
	}
	if (!db_find_variable(&vp)) {
		db_error("Unknown variable\n");
		/*NOTREACHED*/
	}

	t = db_read_token();
	if (t != tEQ)
		db_unread_token(t);

	if (!db_expression(&value)) {
		db_error("No value\n");
		/*NOTREACHED*/
	}
	if (db_read_token() != tEOL) {
		db_error("?\n");
		/*NOTREACHED*/
	}

	db_write_variable(vp, &value);
}

int
db_var_rw_int(struct db_variable *var, db_expr_t *expr, int mode)
{

	if (mode == DB_VAR_SET)
		*(int *)var->valuep = *expr;
	else
		*expr = *(int *)var->valuep;
	return (0);
}
