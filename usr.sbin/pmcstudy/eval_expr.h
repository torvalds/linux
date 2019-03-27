#ifndef __eval_expr_h__
#define __eval_expr_h__
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
__FBSDID("$FreeBSD$");

enum exptype {
	TYPE_OP_PLUS,
	TYPE_OP_MINUS,
	TYPE_OP_MULT,
	TYPE_OP_DIVIDE,
	TYPE_PARN_OPEN,
	TYPE_PARN_CLOSE,
	TYPE_VALUE_CON,
	TYPE_VALUE_PMC
};

#define STATE_UNSET  0		/* We have no setting yet in value */
#define STATE_FILLED 1		/* We have filled in value */

struct expression {
	struct expression *next;	/* Next in expression. */
	struct expression *prev;	/* Prev in expression. */
	double value;			/* If there is a value to set */
	enum exptype type;			/* What is it */
	uint8_t state;			/* Current state if value type */
	char name[252];			/* If a PMC whats the name, con value*/
};

struct expression *parse_expression(char *str);
double run_expr(struct expression *exp, int initial_call, struct expression **lastone);
void print_exp(struct expression *exp);
#endif
