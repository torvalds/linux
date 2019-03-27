%{
/*-
 * DEVD (Device action daemon)
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 M. Warner Losh <imp@freebsd.org>.
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

#include <sys/cdefs.h>
#include "devd.h"
#include <stdio.h>
#include <string.h>

%}

%union {
	char *str;
	int i;
	struct eps *eps;	/* EventProcStatement */
	struct event_proc *eventproc;
}

%token SEMICOLON BEGINBLOCK ENDBLOCK COMMA
%token <i> NUMBER
%token <str> STRING
%token <str> ID
%token OPTIONS SET DIRECTORY PID_FILE DEVICE_NAME ACTION MATCH
%token ATTACH DETACH NOMATCH NOTIFY MEDIA_TYPE CLASS SUBDEVICE

%type <eventproc> match_or_action_list
%type <eps> match_or_action match action

%%

config_file
	: config_list
	|
	;

config_list
	: config
	| config_list config
	;

config
	: option_block
	| attach_block
	| detach_block
	| nomatch_block
	| notify_block
	;

option_block
	: OPTIONS BEGINBLOCK options ENDBLOCK SEMICOLON
	;

options
	: option
	| options option

option
	: directory_option
	| pid_file_option
	| set_option
	;

directory_option
	: DIRECTORY STRING SEMICOLON { add_directory($2); }
	;

pid_file_option
	: PID_FILE STRING SEMICOLON { set_pidfile($2); }
	;

set_option
	: SET ID STRING SEMICOLON { set_variable($2, $3); }
	;

attach_block
	: ATTACH NUMBER BEGINBLOCK match_or_action_list ENDBLOCK SEMICOLON
		{ add_attach($2, $4); }
	| ATTACH NUMBER BEGINBLOCK ENDBLOCK SEMICOLON
	;

detach_block
	: DETACH NUMBER BEGINBLOCK match_or_action_list ENDBLOCK SEMICOLON
		{ add_detach($2, $4); }
	| DETACH NUMBER BEGINBLOCK ENDBLOCK SEMICOLON
	;

nomatch_block
	: NOMATCH NUMBER BEGINBLOCK match_or_action_list ENDBLOCK SEMICOLON
		{ add_nomatch($2, $4); }
	| NOMATCH NUMBER BEGINBLOCK ENDBLOCK SEMICOLON
	;

notify_block
	: NOTIFY NUMBER BEGINBLOCK match_or_action_list ENDBLOCK SEMICOLON
		{ add_notify($2, $4); }
	| NOTIFY NUMBER BEGINBLOCK ENDBLOCK SEMICOLON
	;

match_or_action_list
	: match_or_action { $$ = add_to_event_proc( NULL, $1); }
	| match_or_action_list match_or_action
			{ $$ = add_to_event_proc($1, $2); }
	;

match_or_action
	: match
	| action
	;

match
	: MATCH STRING STRING SEMICOLON	{ $$ = new_match($2, $3); }
	| DEVICE_NAME STRING SEMICOLON
		{ $$ = new_match(strdup("device-name"), $2); }
	| MEDIA_TYPE STRING SEMICOLON
		{ $$ = new_media(strdup("media-type"), $2); }
	| CLASS STRING SEMICOLON
		{ $$ = new_match(strdup("class"), $2); }
	| SUBDEVICE STRING SEMICOLON
		{ $$ = new_match(strdup("subdevice"), $2); }
	;

action
	: ACTION STRING SEMICOLON	{ $$ = new_action($2); }
	;

%%
