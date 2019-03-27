%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctld.h"

extern FILE *yyin;
extern char *yytext;
extern int lineno;

static struct conf *conf = NULL;
static struct auth_group *auth_group = NULL;
static struct portal_group *portal_group = NULL;
static struct target *target = NULL;
static struct lun *lun = NULL;

extern void	yyerror(const char *);
extern int	yylex(void);
extern void	yyrestart(FILE *);

%}

%token ALIAS AUTH_GROUP AUTH_TYPE BACKEND BLOCKSIZE CHAP CHAP_MUTUAL
%token CLOSING_BRACKET CTL_LUN DEBUG DEVICE_ID DEVICE_TYPE
%token DISCOVERY_AUTH_GROUP DISCOVERY_FILTER FOREIGN
%token INITIATOR_NAME INITIATOR_PORTAL ISNS_SERVER ISNS_PERIOD ISNS_TIMEOUT
%token LISTEN LISTEN_ISER LUN MAXPROC OFFLOAD OPENING_BRACKET OPTION
%token PATH PIDFILE PORT PORTAL_GROUP REDIRECT SEMICOLON SERIAL SIZE STR
%token TAG TARGET TIMEOUT

%union
{
	char *str;
}

%token <str> STR

%%

statements:
	|
	statements statement
	|
	statements statement SEMICOLON
	;

statement:
	debug
	|
	timeout
	|
	maxproc
	|
	pidfile
	|
	isns_server
	|
	isns_period
	|
	isns_timeout
	|
	auth_group
	|
	portal_group
	|
	lun
	|
	target
	;

debug:		DEBUG STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}
			
		conf->conf_debug = tmp;
	}
	;

timeout:	TIMEOUT STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		conf->conf_timeout = tmp;
	}
	;

maxproc:	MAXPROC STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		conf->conf_maxproc = tmp;
	}
	;

pidfile:	PIDFILE STR
	{
		if (conf->conf_pidfile_path != NULL) {
			log_warnx("pidfile specified more than once");
			free($2);
			return (1);
		}
		conf->conf_pidfile_path = $2;
	}
	;

isns_server:	ISNS_SERVER STR
	{
		int error;

		error = isns_new(conf, $2);
		free($2);
		if (error != 0)
			return (1);
	}
	;

isns_period:	ISNS_PERIOD STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		conf->conf_isns_period = tmp;
	}
	;

isns_timeout:	ISNS_TIMEOUT STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		conf->conf_isns_timeout = tmp;
	}
	;

auth_group:	AUTH_GROUP auth_group_name
    OPENING_BRACKET auth_group_entries CLOSING_BRACKET
	{
		auth_group = NULL;
	}
	;

auth_group_name:	STR
	{
		/*
		 * Make it possible to redefine default
		 * auth-group. but only once.
		 */
		if (strcmp($1, "default") == 0 &&
		    conf->conf_default_ag_defined == false) {
			auth_group = auth_group_find(conf, $1);
			conf->conf_default_ag_defined = true;
		} else {
			auth_group = auth_group_new(conf, $1);
		}
		free($1);
		if (auth_group == NULL)
			return (1);
	}
	;

auth_group_entries:
	|
	auth_group_entries auth_group_entry
	|
	auth_group_entries auth_group_entry SEMICOLON
	;

auth_group_entry:
	auth_group_auth_type
	|
	auth_group_chap
	|
	auth_group_chap_mutual
	|
	auth_group_initiator_name
	|
	auth_group_initiator_portal
	;

auth_group_auth_type:	AUTH_TYPE STR
	{
		int error;

		error = auth_group_set_type(auth_group, $2);
		free($2);
		if (error != 0)
			return (1);
	}
	;

auth_group_chap:	CHAP STR STR
	{
		const struct auth *ca;

		ca = auth_new_chap(auth_group, $2, $3);
		free($2);
		free($3);
		if (ca == NULL)
			return (1);
	}
	;

auth_group_chap_mutual:	CHAP_MUTUAL STR STR STR STR
	{
		const struct auth *ca;

		ca = auth_new_chap_mutual(auth_group, $2, $3, $4, $5);
		free($2);
		free($3);
		free($4);
		free($5);
		if (ca == NULL)
			return (1);
	}
	;

auth_group_initiator_name:	INITIATOR_NAME STR
	{
		const struct auth_name *an;

		an = auth_name_new(auth_group, $2);
		free($2);
		if (an == NULL)
			return (1);
	}
	;

auth_group_initiator_portal:	INITIATOR_PORTAL STR
	{
		const struct auth_portal *ap;

		ap = auth_portal_new(auth_group, $2);
		free($2);
		if (ap == NULL)
			return (1);
	}
	;

portal_group:	PORTAL_GROUP portal_group_name
    OPENING_BRACKET portal_group_entries CLOSING_BRACKET
	{
		portal_group = NULL;
	}
	;

portal_group_name:	STR
	{
		/*
		 * Make it possible to redefine default
		 * portal-group. but only once.
		 */
		if (strcmp($1, "default") == 0 &&
		    conf->conf_default_pg_defined == false) {
			portal_group = portal_group_find(conf, $1);
			conf->conf_default_pg_defined = true;
		} else {
			portal_group = portal_group_new(conf, $1);
		}
		free($1);
		if (portal_group == NULL)
			return (1);
	}
	;

portal_group_entries:
	|
	portal_group_entries portal_group_entry
	|
	portal_group_entries portal_group_entry SEMICOLON
	;

portal_group_entry:
	portal_group_discovery_auth_group
	|
	portal_group_discovery_filter
	|
	portal_group_foreign
	|
	portal_group_listen
	|
	portal_group_listen_iser
	|
	portal_group_offload
	|
	portal_group_option
	|
	portal_group_redirect
	|
	portal_group_tag
	;

portal_group_discovery_auth_group:	DISCOVERY_AUTH_GROUP STR
	{
		if (portal_group->pg_discovery_auth_group != NULL) {
			log_warnx("discovery-auth-group for portal-group "
			    "\"%s\" specified more than once",
			    portal_group->pg_name);
			return (1);
		}
		portal_group->pg_discovery_auth_group =
		    auth_group_find(conf, $2);
		if (portal_group->pg_discovery_auth_group == NULL) {
			log_warnx("unknown discovery-auth-group \"%s\" "
			    "for portal-group \"%s\"",
			    $2, portal_group->pg_name);
			return (1);
		}
		free($2);
	}
	;

portal_group_discovery_filter:	DISCOVERY_FILTER STR
	{
		int error;

		error = portal_group_set_filter(portal_group, $2);
		free($2);
		if (error != 0)
			return (1);
	}
	;

portal_group_foreign:	FOREIGN
	{

		portal_group->pg_foreign = 1;
	}
	;

portal_group_listen:	LISTEN STR
	{
		int error;

		error = portal_group_add_listen(portal_group, $2, false);
		free($2);
		if (error != 0)
			return (1);
	}
	;

portal_group_listen_iser:	LISTEN_ISER STR
	{
		int error;

		error = portal_group_add_listen(portal_group, $2, true);
		free($2);
		if (error != 0)
			return (1);
	}
	;

portal_group_offload:	OFFLOAD STR
	{
		int error;

		error = portal_group_set_offload(portal_group, $2);
		free($2);
		if (error != 0)
			return (1);
	}
	;

portal_group_option:	OPTION STR STR
	{
		struct option *o;

		o = option_new(&portal_group->pg_options, $2, $3);
		free($2);
		free($3);
		if (o == NULL)
			return (1);
	}
	;

portal_group_redirect:	REDIRECT STR
	{
		int error;

		error = portal_group_set_redirection(portal_group, $2);
		free($2);
		if (error != 0)
			return (1);
	}
	;

portal_group_tag:	TAG STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		portal_group->pg_tag = tmp;
	}
	;

lun:	LUN lun_name
    OPENING_BRACKET lun_entries CLOSING_BRACKET
	{
		lun = NULL;
	}
	;

lun_name:	STR
	{
		lun = lun_new(conf, $1);
		free($1);
		if (lun == NULL)
			return (1);
	}
	;

target:	TARGET target_name
    OPENING_BRACKET target_entries CLOSING_BRACKET
	{
		target = NULL;
	}
	;

target_name:	STR
	{
		target = target_new(conf, $1);
		free($1);
		if (target == NULL)
			return (1);
	}
	;

target_entries:
	|
	target_entries target_entry
	|
	target_entries target_entry SEMICOLON
	;

target_entry:
	target_alias
	|
	target_auth_group
	|
	target_auth_type
	|
	target_chap
	|
	target_chap_mutual
	|
	target_initiator_name
	|
	target_initiator_portal
	|
	target_portal_group
	|
	target_port
	|
	target_redirect
	|
	target_lun
	|
	target_lun_ref
	;

target_alias:	ALIAS STR
	{
		if (target->t_alias != NULL) {
			log_warnx("alias for target \"%s\" "
			    "specified more than once", target->t_name);
			return (1);
		}
		target->t_alias = $2;
	}
	;

target_auth_group:	AUTH_GROUP STR
	{
		if (target->t_auth_group != NULL) {
			if (target->t_auth_group->ag_name != NULL)
				log_warnx("auth-group for target \"%s\" "
				    "specified more than once", target->t_name);
			else
				log_warnx("cannot use both auth-group and explicit "
				    "authorisations for target \"%s\"",
				    target->t_name);
			return (1);
		}
		target->t_auth_group = auth_group_find(conf, $2);
		if (target->t_auth_group == NULL) {
			log_warnx("unknown auth-group \"%s\" for target "
			    "\"%s\"", $2, target->t_name);
			return (1);
		}
		free($2);
	}
	;

target_auth_type:	AUTH_TYPE STR
	{
		int error;

		if (target->t_auth_group != NULL) {
			if (target->t_auth_group->ag_name != NULL) {
				log_warnx("cannot use both auth-group and "
				    "auth-type for target \"%s\"",
				    target->t_name);
				return (1);
			}
		} else {
			target->t_auth_group = auth_group_new(conf, NULL);
			if (target->t_auth_group == NULL) {
				free($2);
				return (1);
			}
			target->t_auth_group->ag_target = target;
		}
		error = auth_group_set_type(target->t_auth_group, $2);
		free($2);
		if (error != 0)
			return (1);
	}
	;

target_chap:	CHAP STR STR
	{
		const struct auth *ca;

		if (target->t_auth_group != NULL) {
			if (target->t_auth_group->ag_name != NULL) {
				log_warnx("cannot use both auth-group and "
				    "chap for target \"%s\"",
				    target->t_name);
				free($2);
				free($3);
				return (1);
			}
		} else {
			target->t_auth_group = auth_group_new(conf, NULL);
			if (target->t_auth_group == NULL) {
				free($2);
				free($3);
				return (1);
			}
			target->t_auth_group->ag_target = target;
		}
		ca = auth_new_chap(target->t_auth_group, $2, $3);
		free($2);
		free($3);
		if (ca == NULL)
			return (1);
	}
	;

target_chap_mutual:	CHAP_MUTUAL STR STR STR STR
	{
		const struct auth *ca;

		if (target->t_auth_group != NULL) {
			if (target->t_auth_group->ag_name != NULL) {
				log_warnx("cannot use both auth-group and "
				    "chap-mutual for target \"%s\"",
				    target->t_name);
				free($2);
				free($3);
				free($4);
				free($5);
				return (1);
			}
		} else {
			target->t_auth_group = auth_group_new(conf, NULL);
			if (target->t_auth_group == NULL) {
				free($2);
				free($3);
				free($4);
				free($5);
				return (1);
			}
			target->t_auth_group->ag_target = target;
		}
		ca = auth_new_chap_mutual(target->t_auth_group,
		    $2, $3, $4, $5);
		free($2);
		free($3);
		free($4);
		free($5);
		if (ca == NULL)
			return (1);
	}
	;

target_initiator_name:	INITIATOR_NAME STR
	{
		const struct auth_name *an;

		if (target->t_auth_group != NULL) {
			if (target->t_auth_group->ag_name != NULL) {
				log_warnx("cannot use both auth-group and "
				    "initiator-name for target \"%s\"",
				    target->t_name);
				free($2);
				return (1);
			}
		} else {
			target->t_auth_group = auth_group_new(conf, NULL);
			if (target->t_auth_group == NULL) {
				free($2);
				return (1);
			}
			target->t_auth_group->ag_target = target;
		}
		an = auth_name_new(target->t_auth_group, $2);
		free($2);
		if (an == NULL)
			return (1);
	}
	;

target_initiator_portal:	INITIATOR_PORTAL STR
	{
		const struct auth_portal *ap;

		if (target->t_auth_group != NULL) {
			if (target->t_auth_group->ag_name != NULL) {
				log_warnx("cannot use both auth-group and "
				    "initiator-portal for target \"%s\"",
				    target->t_name);
				free($2);
				return (1);
			}
		} else {
			target->t_auth_group = auth_group_new(conf, NULL);
			if (target->t_auth_group == NULL) {
				free($2);
				return (1);
			}
			target->t_auth_group->ag_target = target;
		}
		ap = auth_portal_new(target->t_auth_group, $2);
		free($2);
		if (ap == NULL)
			return (1);
	}
	;

target_portal_group:	PORTAL_GROUP STR STR
	{
		struct portal_group *tpg;
		struct auth_group *tag;
		struct port *tp;

		tpg = portal_group_find(conf, $2);
		if (tpg == NULL) {
			log_warnx("unknown portal-group \"%s\" for target "
			    "\"%s\"", $2, target->t_name);
			free($2);
			free($3);
			return (1);
		}
		tag = auth_group_find(conf, $3);
		if (tag == NULL) {
			log_warnx("unknown auth-group \"%s\" for target "
			    "\"%s\"", $3, target->t_name);
			free($2);
			free($3);
			return (1);
		}
		tp = port_new(conf, target, tpg);
		if (tp == NULL) {
			log_warnx("can't link portal-group \"%s\" to target "
			    "\"%s\"", $2, target->t_name);
			free($2);
			return (1);
		}
		tp->p_auth_group = tag;
		free($2);
		free($3);
	}
	|		PORTAL_GROUP STR
	{
		struct portal_group *tpg;
		struct port *tp;

		tpg = portal_group_find(conf, $2);
		if (tpg == NULL) {
			log_warnx("unknown portal-group \"%s\" for target "
			    "\"%s\"", $2, target->t_name);
			free($2);
			return (1);
		}
		tp = port_new(conf, target, tpg);
		if (tp == NULL) {
			log_warnx("can't link portal-group \"%s\" to target "
			    "\"%s\"", $2, target->t_name);
			free($2);
			return (1);
		}
		free($2);
	}
	;

target_port:	PORT STR
	{
		struct pport *pp;
		struct port *tp;
		int ret, i_pp, i_vp = 0;

		ret = sscanf($2, "ioctl/%d/%d", &i_pp, &i_vp);
		if (ret > 0) {
			tp = port_new_ioctl(conf, target, i_pp, i_vp);
			if (tp == NULL) {
				log_warnx("can't create new ioctl port for "
				    "target \"%s\"", target->t_name);
				free($2);
				return (1);
			}
		} else {
			pp = pport_find(conf, $2);
			if (pp == NULL) {
				log_warnx("unknown port \"%s\" for target \"%s\"",
				    $2, target->t_name);
				free($2);
				return (1);
			}
			if (!TAILQ_EMPTY(&pp->pp_ports)) {
				log_warnx("can't link port \"%s\" to target \"%s\", "
				    "port already linked to some target",
				    $2, target->t_name);
				free($2);
				return (1);
			}
			tp = port_new_pp(conf, target, pp);
			if (tp == NULL) {
				log_warnx("can't link port \"%s\" to target \"%s\"",
				    $2, target->t_name);
				free($2);
				return (1);
			}
		}

		free($2);
	}
	;

target_redirect:	REDIRECT STR
	{
		int error;

		error = target_set_redirection(target, $2);
		free($2);
		if (error != 0)
			return (1);
	}
	;

target_lun:	LUN lun_number
    OPENING_BRACKET lun_entries CLOSING_BRACKET
	{
		lun = NULL;
	}
	;

lun_number:	STR
	{
		uint64_t tmp;
		int ret;
		char *name;

		if (expand_number($1, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($1);
			return (1);
		}
		if (tmp >= MAX_LUNS) {
			yyerror("LU number is too big");
			free($1);
			return (1);
		}

		ret = asprintf(&name, "%s,lun,%ju", target->t_name, tmp);
		if (ret <= 0)
			log_err(1, "asprintf");
		lun = lun_new(conf, name);
		if (lun == NULL)
			return (1);

		lun_set_scsiname(lun, name);
		target->t_luns[tmp] = lun;
	}
	;

target_lun_ref:	LUN STR STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			free($3);
			return (1);
		}
		free($2);
		if (tmp >= MAX_LUNS) {
			yyerror("LU number is too big");
			free($3);
			return (1);
		}

		lun = lun_find(conf, $3);
		free($3);
		if (lun == NULL)
			return (1);

		target->t_luns[tmp] = lun;
	}
	;

lun_entries:
	|
	lun_entries lun_entry
	|
	lun_entries lun_entry SEMICOLON
	;

lun_entry:
	lun_backend
	|
	lun_blocksize
	|
	lun_device_id
	|
	lun_device_type
	|
	lun_ctl_lun
	|
	lun_option
	|
	lun_path
	|
	lun_serial
	|
	lun_size
	;

lun_backend:	BACKEND STR
	{
		if (lun->l_backend != NULL) {
			log_warnx("backend for lun \"%s\" "
			    "specified more than once",
			    lun->l_name);
			free($2);
			return (1);
		}
		lun_set_backend(lun, $2);
		free($2);
	}
	;

lun_blocksize:	BLOCKSIZE STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		if (lun->l_blocksize != 0) {
			log_warnx("blocksize for lun \"%s\" "
			    "specified more than once",
			    lun->l_name);
			return (1);
		}
		lun_set_blocksize(lun, tmp);
	}
	;

lun_device_id:	DEVICE_ID STR
	{
		if (lun->l_device_id != NULL) {
			log_warnx("device_id for lun \"%s\" "
			    "specified more than once",
			    lun->l_name);
			free($2);
			return (1);
		}
		lun_set_device_id(lun, $2);
		free($2);
	}
	;

lun_device_type:	DEVICE_TYPE STR
	{
		uint64_t tmp;

		if (strcasecmp($2, "disk") == 0 ||
		    strcasecmp($2, "direct") == 0)
			tmp = 0;
		else if (strcasecmp($2, "processor") == 0)
			tmp = 3;
		else if (strcasecmp($2, "cd") == 0 ||
		    strcasecmp($2, "cdrom") == 0 ||
		    strcasecmp($2, "dvd") == 0 ||
		    strcasecmp($2, "dvdrom") == 0)
			tmp = 5;
		else if (expand_number($2, &tmp) != 0 ||
		    tmp > 15) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		lun_set_device_type(lun, tmp);
	}
	;

lun_ctl_lun:	CTL_LUN STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		if (lun->l_ctl_lun >= 0) {
			log_warnx("ctl_lun for lun \"%s\" "
			    "specified more than once",
			    lun->l_name);
			return (1);
		}
		lun_set_ctl_lun(lun, tmp);
	}
	;

lun_option:	OPTION STR STR
	{
		struct option *o;

		o = option_new(&lun->l_options, $2, $3);
		free($2);
		free($3);
		if (o == NULL)
			return (1);
	}
	;

lun_path:	PATH STR
	{
		if (lun->l_path != NULL) {
			log_warnx("path for lun \"%s\" "
			    "specified more than once",
			    lun->l_name);
			free($2);
			return (1);
		}
		lun_set_path(lun, $2);
		free($2);
	}
	;

lun_serial:	SERIAL STR
	{
		if (lun->l_serial != NULL) {
			log_warnx("serial for lun \"%s\" "
			    "specified more than once",
			    lun->l_name);
			free($2);
			return (1);
		}
		lun_set_serial(lun, $2);
		free($2);
	}
	;

lun_size:	SIZE STR
	{
		uint64_t tmp;

		if (expand_number($2, &tmp) != 0) {
			yyerror("invalid numeric value");
			free($2);
			return (1);
		}

		if (lun->l_size != 0) {
			log_warnx("size for lun \"%s\" "
			    "specified more than once",
			    lun->l_name);
			return (1);
		}
		lun_set_size(lun, tmp);
	}
	;
%%

void
yyerror(const char *str)
{

	log_warnx("error in configuration file at line %d near '%s': %s",
	    lineno, yytext, str);
}

int
parse_conf(struct conf *newconf, const char *path)
{
	int error;

	conf = newconf;
	yyin = fopen(path, "r");
	if (yyin == NULL) {
		log_warn("unable to open configuration file %s", path);
		return (1);
	}

	lineno = 1;
	yyrestart(yyin);
	error = yyparse();
	auth_group = NULL;
	portal_group = NULL;
	target = NULL;
	lun = NULL;
	fclose(yyin);

	return (error);
}
