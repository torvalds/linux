%union {
	char	*str;
	int	val;
	struct	file_list *file;
}

%token	ARCH
%token	COMMA
%token	CONFIG
%token	CPU
%token	NOCPU
%token	DEVICE
%token	NODEVICE
%token	ENV
%token	ENVVAR
%token	EQUALS
%token	PLUSEQUALS
%token	HINTS
%token	IDENT
%token	MAXUSERS
%token	PROFILE
%token	OPTIONS
%token	NOOPTION
%token	MAKEOPTIONS
%token	NOMAKEOPTION 
%token	SEMICOLON
%token	INCLUDE
%token	FILES

%token	<str>	ENVLINE
%token	<str>	ID
%token	<val>	NUMBER

%type	<str>	Save_id
%type	<str>	Opt_value
%type	<str>	Dev
%token	<str>	PATH

%{

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)config.y	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

struct	device_head dtab;
char	*ident;
char	*env;
int	yyline;
const	char *yyfile;
struct  file_list_head ftab;
struct  files_name_head fntab;
char	errbuf[80];
int	maxusers;

#define ns(s)	strdup(s)
int include(const char *, int);
void yyerror(const char *s);
int yywrap(void);

static void newdev(char *name);
static void newfile(char *name);
static void newenvvar(char *name, bool is_file);
static void rmdev_schedule(struct device_head *dh, char *name);
static void newopt(struct opt_head *list, char *name, char *value, int append, int dupe);
static void rmopt_schedule(struct opt_head *list, char *name);

static char *
devopt(char *dev)
{
	char *ret = malloc(strlen(dev) + 5);
	
	sprintf(ret, "DEV_%s", dev);
	raisestr(ret);
	return ret;
}

%}
%%
Configuration:
	Many_specs
		;

Many_specs:
	Many_specs Spec
		|
	/* lambda */
		;

Spec:
	Device_spec SEMICOLON
		|
	Config_spec SEMICOLON
		|
	INCLUDE PATH SEMICOLON {
		if (incignore == 0)
			include($2, 0);
		};
		|
	INCLUDE ID SEMICOLON {
	          if (incignore == 0)
		  	include($2, 0);
		};
		|
	FILES ID SEMICOLON { newfile($2); };
	        |
	SEMICOLON
		|
	error SEMICOLON
		;

Config_spec:
	ARCH Save_id {
		if (machinename != NULL && !eq($2, machinename))
		    errx(1, "%s:%d: only one machine directive is allowed",
			yyfile, yyline);
		machinename = $2;
		machinearch = $2;
	      } |
	ARCH Save_id Save_id {
		if (machinename != NULL &&
		    !(eq($2, machinename) && eq($3, machinearch)))
		    errx(1, "%s:%d: only one machine directive is allowed",
			yyfile, yyline);
		machinename = $2;
		machinearch = $3;
	      } |
	CPU Save_id {
		struct cputype *cp =
		    (struct cputype *)calloc(1, sizeof (struct cputype));
		if (cp == NULL)
			err(EXIT_FAILURE, "calloc");
		cp->cpu_name = $2;
		SLIST_INSERT_HEAD(&cputype, cp, cpu_next);
	      } |
	NOCPU Save_id {
		struct cputype *cp, *cp2;
		SLIST_FOREACH_SAFE(cp, &cputype, cpu_next, cp2) {
			if (eq(cp->cpu_name, $2)) {
				SLIST_REMOVE(&cputype, cp, cputype, cpu_next);
				free(cp);
			}
		}
	      } |
	OPTIONS Opt_list
		|
	NOOPTION NoOpt_list |
	MAKEOPTIONS Mkopt_list
		|
	NOMAKEOPTION Save_id { rmopt_schedule(&mkopt, $2); } |
	IDENT ID { ident = $2; } |
	System_spec
		|
	MAXUSERS NUMBER { maxusers = $2; } |
	PROFILE NUMBER { profiling = $2; } |
	ENV ID { newenvvar($2, true); } |
	ENVVAR ENVLINE { newenvvar($2, false); } |
	HINTS ID {
		struct hint *hint;

		hint = (struct hint *)calloc(1, sizeof (struct hint));
		if (hint == NULL)
			err(EXIT_FAILURE, "calloc");	
		hint->hint_name = $2;
		STAILQ_INSERT_HEAD(&hints, hint, hint_next);
	        }

System_spec:
	CONFIG System_id System_parameter_list {
		errx(1, "%s:%d: root/dump/swap specifications obsolete",
		      yyfile, yyline);
		}
	  |
	CONFIG System_id
	  ;

System_id:
	Save_id { newopt(&mkopt, ns("KERNEL"), $1, 0, 0); };

System_parameter_list:
	  System_parameter_list ID
	| ID
	;

Opt_list:
	Opt_list COMMA Option
		|
	Option
		;

NoOpt_list:
	NoOpt_list COMMA NoOption
	  	|
	NoOption
		;
Option:
	Save_id {
		newopt(&opt, $1, NULL, 0, 1);
		if (strchr($1, '=') != NULL)
			errx(1, "%s:%d: The `=' in options should not be "
			    "quoted", yyfile, yyline);
	      } |
	Save_id EQUALS Opt_value {
		newopt(&opt, $1, $3, 0, 1);
	      } ;

NoOption:
	Save_id {
		rmopt_schedule(&opt, $1);
		};

Opt_value:
	ID { $$ = $1; } |
	NUMBER {
			char buf[80];

			(void) snprintf(buf, sizeof(buf), "%d", $1);
			$$ = ns(buf);
		} ;

Save_id:
	ID { $$ = $1; }
	;

Mkopt_list:
	Mkopt_list COMMA Mkoption
		|
	Mkoption
		;

Mkoption:
	Save_id { newopt(&mkopt, $1, ns(""), 0, 0); } |
	Save_id EQUALS { newopt(&mkopt, $1, ns(""), 0, 0); } |
	Save_id EQUALS Opt_value { newopt(&mkopt, $1, $3, 0, 0); } |
	Save_id PLUSEQUALS Opt_value { newopt(&mkopt, $1, $3, 1, 0); } ;

Dev:
	ID { $$ = $1; }
	;

Device_spec:
	DEVICE Dev_list
		|
	NODEVICE NoDev_list
		;

Dev_list:
	Dev_list COMMA Device
		|
	Device
		;

NoDev_list:
	NoDev_list COMMA NoDevice
		|
	NoDevice
		;

Device:
	Dev {
		newopt(&opt, devopt($1), ns("1"), 0, 0);
		/* and the device part */
		newdev($1);
		}

NoDevice:
	Dev {
		char *s = devopt($1);

		rmopt_schedule(&opt, s);
		free(s);
		/* and the device part */
		rmdev_schedule(&dtab, $1);
		} ;

%%

void
yyerror(const char *s)
{

	errx(1, "%s:%d: %s", yyfile, yyline + 1, s);
}

int
yywrap(void)
{
	if (found_defaults) {
		if (freopen(PREFIX, "r", stdin) == NULL)
			err(2, "%s", PREFIX);		
		yyfile = PREFIX;
		yyline = 0;
		found_defaults = 0;
		return 0;
	}
	return 1;
}

/*
 * Add a new file to the list of files.
 */
static void
newfile(char *name)
{
	struct files_name *nl;
	
	nl = (struct files_name *) calloc(1, sizeof *nl);
	if (nl == NULL)
		err(EXIT_FAILURE, "calloc");
	nl->f_name = name;
	STAILQ_INSERT_TAIL(&fntab, nl, f_next);
}

static void
newenvvar(char *name, bool is_file)
{
	struct envvar *envvar;

	envvar = (struct envvar *)calloc(1, sizeof (struct envvar));
	if (envvar == NULL)
		err(EXIT_FAILURE, "calloc");
	envvar->env_str = name;
	envvar->env_is_file = is_file;
	STAILQ_INSERT_HEAD(&envvars, envvar, envvar_next);
}

/*
 * Find a device in the list of devices.
 */
static struct device *
finddev(struct device_head *dlist, char *name)
{
	struct device *dp;

	STAILQ_FOREACH(dp, dlist, d_next)
		if (eq(dp->d_name, name))
			return (dp);

	return (NULL);
}
	
/*
 * Add a device to the list of devices.
 */
static void
newdev(char *name)
{
	struct device *np;

	if (finddev(&dtab, name)) {
		fprintf(stderr,
		    "WARNING: duplicate device `%s' encountered.\n", name);
		return;
	}

	np = (struct device *) calloc(1, sizeof *np);
	if (np == NULL)
		err(EXIT_FAILURE, "calloc");
	np->d_name = name;
	STAILQ_INSERT_TAIL(&dtab, np, d_next);
}

/*
 * Schedule a device to removal.
 */
static void
rmdev_schedule(struct device_head *dh, char *name)
{
	struct device *dp;

	dp = finddev(dh, name);
	if (dp != NULL) {
		STAILQ_REMOVE(dh, dp, device, d_next);
		free(dp->d_name);
		free(dp);
	}
}

/*
 * Find an option in the list of options.
 */
static struct opt *
findopt(struct opt_head *list, char *name)
{
	struct opt *op;

	SLIST_FOREACH(op, list, op_next)
		if (eq(op->op_name, name))
			return (op);

	return (NULL);
}

/*
 * Add an option to the list of options.
 */
static void
newopt(struct opt_head *list, char *name, char *value, int append, int dupe)
{
	struct opt *op, *op2;

	/*
	 * Ignore inclusions listed explicitly for configuration files.
	 */
	if (eq(name, OPT_AUTOGEN)) {
		incignore = 1;
		return;
	}

	op2 = findopt(list, name);
	if (op2 != NULL && !append && !dupe) {
		fprintf(stderr,
		    "WARNING: duplicate option `%s' encountered.\n", name);
		return;
	}

	op = (struct opt *)calloc(1, sizeof (struct opt));
	if (op == NULL)
		err(EXIT_FAILURE, "calloc");
	op->op_name = name;
	op->op_ownfile = 0;
	op->op_value = value;
	if (op2 != NULL) {
		if (append) {
			while (SLIST_NEXT(op2, op_append) != NULL)
				op2 = SLIST_NEXT(op2, op_append);
			SLIST_NEXT(op2, op_append) = op;
		} else {
			while (SLIST_NEXT(op2, op_next) != NULL)
				op2 = SLIST_NEXT(op2, op_next);
			SLIST_NEXT(op2, op_next) = op;
		}
	} else
		SLIST_INSERT_HEAD(list, op, op_next);
}

/*
 * Remove an option from the list of options.
 */
static void
rmopt_schedule(struct opt_head *list, char *name)
{
	struct opt *op;

	while ((op = findopt(list, name)) != NULL) {
		SLIST_REMOVE(list, op, opt, op_next);
		free(op->op_name);
		free(op);
	}
}
