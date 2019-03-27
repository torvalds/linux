/*-
 * Copyright (c) 2015 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "eval_expr.h"
__FBSDID("$FreeBSD$");

static struct expression *
alloc_and_hook_expr(struct expression **exp_p, struct expression **last_p)
{
	struct expression *ex, *at;

	ex = malloc(sizeof(struct expression));
	if (ex == NULL) {
		printf("Out of memory in exp allocation\n");
		exit(-2);
	}
	memset(ex, 0, sizeof(struct expression));
	if (*exp_p == NULL) {
		*exp_p = ex;
	}
	at = *last_p;
	if (at == NULL) {
		/* First one, its last */
		*last_p = ex;
	} else {
		/* Chain it to the end and update last */
		at->next = ex;
		ex->prev = at;
		*last_p = ex;
	}
	return (ex);
}


static int
validate_expr(struct expression *exp, int val1_is_set, int op_is_set, int val2_is_set, 
	      int *op_cnt)
{
	int val1, op, val2;
	int open_cnt;
	val1 = op = val2 = 0;
	if (val1_is_set) {
		val1 = 1;
	}
	if (op_is_set) {
		op = 1;
	}
	if (val2_is_set) {
		val2 = 1;
	}
	open_cnt = *op_cnt;
	if (exp == NULL) {
		/* End of the road */
		if (val1 && op && val2 && (open_cnt == 0)) {
			return(0);
		} else {
			return(1);
		}
	}
	switch(exp->type) {
	case TYPE_OP_PLUS:
	case TYPE_OP_MINUS:
	case TYPE_OP_MULT:
	case TYPE_OP_DIVIDE:
		if (val1 && op && val2) {
			/* We are at x + y + 
			 * collapse back to val/op
			 */
			val1 = 1;
			op = 1;
			val2 = 0;
		} else if ((op == 0) && (val1)) {
			op = 1;
		} else {
			printf("Op but no val1 set\n");
			return(-1);
		}
		break;
	case TYPE_PARN_OPEN:
		if (exp->next == NULL) {
			printf("NULL after open paren\n");
			exit(-1);
		}
		if ((exp->next->type == TYPE_OP_PLUS) ||
		    (exp->next->type == TYPE_OP_MINUS) ||
		    (exp->next->type == TYPE_OP_DIVIDE) ||
		    (exp->next->type == TYPE_OP_MULT)) {
			printf("'( OP' -- not allowed\n");
			return(-1);
		}
		if (val1 && (op == 0)) {
			printf("'Val (' -- not allowed\n");
			return(-1);
		}
		if (val1 && op && val2) {
			printf("'Val OP Val (' -- not allowed\n");
			return(-1);
		}
		open_cnt++;
		*op_cnt = open_cnt;
		if (val1) {
			if (validate_expr(exp->next, 0, 0, 0, op_cnt) == 0) {
				val2 = 1;
			} else {
				return(-1);
			}
		} else {
			return(validate_expr(exp->next, 0, 0, 0, op_cnt));
		}
		break;
	case TYPE_PARN_CLOSE:
		open_cnt--;
		*op_cnt = open_cnt;
		if (val1 && op && val2) {
			return(0);
		} else {
			printf("Found close paren and not complete\n");
			return(-1);
		}
		break;
	case TYPE_VALUE_CON:
	case TYPE_VALUE_PMC:
		if (val1 == 0) {
			val1 = 1;
		} else if (val1 && op) {
			val2 = 1;
		} else {
			printf("val1 set, val2 about to be set op empty\n");
			return(-1);
		}
		break;
	default:
		printf("unknown type %d\n", exp->type);
		exit(-5);
		break;
	}
	return(validate_expr(exp->next, val1, op, val2, op_cnt));
}

void
print_exp(struct expression *exp)
{
	if (exp == NULL) {
		printf("\n");
		return;
	}
	switch(exp->type) {
	case TYPE_OP_PLUS:
		printf(" + ");
		break;
	case TYPE_OP_MINUS:
		printf(" - ");
		break;
	case TYPE_OP_MULT:
		printf(" * ");
		break;
	case TYPE_OP_DIVIDE:
		printf(" / ");
		break;
	case TYPE_PARN_OPEN:
		printf(" ( ");
		break;
	case TYPE_PARN_CLOSE:
		printf(" ) ");
		break;
	case TYPE_VALUE_CON:
		printf("%f", exp->value);
		break;
	case TYPE_VALUE_PMC:
		printf("%s", exp->name);
		break;
	default:
		printf("Unknown op %d\n", exp->type);
		break;
	}
	print_exp(exp->next);
}

static void
walk_back_and_insert_paren(struct expression **beg, struct expression *frm)
{
	struct expression *at, *ex;

	/* Setup our new open paren */
	ex = malloc(sizeof(struct expression));
	if (ex == NULL) {
		printf("Out of memory in exp allocation\n");
		exit(-2);
	}
	memset(ex, 0, sizeof(struct expression));
	ex->type = TYPE_PARN_OPEN;
	/* Now lets place it */
	at = frm->prev;
	if (at == *beg) {
		/* We are inserting at the head of the list */
	in_beg:
		ex->next = at;
		at->prev = ex;
		*beg = ex;
		return;
	} else if ((at->type == TYPE_VALUE_CON) ||
	    (at->type == TYPE_VALUE_PMC)) {
		/* Simple case we have a value in the previous position */
	in_mid:
		ex->prev = at->prev;
		ex->prev->next = ex;
		ex->next = at;
		at->prev = ex;
		return;
	} else if (at->type == TYPE_PARN_CLOSE) {
		/* Skip through until we reach beg or all ( closes */
		int par_cnt=1;

		at = at->prev;
		while(par_cnt) {
			if (at->type == TYPE_PARN_CLOSE) {
				par_cnt++;
			} else if (at->type == TYPE_PARN_OPEN) {
				par_cnt--;
				if (par_cnt == 0) {
					break;
				}
			}
			at = at->prev;
		}
		if (at == *beg) {
			/* At beginning we insert */
			goto in_beg;
		} else {
			goto in_mid;
		}
	} else {
		printf("%s:Unexpected type:%d?\n", 
		       __FUNCTION__, at->type);
		exit(-1);
	}
}

static void
walk_fwd_and_insert_paren(struct expression *frm, struct expression **added)
{
	struct expression *at, *ex;
	/* Setup our new close paren */
	ex = malloc(sizeof(struct expression));
	if (ex == NULL) {
		printf("Out of memory in exp allocation\n");
		exit(-2);
	}
	memset(ex, 0, sizeof(struct expression));
	ex->type = TYPE_PARN_CLOSE;
	*added = ex;
	/* Now lets place it */
	at = frm->next;
	if ((at->type == TYPE_VALUE_CON) ||
	    (at->type == TYPE_VALUE_PMC)) {
		/* Simple case we have a value in the previous position */
	insertit:
		ex->next = at->next;
		ex->prev = at;
		at->next = ex;
		return;
	} else if (at->type == TYPE_PARN_OPEN) {
		int par_cnt=1;
		at = at->next;
		while(par_cnt) {
			if (at->type == TYPE_PARN_OPEN) {
				par_cnt++;
			} else if (at->type == TYPE_PARN_CLOSE) {
				par_cnt--;
				if (par_cnt == 0) {
					break;
				}
			}
			at = at->next;
		}
		goto insertit;
	} else {
		printf("%s:Unexpected type:%d?\n", 
		       __FUNCTION__,
		       at->type);
		exit(-1);
	}
}


static void
add_precendence(struct expression **beg, struct expression *start, struct expression *end)
{
	/* 
	 * Between start and end add () around any * or /. This
	 * is quite tricky since if there is a () set inside the
	 * list we need to skip over everything in the ()'s considering
	 * that just a value.
	 */
	struct expression *at, *newone;
	int open_cnt;

	at = start;
	open_cnt = 0;
	while(at != end) {
		if (at->type == TYPE_PARN_OPEN) {
			open_cnt++;
		}
		if (at->type == TYPE_PARN_CLOSE) {
			open_cnt--;
		}
		if (open_cnt == 0) {
			if ((at->type == TYPE_OP_MULT) ||
			    (at->type == TYPE_OP_DIVIDE)) {
				walk_back_and_insert_paren(beg, at);
				walk_fwd_and_insert_paren(at, &newone);
				at = newone->next;
				continue;
			}
		}
		at = at->next;
	}
	
}

static void
set_math_precidence(struct expression **beg, struct expression *exp, struct expression **stopped)
{
	struct expression *at, *start, *end;
	int cnt_lower, cnt_upper;
	/* 
	 * Walk through and set any math precedence to 
	 * get proper precedence we insert () around * / over + -
	 */
	end = NULL;
	start = at = exp;
	cnt_lower = cnt_upper = 0;
	while(at) { 
		if (at->type == TYPE_PARN_CLOSE) {
			/* Done with that paren */
			if (stopped) {
				*stopped = at;
			}
			if (cnt_lower && cnt_upper) {
				/* We have a mixed set ... add precedence between start/end */
				add_precendence(beg, start, end);
			}
			return;
		}
		if (at->type == TYPE_PARN_OPEN) {
			set_math_precidence(beg, at->next, &end);
			at = end;
			continue;
		} else if ((at->type == TYPE_OP_PLUS) ||
			   (at->type == TYPE_OP_MINUS)) {
			cnt_lower++;
		} else if ((at->type == TYPE_OP_DIVIDE) ||
			   (at->type == TYPE_OP_MULT)) {
			cnt_upper++;
		}
		at = at->next;
	}
	if (cnt_lower && cnt_upper) {
		add_precendence(beg, start, NULL);
	}
}

extern char **valid_pmcs;
extern int valid_pmc_cnt;

static void
pmc_name_set(struct expression *at)
{
	int i, idx, fnd;

	if (at->name[0] == '%') {
		/* Special number after $ gives index */
		idx = strtol(&at->name[1], NULL, 0);
		if (idx >= valid_pmc_cnt) {
			printf("Unknown PMC %s -- largest we have is $%d -- can't run your expression\n",
			       at->name, valid_pmc_cnt);
			exit(-1);
		}
		strcpy(at->name, valid_pmcs[idx]);
	} else {
		for(i=0, fnd=0; i<valid_pmc_cnt; i++) {
			if (strcmp(valid_pmcs[i], at->name) == 0) {
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
			printf("PMC %s does not exist on this machine -- can't run your expression\n",
			       at->name);
			exit(-1);
		}
	}
}

struct expression *
parse_expression(char *str)
{
	struct expression *exp=NULL, *last=NULL, *at;
	int open_par, close_par;
	int op_cnt=0;
	size_t siz, i, x;
	/* 
	 * Walk through a string expression and convert
	 * it to a linked list of actions. We do this by:
	 * a) Counting the open/close paren's, there must
	 *    be a matching number.
	 * b) If we have balanced paren's then create a linked list
	 *    of the operators, then we validate that expression further.
	 * c) Validating that we have:
	 *      val OP val <or>
	 *      val OP (  <and>
	 *    inside every paran you have a:
	 *      val OP val <or>
	 *      val OP (   <recursively>
	 * d) A final optional step (not implemented yet) would be
	 *    to insert the mathematical precedence paran's. For
	 *    the start we will just do the left to right evaluation and
	 *    then later we can add this guy to add paran's to make it
	 *    mathimatically correct... i.e instead of 1 + 2 * 3 we
	 *    would translate it into 1 + ( 2 * 3).
	 */
	open_par = close_par = 0;
	siz = strlen(str);
	/* No trailing newline please */
	if (str[(siz-1)] == '\n') {
		str[(siz-1)] = 0;
		siz--;
	}
	for(i=0; i<siz; i++) {
		if (str[i] == '(') {
			open_par++;
		} else if (str[i] == ')') {
			close_par++;
		}
	}
	if (open_par != close_par) {
		printf("Invalid expression '%s' %d open paren's and %d close?\n",
		       str, open_par, close_par);
		exit(-1);
	}
	for(i=0; i<siz; i++) {
		if (str[i] == '(') {
			at = alloc_and_hook_expr(&exp, &last);
			at->type = TYPE_PARN_OPEN;
		} else if (str[i] == ')') {
			at = alloc_and_hook_expr(&exp, &last);
			at->type = TYPE_PARN_CLOSE;
		} else if (str[i] == ' ') {
			/* Extra blank */
			continue;
		} else if (str[i] == '\t') {
			/* Extra tab */
			continue;
		} else if (str[i] == '+') {
			at = alloc_and_hook_expr(&exp, &last);
			at->type = TYPE_OP_PLUS;
		} else if (str[i] == '-') {
			at = alloc_and_hook_expr(&exp, &last);
			at->type = TYPE_OP_MINUS;
		} else if (str[i] == '/') {
			at = alloc_and_hook_expr(&exp, &last);
			at->type = TYPE_OP_DIVIDE;
		} else if (str[i] == '*') {
			at = alloc_and_hook_expr(&exp, &last);
			at->type = TYPE_OP_MULT;
		} else {
			/* Its a value or PMC constant */
			at = alloc_and_hook_expr(&exp, &last);
			if (isdigit(str[i]) || (str[i] == '.')) {
				at->type = TYPE_VALUE_CON;
			} else {
				at->type = TYPE_VALUE_PMC;
			}
			x = 0;
			while ((str[i] != ' ') && 
			       (str[i] != '\t') && 
			       (str[i] != 0) && 
			       (str[i] != ')') &&
			       (str[i] != '(')) {
				/* We collect the constant until a space or tab */
				at->name[x] = str[i];
				i++;
				x++;
				if (x >=(sizeof(at->name)-1)) {
					printf("Value/Constant too long %d max:%d\n",
					       (int)x, (int)(sizeof(at->name)-1));
					exit(-3);
				}
			}
			if (str[i] != 0) {
				/* Need to back up and see the last char since
				 * the for will increment the loop.
				 */
				i--;
			}
			/* Now we have pulled the string, set it up */
			if (at->type == TYPE_VALUE_CON) {
				at->state = STATE_FILLED;
				at->value = strtod(at->name, NULL);
			} else {
				pmc_name_set(at);
			}
		}
	}
	/* Now lets validate its a workable expression */
	if (validate_expr(exp, 0, 0, 0, &op_cnt)) {
		printf("Invalid expression\n");
		exit(-4);
	}
	set_math_precidence(&exp, exp, NULL);
	return (exp);
}



static struct expression *
gather_exp_to_paren_close(struct expression *exp, double *val_fill)
{
	/*
	 * I have been given ( ???
	 * so I could see either
	 * (
	 * or
	 * Val Op
	 *
	 */
	struct expression *lastproc;
	double val;

	if (exp->type == TYPE_PARN_OPEN) {
		lastproc = gather_exp_to_paren_close(exp->next, &val);
		*val_fill = val;
	} else {
		*val_fill = run_expr(exp, 0, &lastproc);
	}
	return(lastproc);
}


double
run_expr(struct expression *exp, int initial_call, struct expression **lastone)
{
	/* 
	 * We expect to find either
	 * a) A Open Paren
	 * or
	 * b) Val-> Op -> Val
	 * or
	 * c) Val-> Op -> Open Paren
	 */
	double val1, val2, res;
	struct expression *op, *other_half, *rest;
	
	if (exp->type == TYPE_PARN_OPEN) {
		op = gather_exp_to_paren_close(exp->next, &val1);
	} else if(exp->type == TYPE_VALUE_CON) {
		val1 = exp->value;
		op = exp->next;
	} else if (exp->type ==  TYPE_VALUE_PMC) {
		val1 = exp->value;
		op = exp->next;
	} else {
		printf("Illegal value in %s huh?\n", __FUNCTION__);
		exit(-1);
	}
	if (op == NULL) {
		return (val1);
	}
more_to_do:
	other_half = op->next;
	if (other_half->type == TYPE_PARN_OPEN) {
		rest = gather_exp_to_paren_close(other_half->next, &val2);
	} else if(other_half->type == TYPE_VALUE_CON) {
		val2 = other_half->value;
		rest = other_half->next;
	} else if (other_half->type ==  TYPE_VALUE_PMC) {
		val2 = other_half->value;
		rest = other_half->next;
	} else {
		printf("Illegal2 value in %s huh?\n", __FUNCTION__);
		exit(-1);
	}
	switch(op->type) {
	case TYPE_OP_PLUS:
		res = val1 + val2;
		break;
	case TYPE_OP_MINUS:		
		res = val1 - val2;
		break;
	case TYPE_OP_MULT:
		res = val1 * val2;
		break;
	case TYPE_OP_DIVIDE:
		if (val2 != 0.0)
			res = val1 / val2;
		else {
			printf("Division by zero averted\n");
			res = 1.0;
		}	
		break;
	default:
		printf("Op is not an operator -- its %d\n",
		       op->type);
		exit(-1);
		break;
	}
	if (rest == NULL) {
		if (lastone) {
			*lastone = NULL;
		}
		return (res);
	}
	if ((rest->type == TYPE_PARN_CLOSE) && (initial_call == 0)) {
		if (lastone) {
			*lastone = rest->next;
		}
		return(res);
	}
	/* There is more, as in
	 * a + b + c
	 * where we just did a + b
	 * so now it becomes val1 is set to res and
	 * we need to proceed with the rest of it.
	 */
	val1 = res;
	op = rest;
	if ((op->type != TYPE_OP_PLUS) &&
	    (op->type != TYPE_OP_MULT) &&
	    (op->type != TYPE_OP_MINUS) &&
	    (op->type != TYPE_OP_DIVIDE)) {
		printf("%s ending on type:%d not an op??\n", __FUNCTION__, op->type);
		return(res);
	}
	if (op)
		goto more_to_do;
	return (res);
}

#ifdef STAND_ALONE_TESTING

static double
calc_expr(struct expression *exp)
{
	struct expression *at;
	double xx;

	/* First clear PMC's setting */
	for(at = exp; at != NULL; at = at->next) {
		if (at->type == TYPE_VALUE_PMC) {
			at->state = STATE_UNSET;
		}
	}
	/* Now for all pmc's make up values .. here is where I would pull them */
	for(at = exp; at != NULL; at = at->next) {
		if (at->type == TYPE_VALUE_PMC) {
			at->value = (random() * 1.0);
			at->state = STATE_FILLED;
			if (at->value == 0.0) {
				/* So we don't have div by 0 */
				at->value = 1.0;
			}
		}
	}
	/* Now lets calculate the expression */
	print_exp(exp);
	xx = run_expr(exp, 1, NULL);
	printf("Answer is %f\n", xx);
	return(xx);
}


int 
main(int argc, char **argv) 
{
	struct expression *exp;
	if (argc < 2) {
		printf("Use %s expression\n", argv[0]);
		return(-1);
	}
	exp = parse_expression(argv[1]);
	printf("Now the calc\n");
	calc_expr(exp);
	return(0);
}

#endif
