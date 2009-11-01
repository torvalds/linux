/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

#define DEBUG_EXPR	0

struct expr *expr_alloc_symbol(struct symbol *sym)
{
	struct expr *e = malloc(sizeof(*e));
	memset(e, 0, sizeof(*e));
	e->type = E_SYMBOL;
	e->left.sym = sym;
	return e;
}

struct expr *expr_alloc_one(enum expr_type type, struct expr *ce)
{
	struct expr *e = malloc(sizeof(*e));
	memset(e, 0, sizeof(*e));
	e->type = type;
	e->left.expr = ce;
	return e;
}

struct expr *expr_alloc_two(enum expr_type type, struct expr *e1, struct expr *e2)
{
	struct expr *e = malloc(sizeof(*e));
	memset(e, 0, sizeof(*e));
	e->type = type;
	e->left.expr = e1;
	e->right.expr = e2;
	return e;
}

struct expr *expr_alloc_comp(enum expr_type type, struct symbol *s1, struct symbol *s2)
{
	struct expr *e = malloc(sizeof(*e));
	memset(e, 0, sizeof(*e));
	e->type = type;
	e->left.sym = s1;
	e->right.sym = s2;
	return e;
}

struct expr *expr_alloc_and(struct expr *e1, struct expr *e2)
{
	if (!e1)
		return e2;
	return e2 ? expr_alloc_two(E_AND, e1, e2) : e1;
}

struct expr *expr_alloc_or(struct expr *e1, struct expr *e2)
{
	if (!e1)
		return e2;
	return e2 ? expr_alloc_two(E_OR, e1, e2) : e1;
}

struct expr *expr_copy(struct expr *org)
{
	struct expr *e;

	if (!org)
		return NULL;

	e = malloc(sizeof(*org));
	memcpy(e, org, sizeof(*org));
	switch (org->type) {
	case E_SYMBOL:
		e->left = org->left;
		break;
	case E_NOT:
		e->left.expr = expr_copy(org->left.expr);
		break;
	case E_EQUAL:
	case E_UNEQUAL:
		e->left.sym = org->left.sym;
		e->right.sym = org->right.sym;
		break;
	case E_AND:
	case E_OR:
	case E_LIST:
		e->left.expr = expr_copy(org->left.expr);
		e->right.expr = expr_copy(org->right.expr);
		break;
	default:
		printf("can't copy type %d\n", e->type);
		free(e);
		e = NULL;
		break;
	}

	return e;
}

void expr_free(struct expr *e)
{
	if (!e)
		return;

	switch (e->type) {
	case E_SYMBOL:
		break;
	case E_NOT:
		expr_free(e->left.expr);
		return;
	case E_EQUAL:
	case E_UNEQUAL:
		break;
	case E_OR:
	case E_AND:
		expr_free(e->left.expr);
		expr_free(e->right.expr);
		break;
	default:
		printf("how to free type %d?\n", e->type);
		break;
	}
	free(e);
}

static int trans_count;

#define e1 (*ep1)
#define e2 (*ep2)

static void __expr_eliminate_eq(enum expr_type type, struct expr **ep1, struct expr **ep2)
{
	if (e1->type == type) {
		__expr_eliminate_eq(type, &e1->left.expr, &e2);
		__expr_eliminate_eq(type, &e1->right.expr, &e2);
		return;
	}
	if (e2->type == type) {
		__expr_eliminate_eq(type, &e1, &e2->left.expr);
		__expr_eliminate_eq(type, &e1, &e2->right.expr);
		return;
	}
	if (e1->type == E_SYMBOL && e2->type == E_SYMBOL &&
	    e1->left.sym == e2->left.sym &&
	    (e1->left.sym == &symbol_yes || e1->left.sym == &symbol_no))
		return;
	if (!expr_eq(e1, e2))
		return;
	trans_count++;
	expr_free(e1); expr_free(e2);
	switch (type) {
	case E_OR:
		e1 = expr_alloc_symbol(&symbol_no);
		e2 = expr_alloc_symbol(&symbol_no);
		break;
	case E_AND:
		e1 = expr_alloc_symbol(&symbol_yes);
		e2 = expr_alloc_symbol(&symbol_yes);
		break;
	default:
		;
	}
}

void expr_eliminate_eq(struct expr **ep1, struct expr **ep2)
{
	if (!e1 || !e2)
		return;
	switch (e1->type) {
	case E_OR:
	case E_AND:
		__expr_eliminate_eq(e1->type, ep1, ep2);
	default:
		;
	}
	if (e1->type != e2->type) switch (e2->type) {
	case E_OR:
	case E_AND:
		__expr_eliminate_eq(e2->type, ep1, ep2);
	default:
		;
	}
	e1 = expr_eliminate_yn(e1);
	e2 = expr_eliminate_yn(e2);
}

#undef e1
#undef e2

int expr_eq(struct expr *e1, struct expr *e2)
{
	int res, old_count;

	if (e1->type != e2->type)
		return 0;
	switch (e1->type) {
	case E_EQUAL:
	case E_UNEQUAL:
		return e1->left.sym == e2->left.sym && e1->right.sym == e2->right.sym;
	case E_SYMBOL:
		return e1->left.sym == e2->left.sym;
	case E_NOT:
		return expr_eq(e1->left.expr, e2->left.expr);
	case E_AND:
	case E_OR:
		e1 = expr_copy(e1);
		e2 = expr_copy(e2);
		old_count = trans_count;
		expr_eliminate_eq(&e1, &e2);
		res = (e1->type == E_SYMBOL && e2->type == E_SYMBOL &&
		       e1->left.sym == e2->left.sym);
		expr_free(e1);
		expr_free(e2);
		trans_count = old_count;
		return res;
	case E_LIST:
	case E_RANGE:
	case E_NONE:
		/* panic */;
	}

	if (DEBUG_EXPR) {
		expr_fprint(e1, stdout);
		printf(" = ");
		expr_fprint(e2, stdout);
		printf(" ?\n");
	}

	return 0;
}

struct expr *expr_eliminate_yn(struct expr *e)
{
	struct expr *tmp;

	if (e) switch (e->type) {
	case E_AND:
		e->left.expr = expr_eliminate_yn(e->left.expr);
		e->right.expr = expr_eliminate_yn(e->right.expr);
		if (e->left.expr->type == E_SYMBOL) {
			if (e->left.expr->left.sym == &symbol_no) {
				expr_free(e->left.expr);
				expr_free(e->right.expr);
				e->type = E_SYMBOL;
				e->left.sym = &symbol_no;
				e->right.expr = NULL;
				return e;
			} else if (e->left.expr->left.sym == &symbol_yes) {
				free(e->left.expr);
				tmp = e->right.expr;
				*e = *(e->right.expr);
				free(tmp);
				return e;
			}
		}
		if (e->right.expr->type == E_SYMBOL) {
			if (e->right.expr->left.sym == &symbol_no) {
				expr_free(e->left.expr);
				expr_free(e->right.expr);
				e->type = E_SYMBOL;
				e->left.sym = &symbol_no;
				e->right.expr = NULL;
				return e;
			} else if (e->right.expr->left.sym == &symbol_yes) {
				free(e->right.expr);
				tmp = e->left.expr;
				*e = *(e->left.expr);
				free(tmp);
				return e;
			}
		}
		break;
	case E_OR:
		e->left.expr = expr_eliminate_yn(e->left.expr);
		e->right.expr = expr_eliminate_yn(e->right.expr);
		if (e->left.expr->type == E_SYMBOL) {
			if (e->left.expr->left.sym == &symbol_no) {
				free(e->left.expr);
				tmp = e->right.expr;
				*e = *(e->right.expr);
				free(tmp);
				return e;
			} else if (e->left.expr->left.sym == &symbol_yes) {
				expr_free(e->left.expr);
				expr_free(e->right.expr);
				e->type = E_SYMBOL;
				e->left.sym = &symbol_yes;
				e->right.expr = NULL;
				return e;
			}
		}
		if (e->right.expr->type == E_SYMBOL) {
			if (e->right.expr->left.sym == &symbol_no) {
				free(e->right.expr);
				tmp = e->left.expr;
				*e = *(e->left.expr);
				free(tmp);
				return e;
			} else if (e->right.expr->left.sym == &symbol_yes) {
				expr_free(e->left.expr);
				expr_free(e->right.expr);
				e->type = E_SYMBOL;
				e->left.sym = &symbol_yes;
				e->right.expr = NULL;
				return e;
			}
		}
		break;
	default:
		;
	}
	return e;
}

/*
 * bool FOO!=n => FOO
 */
struct expr *expr_trans_bool(struct expr *e)
{
	if (!e)
		return NULL;
	switch (e->type) {
	case E_AND:
	case E_OR:
	case E_NOT:
		e->left.expr = expr_trans_bool(e->left.expr);
		e->right.expr = expr_trans_bool(e->right.expr);
		break;
	case E_UNEQUAL:
		// FOO!=n -> FOO
		if (e->left.sym->type == S_TRISTATE) {
			if (e->right.sym == &symbol_no) {
				e->type = E_SYMBOL;
				e->right.sym = NULL;
			}
		}
		break;
	default:
		;
	}
	return e;
}

/*
 * e1 || e2 -> ?
 */
static struct expr *expr_join_or(struct expr *e1, struct expr *e2)
{
	struct expr *tmp;
	struct symbol *sym1, *sym2;

	if (expr_eq(e1, e2))
		return expr_copy(e1);
	if (e1->type != E_EQUAL && e1->type != E_UNEQUAL && e1->type != E_SYMBOL && e1->type != E_NOT)
		return NULL;
	if (e2->type != E_EQUAL && e2->type != E_UNEQUAL && e2->type != E_SYMBOL && e2->type != E_NOT)
		return NULL;
	if (e1->type == E_NOT) {
		tmp = e1->left.expr;
		if (tmp->type != E_EQUAL && tmp->type != E_UNEQUAL && tmp->type != E_SYMBOL)
			return NULL;
		sym1 = tmp->left.sym;
	} else
		sym1 = e1->left.sym;
	if (e2->type == E_NOT) {
		if (e2->left.expr->type != E_SYMBOL)
			return NULL;
		sym2 = e2->left.expr->left.sym;
	} else
		sym2 = e2->left.sym;
	if (sym1 != sym2)
		return NULL;
	if (sym1->type != S_BOOLEAN && sym1->type != S_TRISTATE)
		return NULL;
	if (sym1->type == S_TRISTATE) {
		if (e1->type == E_EQUAL && e2->type == E_EQUAL &&
		    ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_mod) ||
		     (e1->right.sym == &symbol_mod && e2->right.sym == &symbol_yes))) {
			// (a='y') || (a='m') -> (a!='n')
			return expr_alloc_comp(E_UNEQUAL, sym1, &symbol_no);
		}
		if (e1->type == E_EQUAL && e2->type == E_EQUAL &&
		    ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_no) ||
		     (e1->right.sym == &symbol_no && e2->right.sym == &symbol_yes))) {
			// (a='y') || (a='n') -> (a!='m')
			return expr_alloc_comp(E_UNEQUAL, sym1, &symbol_mod);
		}
		if (e1->type == E_EQUAL && e2->type == E_EQUAL &&
		    ((e1->right.sym == &symbol_mod && e2->right.sym == &symbol_no) ||
		     (e1->right.sym == &symbol_no && e2->right.sym == &symbol_mod))) {
			// (a='m') || (a='n') -> (a!='y')
			return expr_alloc_comp(E_UNEQUAL, sym1, &symbol_yes);
		}
	}
	if (sym1->type == S_BOOLEAN && sym1 == sym2) {
		if ((e1->type == E_NOT && e1->left.expr->type == E_SYMBOL && e2->type == E_SYMBOL) ||
		    (e2->type == E_NOT && e2->left.expr->type == E_SYMBOL && e1->type == E_SYMBOL))
			return expr_alloc_symbol(&symbol_yes);
	}

	if (DEBUG_EXPR) {
		printf("optimize (");
		expr_fprint(e1, stdout);
		printf(") || (");
		expr_fprint(e2, stdout);
		printf(")?\n");
	}
	return NULL;
}

static struct expr *expr_join_and(struct expr *e1, struct expr *e2)
{
	struct expr *tmp;
	struct symbol *sym1, *sym2;

	if (expr_eq(e1, e2))
		return expr_copy(e1);
	if (e1->type != E_EQUAL && e1->type != E_UNEQUAL && e1->type != E_SYMBOL && e1->type != E_NOT)
		return NULL;
	if (e2->type != E_EQUAL && e2->type != E_UNEQUAL && e2->type != E_SYMBOL && e2->type != E_NOT)
		return NULL;
	if (e1->type == E_NOT) {
		tmp = e1->left.expr;
		if (tmp->type != E_EQUAL && tmp->type != E_UNEQUAL && tmp->type != E_SYMBOL)
			return NULL;
		sym1 = tmp->left.sym;
	} else
		sym1 = e1->left.sym;
	if (e2->type == E_NOT) {
		if (e2->left.expr->type != E_SYMBOL)
			return NULL;
		sym2 = e2->left.expr->left.sym;
	} else
		sym2 = e2->left.sym;
	if (sym1 != sym2)
		return NULL;
	if (sym1->type != S_BOOLEAN && sym1->type != S_TRISTATE)
		return NULL;

	if ((e1->type == E_SYMBOL && e2->type == E_EQUAL && e2->right.sym == &symbol_yes) ||
	    (e2->type == E_SYMBOL && e1->type == E_EQUAL && e1->right.sym == &symbol_yes))
		// (a) && (a='y') -> (a='y')
		return expr_alloc_comp(E_EQUAL, sym1, &symbol_yes);

	if ((e1->type == E_SYMBOL && e2->type == E_UNEQUAL && e2->right.sym == &symbol_no) ||
	    (e2->type == E_SYMBOL && e1->type == E_UNEQUAL && e1->right.sym == &symbol_no))
		// (a) && (a!='n') -> (a)
		return expr_alloc_symbol(sym1);

	if ((e1->type == E_SYMBOL && e2->type == E_UNEQUAL && e2->right.sym == &symbol_mod) ||
	    (e2->type == E_SYMBOL && e1->type == E_UNEQUAL && e1->right.sym == &symbol_mod))
		// (a) && (a!='m') -> (a='y')
		return expr_alloc_comp(E_EQUAL, sym1, &symbol_yes);

	if (sym1->type == S_TRISTATE) {
		if (e1->type == E_EQUAL && e2->type == E_UNEQUAL) {
			// (a='b') && (a!='c') -> 'b'='c' ? 'n' : a='b'
			sym2 = e1->right.sym;
			if ((e2->right.sym->flags & SYMBOL_CONST) && (sym2->flags & SYMBOL_CONST))
				return sym2 != e2->right.sym ? expr_alloc_comp(E_EQUAL, sym1, sym2)
							     : expr_alloc_symbol(&symbol_no);
		}
		if (e1->type == E_UNEQUAL && e2->type == E_EQUAL) {
			// (a='b') && (a!='c') -> 'b'='c' ? 'n' : a='b'
			sym2 = e2->right.sym;
			if ((e1->right.sym->flags & SYMBOL_CONST) && (sym2->flags & SYMBOL_CONST))
				return sym2 != e1->right.sym ? expr_alloc_comp(E_EQUAL, sym1, sym2)
							     : expr_alloc_symbol(&symbol_no);
		}
		if (e1->type == E_UNEQUAL && e2->type == E_UNEQUAL &&
			   ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_no) ||
			    (e1->right.sym == &symbol_no && e2->right.sym == &symbol_yes)))
			// (a!='y') && (a!='n') -> (a='m')
			return expr_alloc_comp(E_EQUAL, sym1, &symbol_mod);

		if (e1->type == E_UNEQUAL && e2->type == E_UNEQUAL &&
			   ((e1->right.sym == &symbol_yes && e2->right.sym == &symbol_mod) ||
			    (e1->right.sym == &symbol_mod && e2->right.sym == &symbol_yes)))
			// (a!='y') && (a!='m') -> (a='n')
			return expr_alloc_comp(E_EQUAL, sym1, &symbol_no);

		if (e1->type == E_UNEQUAL && e2->type == E_UNEQUAL &&
			   ((e1->right.sym == &symbol_mod && e2->right.sym == &symbol_no) ||
			    (e1->right.sym == &symbol_no && e2->right.sym == &symbol_mod)))
			// (a!='m') && (a!='n') -> (a='m')
			return expr_alloc_comp(E_EQUAL, sym1, &symbol_yes);

		if ((e1->type == E_SYMBOL && e2->type == E_EQUAL && e2->right.sym == &symbol_mod) ||
		    (e2->type == E_SYMBOL && e1->type == E_EQUAL && e1->right.sym == &symbol_mod) ||
		    (e1->type == E_SYMBOL && e2->type == E_UNEQUAL && e2->right.sym == &symbol_yes) ||
		    (e2->type == E_SYMBOL && e1->type == E_UNEQUAL && e1->right.sym == &symbol_yes))
			return NULL;
	}

	if (DEBUG_EXPR) {
		printf("optimize (");
		expr_fprint(e1, stdout);
		printf(") && (");
		expr_fprint(e2, stdout);
		printf(")?\n");
	}
	return NULL;
}

static void expr_eliminate_dups1(enum expr_type type, struct expr **ep1, struct expr **ep2)
{
#define e1 (*ep1)
#define e2 (*ep2)
	struct expr *tmp;

	if (e1->type == type) {
		expr_eliminate_dups1(type, &e1->left.expr, &e2);
		expr_eliminate_dups1(type, &e1->right.expr, &e2);
		return;
	}
	if (e2->type == type) {
		expr_eliminate_dups1(type, &e1, &e2->left.expr);
		expr_eliminate_dups1(type, &e1, &e2->right.expr);
		return;
	}
	if (e1 == e2)
		return;

	switch (e1->type) {
	case E_OR: case E_AND:
		expr_eliminate_dups1(e1->type, &e1, &e1);
	default:
		;
	}

	switch (type) {
	case E_OR:
		tmp = expr_join_or(e1, e2);
		if (tmp) {
			expr_free(e1); expr_free(e2);
			e1 = expr_alloc_symbol(&symbol_no);
			e2 = tmp;
			trans_count++;
		}
		break;
	case E_AND:
		tmp = expr_join_and(e1, e2);
		if (tmp) {
			expr_free(e1); expr_free(e2);
			e1 = expr_alloc_symbol(&symbol_yes);
			e2 = tmp;
			trans_count++;
		}
		break;
	default:
		;
	}
#undef e1
#undef e2
}

static void expr_eliminate_dups2(enum expr_type type, struct expr **ep1, struct expr **ep2)
{
#define e1 (*ep1)
#define e2 (*ep2)
	struct expr *tmp, *tmp1, *tmp2;

	if (e1->type == type) {
		expr_eliminate_dups2(type, &e1->left.expr, &e2);
		expr_eliminate_dups2(type, &e1->right.expr, &e2);
		return;
	}
	if (e2->type == type) {
		expr_eliminate_dups2(type, &e1, &e2->left.expr);
		expr_eliminate_dups2(type, &e1, &e2->right.expr);
	}
	if (e1 == e2)
		return;

	switch (e1->type) {
	case E_OR:
		expr_eliminate_dups2(e1->type, &e1, &e1);
		// (FOO || BAR) && (!FOO && !BAR) -> n
		tmp1 = expr_transform(expr_alloc_one(E_NOT, expr_copy(e1)));
		tmp2 = expr_copy(e2);
		tmp = expr_extract_eq_and(&tmp1, &tmp2);
		if (expr_is_yes(tmp1)) {
			expr_free(e1);
			e1 = expr_alloc_symbol(&symbol_no);
			trans_count++;
		}
		expr_free(tmp2);
		expr_free(tmp1);
		expr_free(tmp);
		break;
	case E_AND:
		expr_eliminate_dups2(e1->type, &e1, &e1);
		// (FOO && BAR) || (!FOO || !BAR) -> y
		tmp1 = expr_transform(expr_alloc_one(E_NOT, expr_copy(e1)));
		tmp2 = expr_copy(e2);
		tmp = expr_extract_eq_or(&tmp1, &tmp2);
		if (expr_is_no(tmp1)) {
			expr_free(e1);
			e1 = expr_alloc_symbol(&symbol_yes);
			trans_count++;
		}
		expr_free(tmp2);
		expr_free(tmp1);
		expr_free(tmp);
		break;
	default:
		;
	}
#undef e1
#undef e2
}

struct expr *expr_eliminate_dups(struct expr *e)
{
	int oldcount;
	if (!e)
		return e;

	oldcount = trans_count;
	while (1) {
		trans_count = 0;
		switch (e->type) {
		case E_OR: case E_AND:
			expr_eliminate_dups1(e->type, &e, &e);
			expr_eliminate_dups2(e->type, &e, &e);
		default:
			;
		}
		if (!trans_count)
			break;
		e = expr_eliminate_yn(e);
	}
	trans_count = oldcount;
	return e;
}

struct expr *expr_transform(struct expr *e)
{
	struct expr *tmp;

	if (!e)
		return NULL;
	switch (e->type) {
	case E_EQUAL:
	case E_UNEQUAL:
	case E_SYMBOL:
	case E_LIST:
		break;
	default:
		e->left.expr = expr_transform(e->left.expr);
		e->right.expr = expr_transform(e->right.expr);
	}

	switch (e->type) {
	case E_EQUAL:
		if (e->left.sym->type != S_BOOLEAN)
			break;
		if (e->right.sym == &symbol_no) {
			e->type = E_NOT;
			e->left.expr = expr_alloc_symbol(e->left.sym);
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_mod) {
			printf("boolean symbol %s tested for 'm'? test forced to 'n'\n", e->left.sym->name);
			e->type = E_SYMBOL;
			e->left.sym = &symbol_no;
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_yes) {
			e->type = E_SYMBOL;
			e->right.sym = NULL;
			break;
		}
		break;
	case E_UNEQUAL:
		if (e->left.sym->type != S_BOOLEAN)
			break;
		if (e->right.sym == &symbol_no) {
			e->type = E_SYMBOL;
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_mod) {
			printf("boolean symbol %s tested for 'm'? test forced to 'y'\n", e->left.sym->name);
			e->type = E_SYMBOL;
			e->left.sym = &symbol_yes;
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_yes) {
			e->type = E_NOT;
			e->left.expr = expr_alloc_symbol(e->left.sym);
			e->right.sym = NULL;
			break;
		}
		break;
	case E_NOT:
		switch (e->left.expr->type) {
		case E_NOT:
			// !!a -> a
			tmp = e->left.expr->left.expr;
			free(e->left.expr);
			free(e);
			e = tmp;
			e = expr_transform(e);
			break;
		case E_EQUAL:
		case E_UNEQUAL:
			// !a='x' -> a!='x'
			tmp = e->left.expr;
			free(e);
			e = tmp;
			e->type = e->type == E_EQUAL ? E_UNEQUAL : E_EQUAL;
			break;
		case E_OR:
			// !(a || b) -> !a && !b
			tmp = e->left.expr;
			e->type = E_AND;
			e->right.expr = expr_alloc_one(E_NOT, tmp->right.expr);
			tmp->type = E_NOT;
			tmp->right.expr = NULL;
			e = expr_transform(e);
			break;
		case E_AND:
			// !(a && b) -> !a || !b
			tmp = e->left.expr;
			e->type = E_OR;
			e->right.expr = expr_alloc_one(E_NOT, tmp->right.expr);
			tmp->type = E_NOT;
			tmp->right.expr = NULL;
			e = expr_transform(e);
			break;
		case E_SYMBOL:
			if (e->left.expr->left.sym == &symbol_yes) {
				// !'y' -> 'n'
				tmp = e->left.expr;
				free(e);
				e = tmp;
				e->type = E_SYMBOL;
				e->left.sym = &symbol_no;
				break;
			}
			if (e->left.expr->left.sym == &symbol_mod) {
				// !'m' -> 'm'
				tmp = e->left.expr;
				free(e);
				e = tmp;
				e->type = E_SYMBOL;
				e->left.sym = &symbol_mod;
				break;
			}
			if (e->left.expr->left.sym == &symbol_no) {
				// !'n' -> 'y'
				tmp = e->left.expr;
				free(e);
				e = tmp;
				e->type = E_SYMBOL;
				e->left.sym = &symbol_yes;
				break;
			}
			break;
		default:
			;
		}
		break;
	default:
		;
	}
	return e;
}

int expr_contains_symbol(struct expr *dep, struct symbol *sym)
{
	if (!dep)
		return 0;

	switch (dep->type) {
	case E_AND:
	case E_OR:
		return expr_contains_symbol(dep->left.expr, sym) ||
		       expr_contains_symbol(dep->right.expr, sym);
	case E_SYMBOL:
		return dep->left.sym == sym;
	case E_EQUAL:
	case E_UNEQUAL:
		return dep->left.sym == sym ||
		       dep->right.sym == sym;
	case E_NOT:
		return expr_contains_symbol(dep->left.expr, sym);
	default:
		;
	}
	return 0;
}

bool expr_depends_symbol(struct expr *dep, struct symbol *sym)
{
	if (!dep)
		return false;

	switch (dep->type) {
	case E_AND:
		return expr_depends_symbol(dep->left.expr, sym) ||
		       expr_depends_symbol(dep->right.expr, sym);
	case E_SYMBOL:
		return dep->left.sym == sym;
	case E_EQUAL:
		if (dep->left.sym == sym) {
			if (dep->right.sym == &symbol_yes || dep->right.sym == &symbol_mod)
				return true;
		}
		break;
	case E_UNEQUAL:
		if (dep->left.sym == sym) {
			if (dep->right.sym == &symbol_no)
				return true;
		}
		break;
	default:
		;
	}
 	return false;
}

struct expr *expr_extract_eq_and(struct expr **ep1, struct expr **ep2)
{
	struct expr *tmp = NULL;
	expr_extract_eq(E_AND, &tmp, ep1, ep2);
	if (tmp) {
		*ep1 = expr_eliminate_yn(*ep1);
		*ep2 = expr_eliminate_yn(*ep2);
	}
	return tmp;
}

struct expr *expr_extract_eq_or(struct expr **ep1, struct expr **ep2)
{
	struct expr *tmp = NULL;
	expr_extract_eq(E_OR, &tmp, ep1, ep2);
	if (tmp) {
		*ep1 = expr_eliminate_yn(*ep1);
		*ep2 = expr_eliminate_yn(*ep2);
	}
	return tmp;
}

void expr_extract_eq(enum expr_type type, struct expr **ep, struct expr **ep1, struct expr **ep2)
{
#define e1 (*ep1)
#define e2 (*ep2)
	if (e1->type == type) {
		expr_extract_eq(type, ep, &e1->left.expr, &e2);
		expr_extract_eq(type, ep, &e1->right.expr, &e2);
		return;
	}
	if (e2->type == type) {
		expr_extract_eq(type, ep, ep1, &e2->left.expr);
		expr_extract_eq(type, ep, ep1, &e2->right.expr);
		return;
	}
	if (expr_eq(e1, e2)) {
		*ep = *ep ? expr_alloc_two(type, *ep, e1) : e1;
		expr_free(e2);
		if (type == E_AND) {
			e1 = expr_alloc_symbol(&symbol_yes);
			e2 = expr_alloc_symbol(&symbol_yes);
		} else if (type == E_OR) {
			e1 = expr_alloc_symbol(&symbol_no);
			e2 = expr_alloc_symbol(&symbol_no);
		}
	}
#undef e1
#undef e2
}

struct expr *expr_trans_compare(struct expr *e, enum expr_type type, struct symbol *sym)
{
	struct expr *e1, *e2;

	if (!e) {
		e = expr_alloc_symbol(sym);
		if (type == E_UNEQUAL)
			e = expr_alloc_one(E_NOT, e);
		return e;
	}
	switch (e->type) {
	case E_AND:
		e1 = expr_trans_compare(e->left.expr, E_EQUAL, sym);
		e2 = expr_trans_compare(e->right.expr, E_EQUAL, sym);
		if (sym == &symbol_yes)
			e = expr_alloc_two(E_AND, e1, e2);
		if (sym == &symbol_no)
			e = expr_alloc_two(E_OR, e1, e2);
		if (type == E_UNEQUAL)
			e = expr_alloc_one(E_NOT, e);
		return e;
	case E_OR:
		e1 = expr_trans_compare(e->left.expr, E_EQUAL, sym);
		e2 = expr_trans_compare(e->right.expr, E_EQUAL, sym);
		if (sym == &symbol_yes)
			e = expr_alloc_two(E_OR, e1, e2);
		if (sym == &symbol_no)
			e = expr_alloc_two(E_AND, e1, e2);
		if (type == E_UNEQUAL)
			e = expr_alloc_one(E_NOT, e);
		return e;
	case E_NOT:
		return expr_trans_compare(e->left.expr, type == E_EQUAL ? E_UNEQUAL : E_EQUAL, sym);
	case E_UNEQUAL:
	case E_EQUAL:
		if (type == E_EQUAL) {
			if (sym == &symbol_yes)
				return expr_copy(e);
			if (sym == &symbol_mod)
				return expr_alloc_symbol(&symbol_no);
			if (sym == &symbol_no)
				return expr_alloc_one(E_NOT, expr_copy(e));
		} else {
			if (sym == &symbol_yes)
				return expr_alloc_one(E_NOT, expr_copy(e));
			if (sym == &symbol_mod)
				return expr_alloc_symbol(&symbol_yes);
			if (sym == &symbol_no)
				return expr_copy(e);
		}
		break;
	case E_SYMBOL:
		return expr_alloc_comp(type, e->left.sym, sym);
	case E_LIST:
	case E_RANGE:
	case E_NONE:
		/* panic */;
	}
	return NULL;
}

tristate expr_calc_value(struct expr *e)
{
	tristate val1, val2;
	const char *str1, *str2;

	if (!e)
		return yes;

	switch (e->type) {
	case E_SYMBOL:
		sym_calc_value(e->left.sym);
		return e->left.sym->curr.tri;
	case E_AND:
		val1 = expr_calc_value(e->left.expr);
		val2 = expr_calc_value(e->right.expr);
		return EXPR_AND(val1, val2);
	case E_OR:
		val1 = expr_calc_value(e->left.expr);
		val2 = expr_calc_value(e->right.expr);
		return EXPR_OR(val1, val2);
	case E_NOT:
		val1 = expr_calc_value(e->left.expr);
		return EXPR_NOT(val1);
	case E_EQUAL:
		sym_calc_value(e->left.sym);
		sym_calc_value(e->right.sym);
		str1 = sym_get_string_value(e->left.sym);
		str2 = sym_get_string_value(e->right.sym);
		return !strcmp(str1, str2) ? yes : no;
	case E_UNEQUAL:
		sym_calc_value(e->left.sym);
		sym_calc_value(e->right.sym);
		str1 = sym_get_string_value(e->left.sym);
		str2 = sym_get_string_value(e->right.sym);
		return !strcmp(str1, str2) ? no : yes;
	default:
		printf("expr_calc_value: %d?\n", e->type);
		return no;
	}
}

int expr_compare_type(enum expr_type t1, enum expr_type t2)
{
#if 0
	return 1;
#else
	if (t1 == t2)
		return 0;
	switch (t1) {
	case E_EQUAL:
	case E_UNEQUAL:
		if (t2 == E_NOT)
			return 1;
	case E_NOT:
		if (t2 == E_AND)
			return 1;
	case E_AND:
		if (t2 == E_OR)
			return 1;
	case E_OR:
		if (t2 == E_LIST)
			return 1;
	case E_LIST:
		if (t2 == 0)
			return 1;
	default:
		return -1;
	}
	printf("[%dgt%d?]", t1, t2);
	return 0;
#endif
}

void expr_print(struct expr *e, void (*fn)(void *, struct symbol *, const char *), void *data, int prevtoken)
{
	if (!e) {
		fn(data, NULL, "y");
		return;
	}

	if (expr_compare_type(prevtoken, e->type) > 0)
		fn(data, NULL, "(");
	switch (e->type) {
	case E_SYMBOL:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		break;
	case E_NOT:
		fn(data, NULL, "!");
		expr_print(e->left.expr, fn, data, E_NOT);
		break;
	case E_EQUAL:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		fn(data, NULL, "=");
		fn(data, e->right.sym, e->right.sym->name);
		break;
	case E_UNEQUAL:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		fn(data, NULL, "!=");
		fn(data, e->right.sym, e->right.sym->name);
		break;
	case E_OR:
		expr_print(e->left.expr, fn, data, E_OR);
		fn(data, NULL, " || ");
		expr_print(e->right.expr, fn, data, E_OR);
		break;
	case E_AND:
		expr_print(e->left.expr, fn, data, E_AND);
		fn(data, NULL, " && ");
		expr_print(e->right.expr, fn, data, E_AND);
		break;
	case E_LIST:
		fn(data, e->right.sym, e->right.sym->name);
		if (e->left.expr) {
			fn(data, NULL, " ^ ");
			expr_print(e->left.expr, fn, data, E_LIST);
		}
		break;
	case E_RANGE:
		fn(data, NULL, "[");
		fn(data, e->left.sym, e->left.sym->name);
		fn(data, NULL, " ");
		fn(data, e->right.sym, e->right.sym->name);
		fn(data, NULL, "]");
		break;
	default:
	  {
		char buf[32];
		sprintf(buf, "<unknown type %d>", e->type);
		fn(data, NULL, buf);
		break;
	  }
	}
	if (expr_compare_type(prevtoken, e->type) > 0)
		fn(data, NULL, ")");
}

static void expr_print_file_helper(void *data, struct symbol *sym, const char *str)
{
	fwrite(str, strlen(str), 1, data);
}

void expr_fprint(struct expr *e, FILE *out)
{
	expr_print(e, expr_print_file_helper, out, E_NONE);
}

static void expr_print_gstr_helper(void *data, struct symbol *sym, const char *str)
{
	str_append((struct gstr*)data, str);
	if (sym)
		str_printf((struct gstr*)data, " [=%s]", sym_get_string_value(sym));
}

void expr_gstr_print(struct expr *e, struct gstr *gs)
{
	expr_print(e, expr_print_gstr_helper, gs, E_NONE);
}
