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
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <ddb/ddb.h>
#include <ddb/db_lex.h>
#include <ddb/db_access.h>
#include <ddb/db_command.h>

static bool	db_add_expr(db_expr_t *valuep);
static bool	db_mult_expr(db_expr_t *valuep);
static bool	db_shift_expr(db_expr_t *valuep);
static bool	db_term(db_expr_t *valuep);
static bool	db_unary(db_expr_t *valuep);
static bool	db_logical_or_expr(db_expr_t *valuep);
static bool	db_logical_and_expr(db_expr_t *valuep);
static bool	db_logical_relation_expr(db_expr_t *valuep);

static bool
db_term(db_expr_t *valuep)
{
	int	t;

	t = db_read_token();
	if (t == tIDENT) {
	    if (!db_value_of_name(db_tok_string, valuep) &&
		!db_value_of_name_pcpu(db_tok_string, valuep) &&
		!db_value_of_name_vnet(db_tok_string, valuep)) {
		db_printf("Symbol '%s' not found\n", db_tok_string);
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    return (true);
	}
	if (t == tNUMBER) {
	    *valuep = (db_expr_t)db_tok_number;
	    return (true);
	}
	if (t == tDOT) {
	    *valuep = (db_expr_t)db_dot;
	    return (true);
	}
	if (t == tDOTDOT) {
	    *valuep = (db_expr_t)db_prev;
	    return (true);
	}
	if (t == tPLUS) {
	    *valuep = (db_expr_t) db_next;
	    return (true);
	}
	if (t == tDITTO) {
	    *valuep = (db_expr_t)db_last_addr;
	    return (true);
	}
	if (t == tDOLLAR) {
	    if (!db_get_variable(valuep))
		return (false);
	    return (true);
	}
	if (t == tLPAREN) {
	    if (!db_expression(valuep)) {
		db_printf("Expression syntax error after '%c'\n", '(');
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    t = db_read_token();
	    if (t != tRPAREN) {
		db_printf("Expression syntax error -- expected '%c'\n", ')');
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    return (true);
	}
	db_unread_token(t);
	return (false);
}

static bool
db_unary(db_expr_t *valuep)
{
	int	t;

	t = db_read_token();
	if (t == tMINUS) {
	    if (!db_unary(valuep)) {
		db_printf("Expression syntax error after '%c'\n", '-');
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    *valuep = -*valuep;
	    return (true);
	}
	if (t == tEXCL) {
	    if(!db_unary(valuep)) {
		db_printf("Expression syntax error after '%c'\n", '!');
		db_error(NULL);
		/* NOTREACHED  */
	    }
	    *valuep = (!(*valuep));
	    return (true);
	}
	if (t == tBIT_NOT) {
	    if(!db_unary(valuep)) {
		db_printf("Expression syntax error after '%c'\n", '~');
		db_error(NULL);
		/* NOTREACHED */
	    }
	    *valuep = (~(*valuep));
	    return (true);
	}
	if (t == tSTAR) {
	    /* indirection */
	    if (!db_unary(valuep)) {
		db_printf("Expression syntax error after '%c'\n", '*');
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    *valuep = db_get_value((db_addr_t)*valuep, sizeof(void *),
		false);
	    return (true);
	}
	db_unread_token(t);
	return (db_term(valuep));
}

static bool
db_mult_expr(db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_unary(&lhs))
	    return (false);

	t = db_read_token();
	while (t == tSTAR || t == tSLASH || t == tPCT || t == tHASH ||
	    t == tBIT_AND ) {
	    if (!db_term(&rhs)) {
		db_printf("Expression syntax error after '%c'\n",
		    t == tSTAR ? '*' : t == tSLASH ? '/' : t == tPCT ? '%' :
		    t == tHASH ? '#' : '&');
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    switch(t)  {
		case tSTAR:
		    lhs *= rhs;
		    break;
		case tBIT_AND:
		    lhs &= rhs;
		    break;
		default:
		    if (rhs == 0) {
			db_error("Division by 0\n");
			/*NOTREACHED*/
		    }
		    if (t == tSLASH)
			lhs /= rhs;
		    else if (t == tPCT)
			lhs %= rhs;
		    else
			lhs = roundup(lhs, rhs);
	    }
	    t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return (true);
}

static bool
db_add_expr(db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_mult_expr(&lhs))
	    return (false);

	t = db_read_token();
	while (t == tPLUS || t == tMINUS || t == tBIT_OR) {
	    if (!db_mult_expr(&rhs)) {
		db_printf("Expression syntax error after '%c'\n",
		    t == tPLUS ? '+' : t == tMINUS ? '-' : '|');
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    switch (t) {
	    case tPLUS:
		lhs += rhs;
		break;
	    case tMINUS:
		lhs -= rhs;
		break;
	    case tBIT_OR:
		lhs |= rhs;
		break;
	    default:
		__unreachable();
	    }
	    t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return (true);
}

static bool
db_shift_expr(db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_add_expr(&lhs))
		return (false);
	t = db_read_token();
	while (t == tSHIFT_L || t == tSHIFT_R) {
	    if (!db_add_expr(&rhs)) {
		db_printf("Expression syntax error after '%s'\n",
		    t == tSHIFT_L ? "<<" : ">>");
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    if (rhs < 0) {
		db_printf("Negative shift amount %jd\n", (intmax_t)rhs);
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    if (t == tSHIFT_L)
		lhs <<= rhs;
	    else {
		/* Shift right is unsigned */
		lhs = (db_addr_t)lhs >> rhs;
	    }
	    t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return (true);
}

static bool
db_logical_relation_expr(
	db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_shift_expr(&lhs))
	    return (false);

	t = db_read_token();
	while (t == tLOG_EQ || t == tLOG_NOT_EQ || t == tGREATER ||
	    t == tGREATER_EQ || t == tLESS || t == tLESS_EQ) {
	    if (!db_shift_expr(&rhs)) {
		db_printf("Expression syntax error after '%s'\n",
		    t == tLOG_EQ ? "==" : t == tLOG_NOT_EQ ? "!=" :
		    t == tGREATER ? ">" : t == tGREATER_EQ ? ">=" :
		    t == tLESS ? "<" : "<=");
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    switch(t) {
		case tLOG_EQ:
		    lhs = (lhs == rhs);
		    break;
		case tLOG_NOT_EQ:
		    lhs = (lhs != rhs);
		    break;
		case tGREATER:
		    lhs = (lhs > rhs);
		    break;
		case tGREATER_EQ:
		    lhs = (lhs >= rhs);
		    break;
		case tLESS:
		    lhs = (lhs < rhs);
		    break;
		case tLESS_EQ:
		    lhs = (lhs <= rhs);
		    break;
		default:
		    __unreachable();
	    }
	    t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return (true);
}

static bool
db_logical_and_expr(
	db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_logical_relation_expr(&lhs))
	    return (false);

	t = db_read_token();
	while (t == tLOG_AND) {
	    if (!db_logical_relation_expr(&rhs)) {
		db_printf("Expression syntax error after '%s'\n", "&&");
		db_error(NULL);
		/*NOTREACHED*/
	    }
	    lhs = (lhs && rhs);
	    t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return (true);
}

static bool
db_logical_or_expr(
	db_expr_t *valuep)
{
	db_expr_t	lhs, rhs;
	int		t;

	if (!db_logical_and_expr(&lhs))
		return(false);

	t = db_read_token();
	while (t == tLOG_OR) {
		if (!db_logical_and_expr(&rhs)) {
			db_printf("Expression syntax error after '%s'\n", "||");
			db_error(NULL);
			/*NOTREACHED*/
		}
		lhs = (lhs || rhs);
		t = db_read_token();
	}
	db_unread_token(t);
	*valuep = lhs;
	return (true);
}

int
db_expression(db_expr_t *valuep)
{
	return (db_logical_or_expr(valuep));
}
