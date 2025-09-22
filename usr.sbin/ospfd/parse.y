/*	$OpenBSD: parse.y,v 1.106 2024/08/22 08:34:51 tb Exp $ */

/*
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2004 Ryan McBride <mcbride@openbsd.org>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

%{
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "ospf.h"
#include "ospfd.h"
#include "ospfe.h"
#include "log.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t			 ungetpos;
	size_t			 ungetsize;
	u_char			*ungetbuf;
	int			 eof_reached;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *, int);
int		 popfile(void);
int		 check_file_secrecy(int, const char *);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 igetc(void);
int		 lgetc(int);
void		 lungetc(int);
int		 findeol(void);

TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
int		 symset(const char *, const char *, int);
char		*symget(const char *);

void		 clear_config(struct ospfd_conf *xconf);
int		 host(const char *, struct in_addr *, struct in_addr *);

static struct ospfd_conf	*conf;
static int			 errors = 0;

struct area	*area = NULL;
struct iface	*iface = NULL;

struct config_defaults {
	char		auth_key[MAX_SIMPLE_AUTH_LEN];
	struct auth_md_head	 md_list;
	u_int32_t	dead_interval;
	u_int32_t	fast_hello_interval;
	u_int16_t	transmit_delay;
	u_int16_t	hello_interval;
	u_int16_t	rxmt_interval;
	u_int16_t	metric;
	enum auth_type	auth_type;
	u_int8_t	auth_keyid;
	u_int8_t	priority;
	u_int8_t	p2p;
};

struct config_defaults	 globaldefs;
struct config_defaults	 areadefs;
struct config_defaults	 ifacedefs;
struct config_defaults	*defs;

struct area	*conf_get_area(struct in_addr);
struct iface	*conf_get_if(struct kif *, struct kif_addr *);
int		 conf_check_rdomain(unsigned int);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
		struct redistribute *redist;
		struct in_addr	 id;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	AREA INTERFACE ROUTERID FIBPRIORITY FIBUPDATE REDISTRIBUTE RTLABEL
%token	RDOMAIN RFC1583COMPAT STUB ROUTER SPFDELAY SPFHOLDTIME EXTTAG
%token	AUTHKEY AUTHTYPE AUTHMD AUTHMDKEYID
%token	METRIC P2P PASSIVE
%token	HELLOINTERVAL FASTHELLOINTERVAL TRANSMITDELAY
%token	RETRANSMITINTERVAL ROUTERDEADTIME ROUTERPRIORITY
%token	SET TYPE
%token	YES NO
%token	MSEC MINIMAL
%token	DEMOTE
%token	INCLUDE
%token	ERROR
%token	DEPEND ON
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.number>	yesno no optlist optlist_l option demotecount msec
%type	<v.number>	deadtime
%type	<v.string>	string dependon dependonopt
%type	<v.redist>	redistribute
%type	<v.id>		areaid

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar conf_main '\n'
		| grammar varset '\n'
		| grammar area '\n'
		| grammar error '\n'		{ file->errors++; }
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2,
			    !(conf->opts & OSPFD_OPT_NOACTION))) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

string		: string STRING	{
			if (asprintf(&$$, "%s %s", $1, $2) == -1) {
				free($1);
				free($2);
				yyerror("string: asprintf");
				YYERROR;
			}
			free($1);
			free($2);
		}
		| STRING
		;

yesno		: YES	{ $$ = 1; }
		| NO	{ $$ = 0; }
		;

no		: /* empty */	{ $$ = 0; }
		| NO		{ $$ = 1; }
		;

msec		: MSEC NUMBER {
			$$ = $2;
		}
		| NUMBER {
			$$ = $1 * 1000;
		}
		;

varset		: STRING '=' string		{
			char *s = $1;
			if (conf->opts & OSPFD_OPT_VERBOSE)
				printf("%s = \"%s\"\n", $1, $3);
			while (*s++) {
				if (isspace((unsigned char)*s)) {
					yyerror("macro name cannot contain "
					    "whitespace");
					free($1);
					free($3);
					YYERROR;
				}
			}
			if (symset($1, $3, 0) == -1)
				fatal("cannot store variable");
			free($1);
			free($3);
		}
		;

conf_main	: ROUTERID STRING {
			if (inet_pton(AF_INET, $2, &conf->rtr_id) != 1) {
				yyerror("error parsing router-id");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| FIBPRIORITY NUMBER {
			if ($2 <= RTP_NONE || $2 > RTP_MAX) {
				yyerror("invalid fib-priority");
				YYERROR;
			}
			conf->fib_priority = $2;
		}
		| FIBUPDATE yesno {
			if ($2 == 0)
				conf->flags |= OSPFD_FLAG_NO_FIB_UPDATE;
			else
				conf->flags &= ~OSPFD_FLAG_NO_FIB_UPDATE;
		}
		| redistribute {
			SIMPLEQ_INSERT_TAIL(&conf->redist_list, $1, entry);
			conf->redistribute = 1;
		}
		| RTLABEL STRING EXTTAG NUMBER {
			if ($4 < 0 || $4 > UINT_MAX) {
				yyerror("invalid external route tag");
				free($2);
				YYERROR;
			}
			rtlabel_tag(rtlabel_name2id($2), $4);
			free($2);
		}
		| RDOMAIN NUMBER {
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rdomain");
				YYERROR;
			}
			conf->rdomain = $2;
		}
		| RFC1583COMPAT yesno {
			conf->rfc1583compat = $2;
		}
		| SPFDELAY msec {
			if ($2 < MIN_SPF_DELAY || $2 > MAX_SPF_DELAY) {
				yyerror("spf-delay out of range "
				    "(%d-%d)", MIN_SPF_DELAY,
				    MAX_SPF_DELAY);
				YYERROR;
			}
			conf->spf_delay = $2;
		}
		| SPFHOLDTIME msec {
			if ($2 < MIN_SPF_HOLDTIME || $2 > MAX_SPF_HOLDTIME) {
				yyerror("spf-holdtime out of range "
				    "(%d-%d)", MIN_SPF_HOLDTIME,
				    MAX_SPF_HOLDTIME);
				YYERROR;
			}
			conf->spf_hold_time = $2;
		}
		| STUB ROUTER yesno {
			if ($3)
				conf->flags |= OSPFD_FLAG_STUB_ROUTER;
			else
				/* allow to force non stub mode */
				conf->flags &= ~OSPFD_FLAG_STUB_ROUTER;
		}
		| defaults
		;


redistribute	: no REDISTRIBUTE NUMBER '/' NUMBER optlist dependonopt {
			struct redistribute	*r;

			if ((r = calloc(1, sizeof(*r))) == NULL)
				fatal(NULL);
			r->type = REDIST_ADDR;
			if ($3 < 0 || $3 > 255 || $5 < 1 || $5 > 32) {
				yyerror("bad network: %llu/%llu", $3, $5);
				free(r);
				YYERROR;
			}
			r->addr.s_addr = htonl($3 << IN_CLASSA_NSHIFT);
			r->mask.s_addr = prefixlen2mask($5);

			if ($1)
				r->type |= REDIST_NO;
			else
				conf->redist_label_or_prefix = 1;
			r->metric = $6;
			if ($7)
				strlcpy(r->dependon, $7, sizeof(r->dependon));
			else
				r->dependon[0] = '\0';
			free($7);
			$$ = r;
		}
		| no REDISTRIBUTE STRING optlist dependonopt {
			struct redistribute	*r;

			if ((r = calloc(1, sizeof(*r))) == NULL)
				fatal(NULL);
			if (!strcmp($3, "default"))
				r->type = REDIST_DEFAULT;
			else if (!strcmp($3, "static"))
				r->type = REDIST_STATIC;
			else if (!strcmp($3, "connected"))
				r->type = REDIST_CONNECTED;
			else if (host($3, &r->addr, &r->mask)) {
				r->type = REDIST_ADDR;
				conf->redist_label_or_prefix = !$1;
			} else {
				yyerror("unknown redistribute type");
				free($3);
				free(r);
				YYERROR;
			}

			if ($1)
				r->type |= REDIST_NO;
			r->metric = $4;
			if ($5)
				strlcpy(r->dependon, $5, sizeof(r->dependon));
			else
				r->dependon[0] = '\0';
			free($3);
			free($5);
			$$ = r;
		}
		| no REDISTRIBUTE RTLABEL STRING optlist dependonopt {
			struct redistribute	*r;

			if ((r = calloc(1, sizeof(*r))) == NULL)
				fatal(NULL);
			r->type = REDIST_LABEL;
			r->label = rtlabel_name2id($4);
			if ($1)
				r->type |= REDIST_NO;
			else
				conf->redist_label_or_prefix = 1;
			r->metric = $5;
			if ($6)
				strlcpy(r->dependon, $6, sizeof(r->dependon));
			else
				r->dependon[0] = '\0';
			free($4);
			free($6);
			$$ = r;
		}
		;

optlist		: /* empty */ 			{ $$ = DEFAULT_REDIST_METRIC; }
		| SET option 			{
			$$ = $2;
			if (($$ & LSA_METRIC_MASK) == 0)
				$$ |= DEFAULT_REDIST_METRIC;
		}
		| SET optnl '{' optnl optlist_l optnl '}' {
			$$ = $5;
			if (($$ & LSA_METRIC_MASK) == 0)
				$$ |= DEFAULT_REDIST_METRIC;
		}
		;

optlist_l	: optlist_l comma option {
			if ($1 & LSA_ASEXT_E_FLAG && $3 & LSA_ASEXT_E_FLAG) {
				yyerror("redistribute type already defined");
				YYERROR;
			}
			if ($1 & LSA_METRIC_MASK && $3 & LSA_METRIC_MASK) {
				yyerror("redistribute metric already defined");
				YYERROR;
			}
			$$ = $1 | $3;
		}
		| option { $$ = $1; }
		;

option		: METRIC NUMBER {
			if ($2 == 0 || $2 > MAX_METRIC) {
				yyerror("invalid redistribute metric");
				YYERROR;
			}
			$$ = $2;
		}
		| TYPE NUMBER {
			switch ($2) {
			case 1:
				$$ = 0;
				break;
			case 2:
				$$ = LSA_ASEXT_E_FLAG;
				break;
			default:
				yyerror("only external type 1 and 2 allowed");
				YYERROR;
			}
		}
		;

dependonopt	: /* empty */		{ $$ = NULL; }
		| dependon

dependon	: DEPEND ON STRING	{
			struct in_addr	 addr;
			struct kif	*kif;

			if (strlen($3) >= IFNAMSIZ) {
				yyerror("interface name %s too long", $3);
				free($3);
				YYERROR;
			}
			bzero(&addr, sizeof(addr));
			if ((kif = kif_findname($3, addr, NULL)) == NULL) {
				yyerror("unknown interface %s", $3);
				free($3);
				YYERROR;
			}
			$$ = $3;
		}
		;

authmd		: AUTHMD NUMBER STRING {
			if ($2 < MIN_MD_ID || $2 > MAX_MD_ID) {
				yyerror("auth-md key-id out of range "
				    "(%d-%d)", MIN_MD_ID, MAX_MD_ID);
				free($3);
				YYERROR;
			}
			if (strlen($3) > MD5_DIGEST_LENGTH) {
				yyerror("auth-md key length out of range "
				    "(max length %d)",
				    MD5_DIGEST_LENGTH);
				free($3);
				YYERROR;
			}
			md_list_add(&defs->md_list, $2, $3);
			free($3);
		}

authmdkeyid	: AUTHMDKEYID NUMBER {
			if ($2 < MIN_MD_ID || $2 > MAX_MD_ID) {
				yyerror("auth-md-keyid out of range "
				    "(%d-%d)", MIN_MD_ID, MAX_MD_ID);
				YYERROR;
			}
			defs->auth_keyid = $2;
		}

authtype	: AUTHTYPE STRING {
			enum auth_type	type;

			if (!strcmp($2, "none"))
				type = AUTH_NONE;
			else if (!strcmp($2, "simple"))
				type = AUTH_SIMPLE;
			else if (!strcmp($2, "crypt"))
				type = AUTH_CRYPT;
			else {
				yyerror("unknown auth-type");
				free($2);
				YYERROR;
			}
			free($2);
			defs->auth_type = type;
		}
		;

authkey		: AUTHKEY STRING {
			if (strlen($2) > MAX_SIMPLE_AUTH_LEN) {
				yyerror("auth-key too long (max length %d)",
				    MAX_SIMPLE_AUTH_LEN);
					free($2);
					YYERROR;
			}
			strncpy(defs->auth_key, $2,
			    sizeof(defs->auth_key));
			free($2);
		}
		;

defaults	: METRIC NUMBER {
			if ($2 < MIN_METRIC || $2 > MAX_METRIC) {
				yyerror("metric out of range (%d-%d)",
				    MIN_METRIC, MAX_METRIC);
				YYERROR;
			}
			defs->metric = $2;
		}
		| ROUTERPRIORITY NUMBER {
			if ($2 < MIN_PRIORITY || $2 > MAX_PRIORITY) {
				yyerror("router-priority out of range (%d-%d)",
				    MIN_PRIORITY, MAX_PRIORITY);
				YYERROR;
			}
			defs->priority = $2;
		}
		| ROUTERDEADTIME deadtime {
			defs->dead_interval = $2;
		}
		| TRANSMITDELAY NUMBER {
			if ($2 < MIN_TRANSMIT_DELAY ||
			    $2 > MAX_TRANSMIT_DELAY) {
				yyerror("transmit-delay out of range (%d-%d)",
				    MIN_TRANSMIT_DELAY, MAX_TRANSMIT_DELAY);
				YYERROR;
			}
			defs->transmit_delay = $2;
		}
		| HELLOINTERVAL NUMBER {
			if ($2 < MIN_HELLO_INTERVAL ||
			    $2 > MAX_HELLO_INTERVAL) {
				yyerror("hello-interval out of range (%d-%d)",
				    MIN_HELLO_INTERVAL, MAX_HELLO_INTERVAL);
				YYERROR;
			}
			defs->hello_interval = $2;
		}
		| FASTHELLOINTERVAL MSEC NUMBER {
			if ($3 < MIN_FAST_INTERVAL ||
			    $3 > MAX_FAST_INTERVAL) {
				yyerror("fast-hello-interval msec out of "
				    "range (%d-%d)", MIN_FAST_INTERVAL,
				    MAX_FAST_INTERVAL);
				YYERROR;
			}
			defs->fast_hello_interval = $3;
		}
		| RETRANSMITINTERVAL NUMBER {
			if ($2 < MIN_RXMT_INTERVAL || $2 > MAX_RXMT_INTERVAL) {
				yyerror("retransmit-interval out of range "
				    "(%d-%d)", MIN_RXMT_INTERVAL,
				    MAX_RXMT_INTERVAL);
				YYERROR;
			}
			defs->rxmt_interval = $2;
		}
		| TYPE P2P		{
			defs->p2p = 1;
		}
		| authtype
		| authkey
		| authmdkeyid
		| authmd
		;

deadtime	: NUMBER {
			if ($1 < MIN_RTR_DEAD_TIME || $1 > MAX_RTR_DEAD_TIME) {
				yyerror("router-dead-time out of range (%d-%d)",
				    MIN_RTR_DEAD_TIME, MAX_RTR_DEAD_TIME);
				YYERROR;
			}
			$$ = $1;
		}
		| MINIMAL {
			$$ = FAST_RTR_DEAD_TIME;
		}

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl		/* one newline or more */
		;

comma		: ','
		| /*empty*/
		;

area		: AREA areaid {
			area = conf_get_area($2);

			memcpy(&areadefs, defs, sizeof(areadefs));
			md_list_copy(&areadefs.md_list, &defs->md_list);
			defs = &areadefs;
		} '{' optnl areaopts_l optnl '}' {
			area = NULL;
			md_list_clr(&defs->md_list);
			defs = &globaldefs;
		}
		;

demotecount	: NUMBER	{ $$ = $1; }
		| /*empty*/	{ $$ = 1; }
		;

areaid		: NUMBER {
			if ($1 < 0 || $1 > 0xffffffff) {
				yyerror("invalid area id");
				YYERROR;
			}
			$$.s_addr = htonl($1);
		}
		| STRING {
			if (inet_pton(AF_INET, $1, &$$) != 1) {
				yyerror("error parsing area");
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

areaopts_l	: areaopts_l nl areaoptsl
		| areaoptsl
		;

areaoptsl	: interface
		| DEMOTE STRING	demotecount {
			if ($3 < 1 || $3 > 255) {
				yyerror("demote count out of range (1-255)");
				free($2);
				YYERROR;
			}
			area->demote_level = $3;
			if (strlcpy(area->demote_group, $2,
			    sizeof(area->demote_group)) >=
			    sizeof(area->demote_group)) {
				yyerror("demote group name \"%s\" too long",
				    $2);
				free($2);
				YYERROR;
			}
			free($2);
			if (carp_demote_init(area->demote_group,
			    conf->opts & OSPFD_OPT_FORCE_DEMOTE) == -1) {
				yyerror("error initializing group \"%s\"",
				    area->demote_group);
				YYERROR;
			}
		}
		| defaults
		| STUB 			{ area->stub = 1; }
		| STUB redistribute {
			area->stub = 1;
			if ($2->type != REDIST_DEFAULT) {
				yyerror("bad redistribute option");
				YYERROR;
			}
			if (!SIMPLEQ_EMPTY(&area->redist_list)) {
				yyerror("area redistribute option only "
				    "allowed once");
				YYERROR;
			}
			SIMPLEQ_INSERT_TAIL(&area->redist_list, $2, entry);
		}
		;

interface	: INTERFACE STRING	{
			struct kif	*kif;
			struct kif_addr	*ka = NULL;
			char		*s;
			struct in_addr	 addr;

			s = strchr($2, ':');
			if (s) {
				*s++ = '\0';
				if (inet_pton(AF_INET, s, &addr) != 1) {
					yyerror(
					    "error parsing interface address");
					free($2);
					YYERROR;
				}
			} else
				addr.s_addr = 0;

			if ((kif = kif_findname($2, addr, &ka)) == NULL) {
				yyerror("unknown interface %s", $2);
				free($2);
				YYERROR;
			}
			if (ka == NULL) {
				if (s)
					yyerror("address %s not configured on "
					    "interface %s", s, $2);
				else
					yyerror("unnumbered interface %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
			iface = conf_get_if(kif, ka);
			if (iface == NULL)
				YYERROR;
			iface->area = area;
			LIST_INSERT_HEAD(&area->iface_list, iface, entry);

			memcpy(&ifacedefs, defs, sizeof(ifacedefs));
			md_list_copy(&ifacedefs.md_list, &defs->md_list);
			defs = &ifacedefs;
		} interface_block {
			iface->dead_interval = defs->dead_interval;
			iface->fast_hello_interval = defs->fast_hello_interval;
			iface->transmit_delay = defs->transmit_delay;
			if (iface->dead_interval == FAST_RTR_DEAD_TIME)
				iface->hello_interval = 0;
			else
				iface->hello_interval = defs->hello_interval;
			iface->rxmt_interval = defs->rxmt_interval;
			iface->metric = defs->metric;
			iface->priority = defs->priority;
			iface->auth_type = defs->auth_type;
			iface->auth_keyid = defs->auth_keyid;
			if (defs->p2p == 1)
				iface->type = IF_TYPE_POINTOPOINT;
			memcpy(iface->auth_key, defs->auth_key,
			    sizeof(iface->auth_key));
			md_list_copy(&iface->auth_md_list, &defs->md_list);
			md_list_clr(&defs->md_list);
			iface = NULL;
			/* interface is always part of an area */
			defs = &areadefs;
		}
		;

interface_block	: '{' optnl interfaceopts_l optnl '}'
		| '{' optnl '}'
		| /* empty */
		;

interfaceopts_l	: interfaceopts_l nl interfaceoptsl
		| interfaceoptsl
		;

interfaceoptsl	: PASSIVE		{ iface->passive = 1; }
		| DEMOTE STRING		{
			if (strlcpy(iface->demote_group, $2,
			    sizeof(iface->demote_group)) >=
			    sizeof(iface->demote_group)) {
				yyerror("demote group name \"%s\" too long",
				    $2);
				free($2);
				YYERROR;
			}
			free($2);
			if (carp_demote_init(iface->demote_group,
			    conf->opts & OSPFD_OPT_FORCE_DEMOTE) == -1) {
				yyerror("error initializing group \"%s\"",
				    iface->demote_group);
				YYERROR;
			}
		}
		| dependon {
			struct in_addr	 addr;
			struct kif	*kif;

			if ($1) {
				strlcpy(iface->dependon, $1,
				       	sizeof(iface->dependon));
				bzero(&addr, sizeof(addr));
				kif = kif_findname($1, addr, NULL);
				iface->depend_ok = ifstate_is_up(kif);
			} else {
				iface->dependon[0] = '\0';
				iface->depend_ok = 1;
			}

			free($1);
		}
		| defaults
		;

%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;
	char		*msg;

	file->errors++;
	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) == -1)
		fatalx("yyerror vasprintf");
	va_end(ap);
	logit(LOG_CRIT, "%s:%d: %s", file->name, yylval.lineno, msg);
	free(msg);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{"area",		AREA},
		{"auth-key",		AUTHKEY},
		{"auth-md",		AUTHMD},
		{"auth-md-keyid",	AUTHMDKEYID},
		{"auth-type",		AUTHTYPE},
		{"demote",		DEMOTE},
		{"depend",		DEPEND},
		{"external-tag",	EXTTAG},
		{"fast-hello-interval",	FASTHELLOINTERVAL},
		{"fib-priority",	FIBPRIORITY},
		{"fib-update",		FIBUPDATE},
		{"hello-interval",	HELLOINTERVAL},
		{"include",		INCLUDE},
		{"interface",		INTERFACE},
		{"metric",		METRIC},
		{"minimal",		MINIMAL},
		{"msec",		MSEC},
		{"no",			NO},
		{"on",			ON},
		{"p2p",			P2P},
		{"passive",		PASSIVE},
		{"rdomain",		RDOMAIN},
		{"redistribute",	REDISTRIBUTE},
		{"retransmit-interval",	RETRANSMITINTERVAL},
		{"rfc1583compat",	RFC1583COMPAT},
		{"router",		ROUTER},
		{"router-dead-time",	ROUTERDEADTIME},
		{"router-id",		ROUTERID},
		{"router-priority",	ROUTERPRIORITY},
		{"rtlabel",		RTLABEL},
		{"set",			SET},
		{"spf-delay",		SPFDELAY},
		{"spf-holdtime",	SPFHOLDTIME},
		{"stub",		STUB},
		{"transmit-delay",	TRANSMITDELAY},
		{"type",		TYPE},
		{"yes",			YES}
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (STRING);
}

#define START_EXPAND	1
#define DONE_EXPAND	2

static int	expanding;

int
igetc(void)
{
	int	c;

	while (1) {
		if (file->ungetpos > 0)
			c = file->ungetbuf[--file->ungetpos];
		else
			c = getc(file->stream);

		if (c == START_EXPAND)
			expanding = 1;
		else if (c == DONE_EXPAND)
			expanding = 0;
		else
			break;
	}
	return (c);
}

int
lgetc(int quotec)
{
	int		c, next;

	if (quotec) {
		if ((c = igetc()) == EOF) {
			yyerror("reached end of file while parsing "
			    "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = igetc()) == '\\') {
		next = igetc();
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	if (c == EOF) {
		/*
		 * Fake EOL when hit EOF for the first time. This gets line
		 * count right if last line in included file is syntactically
		 * invalid and has no newline.
		 */
		if (file->eof_reached == 0) {
			file->eof_reached = 1;
			return ('\n');
		}
		while (c == EOF) {
			if (file == topfile || popfile() == EOF)
				return (EOF);
			c = igetc();
		}
	}
	return (c);
}

void
lungetc(int c)
{
	if (c == EOF)
		return;

	if (file->ungetpos >= file->ungetsize) {
		void *p = reallocarray(file->ungetbuf, file->ungetsize, 2);
		if (p == NULL)
			err(1, "%s", __func__);
		file->ungetbuf = p;
		file->ungetsize *= 2;
	}
	file->ungetbuf[file->ungetpos++] = c;
}

int
findeol(void)
{
	int	c;

	/* skip to either EOF or the first real EOL */
	while (1) {
		c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p, *val;
	int	 quotec, next, c;
	int	 token;

top:
	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */
	if (c == '$' && !expanding) {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = c;
				continue;
			}
			*p = '\0';
			lungetc(c);
			break;
		}
		val = symget(buf);
		if (val == NULL) {
			yyerror("macro '%s' not defined", buf);
			return (findeol());
		}
		p = val + strlen(val) - 1;
		lungetc(DONE_EXPAND);
		while (p >= val) {
			lungetc((unsigned char)*p);
			p--;
		}
		lungetc(START_EXPAND);
		goto top;
	}

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || next == ' ' ||
				    next == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			err(1, "%s", __func__);
		return (STRING);
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum(buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc((unsigned char)*--p);
			c = (unsigned char)*--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && \
	x != '!' && x != '=' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_') {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				err(1, "%s", __func__);
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

int
check_file_secrecy(int fd, const char *fname)
{
	struct stat	st;

	if (fstat(fd, &st)) {
		log_warn("cannot stat %s", fname);
		return (-1);
	}
	if (st.st_uid != 0 && st.st_uid != getuid()) {
		log_warnx("%s: owner not root or current user", fname);
		return (-1);
	}
	if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO)) {
		log_warnx("%s: group writable or world read/writable", fname);
		return (-1);
	}
	return (0);
}

struct file *
pushfile(const char *name, int secret)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		log_warn("%s", __func__);
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		log_warn("%s", __func__);
		free(nfile);
		return (NULL);
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		log_warn("%s: %s", __func__, nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	} else if (secret &&
	    check_file_secrecy(fileno(nfile->stream), nfile->name)) {
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = TAILQ_EMPTY(&files) ? 1 : 0;
	nfile->ungetsize = 16;
	nfile->ungetbuf = malloc(nfile->ungetsize);
	if (nfile->ungetbuf == NULL) {
		log_warn("%s", __func__);
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file->ungetbuf);
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

struct ospfd_conf *
parse_config(char *filename, int opts)
{
	struct sym	*sym, *next;

	if ((conf = calloc(1, sizeof(struct ospfd_conf))) == NULL)
		fatal("parse_config");
	conf->opts = opts;
	if (conf->opts & OSPFD_OPT_STUB_ROUTER)
		conf->flags |= OSPFD_FLAG_STUB_ROUTER;

	bzero(&globaldefs, sizeof(globaldefs));
	defs = &globaldefs;
	TAILQ_INIT(&defs->md_list);
	defs->dead_interval = DEFAULT_RTR_DEAD_TIME;
	defs->fast_hello_interval = DEFAULT_FAST_INTERVAL;
	defs->transmit_delay = DEFAULT_TRANSMIT_DELAY;
	defs->hello_interval = DEFAULT_HELLO_INTERVAL;
	defs->rxmt_interval = DEFAULT_RXMT_INTERVAL;
	defs->metric = DEFAULT_METRIC;
	defs->priority = DEFAULT_PRIORITY;
	defs->p2p = 0;

	conf->spf_delay = DEFAULT_SPF_DELAY;
	conf->spf_hold_time = DEFAULT_SPF_HOLDTIME;
	conf->spf_state = SPF_IDLE;
	conf->fib_priority = RTP_OSPF;

	if ((file = pushfile(filename,
	    !(conf->opts & OSPFD_OPT_NOACTION))) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	LIST_INIT(&conf->area_list);
	LIST_INIT(&conf->cand_list);
	SIMPLEQ_INIT(&conf->redist_list);

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((conf->opts & OSPFD_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	/* free global config defaults */
	md_list_clr(&globaldefs.md_list);

	/* check that all interfaces belong to the configured rdomain */
	errors += conf_check_rdomain(conf->rdomain);

	if (errors) {
		clear_config(conf);
		return (NULL);
	}

	return (conf);
}

int
symset(const char *nam, const char *val, int persist)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0)
			break;
	}

	if (sym != NULL) {
		if (sym->persist == 1)
			return (0);
		else {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}
	if ((sym = calloc(1, sizeof(*sym))) == NULL)
		return (-1);

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return (-1);
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return (-1);
	}
	sym->used = 0;
	sym->persist = persist;
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return (0);
}

int
cmdline_symset(char *s)
{
	char	*sym, *val;
	int	ret;

	if ((val = strrchr(s, '=')) == NULL)
		return (-1);
	sym = strndup(s, val - s);
	if (sym == NULL)
		errx(1, "%s: strndup", __func__);
	ret = symset(sym, val + 1, 1);
	free(sym);

	return (ret);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	}
	return (NULL);
}

struct area *
conf_get_area(struct in_addr id)
{
	struct area	*a;

	a = area_find(conf, id);
	if (a)
		return (a);
	a = area_new();
	LIST_INSERT_HEAD(&conf->area_list, a, entry);

	a->id.s_addr = id.s_addr;

	return (a);
}

struct iface *
conf_get_if(struct kif *kif, struct kif_addr *ka)
{
	struct area	*a;
	struct iface	*i;

	LIST_FOREACH(a, &conf->area_list, entry)
		LIST_FOREACH(i, &a->iface_list, entry)
			if (i->ifindex == kif->ifindex &&
			    i->addr.s_addr == ka->addr.s_addr) {
				yyerror("interface %s already configured",
				    kif->ifname);
				return (NULL);
			}

	i = if_new(kif, ka);
	i->auth_keyid = 1;

	return (i);
}

int
conf_check_rdomain(unsigned int rdomain)
{
	struct area		*a;
	struct iface		*i;
	struct in_addr		 addr;
	struct kif		*kif;
	struct redistribute	*r;
	int			 errs = 0;

	SIMPLEQ_FOREACH(r, &conf->redist_list, entry)
		if (r->dependon[0] != '\0') {
			bzero(&addr, sizeof(addr));
			kif = kif_findname(r->dependon, addr, NULL);
			if (kif->rdomain != rdomain) {
				logit(LOG_CRIT,
				    "depend on %s: interface not in rdomain %u",
				    kif->ifname, rdomain);
				errs++;
			}
		}

	LIST_FOREACH(a, &conf->area_list, entry)
		LIST_FOREACH(i, &a->iface_list, entry) {
			if (i->rdomain != rdomain) {
				logit(LOG_CRIT,
				    "interface %s not in rdomain %u",
				    i->name, rdomain);
				errs++;
			}
			if (i->dependon[0] != '\0') {
				bzero(&addr, sizeof(addr));
				kif = kif_findname(i->dependon, addr, NULL);
				if (kif->rdomain != rdomain) {
					logit(LOG_CRIT,
					    "depend on %s: interface not in "
					    "rdomain %u",
					    kif->ifname, rdomain);
					errs++;
				}
			}
		}

	return (errs);
}

void
conf_clear_redist_list(struct redist_list *rl)
{
	struct redistribute *r;
	while ((r = SIMPLEQ_FIRST(rl)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(rl, entry);
		free(r);
	}
}

void
clear_config(struct ospfd_conf *xconf)
{
	struct area	*a;

	while ((a = LIST_FIRST(&xconf->area_list)) != NULL) {
		LIST_REMOVE(a, entry);
		area_del(a);
	}

	conf_clear_redist_list(&xconf->redist_list);

	free(xconf);
}

u_int32_t
get_rtr_id(void)
{
	struct ifaddrs		*ifap, *ifa;
	u_int32_t		 ip = 0, cur, localnet;

	localnet = htonl(INADDR_LOOPBACK & IN_CLASSA_NET);

	if (getifaddrs(&ifap) == -1)
		fatal("getifaddrs");

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (strncmp(ifa->ifa_name, "carp", 4) == 0)
			continue;
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET)
			continue;
		cur = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
		if ((cur & localnet) == localnet)	/* skip 127/8 */
			continue;
		if (ntohl(cur) < ntohl(ip) || ip == 0)
			ip = cur;
	}
	freeifaddrs(ifap);

	if (ip == 0)
		fatal("router-id is 0.0.0.0");

	return (ip);
}

int
host(const char *s, struct in_addr *addr, struct in_addr *mask)
{
	struct in_addr		 ina;
	int			 bits = 32;

	bzero(&ina, sizeof(struct in_addr));
	if (strrchr(s, '/') != NULL) {
		if ((bits = inet_net_pton(AF_INET, s, &ina, sizeof(ina))) == -1)
			return (0);
	} else {
		if (inet_pton(AF_INET, s, &ina) != 1)
			return (0);
	}

	addr->s_addr = ina.s_addr;
	mask->s_addr = prefixlen2mask(bits);

	return (1);
}
