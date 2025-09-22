/*	$OpenBSD: parse.y,v 1.78 2021/10/15 15:01:28 naddy Exp $ */

/*
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
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "ntpd.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *);
int		 popfile(void);
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

struct sockaddr_in		 query_addr4;
struct sockaddr_in6		 query_addr6;
int				 poolseqnum;

struct opts {
	int		weight;
	int		correction;
	int		stratum;
	int		rtable;
	int		trusted;
	char		*refstr;
} opts;
void		opts_default(void);

typedef struct {
	union {
		int64_t			 number;
		char			*string;
		struct ntp_addr_wrap	*addr;
		struct opts		 opts;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	LISTEN ON CONSTRAINT CONSTRAINTS FROM QUERY TRUSTED
%token	SERVER SERVERS SENSOR CORRECTION RTABLE REFID STRATUM WEIGHT
%token	ERROR
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.addr>		address url urllist
%type	<v.opts>		listen_opts listen_opts_l listen_opt
%type	<v.opts>		server_opts server_opts_l server_opt
%type	<v.opts>		sensor_opts sensor_opts_l sensor_opt
%type	<v.opts>		correction
%type	<v.opts>		rtable
%type	<v.opts>		refid
%type	<v.opts>		stratum
%type	<v.opts>		weight
%type	<v.opts>		trusted
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar main '\n'
		| grammar error '\n'		{ file->errors++; }
		;

main		: LISTEN ON address listen_opts	{
			struct listen_addr	*la;
			struct ntp_addr		*h, *next;

			if ((h = $3->a) == NULL &&
			    (host_dns($3->name, 0, &h) == -1 || !h)) {
				yyerror("could not resolve \"%s\"", $3->name);
				free($3->name);
				free($3);
				YYERROR;
			}

			for (; h != NULL; h = next) {
				next = h->next;
				la = calloc(1, sizeof(struct listen_addr));
				if (la == NULL)
					fatal("listen on calloc");
				la->fd = -1;
				la->rtable = $4.rtable;
				memcpy(&la->sa, &h->ss,
				    sizeof(struct sockaddr_storage));
				TAILQ_INSERT_TAIL(&conf->listen_addrs, la,
				    entry);
				free(h);
			}
			free($3->name);
			free($3);
		}
		| QUERY FROM STRING {
			struct sockaddr_in sin4;
			struct sockaddr_in6 sin6;

			memset(&sin4, 0, sizeof(sin4));
			sin4.sin_family = AF_INET;
			sin4.sin_len = sizeof(struct sockaddr_in);
			memset(&sin6, 0, sizeof(sin6));
			sin6.sin6_family = AF_INET6;
			sin6.sin6_len = sizeof(struct sockaddr_in6);

			if (inet_pton(AF_INET, $3, &sin4.sin_addr) == 1)
				memcpy(&query_addr4, &sin4, sin4.sin_len);
			else if (inet_pton(AF_INET6, $3, &sin6.sin6_addr) == 1)
				memcpy(&query_addr6, &sin6, sin6.sin6_len);
			else {
				yyerror("invalid IPv4 or IPv6 address: %s\n",
				    $3);
				free($3);
				YYERROR;
			}

			free($3);
		}
		| SERVERS address server_opts	{
			struct ntp_peer		*p;
			struct ntp_addr		*h, *next;

			h = $2->a;
			do {
				if (h != NULL) {
					next = h->next;
					if (h->ss.ss_family != AF_INET &&
					    h->ss.ss_family != AF_INET6) {
						yyerror("IPv4 or IPv6 address "
						    "or hostname expected");
						free(h);
						free($2->name);
						free($2);
						YYERROR;
					}
					h->next = NULL;
				} else
					next = NULL;

				p = new_peer();
				p->weight = $3.weight;
				p->trusted = $3.trusted;
				conf->trusted_peers = conf->trusted_peers ||
				    $3.trusted;
				p->query_addr4 = query_addr4;
				p->query_addr6 = query_addr6;
				p->addr = h;
				p->addr_head.a = h;
				p->addr_head.pool = ++poolseqnum;
				p->addr_head.name = strdup($2->name);
				if (p->addr_head.name == NULL)
					fatal(NULL);
				if (p->addr != NULL)
					p->state = STATE_DNS_DONE;
				TAILQ_INSERT_TAIL(&conf->ntp_peers, p, entry);
				h = next;
			} while (h != NULL);

			free($2->name);
			free($2);
		}
		| SERVER address server_opts {
			struct ntp_peer		*p;
			struct ntp_addr		*h, *next;

			p = new_peer();
			for (h = $2->a; h != NULL; h = next) {
				next = h->next;
				if (h->ss.ss_family != AF_INET &&
				    h->ss.ss_family != AF_INET6) {
					yyerror("IPv4 or IPv6 address "
					    "or hostname expected");
					free(h);
					free(p);
					free($2->name);
					free($2);
					YYERROR;
				}
				h->next = p->addr;
				p->addr = h;
			}

			p->weight = $3.weight;
			p->trusted = $3.trusted;
			conf->trusted_peers = conf->trusted_peers ||
			    $3.trusted;
			p->query_addr4 = query_addr4;
			p->query_addr6 = query_addr6;
			p->addr_head.a = p->addr;
			p->addr_head.pool = 0;
			p->addr_head.name = strdup($2->name);
			if (p->addr_head.name == NULL)
				fatal(NULL);
			if (p->addr != NULL)
				p->state = STATE_DNS_DONE;
			TAILQ_INSERT_TAIL(&conf->ntp_peers, p, entry);
			free($2->name);
			free($2);
		}
		| CONSTRAINTS FROM url		{
			struct constraint	*p;
			struct ntp_addr		*h, *next;

			h = $3->a;
			do {
				if (h != NULL) {
					next = h->next;
					if (h->ss.ss_family != AF_INET &&
					    h->ss.ss_family != AF_INET6) {
						yyerror("IPv4 or IPv6 address "
						    "or hostname expected");
						free(h);
						free($3->name);
						free($3->path);
						free($3);
						YYERROR;
					}
					h->next = NULL;
				} else
					next = NULL;

				p = new_constraint();
				p->addr = h;
				p->addr_head.a = h;
				p->addr_head.pool = ++poolseqnum;
				p->addr_head.name = strdup($3->name);
				p->addr_head.path = strdup($3->path);
				if (p->addr_head.name == NULL ||
				    p->addr_head.path == NULL)
					fatal(NULL);
				if (p->addr != NULL)
					p->state = STATE_DNS_DONE;
				constraint_add(p);
				h = next;
			} while (h != NULL);

			free($3->name);
			free($3);
		}
		| CONSTRAINT FROM urllist		{
			struct constraint	*p;
			struct ntp_addr		*h, *next;

			p = new_constraint();
			for (h = $3->a; h != NULL; h = next) {
				next = h->next;
				if (h->ss.ss_family != AF_INET &&
				    h->ss.ss_family != AF_INET6) {
					yyerror("IPv4 or IPv6 address "
					    "or hostname expected");
					free(h);
					free(p);
					free($3->name);
					free($3->path);
					free($3);
					YYERROR;
				}
				h->next = p->addr;
				p->addr = h;
			}

			p->addr_head.a = p->addr;
			p->addr_head.pool = 0;
			p->addr_head.name = strdup($3->name);
			p->addr_head.path = strdup($3->path);
			if (p->addr_head.name == NULL ||
			    p->addr_head.path == NULL)
				fatal(NULL);
			if (p->addr != NULL)
				p->state = STATE_DNS_DONE;
			constraint_add(p);
			free($3->name);
			free($3);
		}
		| SENSOR STRING	sensor_opts {
			struct ntp_conf_sensor	*s;

			s = new_sensor($2);
			s->weight = $3.weight;
			s->correction = $3.correction;
			s->refstr = $3.refstr;
			s->stratum = $3.stratum;
			s->trusted = $3.trusted;
			conf->trusted_sensors = conf->trusted_sensors ||
			    $3.trusted;
			free($2);
			TAILQ_INSERT_TAIL(&conf->ntp_conf_sensors, s, entry);
		}
		;

address		: STRING		{
			if (($$ = calloc(1, sizeof(struct ntp_addr_wrap))) ==
			    NULL)
				fatal(NULL);
			host($1, &$$->a);
			$$->name = $1;
		}
		;

urllist		: urllist address {
			struct ntp_addr *p, *q = NULL;
			struct in_addr ina;
			struct in6_addr in6a;

			if (inet_pton(AF_INET, $2->name, &ina) != 1 &&
			    inet_pton(AF_INET6, $2->name, &in6a) != 1) {
				yyerror("url can only be followed by IP "
				    "addresses");
				free($2->name);
				free($2);
				YYERROR;
			}
			p = $2->a;
			while (p != NULL) {
				q = p;
				p = p->next;
			}
			if (q != NULL) {
				q->next = $1->a;
				$1->a = $2->a;
				free($2);
			}
			$$ = $1;
		}
		| url {
			$$ = $1;
		}
		;

url		: STRING		{
			char	*hname, *path;

			if (($$ = calloc(1, sizeof(struct ntp_addr_wrap))) ==
			    NULL)
				fatal("calloc");

			if (strncmp("https://", $1,
			    strlen("https://")) != 0) {
				host($1, &$$->a);
				$$->name = $1;
			} else {
				hname = $1 + strlen("https://");

				path = hname + strcspn(hname, "/\\");
				if (*path != '\0') {
					if (($$->path = strdup(path)) == NULL)
						fatal("strdup");
					*path = '\0';
				}
				host(hname, &$$->a);
				if (($$->name = strdup(hname)) == NULL)
					fatal("strdup");
			}
			if ($$->path == NULL &&
			    ($$->path = strdup("/")) == NULL)
				fatal("strdup");
		}
		;

listen_opts	:	{ opts_default(); }
		  listen_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
listen_opts_l	: listen_opts_l listen_opt
		| listen_opt
		;
listen_opt	: rtable
		;

server_opts	:	{ opts_default(); }
		  server_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
server_opts_l	: server_opts_l server_opt
		| server_opt
		;
server_opt	: weight
		| trusted
		;

sensor_opts	:	{ opts_default(); }
		  sensor_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
sensor_opts_l	: sensor_opts_l sensor_opt
		| sensor_opt
		;
sensor_opt	: correction
		| refid
		| stratum
		| weight
		| trusted
		;

correction	: CORRECTION NUMBER {
			if ($2 < -127000000 || $2 > 127000000) {
				yyerror("correction must be between "
				    "-127000000 and 127000000 microseconds");
				YYERROR;
			}
			opts.correction = $2;
		}
		;

refid		: REFID STRING {
			size_t l = strlen($2);

			if (l < 1 || l > 4) {
				yyerror("refid must be 1 to 4 characters");
				free($2);
				YYERROR;
			}
			opts.refstr = $2;
		}
		;

stratum		: STRATUM NUMBER {
			if ($2 < 1 || $2 > 15) {
				yyerror("stratum must be between "
				    "1 and 15");
				YYERROR;
			}
			opts.stratum = $2;
		}
		;

weight		: WEIGHT NUMBER	{
			if ($2 < 1 || $2 > 10) {
				yyerror("weight must be between 1 and 10");
				YYERROR;
			}
			opts.weight = $2;
		}
rtable		: RTABLE NUMBER {
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("rtable must be between 1"
				    " and RT_TABLEID_MAX");
				YYERROR;
			}
			opts.rtable = $2;
		}
		;

trusted		: TRUSTED	{
			opts.trusted = 1;
		}

%%

void
opts_default(void)
{
	memset(&opts, 0, sizeof opts);
	opts.weight = 1;
	opts.stratum = 1;
}

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
	log_warnx("%s:%d: %s", file->name, yylval.lineno, msg);
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
		{ "constraint",		CONSTRAINT},
		{ "constraints",	CONSTRAINTS},
		{ "correction",		CORRECTION},
		{ "from",		FROM},
		{ "listen",		LISTEN},
		{ "on",			ON},
		{ "query",		QUERY},
		{ "refid",		REFID},
		{ "rtable",		RTABLE},
		{ "sensor",		SENSOR},
		{ "server",		SERVER},
		{ "servers",		SERVERS},
		{ "stratum",		STRATUM},
		{ "trusted",		TRUSTED},
		{ "weight",		WEIGHT}
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
	char	*p;
	int	 quotec, next, c;
	int	 token;

	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */

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
			fatal("yylex: strdup");
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
	x != '{' && x != '}' && x != '<' && x != '>' && \
	x != '!' && x != '=' && x != '/' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_' || c == '*') {
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
				fatal("yylex: strdup");
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

struct file *
pushfile(const char *name)
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

int
parse_config(const char *filename, struct ntpd_conf *xconf)
{
	int		 errors = 0;

	conf = xconf;
	TAILQ_INIT(&conf->listen_addrs);
	TAILQ_INIT(&conf->ntp_peers);
	TAILQ_INIT(&conf->ntp_conf_sensors);
	TAILQ_INIT(&conf->constraints);

	if ((file = pushfile(filename)) == NULL) {
		return (-1);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	return (errors ? -1 : 0);
}
