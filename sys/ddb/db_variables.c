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

#include <sys/param.h>
#include <sys/systm.h>

#include <ddb/ddb.h>
#include <ddb/db_lex.h>
#include <ddb/db_variables.h>

static int	db_find_variable(struct db_variable **varp);

static struct db_variable db_vars[] = {
	{ "radix",	&db_radix, FCN_NULL },
	{ "maxoff",	&db_maxoff, FCN_NULL },
	{ "maxwidth",	&db_max_width, FCN_NULL },
	{ "tabstops",	&db_tab_stop_width, FCN_NULL },
	{ "lines",	&db_lines_per_page, FCN_NULL },
	{ "curcpu",	NULL, db_var_curcpu },
	{ "db_cpu",	NULL, db_var_db_cpu },
#ifdef VIMAGE
	{ "curvnet",	NULL, db_var_curvnet },
	{ "db_vnet",	NULL, db_var_db_vnet },
#endif
};
static struct db_variable *db_evars = db_vars + nitems(db_vars);

static int
db_find_variable(struct db_variable **varp)
{
	struct db_variable *vp;
	int t;

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
	return (0);
}

int
db_get_variable(db_expr_t *valuep)
{
	struct db_variable *vp;

	if (!db_find_variable(&vp))
		return (0);

	return (db_read_variable(vp, valuep));
}

int
db_set_variable(db_expr_t value)
{
	struct db_variable *vp;

	if (!db_find_variable(&vp))
		return (0);

	return (db_write_variable(vp, value));
}

int
db_read_variable(struct db_variable *vp, db_expr_t *valuep)
{
	db_varfcn_t *func = vp->fcn;

	if (func == FCN_NULL) {
		*valuep = *(vp->valuep);
		return (1);
	}
	return ((*func)(vp, valuep, DB_VAR_GET));
}

int
db_write_variable(struct db_variable *vp, db_expr_t value)
{
	db_varfcn_t *func = vp->fcn;

	if (func == FCN_NULL) {
		*(vp->valuep) = value;
		return (1);
	}
	return ((*func)(vp, &value, DB_VAR_SET));
}

void
db_set_cmd(db_expr_t dummy1, bool dummy2, db_expr_t dummy3, char *dummy4)
{
	struct db_variable *vp;
	db_expr_t value;
	int t;

	t = db_read_token();
	if (t == tEOL) {
		for (vp = db_vars; vp < db_evars; vp++) {
			if (!db_read_variable(vp, &value)) {
				db_printf("$%s\n", vp->name);
				continue;
			}
			db_printf("$%-8s = %ld\n",
			    vp->name, (unsigned long)value);
		}
		return;
	}
	if (t != tDOLLAR) {
		db_error("Unknown variable\n");
		return;
	}
	if (!db_find_variable(&vp)) {
		db_error("Unknown variable\n");
		return;
	}

	t = db_read_token();
	if (t != tEQ)
		db_unread_token(t);

	if (!db_expression(&value)) {
		db_error("No value\n");
		return;
	}
	if (db_read_token() != tEOL)
		db_error("?\n");

	db_write_variable(vp, value);
}
