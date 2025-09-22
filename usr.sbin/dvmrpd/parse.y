/*	$OpenBSD: parse.y,v 1.40 2021/10/15 15:01:27 naddy Exp $ */

/*
 * Copyright (c) 2004, 2005, 2006 Esben Norby <norby@openbsd.org>
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
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "igmp.h"
#include "dvmrp.h"
#include "dvmrpd.h"
#include "dvmrpe.h"
#include "log.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
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
int		 lgetc(int);
int		 lungetc(int);
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

static struct dvmrpd_conf	*conf;
char				*start_state;
struct iface			*iface = NULL;

static struct {
	u_int32_t	 probe_interval;
	u_int32_t	 query_interval;
	u_int32_t	 query_resp_interval;
	u_int32_t	 startup_query_interval;
	u_int32_t	 startup_query_cnt;
	u_int32_t	 last_member_query_interval;
	u_int32_t	 last_member_query_cnt;
	u_int32_t	 dead_interval;
	u_int16_t	 metric;
	u_int8_t	 robustness;
	u_int8_t	 igmp_version;
} *defs, *grdefs, globaldefs, groupdefs, ifacedefs;

void		 clear_config(struct dvmrpd_conf *xconf);
struct iface	*conf_get_if(struct kif *);
struct iface	*new_group(void);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	INTERFACE FIBUPDATE
%token	GROUP
%token	METRIC PASSIVE
%token	ROBUSTNESS QUERYINTERVAL QUERYRESPINTERVAL
%token	STARTUPQUERYINTERVAL STARTUPQUERYCNT
%token	LASTMEMBERQUERYINTERVAL LASTMEMBERQUERYCNT
%token	IGMPVERSION
%token	ERROR
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.number>	yesno
%type	<v.string>	string

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar conf_main '\n'
		| grammar varset '\n'
		| grammar interface '\n'
		| grammar group '\n'
		| grammar error '\n'		{ file->errors++; }
		;

string		: string STRING {
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

yesno		: STRING {
			if (!strcmp($1, "yes"))
				$$ = 1;
			else if (!strcmp($1, "no"))
				$$ = 0;
			else {
				yyerror("syntax error, "
				    "either yes or no expected");
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

varset		: STRING '=' string		{
			char *s = $1;
			if (conf->opts & DVMRPD_OPT_VERBOSE)
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

conf_main	: FIBUPDATE yesno {
			if ($2 == 0)
				conf->flags |= DVMRPD_FLAG_NO_FIB_UPDATE;
			else
				conf->flags &= ~DVMRPD_FLAG_NO_FIB_UPDATE;
		}
		| defaults
		;

defaults	: LASTMEMBERQUERYCNT NUMBER {
			if ($2 < MIN_LAST_MEMBER_QUERY_CNT ||
			    $2 > MAX_LAST_MEMBER_QUERY_CNT) {
				yyerror("last-member-query-count out of "
				    "range (%d-%d)",
				    MIN_LAST_MEMBER_QUERY_CNT,
				    MAX_LAST_MEMBER_QUERY_CNT);
				YYERROR;
			}
			defs->last_member_query_cnt = $2;
		}
		| LASTMEMBERQUERYINTERVAL NUMBER {
			if ($2 < MIN_LAST_MEMBER_QUERY_INTERVAL ||
			    $2 > MAX_LAST_MEMBER_QUERY_INTERVAL) {
				yyerror("last-member-query-interval out of "
				    "range (%d-%d)",
				    MIN_LAST_MEMBER_QUERY_INTERVAL,
				    MAX_LAST_MEMBER_QUERY_INTERVAL);
				YYERROR;
			}
			defs->last_member_query_interval = $2;
		}
		| METRIC NUMBER {
			if ($2 < MIN_METRIC || $2 > MAX_METRIC) {
				yyerror("metric out of range (%d-%d)",
				    MIN_METRIC, MAX_METRIC);
				YYERROR;
			}
			defs->metric = $2;
		}
		| QUERYINTERVAL NUMBER {
			if ($2 < MIN_QUERY_INTERVAL ||
			    $2 > MAX_QUERY_INTERVAL) {
				yyerror("query-interval out of range (%d-%d)",
				    MIN_QUERY_INTERVAL, MAX_QUERY_INTERVAL);
				YYERROR;
			}
			defs->query_interval = $2;
		}
		| QUERYRESPINTERVAL NUMBER {
			if ($2 < MIN_QUERY_RESP_INTERVAL ||
			    $2 > MAX_QUERY_RESP_INTERVAL) {
				yyerror("query-response-interval out of "
				    "range (%d-%d)",
				    MIN_QUERY_RESP_INTERVAL,
				    MAX_QUERY_RESP_INTERVAL);
				YYERROR;
			}
			defs->query_resp_interval = $2;
		}
		| ROBUSTNESS NUMBER {
			if ($2 < MIN_ROBUSTNESS || $2 > MAX_ROBUSTNESS) {
				yyerror("robustness out of range (%d-%d)",
				    MIN_ROBUSTNESS, MAX_ROBUSTNESS);
				YYERROR;
			}
			defs->robustness = $2;
		}
		| STARTUPQUERYCNT NUMBER {
			if ($2 < MIN_STARTUP_QUERY_CNT ||
			    $2 > MAX_STARTUP_QUERY_CNT) {
				yyerror("startup-query-count out of "
				    "range (%d-%d)",
				    MIN_STARTUP_QUERY_CNT,
				    MAX_STARTUP_QUERY_CNT);
				YYERROR;
			}
			defs->startup_query_cnt = $2;
		}
		| STARTUPQUERYINTERVAL NUMBER {
			if ($2 < MIN_STARTUP_QUERY_INTERVAL ||
			    $2 > MAX_STARTUP_QUERY_INTERVAL) {
				yyerror("startup-query-interval out of "
				    "range (%d-%d)",
				    MIN_STARTUP_QUERY_INTERVAL,
				    MAX_STARTUP_QUERY_INTERVAL);
				YYERROR;
			}
			defs->startup_query_interval = $2;
		}
		| IGMPVERSION NUMBER {
			if ($2 < MIN_IGMP_VERSION ||
			    $2 > MAX_IGMP_VERSION) {
				yyerror("igmp-version out of range (%d-%d)",
				    MIN_IGMP_VERSION, MAX_IGMP_VERSION);
				YYERROR;
			}
			defs->igmp_version = $2;
		}
		;

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl		/* one newline or more */
		;

interface	: INTERFACE STRING	{
			struct kif *kif;

			if ((kif = kif_findname($2)) == NULL) {
				yyerror("unknown interface %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
			iface = conf_get_if(kif);
			if (iface == NULL)
				YYERROR;
			LIST_INSERT_HEAD(&conf->iface_list, iface, entry);

			memcpy(&ifacedefs, defs, sizeof(ifacedefs));
			defs = &ifacedefs;
		} interface_block {
			iface->probe_interval = defs->probe_interval;
			iface->query_interval = defs->query_interval;
			iface->query_resp_interval = defs->query_resp_interval;
			iface->startup_query_interval =
			    defs->startup_query_interval;
			iface->startup_query_cnt = defs->startup_query_cnt;
			iface->last_member_query_interval =
			    defs->last_member_query_interval;
			iface->last_member_query_cnt =
			    defs->last_member_query_cnt;
			iface->dead_interval = defs->dead_interval;
			iface->metric = defs->metric;
			iface->robustness = defs->robustness;
			iface->igmp_version = defs->igmp_version;
			if (grdefs)
				defs = grdefs;
			else
				defs = &globaldefs;
			iface = NULL;
		}
		;

interface_block	: '{' optnl interfaceopts_l '}'
		| '{' optnl '}'
		|
		;

interfaceopts_l	: interfaceopts_l interfaceoptsl
		| interfaceoptsl
		;

interfaceoptsl	: PASSIVE nl		{ iface->passive = 1; }
		| defaults nl
		;

group		: GROUP optnl '{' optnl {
			memcpy(&groupdefs, defs, sizeof(groupdefs));
			grdefs = defs = &groupdefs;
		}
		    groupopts_l '}' {
			grdefs = NULL;
			defs = &globaldefs;
		}
		;

groupopts_l	: groupopts_l groupoptsl
		| groupoptsl
		;

groupoptsl	: interface nl
		| defaults nl
		| error nl
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
		{"fib-update",			FIBUPDATE},
		{"group",			GROUP},
		{"igmp-version",		IGMPVERSION},
		{"interface",			INTERFACE},
		{"last-member-query-count",	LASTMEMBERQUERYCNT},
		{"last-member-query-interval",	LASTMEMBERQUERYINTERVAL},
		{"metric",			METRIC},
		{"passive",			PASSIVE},
		{"query-interval",		QUERYINTERVAL},
		{"query-response-interval",	QUERYRESPINTERVAL},
		{"robustness",			ROBUSTNESS},
		{"startup-query-count",		STARTUPQUERYCNT},
		{"startup-query-interval",	STARTUPQUERYINTERVAL}
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (STRING);
}

#define MAXPUSHBACK	128

char	*parsebuf;
int	 parseindex;
char	 pushback_buffer[MAXPUSHBACK];
int	 pushback_index = 0;

int
lgetc(int quotec)
{
	int		c, next;

	if (parsebuf) {
		/* Read character from the parsebuffer instead of input. */
		if (parseindex >= 0) {
			c = (unsigned char)parsebuf[parseindex++];
			if (c != '\0')
				return (c);
			parsebuf = NULL;
		} else
			parseindex++;
	}

	if (pushback_index)
		return ((unsigned char)pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing "
			    "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		if (file == topfile || popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (parsebuf) {
		parseindex--;
		if (parseindex >= 0)
			return (c);
	}
	if (pushback_index + 1 >= MAXPUSHBACK)
		return (EOF);
	pushback_buffer[pushback_index++] = c;
	return (c);
}

int
findeol(void)
{
	int	c;

	parsebuf = NULL;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = (unsigned char)pushback_buffer[--pushback_index];
		else
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
	if (c == '$' && parsebuf == NULL) {
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
		parsebuf = val;
		parseindex = 0;
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
	nfile->lineno = 1;
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
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

struct dvmrpd_conf *
parse_config(char *filename, int opts)
{
	int		 errors = 0;
	struct sym	*sym, *next;
	struct timeval	 now;

	if ((conf = calloc(1, sizeof(struct dvmrpd_conf))) == NULL) {
		errx(1, "parse_config calloc");
		return (NULL);
	}

	defs = &globaldefs;
	defs->probe_interval = DEFAULT_PROBE_INTERVAL;
	defs->last_member_query_cnt = DEFAULT_LAST_MEMBER_QUERY_CNT;
	defs->last_member_query_interval = DEFAULT_LAST_MEMBER_QUERY_INTERVAL;
	defs->metric = DEFAULT_METRIC;
	defs->query_interval = DEFAULT_QUERY_INTERVAL;
	defs->query_resp_interval = DEFAULT_QUERY_RESP_INTERVAL;
	defs->robustness = DEFAULT_ROBUSTNESS;
	defs->startup_query_cnt = DEFAULT_STARTUP_QUERY_CNT;
	defs->startup_query_interval = DEFAULT_STARTUP_QUERY_INTERVAL;
	defs->igmp_version = DEFAULT_IGMP_VERSION;
	defs->dead_interval = NBR_TMOUT;

	if ((file = pushfile(filename, 1)) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	gettimeofday(&now, NULL);
	conf->gen_id = (u_int32_t)now.tv_sec;	/* for a while after 2038 */
	conf->opts = opts;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((conf->opts & DVMRPD_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

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

struct iface *
conf_get_if(struct kif *kif)
{
	struct iface	*i;

	if (kif->ifindex >= MAXVIFS) {
		yyerror("interface %s index too large", kif->ifname);
		return (NULL);
	}

	LIST_FOREACH(i, &conf->iface_list, entry)
		if (i->ifindex == kif->ifindex) {
			yyerror("interface %s already configured",
			    kif->ifname);
			return (NULL);
		}

	i = if_new(kif);
	i->passive = 0;
	i->recv_query_resp_interval = DEFAULT_QUERY_RESP_INTERVAL;

	return (i);
}

void
clear_config(struct dvmrpd_conf *xconf)
{
	/* XXX clear conf */
		/* ... */
}
