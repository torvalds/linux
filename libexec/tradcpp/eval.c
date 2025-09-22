/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

//#define DEBUG
#ifdef DEBUG
#include <stdio.h>
#endif

#include "utils.h"
#include "array.h"
#include "mode.h"
#include "place.h"
#include "eval.h"

/*
 * e ::=
 *    e1 ? e2 : e3
 *    e1 || e2
 *    e1 && e2
 *    e1 | e2
 *    e1 ^ e2
 *    e1 & e2
 *    e1 == e2  |  e1 != e2
 *    e1 < e2   |  e1 <= e2  |  e1 > e2  |  e1 >= e2
 *    e1 << e2  |  e1 >> e2
 *    e1 + e2   |  e1 - e2
 *    e1 * e2   |  e1 / e2   |  e1 % e2
 *    !e  |  ~e  |  -e  |  +e
 *    ( e )  |  ident
 */

enum tokens {
	T_EOF,		/* end of input */
	T_VAL,		/* value */
	T_LPAREN,	/* parens */
	T_RPAREN,
	T_PIPEPIPE,	/* operators */
	T_AMPAMP,
	T_EQEQ,
	T_BANGEQ,
	T_LTEQ,
	T_GTEQ,
	T_LTLT,
	T_GTGT,
	T_QUES,
	T_COLON,
	T_PIPE,
	T_CARET,
	T_AMP,
	T_LT,
	T_GT,
	T_PLUS,
	T_MINUS,
	T_STAR,
	T_SLASH,
	T_PCT,
	T_BANG,
	T_TILDE,
};

static const struct {
	char c1, c2;
	enum tokens tok;
} tokens_2[] = {
	{ '|', '|', T_PIPEPIPE },
	{ '&', '&', T_AMPAMP },
	{ '=', '=', T_EQEQ },
	{ '!', '=', T_BANGEQ },
	{ '<', '=', T_LTEQ },
	{ '>', '=', T_GTEQ },
	{ '<', '<', T_LTLT },
	{ '>', '>', T_GTGT },
};
static const unsigned num_tokens_2 = HOWMANY(tokens_2);

static const struct {
	char c1;
	enum tokens tok;
} tokens_1[] = {
	{ '?', T_QUES },
	{ ':', T_COLON },
	{ '|', T_PIPE },
	{ '^', T_CARET },
	{ '&', T_AMP },
	{ '<', T_LT },
	{ '>', T_GT },
	{ '+', T_PLUS },
	{ '-', T_MINUS },
	{ '*', T_STAR },
	{ '/', T_SLASH },
	{ '%', T_PCT },
	{ '!', T_BANG },
	{ '~', T_TILDE },
	{ '(', T_LPAREN },
	{ ')', T_RPAREN },
};
static const unsigned num_tokens_1 = HOWMANY(tokens_1);

struct token {
	struct place place;
	enum tokens tok;
	int val;
};
DECLARRAY(token, static UNUSED);
DEFARRAY(token, static);

static struct tokenarray tokens;

static
struct token *
token_create(const struct place *p, enum tokens tok, int val)
{
	struct token *t;

	t = domalloc(sizeof(*t));
	t->place = *p;
	t->tok = tok;
	t->val = val;
	return t;
}

static
void
token_destroy(struct token *t)
{
	dofree(t, sizeof(*t));
}

DESTROYALL_ARRAY(token, );

#ifdef DEBUG
static
void
printtokens(void)
{
	unsigned i, num;
	struct token *t;

	fprintf(stderr, "tokens:");
	num = tokenarray_num(&tokens);
	for (i=0; i<num; i++) {
		t = tokenarray_get(&tokens, i);
		switch (t->tok) {
		    case T_EOF: fprintf(stderr, " <eof>"); break;
		    case T_VAL: fprintf(stderr, " %d", t->val); break;
		    case T_LPAREN: fprintf(stderr, " ("); break;
		    case T_RPAREN: fprintf(stderr, " )"); break;
		    case T_PIPEPIPE: fprintf(stderr, " ||"); break;
		    case T_AMPAMP: fprintf(stderr, " &&"); break;
		    case T_EQEQ: fprintf(stderr, " =="); break;
		    case T_BANGEQ: fprintf(stderr, " !="); break;
		    case T_LTEQ: fprintf(stderr, " <="); break;
		    case T_GTEQ: fprintf(stderr, " >="); break;
		    case T_LTLT: fprintf(stderr, " <<"); break;
		    case T_GTGT: fprintf(stderr, " >>"); break;
		    case T_QUES: fprintf(stderr, " ?"); break;
		    case T_COLON: fprintf(stderr, " :"); break;
		    case T_PIPE: fprintf(stderr, " |"); break;
		    case T_CARET: fprintf(stderr, " ^"); break;
		    case T_AMP: fprintf(stderr, " &"); break;
		    case T_LT: fprintf(stderr, " <"); break;
		    case T_GT: fprintf(stderr, " >"); break;
		    case T_PLUS: fprintf(stderr, " +"); break;
		    case T_MINUS: fprintf(stderr, " -"); break;
		    case T_STAR: fprintf(stderr, " *"); break;
		    case T_SLASH: fprintf(stderr, " /"); break;
		    case T_PCT: fprintf(stderr, " %%"); break;
		    case T_BANG: fprintf(stderr, " !"); break;
		    case T_TILDE: fprintf(stderr, " ~"); break;
		}
	}
	fprintf(stderr, "\n");
}
#endif

static
bool
isuop(enum tokens tok)
{
	switch (tok) {
	    case T_BANG:
	    case T_TILDE:
	    case T_MINUS:
	    case T_PLUS:
		return true;
	    default:
		break;
	}
	return false;
}

static
bool
isbop(enum tokens tok)
{
	switch (tok) {
	    case T_EOF:
	    case T_VAL:
	    case T_LPAREN:
	    case T_RPAREN:
	    case T_COLON:
	    case T_QUES:
	    case T_BANG:
	    case T_TILDE:
		return false;
	    default:
		break;
	}
	return true;
}

static
bool
isop(enum tokens tok)
{
	switch (tok) {
	    case T_EOF:
	    case T_VAL:
	    case T_LPAREN:
	    case T_RPAREN:
		return false;
	    default:
		break;
	}
	return true;
}

static
int
getprec(enum tokens tok)
{
	switch (tok) {
	    case T_BANG: case T_TILDE: return -1;
	    case T_STAR: case T_SLASH: case T_PCT: return 0;
	    case T_PLUS: case T_MINUS: return 1;
	    case T_LTLT: case T_GTGT: return 2;
	    case T_LT: case T_LTEQ: case T_GT: case T_GTEQ: return 3;
	    case T_EQEQ: case T_BANGEQ: return 4;
	    case T_AMP: return 5;
	    case T_CARET: return 6;
	    case T_PIPE: return 7;
	    case T_AMPAMP: return 8;
	    case T_PIPEPIPE: return 9;
	    default: break;
	}
	return 10;
}

static
bool
looser(enum tokens t1, enum tokens t2)
{
	return getprec(t1) >= getprec(t2);
}

static
int
eval_uop(enum tokens op, int val)
{
	switch (op) {
	    case T_BANG: val = !val; break;
	    case T_TILDE: val = (int)~(unsigned)val; break;
	    case T_MINUS: val = -val; break;
	    case T_PLUS: break;
	    default: assert(0); break;
	}
	return val;
}

static
int
eval_bop(struct place *p, int lv, enum tokens op, int rv)
{
	unsigned mask;

	switch (op) {
	    case T_PIPEPIPE: return lv || rv;
	    case T_AMPAMP:   return lv && rv;
	    case T_PIPE:     return (int)((unsigned)lv | (unsigned)rv);
	    case T_CARET:    return (int)((unsigned)lv ^ (unsigned)rv);
	    case T_AMP:      return (int)((unsigned)lv & (unsigned)rv);
	    case T_EQEQ:     return lv == rv;
	    case T_BANGEQ:   return lv != rv;
	    case T_LT:       return lv < rv;
	    case T_GT:       return lv > rv;
	    case T_LTEQ:     return lv <= rv;
	    case T_GTEQ:     return lv >= rv;

	    case T_LTLT:
	    case T_GTGT:
		if (rv < 0) {
			complain(p, "Negative bit-shift");
			complain_fail();
			rv = 0;
		}
		if ((unsigned)rv >= CHAR_BIT * sizeof(unsigned)) {
			complain(p, "Bit-shift farther than type width");
			complain_fail();
			rv = 0;
		}
		if (op == T_LTLT) {
			return (int)((unsigned)lv << (unsigned)rv);
		}
		mask = ((unsigned)-1) << (CHAR_BIT * sizeof(unsigned) - rv);
		lv = (int)(((unsigned)lv >> (unsigned)rv) | mask);
		return lv;

	    case T_MINUS:
		if (rv == INT_MIN) {
			if (lv == INT_MIN) {
				return 0;
			}
			lv--;
			rv++;
		}
		rv = -rv;
		/* FALLTHROUGH */
	    case T_PLUS:
		if (rv > 0 && lv > (INT_MAX - rv)) {
			complain(p, "Integer overflow");
			complain_fail();
			return INT_MAX;
		}
		if (rv < 0 && lv < (INT_MIN - rv)) {
			complain(p, "Integer underflow");
			complain_fail();
			return INT_MIN;
		}
		return lv + rv;

	    case T_STAR:
		if (rv == 0) {
			return 0;
		}
		if (rv == 1) {
			return lv;
		}
		if (rv == -1 && lv == INT_MIN) {
			lv++;
			lv = -lv;
			if (lv == INT_MAX) {
				complain(p, "Integer overflow");
				complain_fail();
				return INT_MAX;
			}
			lv++;
			return lv;
		}
		if (lv == INT_MIN && rv < 0) {
			complain(p, "Integer overflow");
			complain_fail();
			return INT_MAX;
		}
		if (lv == INT_MIN && rv > 0) {
			complain(p, "Integer underflow");
			complain_fail();
			return INT_MIN;
		}
		if (rv < 0) {
			rv = -rv;
			lv = -lv;
		}
		if (lv > 0 && lv > INT_MAX / rv) {
			complain(p, "Integer overflow");
			complain_fail();
			return INT_MAX;
		}
		if (lv < 0 && lv < INT_MIN / rv) {
			complain(p, "Integer underflow");
			complain_fail();
			return INT_MIN;
		}
		return lv * rv;

	    case T_SLASH:
		if (rv == 0) {
			complain(p, "Division by zero");
			complain_fail();
			return 0;
		}
		return lv / rv;

	    case T_PCT:
		if (rv == 0) {
			complain(p, "Modulus by zero");
			complain_fail();
			return 0;
		}
		return lv % rv;

	    default: assert(0); break;
	}
	return 0;
}

static
void
tryreduce(void)
{
	unsigned num;
	struct token *t1, *t2, *t3, *t4, *t5, *t6;

	while (1) {
#ifdef DEBUG
		printtokens();
#endif
		num = tokenarray_num(&tokens);
		t1 = (num >= 1) ? tokenarray_get(&tokens, num-1) : NULL;
		t2 = (num >= 2) ? tokenarray_get(&tokens, num-2) : NULL;
		t3 = (num >= 3) ? tokenarray_get(&tokens, num-3) : NULL;

		if (num >= 3 &&
		    t3->tok == T_LPAREN &&
		    t2->tok == T_VAL &&
		    t1->tok == T_RPAREN) {
			/* (x) -> x */
			t2->place = t3->place;
			token_destroy(t1);
			token_destroy(t3);
			tokenarray_remove(&tokens, num-1);
			tokenarray_remove(&tokens, num-3);
			continue;
		}

		if (num >= 2 &&
		    (num == 2 || isop(t3->tok) || t3->tok == T_LPAREN) &&
		    isuop(t2->tok) &&
		    t1->tok == T_VAL) {
			/* unary operator */
			t1->val = eval_uop(t2->tok, t1->val);
			t1->place = t2->place;
			token_destroy(t2);
			tokenarray_remove(&tokens, num-2);
			continue;
		}
		if (num >= 2 &&
		    (num == 2 || isop(t3->tok) || t3->tok == T_LPAREN) &&
		    t2->tok != T_LPAREN && t2->tok != T_VAL &&
		    t1->tok == T_VAL) {
			complain(&t2->place, "Invalid unary operator");
			complain_fail();
			token_destroy(t2);
			tokenarray_remove(&tokens, num-2);
			continue;
		}

			
		t4 = (num >= 4) ? tokenarray_get(&tokens, num-4) : NULL;

		if (num >= 4 &&
		    t4->tok == T_VAL &&
		    isbop(t3->tok) &&
		    t2->tok == T_VAL) {
			/* binary operator */
			if (looser(t1->tok, t3->tok)) {
				t4->val = eval_bop(&t3->place,
						   t4->val, t3->tok, t2->val);
				token_destroy(t2);
				token_destroy(t3);
				tokenarray_remove(&tokens, num-2);
				tokenarray_remove(&tokens, num-3);
				continue;
			}
			break;
		}

		t5 = (num >= 5) ? tokenarray_get(&tokens, num-5) : NULL;
		t6 = (num >= 6) ? tokenarray_get(&tokens, num-6) : NULL;

		if (num >= 6 &&
		    t6->tok == T_VAL &&
		    t5->tok == T_QUES &&
		    t4->tok == T_VAL &&
		    t3->tok == T_COLON &&
		    t2->tok == T_VAL &&
		    !isop(t1->tok)) {
			/* conditional expression */
			t6->val = t6->val ? t4->val : t2->val;
			token_destroy(t2);
			token_destroy(t3);
			token_destroy(t4);
			token_destroy(t5);
			tokenarray_remove(&tokens, num-2);
			tokenarray_remove(&tokens, num-3);
			tokenarray_remove(&tokens, num-4);
			tokenarray_remove(&tokens, num-5);
			continue;
		}

		if (num >= 2 &&
		    t2->tok == T_LPAREN &&
		    t1->tok == T_RPAREN) {
			complain(&t1->place, "Value expected within ()");
			complain_fail();
			t1->tok = T_VAL;
			t1->val = 0;
			token_destroy(t1);
			tokenarray_remove(&tokens, num-1);
			continue;
		}

		if (num >= 2 &&
		    t2->tok == T_VAL &&
		    t1->tok == T_VAL) {
			complain(&t1->place, "Operator expected");
			complain_fail();
			token_destroy(t1);
			tokenarray_remove(&tokens, num-1);
			continue;
		}

		if (num >= 2 &&
		    isop(t2->tok) &&
		    t1->tok == T_EOF) {
			complain(&t1->place, "Value expected after operator");
			complain_fail();
			token_destroy(t2);
			tokenarray_remove(&tokens, num-2);
			continue;
		}

		if (num == 2 &&
		    t2->tok == T_VAL &&
		    t1->tok == T_RPAREN) {
			complain(&t1->place, "Excess right parenthesis");
			complain_fail();
			token_destroy(t1);
			tokenarray_remove(&tokens, num-1);
			continue;
		}

		if (num == 3 &&
		    t3->tok == T_LPAREN &&
		    t2->tok == T_VAL &&
		    t1->tok == T_EOF) {
			complain(&t1->place, "Unclosed left parenthesis");
			complain_fail();
			token_destroy(t3);
			tokenarray_remove(&tokens, num-3);
			continue;
		}

		if (num == 2 &&
		    t2->tok == T_VAL &&
		    t1->tok == T_EOF) {
			/* accepting state */
			break;
		}

		if (num >= 1 &&
		    t1->tok == T_EOF) {
			/* any other configuration at eof is an error */
			complain(&t1->place, "Parse error");
			complain_fail();
			break;
		}

		/* otherwise, wait for more input */
		break;
	}
}

static
void
token(struct place *p, enum tokens tok, int val)
{
	struct token *t;

	t = token_create(p, tok, val);

	tokenarray_add(&tokens, t, NULL);
	tryreduce();
}

static
int
wordval(struct place *p, char *word)
{
	unsigned long val;
	char *t;

	if (word[0] >= '0' && word[0] <= '9') {
		errno = 0;
		val = strtoul(word, &t, 0);
		if (errno) {
			complain(p, "Invalid integer constant");
			complain_fail();
			return 0;
		}
		while (*t == 'U' || *t == 'L') {
			t++;
		}
		if (*t != '\0') {
			complain(p, "Trailing garbage after integer constant");
			complain_fail();
			return 0;
		}
		if (val > INT_MAX) {
			complain(p, "Integer constant too large");
			complain_fail();
			return INT_MAX;
		}
		return val;
	}

	/* if it's a symbol, warn and substitute 0. */
	if (warns.undef) {
		complain(p, "Warning: value of undefined symbol %s is 0",
			 word);
		if (mode.werror) {
			complain_fail();
		}
	}
	debuglog(p, "Undefined symbol %s; substituting 0", word);
	return 0;
}

static
bool
check_word(struct place *p, char *expr, size_t pos, size_t *len_ret)
{
	size_t len;
	int val;
	char tmp;

	if (!strchr(alnum, expr[pos])) {
		return false;
	}
	len = strspn(expr + pos, alnum);
	tmp = expr[pos + len];
	expr[pos + len] = '\0';
	val = wordval(p, expr + pos);
	expr[pos + len] = tmp;
	token(p, T_VAL, val);
	*len_ret = len;
	return true;
}

static
bool
check_tokens_2(struct place *p, char *expr, size_t pos)
{
	unsigned i;

	for (i=0; i<num_tokens_2; i++) {
		if (expr[pos] == tokens_2[i].c1 &&
		    expr[pos+1] == tokens_2[i].c2) {
			token(p, tokens_2[i].tok, 0);
			return true;
		}
	}
	return false;
}

static
bool
check_tokens_1(struct place *p, char *expr, size_t pos)
{
	unsigned i;

	for (i=0; i<num_tokens_1; i++) {
		if (expr[pos] == tokens_1[i].c1) {
			token(p, tokens_1[i].tok, 0);
			return true;
		}
	}
	return false;
}

static
void
tokenize(struct place *p, char *expr)
{
	size_t pos, len;

	pos = 0;
	while (expr[pos] != '\0') {
		len = strspn(expr+pos, ws);
		pos += len;
		place_addcolumns(p, len);
		/* trailing whitespace is supposed to have been pruned */
		assert(expr[pos] != '\0');
		if (check_word(p, expr, pos, &len)) {
			pos += len;
			place_addcolumns(p, len);
			continue;
		}
		if (check_tokens_2(p, expr, pos)) {
			pos += 2;
			place_addcolumns(p, 2);
			continue;
		}
		if (check_tokens_1(p, expr, pos)) {
			pos++;
			place_addcolumns(p, 1);
			continue;
		}
		complain(p, "Invalid character %u in #if-expression",
			 (unsigned char)expr[pos]);
		complain_fail();
		pos++;
		place_addcolumns(p, 1);
	}
	token(p, T_EOF, 0);
}

bool
eval(struct place *p, char *expr)
{
	struct token *t1, *t2;
	unsigned num;
	bool result;

#ifdef DEBUG
	fprintf(stderr, "eval: %s\n", expr);
#endif
	debuglog(p, "eval: %s", expr);

	tokenarray_init(&tokens);
	tokenize(p, expr);

	result = false;
	num = tokenarray_num(&tokens);
	if (num == 2) {
		t1 = tokenarray_get(&tokens, num-1);
		t2 = tokenarray_get(&tokens, num-2);
		if (t2->tok == T_VAL &&
		    t1->tok == T_EOF) {
			result = t2->val != 0;
		}
	}

	tokenarray_destroyall(&tokens);
	tokenarray_cleanup(&tokens);
	return result;
}
