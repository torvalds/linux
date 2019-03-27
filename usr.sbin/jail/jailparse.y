%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 James Gritton
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <string.h>

#include "jailp.h"

#ifdef DEBUG
#define YYDEBUG 1
#endif
%}

%union {
	struct cfjail		*j;
	struct cfparams		*pp;
	struct cfparam		*p;
	struct cfstrings	*ss;
	struct cfstring		*s;
	char			*cs;
}

%token      PLEQ
%token <cs> STR STR1 VAR VAR1

%type <j>  jail
%type <pp> param_l
%type <p>  param name
%type <ss> value
%type <s>  string

%%

/*
 * A config file is a series of jails (containing parameters) and jail-less
 * parameters which really belong to a global pseudo-jail.
 */
conf	:
	;
	| conf jail
	;
	| conf param ';'
	{
		struct cfjail *j;

		j = TAILQ_LAST(&cfjails, cfjails);
		if (!j || strcmp(j->name, "*")) {
			j = add_jail();
			j->name = estrdup("*");
		}
		TAILQ_INSERT_TAIL(&j->params, $2, tq);
	}
	| conf ';'

jail	: STR '{' param_l '}'
	{
		$$ = add_jail();
		$$->name = $1;
		TAILQ_CONCAT(&$$->params, $3, tq);
		free($3);
	}
	;

param_l	:
	{
		$$ = emalloc(sizeof(struct cfparams));
		TAILQ_INIT($$);
	}
	| param_l param ';'
	{
		$$ = $1;
		TAILQ_INSERT_TAIL($$, $2, tq);
	}
	| param_l ';'
	;

/*
 * Parameters have a name and an optional list of value strings,
 * which may have "+=" or "=" preceding them.
 */
param	: name
	{
		$$ = $1;
	}
	| name '=' value
	{
		$$ = $1;
		TAILQ_CONCAT(&$$->val, $3, tq);
		free($3);
	}
	| name PLEQ value
	{
		$$ = $1;
		TAILQ_CONCAT(&$$->val, $3, tq);
		$$->flags |= PF_APPEND;
		free($3);
	}
	| name value
	{
		$$ = $1;
		TAILQ_CONCAT(&$$->val, $2, tq);
		free($2);
	}
	| error
	{
	}
	;

/*
 * A parameter has a fixed name.  A variable definition looks just like a
 * parameter except that the name is a variable.
 */
name	: STR
	{
		$$ = emalloc(sizeof(struct cfparam));
		$$->name = $1;
		TAILQ_INIT(&$$->val);
		$$->flags = 0;
	}
	| VAR
	{
		$$ = emalloc(sizeof(struct cfparam));
		$$->name = $1;
		TAILQ_INIT(&$$->val);
		$$->flags = PF_VAR;
	}
	;

value	: string
	{
		$$ = emalloc(sizeof(struct cfstrings));
		TAILQ_INIT($$);
		TAILQ_INSERT_TAIL($$, $1, tq);
	}
	| value ',' string
	{
		$$ = $1;
		TAILQ_INSERT_TAIL($$, $3, tq);
	}
	;

/*
 * Strings may be passed in pieces, because of quoting and/or variable
 * interpolation.  Reassemble them into a single string.
 */
string	: STR
	{
		$$ = emalloc(sizeof(struct cfstring));
		$$->s = $1;
		$$->len = strlen($1);
		STAILQ_INIT(&$$->vars);
	}
	| VAR
	{
		struct cfvar *v;

		$$ = emalloc(sizeof(struct cfstring));
		$$->s = estrdup("");
		$$->len = 0;
		STAILQ_INIT(&$$->vars);
		v = emalloc(sizeof(struct cfvar));
		v->name = $1;
		v->pos = 0;
		STAILQ_INSERT_TAIL(&$$->vars, v, tq);
	}
	| string STR1
	{
		size_t len1;

		$$ = $1;
		len1 = strlen($2);
		$$->s = erealloc($$->s, $$->len + len1 + 1);
		strcpy($$->s + $$->len, $2);
		free($2);
		$$->len += len1;
	}
	| string VAR1
	{
		struct cfvar *v;

		$$ = $1;
		v = emalloc(sizeof(struct cfvar));
		v->name = $2;
		v->pos = $$->len;
		STAILQ_INSERT_TAIL(&$$->vars, v, tq);
	}
	;

%%
