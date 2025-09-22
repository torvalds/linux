%{
/*	$OpenBSD: aicasm_macro_gram.y,v 1.3 2006/12/23 21:08:01 krw Exp $	*/
/*	$NetBSD: aicasm_macro_gram.y,v 1.1 2003/04/19 19:26:11 fvdl Exp $	*/

/*
 * Sub-parser for macro invocation in the Aic7xxx SCSI
 * Host adapter sequencer assembler.
 *
 * Copyright (c) 2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/aic7xxx/aicasm/aicasm_macro_gram.y,v 1.2 2002/08/31 06:39:40 gibbs Exp $
 */

#include <sys/types.h>

#include <inttypes.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#ifdef __linux__
#include "../queue.h"
#else
#include <sys/queue.h>
#endif

#include "aicasm.h"
#include "aicasm_symbol.h"
#include "aicasm_insformat.h"

static symbol_t *macro_symbol;

static void add_macro_arg(const char *argtext, int position);

%}

%union {
	int		value;
	char		*str;
	symbol_t	*sym;
}


%token <str> T_ARG

%token <sym> T_SYMBOL

%type <value> macro_arglist

%%

macrocall:
	T_SYMBOL '('
	{
		macro_symbol = $1;
	}
	macro_arglist ')'
	{
		if (macro_symbol->info.macroinfo->narg != $4) {
			printf("Narg == %d", macro_symbol->info.macroinfo->narg);
			stop("Too few arguments for macro invocation",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		macro_symbol = NULL;
		YYACCEPT;
	}
;

macro_arglist:
	{
		/* Macros can take 0 arguments */
		$$ = 0;
	}
|	T_ARG
	{
		$$ = 1;
		add_macro_arg($1, 1);
	}
|	macro_arglist ',' T_ARG
	{
		if ($1 == 0) {
			stop("Comma without preceding argument in arg list",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		$$ = $1 + 1;
		add_macro_arg($3, $$);
	}
;

%%

static void
add_macro_arg(const char *argtext, int argnum)
{
	struct macro_arg *marg;
	int i;

	if (macro_symbol == NULL || macro_symbol->type != MACRO) {
		stop("Invalid current symbol for adding macro arg",
		     EX_SOFTWARE);
		/* NOTREACHED */
	}
	/*
	 * Macro Invocation.  Find the appropriate argument and fill
	 * in the replace ment text for this call.
	 */
	i = 0;
	TAILQ_FOREACH(marg, &macro_symbol->info.macroinfo->args, links) {
		i++;
		if (i == argnum)
			break;
	}
	if (marg == NULL) {
		stop("Too many arguments for macro invocation", EX_DATAERR);
		/* NOTREACHED */
	}
	marg->replacement_text = strdup(argtext);
	if (marg->replacement_text == NULL) {
		stop("Unable to replicate replacement text", EX_SOFTWARE);
		/* NOTREACHED */
	}
}

void
mmerror(const char *string)
{
	stop(string, EX_DATAERR);
}
