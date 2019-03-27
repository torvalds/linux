%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * APM (Advanced Power Management) Event Dispatcher
 *
 * Copyright (c) 1999 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
 * Copyright (c) 1999 KOIE Hidetaka <koie@suri.co.jp>
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <stdio.h>
#include <bitstring.h>
#include <stdlib.h>
#include <string.h>
#include "apmd.h"

#ifdef DEBUG
#define YYDEBUG 1
#endif

extern int first_time;

%}

%union {
	char *str;
	bitstr_t bit_decl(evlist, EVENT_MAX);
	int ev;
	struct event_cmd * evcmd;
	int i;
}

%token BEGINBLOCK ENDBLOCK
%token COMMA SEMICOLON
%token APMEVENT
%token APMBATT
%token BATTCHARGE BATTDISCHARGE
%token <i> BATTTIME BATTPERCENT
%token EXECCMD REJECTCMD
%token <ev> EVENT
%token <str> STRING UNKNOWN

%type <i> apm_battery_level
%type <i> apm_battery_direction
%type <str> string
%type <str> unknown
%type <evlist> event_list
%type <evcmd> cmd_list
%type <evcmd> cmd
%type <evcmd> exec_cmd reject_cmd

%%

config_file
	: { first_time = 1; } config_list
	;

config_list
	: config
	| config_list config
	;

config
	: apm_event_statement
	| apm_battery_statement
	;

apm_event_statement
	: APMEVENT event_list BEGINBLOCK cmd_list ENDBLOCK
		{
			if (register_apm_event_handlers($2, $4) < 0)
				abort(); /* XXX */
			free_event_cmd_list($4);
		}
	;

apm_battery_level
	: BATTPERCENT
		{
			$$ = $1;
		}
	| BATTTIME
		{
			$$ = $1;
		}
	;

apm_battery_direction
	: BATTCHARGE
		{
			$$ = 1;
		}
	| BATTDISCHARGE
		{
			$$ = -1;
		}
	;
apm_battery_statement
	: APMBATT apm_battery_level apm_battery_direction
		BEGINBLOCK cmd_list ENDBLOCK
		{
			if (register_battery_handlers($2, $3, $5) < 0)
				abort(); /* XXX */
			free_event_cmd_list($5);
		}
	;

event_list
	: EVENT
		{
			bit_nclear($$, 0, EVENT_MAX - 1);
			bit_set($$, $1);
		}
	| event_list COMMA EVENT
		{
			memcpy(&($$), &($1), bitstr_size(EVENT_MAX));
			bit_set($$, $3);
		}
	;

cmd_list
	: /* empty */
		{
			$$  = NULL;
		}
	| cmd_list cmd
		{
			struct event_cmd * p = $1;
			if (p) {
				while (p->next != NULL)
					p = p->next;
				p->next = $2;
				$$ = $1;
			} else {
				$$ = $2;
			}
		}
	;

cmd
	: exec_cmd SEMICOLON	{ $$ = $1; }
	| reject_cmd SEMICOLON	{ $$ = $1; }
	;

exec_cmd
	: EXECCMD string
		{
			size_t len = sizeof (struct event_cmd_exec);
			struct event_cmd_exec *cmd = malloc(len);
			cmd->evcmd.next = NULL;
			cmd->evcmd.len = len;
			cmd->evcmd.name = "exec";
			cmd->evcmd.op = &event_cmd_exec_ops;
			cmd->line = $2;
			$$ = (struct event_cmd *) cmd;
		}
	;

reject_cmd
	: REJECTCMD
		{
			size_t len = sizeof (struct event_cmd_reject);
			struct event_cmd_reject *cmd = malloc(len);
			cmd->evcmd.next = NULL;
			cmd->evcmd.len = len;
			cmd->evcmd.name = "reject";
			cmd->evcmd.op = &event_cmd_reject_ops;
			$$ = (struct event_cmd *) cmd;
		}
	;

string
	: STRING	{ $$ = $1; }
	;

unknown
	: UNKNOWN
		{
			$$ = $1;
		}
	;
%%

