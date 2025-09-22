/*	$OpenBSD: parse.y,v 1.34 2024/08/22 08:17:54 florian Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <ifaddrs.h>
#include <limits.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "log.h"

struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t			 ungetpos;
	size_t			 ungetsize;
	u_char			*ungetbuf;
	int			 eof_reached;
	int			 lineno;
	int			 errors;
};
TAILQ_HEAD(files, file);

struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
TAILQ_HEAD(symhead, sym);

struct config_defaults {
	uint8_t		kvalues[6];
	uint16_t	active_timeout;
	uint8_t		maximum_hops;
	uint8_t		maximum_paths;
	uint8_t		variance;
	struct redist_metric *dflt_metric;
	uint16_t	hello_interval;
	uint16_t	hello_holdtime;
	uint32_t	delay;
	uint32_t	bandwidth;
	uint8_t		splithorizon;
};

typedef struct {
	union {
		int64_t			 number;
		char			*string;
		struct redistribute	*redist;
		struct redist_metric	*redist_metric;
	} v;
	int lineno;
} YYSTYPE;

static int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
static int		 kw_cmp(const void *, const void *);
static int		 lookup(char *);
static int		 igetc(void);
static int		 lgetc(int);
void			 lungetc(int);
static int		 findeol(void);
static int		 yylex(void);
static int		 check_file_secrecy(int, const char *);
static struct file	*pushfile(const char *, int);
static int		 popfile(void);
static int		 yyparse(void);
static int		 symset(const char *, const char *, int);
static char		*symget(const char *);
static struct eigrp	*conf_get_instance(uint16_t);
static struct eigrp_iface *conf_get_if(struct kif *);
int			 conf_check_rdomain(unsigned int);
static void		 clear_config(struct eigrpd_conf *xconf);
static uint32_t	 get_rtr_id(void);
static int		 get_prefix(const char *, union eigrpd_addr *, uint8_t *);

static struct file		*file, *topfile;
static struct files		 files = TAILQ_HEAD_INITIALIZER(files);
static struct symhead		 symhead = TAILQ_HEAD_INITIALIZER(symhead);
static struct eigrpd_conf	*conf;
static int			 errors;

static int			 af;
static struct eigrp		*eigrp;
static struct eigrp_iface	*ei;

static struct config_defaults	 globaldefs;
static struct config_defaults	 afdefs;
static struct config_defaults	 asdefs;
static struct config_defaults	 ifacedefs;
static struct config_defaults	*defs;

%}

%token	ROUTERID AS FIBUPDATE RDOMAIN REDISTRIBUTE METRIC DFLTMETRIC
%token	MAXHOPS MAXPATHS VARIANCE FIBPRIORITY_INT FIBPRIORITY_EXT
%token	FIBPRIORITY_SUMM SUMMARY_ADDR
%token	AF IPV4 IPV6 HELLOINTERVAL HOLDTIME KVALUES ACTIVETIMEOUT
%token	INTERFACE PASSIVE DELAY BANDWIDTH SPLITHORIZON
%token	YES NO
%token	INCLUDE
%token	ERROR
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.number>	yesno no eigrp_af
%type	<v.string>	string
%type	<v.redist>	redistribute
%type	<v.redist_metric> redist_metric opt_red_metric

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar conf_main '\n'
		| grammar varset '\n'
		| grammar af '\n'
		| grammar error '\n'		{ file->errors++; }
		;

include		: INCLUDE STRING {
			struct file	*nfile;

			if ((nfile = pushfile($2,
			    !(global.cmd_opts & EIGRPD_OPT_NOACTION))) == NULL) {
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

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl		/* one newline or more */
		;

yesno		: YES	{ $$ = 1; }
		| NO	{ $$ = 0; }
		;

no		: /* empty */	{ $$ = 0; }
		| NO		{ $$ = 1; }
		;

eigrp_af	: IPV4	{ $$ = AF_INET; }
		| IPV6	{ $$ = AF_INET6; }
		;

varset		: STRING '=' string {
			char *s = $1;
			if (global.cmd_opts & EIGRPD_OPT_VERBOSE)
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
			if (bad_addr_v4(conf->rtr_id)) {
				yyerror("invalid router-id");
				YYERROR;
			}
		}
		| FIBUPDATE yesno {
			if ($2 == 0)
				conf->flags |= EIGRPD_FLAG_NO_FIB_UPDATE;
			else
				conf->flags &= ~EIGRPD_FLAG_NO_FIB_UPDATE;
		}
		| RDOMAIN NUMBER {
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rdomain");
				YYERROR;
			}
			conf->rdomain = $2;
		}
		| FIBPRIORITY_INT NUMBER {
			if ($2 <= RTP_NONE || $2 > RTP_MAX) {
				yyerror("invalid fib-priority");
				YYERROR;
			}
			conf->fib_priority_internal = $2;
		}
		| FIBPRIORITY_EXT NUMBER {
			if ($2 <= RTP_NONE || $2 > RTP_MAX) {
				yyerror("invalid fib-priority");
				YYERROR;
			}
			conf->fib_priority_external = $2;
		}
		| FIBPRIORITY_SUMM NUMBER {
			if ($2 <= RTP_NONE || $2 > RTP_MAX) {
				yyerror("invalid fib-priority");
				YYERROR;
			}
			conf->fib_priority_summary = $2;
		}
		| defaults
		;

af		: AF eigrp_af {
			af = $2;
			afdefs = *defs;
			defs = &afdefs;
		} af_block {
			af = AF_UNSPEC;
			defs = &globaldefs;
		}
		;

af_block	: '{' optnl afopts_l '}'
		| '{' optnl '}'
		|
		;

afopts_l	: afopts_l afoptsl nl
		| afoptsl optnl
		;

afoptsl		: as
		| defaults
		;

as		: AS NUMBER {
			if ($2 < EIGRP_MIN_AS || $2 > EIGRP_MAX_AS) {
				yyerror("invalid autonomous-system");
				YYERROR;
			}
			eigrp = conf_get_instance($2);
			if (eigrp == NULL)
				YYERROR;

			asdefs = *defs;
			defs = &asdefs;
		} as_block {
			memcpy(eigrp->kvalues, defs->kvalues,
			    sizeof(eigrp->kvalues));
			eigrp->active_timeout = defs->active_timeout;
			eigrp->maximum_hops = defs->maximum_hops;
			eigrp->maximum_paths = defs->maximum_paths;
			eigrp->variance = defs->variance;
			eigrp->dflt_metric = defs->dflt_metric;
			eigrp = NULL;
			defs = &afdefs;
		}
		;

as_block	: '{' optnl asopts_l '}'
		| '{' optnl '}'
		|
		;

asopts_l	: asopts_l asoptsl nl
		| asoptsl optnl
		;

asoptsl		: interface
		| redistribute {
			SIMPLEQ_INSERT_TAIL(&eigrp->redist_list, $1, entry);
		}
		| defaults
		;

interface	: INTERFACE STRING {
			struct kif	*kif;

			if ((kif = kif_findname($2)) == NULL) {
				yyerror("unknown interface %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
			ei = conf_get_if(kif);
			if (ei == NULL)
				YYERROR;

			ifacedefs = *defs;
			defs = &ifacedefs;
		} interface_block {
			ei->hello_holdtime = defs->hello_holdtime;
			ei->hello_interval = defs->hello_interval;
			ei->delay = defs->delay;
			ei->bandwidth = defs->bandwidth;
			ei->splithorizon = defs->splithorizon;
			ei = NULL;
			defs = &asdefs;
		}
		;

interface_block	: '{' optnl interfaceopts_l '}'
		| '{' optnl '}'
		|
		;

interfaceopts_l	: interfaceopts_l interfaceoptsl nl
		| interfaceoptsl optnl
		;

interfaceoptsl	: PASSIVE { ei->passive = 1; }
		| SUMMARY_ADDR STRING {
			struct summary_addr	*s, *tmp;

			if ((s = calloc(1, sizeof(*s))) == NULL)
				fatal(NULL);
			if (get_prefix($2, &s->prefix, &s->prefixlen) < 0) {
				yyerror("invalid summary-address");
				free($2);
				free(s);
				YYERROR;
			}
			free($2);

			TAILQ_FOREACH(tmp, &ei->summary_list, entry) {
				if (eigrp_prefixcmp(af, &s->prefix,
				    &tmp->prefix, min(s->prefixlen,
				    tmp->prefixlen)) == 0) {
					yyerror("summary-address conflicts "
					    "with another summary-address "
					    "already configured");
					YYERROR;
				}
			}

			TAILQ_INSERT_TAIL(&ei->summary_list, s, entry);
		}
		| iface_defaults
		;

redistribute	: no REDISTRIBUTE STRING opt_red_metric {
			struct redistribute	*r;

			if ((r = calloc(1, sizeof(*r))) == NULL)
				fatal(NULL);
			if (!strcmp($3, "default"))
				r->type = REDIST_DEFAULT;
			else if (!strcmp($3, "static"))
				r->type = REDIST_STATIC;
			else if (!strcmp($3, "rip"))
				r->type = REDIST_RIP;
			else if (!strcmp($3, "ospf"))
				r->type = REDIST_OSPF;
			else if (!strcmp($3, "connected"))
				r->type = REDIST_CONNECTED;
			else if (get_prefix($3, &r->addr, &r->prefixlen) >= 0)
				r->type = REDIST_ADDR;
			else {
				yyerror("invalid redistribute");
				free($3);
				free(r);
				YYERROR;
			}

			r->af = af;
			if ($1)
				r->type |= REDIST_NO;
			r->metric = $4;
			free($3);
			$$ = r;
		}
		;

redist_metric	: NUMBER NUMBER NUMBER NUMBER NUMBER {
			struct redist_metric	*m;

			if ($1 < MIN_BANDWIDTH || $1 > MAX_BANDWIDTH) {
				yyerror("bandwidth out of range (%d-%d)",
				    MIN_BANDWIDTH, MAX_BANDWIDTH);
				YYERROR;
			}
			if ($2 < MIN_DELAY || $2 > MAX_DELAY) {
				yyerror("delay out of range (%d-%d)",
				    MIN_DELAY, MAX_DELAY);
				YYERROR;
			}
			if ($3 < MIN_RELIABILITY || $3 > MAX_RELIABILITY) {
				yyerror("reliability out of range (%d-%d)",
				    MIN_RELIABILITY, MAX_RELIABILITY);
				YYERROR;
			}
			if ($4 < MIN_LOAD || $4 > MAX_LOAD) {
				yyerror("load out of range (%d-%d)",
				    MIN_LOAD, MAX_LOAD);
				YYERROR;
			}
			if ($5 < MIN_MTU || $5 > MAX_MTU) {
				yyerror("mtu out of range (%d-%d)",
				    MIN_MTU, MAX_MTU);
				YYERROR;
			}

			if ((m = calloc(1, sizeof(*m))) == NULL)
				fatal(NULL);
			m->bandwidth = $1;
			m->delay = $2;
			m->reliability = $3;
			m->load = $4;
			m->mtu = $5;

			$$ = m;
		}
		;

opt_red_metric	: /* empty */		{ $$ = NULL; }
		| METRIC redist_metric 	{ $$ = $2; }
		;

defaults	: KVALUES NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER {
			if ($2 < MIN_KVALUE || $2 > MAX_KVALUE ||
			    $3 < MIN_KVALUE || $3 > MAX_KVALUE ||
			    $4 < MIN_KVALUE || $4 > MAX_KVALUE ||
			    $5 < MIN_KVALUE || $5 > MAX_KVALUE ||
			    $6 < MIN_KVALUE || $6 > MAX_KVALUE ||
			    $7 < MIN_KVALUE || $7 > MAX_KVALUE) {
				yyerror("k-value out of range (%d-%d)",
				    MIN_KVALUE, MAX_KVALUE);
				YYERROR;
			}
			defs->kvalues[0] = $2;
			defs->kvalues[1] = $3;
			defs->kvalues[2] = $4;
			defs->kvalues[3] = $5;
			defs->kvalues[4] = $6;
			defs->kvalues[5] = $7;
		}
		| ACTIVETIMEOUT NUMBER {
			if ($2 < MIN_ACTIVE_TIMEOUT ||
			    $2 > MAX_ACTIVE_TIMEOUT) {
				yyerror("active-timeout out of range (%d-%d)",
				    MIN_ACTIVE_TIMEOUT, MAX_ACTIVE_TIMEOUT);
				YYERROR;
			}
			defs->active_timeout = $2;
		}
		| MAXHOPS NUMBER {
			if ($2 < MIN_MAXIMUM_HOPS ||
			    $2 > MAX_MAXIMUM_HOPS) {
				yyerror("maximum-hops out of range (%d-%d)",
				    MIN_MAXIMUM_HOPS, MAX_MAXIMUM_HOPS);
				YYERROR;
			}
			defs->maximum_hops = $2;
		}
		| MAXPATHS NUMBER {
			if ($2 < MIN_MAXIMUM_PATHS ||
			    $2 > MAX_MAXIMUM_PATHS) {
				yyerror("maximum-paths out of range (%d-%d)",
				    MIN_MAXIMUM_PATHS, MAX_MAXIMUM_PATHS);
				YYERROR;
			}
			defs->maximum_paths = $2;
		}
		| VARIANCE NUMBER {
			if ($2 < MIN_VARIANCE ||
			    $2 > MAX_VARIANCE) {
				yyerror("variance out of range (%d-%d)",
				    MIN_VARIANCE, MAX_VARIANCE);
				YYERROR;
			}
			defs->variance = $2;
		}
		| DFLTMETRIC redist_metric {
			defs->dflt_metric = $2;
		}
		| iface_defaults
		;

iface_defaults	: HELLOINTERVAL NUMBER {
			if ($2 < MIN_HELLO_INTERVAL ||
			    $2 > MAX_HELLO_INTERVAL) {
				yyerror("hello-interval out of range (%d-%d)",
				    MIN_HELLO_INTERVAL, MAX_HELLO_INTERVAL);
				YYERROR;
			}
			defs->hello_interval = $2;
		}
		| HOLDTIME NUMBER {
			if ($2 < MIN_HELLO_HOLDTIME ||
			    $2 > MAX_HELLO_HOLDTIME) {
				yyerror("hold-timel out of range (%d-%d)",
				    MIN_HELLO_HOLDTIME,
				    MAX_HELLO_HOLDTIME);
				YYERROR;
			}
			defs->hello_holdtime = $2;
		}
		| DELAY NUMBER {
			if ($2 < MIN_DELAY || $2 > MAX_DELAY) {
				yyerror("delay out of range (%d-%d)",
				    MIN_DELAY, MAX_DELAY);
				YYERROR;
			}
			defs->delay = $2;
		}
		| BANDWIDTH NUMBER {
			if ($2 < MIN_BANDWIDTH || $2 > MAX_BANDWIDTH) {
				yyerror("bandwidth out of range (%d-%d)",
				    MIN_BANDWIDTH, MAX_BANDWIDTH);
				YYERROR;
			}
			defs->bandwidth = $2;
		}
		| SPLITHORIZON yesno {
			defs->splithorizon = $2;
		}
		;

%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

static int
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

static int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

static int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{"active-timeout",		ACTIVETIMEOUT},
		{"address-family",		AF},
		{"autonomous-system",		AS},
		{"bandwidth",			BANDWIDTH},
		{"default-metric",		DFLTMETRIC},
		{"delay",			DELAY},
		{"fib-priority-external",	FIBPRIORITY_EXT},
		{"fib-priority-internal",	FIBPRIORITY_INT},
		{"fib-priority-summary",	FIBPRIORITY_SUMM},
		{"fib-update",			FIBUPDATE},
		{"hello-interval",		HELLOINTERVAL},
		{"holdtime",			HOLDTIME},
		{"include",			INCLUDE},
		{"interface",			INTERFACE},
		{"ipv4",			IPV4},
		{"ipv6",			IPV6},
		{"k-values",			KVALUES},
		{"maximum-hops",		MAXHOPS},
		{"maximum-paths",		MAXPATHS},
		{"metric",			METRIC},
		{"no",				NO},
		{"passive",			PASSIVE},
		{"rdomain",			RDOMAIN},
		{"redistribute",		REDISTRIBUTE},
		{"router-id",			ROUTERID},
		{"split-horizon",		SPLITHORIZON},
		{"summary-address",		SUMMARY_ADDR},
		{"variance",			VARIANCE},
		{"yes",				YES}
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

static int
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

static int
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

static int
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

static int
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

static struct file *
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

static int
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

struct eigrpd_conf *
parse_config(char *filename)
{
	struct sym	*sym, *next;

	conf = config_new_empty();
	conf->rdomain = 0;
	conf->fib_priority_internal = RTP_EIGRP;
	conf->fib_priority_external = RTP_EIGRP;
	conf->fib_priority_summary = RTP_EIGRP;

	defs = &globaldefs;
	defs->kvalues[0] = defs->kvalues[2] = 1;
	defs->active_timeout = DEFAULT_ACTIVE_TIMEOUT;
	defs->maximum_hops = DEFAULT_MAXIMUM_HOPS;
	defs->maximum_paths = DEFAULT_MAXIMUM_PATHS;
	defs->variance = DEFAULT_VARIANCE;
	defs->hello_holdtime = DEFAULT_HELLO_HOLDTIME;
	defs->hello_interval = DEFAULT_HELLO_INTERVAL;
	defs->delay = DEFAULT_DELAY;
	defs->bandwidth = DEFAULT_BANDWIDTH;
	defs->splithorizon = 1;

	if ((file = pushfile(filename,
	    !(global.cmd_opts & EIGRPD_OPT_NOACTION))) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((global.cmd_opts & EIGRPD_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	/* check that all interfaces belong to the configured rdomain */
	errors += conf_check_rdomain(conf->rdomain);

	if (errors) {
		clear_config(conf);
		return (NULL);
	}

	if (conf->rtr_id.s_addr == 0)
		conf->rtr_id.s_addr = get_rtr_id();

	return (conf);
}

static int
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

static char *
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

static struct eigrp *
conf_get_instance(uint16_t as)
{
	struct eigrp	*e, *tmp;

	if (eigrp_find(conf, af, as)) {
		yyerror("autonomous-system %u already configured"
		    "for address-family %s", as, af_name(af));
		return (NULL);
	}

	e = calloc(1, sizeof(struct eigrp));
	if (e == NULL)
		fatal(NULL);

	e->af = af;
	e->as = as;
	SIMPLEQ_INIT(&e->redist_list);
	TAILQ_INIT(&e->ei_list);
	RB_INIT(&e->nbrs);
	RB_INIT(&e->topology);

	/* start local sequence number used by RTP */
	e->seq_num = 1;

	/* order by address-family and then by autonomous-system */
	TAILQ_FOREACH(tmp, &conf->instances, entry)
		if (tmp->af > e->af ||
		    (tmp->af == e->af && tmp->as > e->as))
			break;
	if (tmp)
		TAILQ_INSERT_BEFORE(tmp, e, entry);
	else
		TAILQ_INSERT_TAIL(&conf->instances, e, entry);

	return (e);
}

static struct eigrp_iface *
conf_get_if(struct kif *kif)
{
	struct eigrp_iface	*e;

	TAILQ_FOREACH(e, &eigrp->ei_list, e_entry)
		if (e->iface->ifindex == kif->ifindex) {
			yyerror("interface %s already configured "
			    "for address-family %s and "
			    "autonomous-system %u", kif->ifname,
			    af_name(af), eigrp->as);
			return (NULL);
		}

	e = eigrp_if_new(conf, eigrp, kif);

	return (e);
}

int
conf_check_rdomain(unsigned int rdomain)
{
	struct iface	*iface;
	int		 errs = 0;

	TAILQ_FOREACH(iface, &conf->iface_list, entry) {
		if (iface->rdomain != rdomain) {
			logit(LOG_CRIT, "interface %s not in rdomain %u",
			    iface->name, rdomain);
			errs++;
		}
	}

	return (errs);
}

static void
clear_config(struct eigrpd_conf *xconf)
{
	struct eigrp		*e;
	struct redistribute	*r;
	struct eigrp_iface	*i;
	struct summary_addr	*s;

	while ((e = TAILQ_FIRST(&xconf->instances)) != NULL) {
		while (!SIMPLEQ_EMPTY(&e->redist_list)) {
			r = SIMPLEQ_FIRST(&e->redist_list);
			SIMPLEQ_REMOVE_HEAD(&e->redist_list, entry);
			free(r);
		}

		while ((i = TAILQ_FIRST(&e->ei_list)) != NULL) {
			RB_REMOVE(iface_id_head, &ifaces_by_id, i);
			TAILQ_REMOVE(&e->ei_list, i, e_entry);
			TAILQ_REMOVE(&e->ei_list, i, i_entry);
			while ((s = TAILQ_FIRST(&i->summary_list)) != NULL) {
				TAILQ_REMOVE(&i->summary_list, s, entry);
				free(s);
			}
			if (TAILQ_EMPTY(&i->iface->ei_list)) {
				TAILQ_REMOVE(&xconf->iface_list, i->iface, entry);
				free(i->iface);
			}
			free(i);
		}

		TAILQ_REMOVE(&xconf->instances, e, entry);
		free(e);
	}

	free(xconf);
}

static uint32_t
get_rtr_id(void)
{
	struct ifaddrs		*ifap, *ifa;
	uint32_t		 ip = 0, cur, localnet;

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

static int
get_prefix(const char *s, union eigrpd_addr *addr, uint8_t *plen)
{
	char			*p, *ps;
	const char		*errstr;
	int			 maxplen;

	switch (af) {
	case AF_INET:
		maxplen = 32;
		break;
	case AF_INET6:
		maxplen = 128;
		break;
	default:
		return (-1);
	}

	if ((p = strrchr(s, '/')) != NULL) {
		*plen = strtonum(p + 1, 0, maxplen, &errstr);
		if (errstr) {
			log_warnx("prefixlen is %s: %s", errstr, p + 1);
			return (-1);
		}
		if ((ps = malloc(strlen(s) - strlen(p) + 1)) == NULL)
			fatal("get_prefix: malloc");
		strlcpy(ps, s, strlen(s) - strlen(p) + 1);
	} else {
		if ((ps = strdup(s)) == NULL)
			fatal("get_prefix: strdup");
		*plen = maxplen;
	}

	memset(addr, 0, sizeof(union eigrpd_addr));
	switch (af) {
	case AF_INET:
		if (inet_pton(AF_INET, ps, &addr->v4) != 1) {
			free(ps);
			return (-1);
		}
		break;
	case AF_INET6:
		if (inet_pton(AF_INET6, ps, &addr->v6) != 1) {
			free(ps);
			return (-1);
		}
		break;
	default:
		free(ps);
		return (-1);
	}
	eigrp_applymask(af, addr, addr, *plen);
	free(ps);

	return (0);
}
