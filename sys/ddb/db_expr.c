/*	$OpenBSD: db_expr.c,v 1.18 2020/10/15 03:14:00 deraadt Exp $	*/
/*	$NetBSD: db_expr.c,v 1.5 1996/02/05 01:56:58 christos Exp $	*/

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
 *
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/param.h>

#include <machine/db_machdep.h>

#include <ddb/db_lex.h>
#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#include <ddb/db_variables.h>

int db_term(db_expr_t *);
int db_unary(db_expr_t *);
int db_mult_expr(db_expr_t *);
int db_add_expr(db_expr_t *);
int db_shift_expr(db_expr_t *);

int
db_term(db_expr_t *valuep)
{
	int	t;

	t = db_read_token();
	if (t == tIDENT) {
		if (db_symbol_by_name(db_tok_string, valuep) == NULL) {
			db_error("Symbol not found\n");
			/*NOTREACHED*/
		}
		return 1;
	}
	if (t == tNUMBER) {
		*valuep = db_tok_number;
		return 1;
	}
	if (t == tDOT) {
		*valuep = (db_expr_t)db_dot;
		return 1;
	}
	if (t == tDOTDOT) {
		*valuep = (db_expr_t)db_prev;
		return 1;
	}
	if (t == tPLUS) {
		*valuep = (db_expr_t) db_next;
		return 1;
	}
	if (t == tDITTO) {
		*valuep = (db_expr_t)db_last_addr;
		return 1;
	}
	if (t == tDOLLAR) {
		if (!db_get_variable(valuep))
			return 0;
		return 1;
	}
	if (t == tLPAREN) {
		if (!db_expression(valuep)) {
			db_error("Syntax error\n");
			/*NOTREACHED*/
		}
		t = db_read_token();
		if (t != tRPAREN) {
			db_error("Syntax error\n");
			/*NOTREACHED*/
		}
		return 1;
	}
	db_unread_token(t);
	return 0;
}

int
db_unary(db_expr_t *valuep)
{
	int	t;

	t = db_read_token();
	if (t == tMINUS) {
		if (!db_unary(valuep)) {
			db_error("Syntax error\n");
			/*NOTREACHED*/
		}
		*valuep = -*valuep;
		return 1;
	}
	if (t == tSTAR) {
		/* indirection */
		if (!db_unary(valuep)) {
			db_error("Syntax error\n");
			/*NOTREACHED*/
		}
		*valuep = db_get_value((vaddr_t)*valuep, sizeof(vaddr_t), 0);
		return 1;
	}
	db_unread_token(t);
	return (db_term(valuep));
}

int
db_mult_expr(db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_unary(&lhs))
		return 0;

	t = db_read_token();
	while (t == tSTAR || t == tSLASH || t == tPCT || t == tHASH) {
		if (!db_term(&rhs)) {
			db_error("Syntax error\n");
			/*NOTREACHED*/
		}
		if (t == tSTAR)
			lhs *= rhs;
		else {
			if (rhs == 0) {
				db_error("Divide by 0\n");
				/*NOTREACHED*/
			}
			if (t == tSLASH)
				lhs /= rhs;
			else if (t == tPCT)
				lhs %= rhs;
			else
				lhs = ((lhs+rhs-1)/rhs)*rhs;
		}
		t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return 1;
}

int
db_add_expr(db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_mult_expr(&lhs))
		return 0;

	t = db_read_token();
	while (t == tPLUS || t == tMINUS) {
		if (!db_mult_expr(&rhs)) {
			db_error("Syntax error\n");
			/*NOTREACHED*/
		}
		if (t == tPLUS)
			lhs += rhs;
		else
			lhs -= rhs;
		t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return 1;
}

int
db_shift_expr(db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_add_expr(&lhs))
		return 0;

	t = db_read_token();
	while (t == tSHIFT_L || t == tSHIFT_R) {
		if (!db_add_expr(&rhs)) {
			db_error("Syntax error\n");
			/*NOTREACHED*/
		}
		if (rhs < 0) {
			db_error("Negative shift amount\n");
			/*NOTREACHED*/
		}
		if (t == tSHIFT_L)
			lhs <<= rhs;
		else {
			/* Shift right is unsigned */
			lhs = (unsigned) lhs >> rhs;
		}
		t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return 1;
}

int
db_expression(db_expr_t *valuep)
{
	return (db_shift_expr(valuep));
}
