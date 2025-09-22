/*	$OpenBSD: parse.y,v 1.27 2024/08/15 07:24:28 yasuoka Exp $	*/

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
#include <sys/queue.h>
#include <sys/socket.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "radiusd.h"
#include "radiusd_local.h"
#include "log.h"

static struct	 radiusd *conf;
static struct	 radiusd_authentication  authen;
static struct	 radiusd_module		*conf_module = NULL;
static struct	 radiusd_client		 client;

static struct radiusd_authentication
		*create_authen(const char *, char **, int, char **);
static struct radiusd_module
		*find_module(const char *);
static void	 free_str_l(void *);
static struct	 radiusd_module_ref *create_module_ref(const char *);
static void	 radiusd_authentication_init(struct radiusd_authentication *);
static void	 radiusd_client_init(struct radiusd_client *);
static const char
		*default_module_path(const char *);

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

typedef struct {
	union {
		int64_t				  number;
		char				 *string;
		struct radiusd_listen		  listen;
		int				  yesno;
		struct {
			char			**v;
			int			  c;
		} str_l;
		struct {
			int			 af;
			struct radiusd_addr	 addr;
			struct radiusd_addr	 mask;
		} prefix;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	INCLUDE LISTEN ON PORT CLIENT SECRET LOAD MODULE MSGAUTH_REQUIRED
%token	ACCOUNT ACCOUNTING AUTHENTICATE AUTHENTICATE_BY AUTHENTICATION_FILTER
%token	BY DECORATE_BY QUICK SET TO ERROR YES NO
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.number>		optport optacct
%type	<v.listen>		listen_addr
%type	<v.str_l>		str_l optdeco
%type	<v.prefix>		prefix
%type	<v.yesno>		yesno optquick
%type	<v.string>		strnum
%type	<v.string>		key
%type	<v.string>		optstring
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar include '\n'
		| grammar listen '\n'
		| grammar client '\n'
		| grammar module '\n'
		| grammar authenticate '\n'
		| grammar account '\n'
		| grammar error '\n'
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
			nfile->lineno--;
		}
		;
listen		: LISTEN ON listen_addr {
			struct radiusd_listen *n;

			if ((n = calloc(1, sizeof(struct radiusd_listen)))
			    == NULL) {
outofmemory:
				yyerror("Out of memory: %s", strerror(errno));
				YYERROR;
			}
			*n = $3;
			TAILQ_INSERT_TAIL(&conf->listen, n, next);
		}
listen_addr	: STRING optacct optport {
			int		 gai_errno;
			struct addrinfo hints, *res;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_flags = AI_PASSIVE;
			hints.ai_flags |= AI_NUMERICHOST | AI_NUMERICSERV;

			if ((gai_errno =
				    getaddrinfo($1, NULL, &hints, &res)) != 0 ||
			    res->ai_addrlen > sizeof($$.addr)) {
				yyerror("Could not parse the address: %s: %s",
				    $1, gai_strerror(gai_errno));
				free($1);
				YYERROR;
			}
			free($1);
			$$.stype = res->ai_socktype;
			$$.sproto = res->ai_protocol;
			$$.accounting = $2;
			memcpy(&$$.addr, res->ai_addr, res->ai_addrlen);
			if ($3 != 0)
				$$.addr.ipv4.sin_port = htons($3);
			else if ($2)
				$$.addr.ipv4.sin_port =
				    htons(RADIUS_ACCT_DEFAULT_PORT);
			else
				$$.addr.ipv4.sin_port =
				    htons(RADIUS_DEFAULT_PORT);

			freeaddrinfo(res);
		}
optacct		: ACCOUNTING { $$ = 1; }
		| { $$ = 0; }
		;
optport		: { $$ = 0; }
		| PORT NUMBER	{ $$ = $2; }
		;
client		: CLIENT {
			radiusd_client_init(&client);
		  } prefix optnl '{' clientopts '}' {
			struct radiusd_client *client0;

			if (client.secret[0] == '\0') {
				yyerror("secret is required for client");
				YYERROR;
			}

			client0 = calloc(1, sizeof(struct radiusd_client));
			if (client0 == NULL)
				goto outofmemory;
			strlcpy(client0->secret, client.secret,
			    sizeof(client0->secret));
			client0->msgauth_required = client.msgauth_required;
			client0->af = $3.af;
			client0->addr = $3.addr;
			client0->mask = $3.mask;
			TAILQ_INSERT_TAIL(&conf->client, client0, next);
		}

clientopts	: clientopts '\n' clientopt
		| clientopt
		;

clientopt	: SECRET STRING {
			if (client.secret[0] != '\0') {
				free($2);
				yyerror("secret is specified already");
				YYERROR;
			} else if (strlcpy(client.secret, $2,
			    sizeof(client.secret)) >= sizeof(client.secret)) {
				free($2);
				yyerror("secret is too long");
				YYERROR;
			}
			free($2);
		}
		| MSGAUTH_REQUIRED yesno {
			client.msgauth_required = $2;
		}
		|
		;

prefix		: STRING '/' NUMBER {
			int		 gai_errno, q, r;
			struct addrinfo	 hints, *res;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_DGRAM;	/* dummy */
			hints.ai_flags |= AI_NUMERICHOST | AI_NUMERICSERV;

			if ((gai_errno = getaddrinfo($1, NULL, &hints, &res))
			    != 0) {
				yyerror("Could not parse the address: %s: %s",
				    $1, gai_strerror(gai_errno));
				free($1);
				YYERROR;
			}
			free($1);
			q = $3 >> 3;
			r = $3 & 7;
			switch (res->ai_family) {
			case AF_INET:
				if ($3 < 0 || 32 < $3) {
					yyerror("mask len %lld is out of range",
					    (long long)$3);
					YYERROR;
				}
				$$.addr.addr.ipv4 = ((struct sockaddr_in *)
				    res->ai_addr)->sin_addr;
				$$.mask.addr.ipv4.s_addr = htonl((uint32_t)
				    ((0xffffffffffULL) << (32 - $3)));
				break;
			case AF_INET6:
				if ($3 < 0 || 128 < $3) {
					yyerror("mask len %lld is out of range",
					    (long long)$3);
					YYERROR;
				}
				$$.addr.addr.ipv6 = ((struct sockaddr_in6 *)
				    res->ai_addr)->sin6_addr;
				memset(&$$.mask.addr.ipv6, 0,
				    sizeof($$.mask.addr.ipv6));
				if (q > 0)
					memset(&$$.mask.addr.ipv6, 0xff, q);
				if (r > 0)
					*((u_char *)&$$.mask.addr.ipv6 + q) =
					    (0xff00 >> r) & 0xff;
				break;
			}
			$$.af = res->ai_family;
			freeaddrinfo(res);
		}
		;
module		: MODULE STRING optstring {
			const char *path = $3;
			if (path == NULL && (path = default_module_path($2))
			    == NULL) {
				yyerror("default path for `%s' is unknown.",
				    $2);
				free($2);
				free($3);
				YYERROR;
			}
			conf_module = radiusd_module_load(conf, path, $2);
			free($2);
			free($3);
			if (conf_module == NULL)
				YYERROR;
			TAILQ_INSERT_TAIL(&conf->module, conf_module, next);
			conf_module = NULL;
		}
		| MODULE STRING optstring {
			const char *path = $3;
			if (path == NULL && (path = default_module_path($2))
			    == NULL) {
				yyerror("default path for `%s' is unknown.",
				    $2);
				free($2);
				free($3);
				YYERROR;
			}
			conf_module = radiusd_module_load(conf, path, $2);
			free($2);
			free($3);
			if (conf_module == NULL)
				YYERROR;
		} '{' moduleopts '}' {
			TAILQ_INSERT_TAIL(&conf->module, conf_module, next);
			conf_module = NULL;
		}
		/* following syntaxes are for backward compatilities */
		| MODULE LOAD STRING STRING {
			struct radiusd_module *module;
			if ((module = radiusd_module_load(conf, $4, $3))
			    == NULL) {
				free($3);
				free($4);
				YYERROR;
			}
			free($3);
			free($4);
			TAILQ_INSERT_TAIL(&conf->module, module, next);
		}
		| MODULE SET STRING key str_l {
			struct radiusd_module	*module;

			module = find_module($3);
			if (module == NULL) {
				yyerror("module `%s' is not found", $3);
setstrerr:
				free($3);
				free($4);
				free_str_l(&$5);
				YYERROR;
			}
			if ($4[0] == '_') {
				yyerror("setting `%s' is not allowed", $4);
				goto setstrerr;
			}
			if (radiusd_module_set(module, $4, $5.c, $5.v)) {
				yyerror("syntax error by module `%s'", $3);
				goto setstrerr;
			}
			free($3);
			free($4);
			free_str_l(&$5);
		}
		;

moduleopts	: moduleopts '\n' moduleopt
		| moduleopt
		;
moduleopt	: /* empty */
		| SET key str_l {
			if ($2[0] == '_') {
				yyerror("setting `%s' is not allowed", $2);
				free($2);
				free_str_l(&$3);
				YYERROR;
			}
			if (radiusd_module_set(conf_module, $2, $3.c, $3.v)) {
				yyerror("syntax error by module `%s'",
				    conf_module->name);
				free($2);
				free_str_l(&$3);
				YYERROR;
			}
			free($2);
			free_str_l(&$3);
		}
		;

key		: STRING
		| SECRET { $$ = strdup("secret"); }
		;

authenticate	: AUTHENTICATE str_l BY STRING optdeco {
			struct radiusd_authentication	*auth;

			auth = create_authen($4, $2.v, $5.c, $5.v);
			free($4);
			free_str_l(&$5);
			if (auth == NULL) {
				free_str_l(&$2);
				YYERROR;
			} else
				TAILQ_INSERT_TAIL(&conf->authen, auth, next);
		}
		| AUTHENTICATION_FILTER str_l BY STRING optdeco {
			struct radiusd_authentication	*auth;

			auth = create_authen($4, $2.v, $5.c, $5.v);
			free($4);
			free_str_l(&$5);
			if (auth == NULL) {
				free_str_l(&$2);
				YYERROR;
			} else {
				auth->isfilter = true;
				TAILQ_INSERT_TAIL(&conf->authen, auth, next);
			}
		}
		/* the followings are for backward compatibilities */
		| AUTHENTICATE str_l optnl '{' {
			radiusd_authentication_init(&authen);
			authen.username = $2.v;
		} authopts '}' {
			int				 i;
			struct radiusd_authentication	*a;

			if (authen.auth == NULL) {
				yyerror("no authentication module specified");
				for (i = 0; authen.username[i] != NULL; i++)
					free(authen.username[i]);
				free(authen.username);
				YYERROR;
			}
			if ((a = calloc(1,
			    sizeof(struct radiusd_authentication))) == NULL) {
				for (i = 0; authen.username[i] != NULL; i++)
					free(authen.username[i]);
				free(authen.username);
				goto outofmemory;
			}
			a->auth = authen.auth;
			authen.auth = NULL;
			a->deco = authen.deco;
			a->username = authen.username;
			TAILQ_INSERT_TAIL(&conf->authen, a, next);
		}
		;

optdeco		: { $$.c = 0; $$.v = NULL; }
		| DECORATE_BY str_l { $$ = $2; }
		;

authopts	: authopts '\n' authopt
		| authopt
		;

authopt		: /* empty */
		| AUTHENTICATE_BY STRING {
			struct radiusd_module_ref	*modref;

			if (authen.auth != NULL) {
				free($2);
				yyerror("authenticate is specified already");
				YYERROR;
			}
			modref = create_module_ref($2);
			free($2);
			if (modref == NULL)
				YYERROR;
			authen.auth = modref;
		}
		| DECORATE_BY str_l {
			int				 i;
			struct radiusd_module_ref	*modref;

			for (i = 0; i < $2.c; i++) {
				if ((modref = create_module_ref($2.v[i]))
				    == NULL) {
					free_str_l(&$2);
					YYERROR;
				}
				TAILQ_INSERT_TAIL(&authen.deco, modref, next);
			}
			free_str_l(&$2);
		}
		;

account		: ACCOUNT optquick str_l TO STRING optdeco {
			int				 i, error = 1;
			struct radiusd_accounting	*acct;
			struct radiusd_module_ref	*modref, *modreft;

			if ((acct = calloc(1,
			    sizeof(struct radiusd_accounting))) == NULL) {
				yyerror("Out of memory: %s", strerror(errno));
				goto account_error;
			}
			if ((acct->acct = create_module_ref($5)) == NULL)
				goto account_error;
			acct->username = $3.v;
			acct->quick = $2;
			TAILQ_INIT(&acct->deco);
			for (i = 0; i < $6.c; i++) {
				if ((modref = create_module_ref($6.v[i]))
				    == NULL)
					goto account_error;
				TAILQ_INSERT_TAIL(&acct->deco, modref, next);
			}
			TAILQ_INSERT_TAIL(&conf->account, acct, next);
			acct = NULL;
			error = 0;
 account_error:
			if (acct != NULL) {
				free(acct->acct);
				TAILQ_FOREACH_SAFE(modref, &acct->deco, next,
				    modreft) {
					TAILQ_REMOVE(&acct->deco, modref, next);
					free(modref);
				}
				free_str_l(&$3);
			}
			free(acct);
			free($5);
			free_str_l(&$6);
			if (error > 0)
				YYERROR;
		}
		;

optquick	: { $$ = 0; }
		| QUICK { $$ = 1; }

str_l		: str_l strnum {
			int	  i;
			char	**v;
			if ((v = calloc(sizeof(char **), $$.c + 2)) == NULL)
				goto outofmemory;
			for (i = 0; i < $$.c; i++)
				v[i] = $$.v[i];
			v[i++] = $2;
			v[i] = NULL;
			$$.c++;
			free($$.v);
			$$.v = v;
		}
		| strnum {
			if (($$.v = calloc(sizeof(char **), 2)) == NULL)
				goto outofmemory;
			$$.v[0] = $1;
			$$.v[1] = NULL;
			$$.c = 1;
		}
		;
strnum		: STRING	{ $$ = $1; }
		| NUMBER {
			/* Treat number as a string */
			asprintf(&($$), "%jd", (intmax_t)$1);
			if ($$ == NULL)
				goto outofmemory;
		}
		;
optnl		:
		| '\n'
		;
optstring	: { $$ = NULL; }
		| STRING { $$ = $1; }
		;
yesno		: YES { $$ = true; }
		| NO  { $$ = false; }
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
		{ "account",			ACCOUNT},
		{ "accounting",			ACCOUNTING},
		{ "authenticate",		AUTHENTICATE},
		{ "authenticate-by",		AUTHENTICATE_BY},
		{ "authentication-filter",	AUTHENTICATION_FILTER},
		{ "by",				BY},
		{ "client",			CLIENT},
		{ "decorate-by",		DECORATE_BY},
		{ "include",			INCLUDE},
		{ "listen",			LISTEN},
		{ "load",			LOAD},
		{ "module",			MODULE},
		{ "msgauth-required",		MSGAUTH_REQUIRED},
		{ "no",				NO},
		{ "on",				ON},
		{ "port",			PORT},
		{ "quick",			QUICK},
		{ "secret",			SECRET},
		{ "set",			SET},
		{ "to",				TO},
		{ "yes",			YES},
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
parse_config(const char *filename, struct radiusd *radiusd)
{
	int				 errors = 0;
	struct radiusd_listen		*l;

	conf = radiusd;
	radiusd_conf_init(conf);
	radiusd_authentication_init(&authen);
	radiusd_client_init(&client);

	if ((file = pushfile(filename)) == NULL) {
		errors++;
		goto out;
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	if (TAILQ_EMPTY(&conf->listen)) {
		if ((l = calloc(1, sizeof(struct radiusd_listen))) == NULL) {
			log_warn("Out of memory");
			return (-1);
		}
		l->stype = SOCK_DGRAM;
		l->sproto = IPPROTO_UDP;
		l->addr.ipv4.sin_family = AF_INET;
		l->addr.ipv4.sin_len = sizeof(struct sockaddr_in);
		l->addr.ipv4.sin_addr.s_addr = htonl(0x7F000001L);
		l->addr.ipv4.sin_port = htons(RADIUS_DEFAULT_PORT);
		TAILQ_INSERT_TAIL(&conf->listen, l, next);
	}
	TAILQ_FOREACH(l, &conf->listen, next) {
		l->sock = -1;
	}
	radiusd_authentication_init(&authen);
	if (conf_module != NULL)
		radiusd_module_unload(conf_module);
out:
	conf = NULL;
	return (errors ? -1 : 0);
}

static struct radiusd_authentication *
create_authen(const char *byname, char **username, int decoc, char **deco)
{
	int				 i;
	struct radiusd_authentication	*auth;
	struct radiusd_module_ref	*modref, *modreft;

	if ((auth = calloc(1, sizeof(struct radiusd_authentication)))
	    == NULL) {
		yyerror("Out of memory: %s", strerror(errno));
		return (NULL);
	}
	if ((auth->auth = create_module_ref(byname)) == NULL)
		goto on_error;

	auth->username = username;
	TAILQ_INIT(&auth->deco);
	for (i = 0; i < decoc; i++) {
		if ((modref = create_module_ref(deco[i])) == NULL)
			goto on_error;
		TAILQ_INSERT_TAIL(&auth->deco, modref, next);
	}
	return (auth);
 on_error:
	TAILQ_FOREACH_SAFE(modref, &auth->deco, next, modreft) {
		TAILQ_REMOVE(&auth->deco, modref, next);
		free(modref);
	}
	free(auth);
	return (NULL);
}

static struct radiusd_module *
find_module(const char *name)
{
	struct radiusd_module	*module;

	TAILQ_FOREACH(module, &conf->module, next) {
		if (strcmp(name, module->name) == 0)
			return (module);
	}

	return (NULL);
}

static void
free_str_l(void *str_l0)
{
	int				  i;
	struct {
		char			**v;
		int			  c;
	}				 *str_l = str_l0;

	for (i = 0; i < str_l->c; i++)
		free(str_l->v[i]);
	free(str_l->v);
}

static struct radiusd_module_ref *
create_module_ref(const char *modulename)
{
	struct radiusd_module		*module;
	struct radiusd_module_ref	*modref;

	if ((module = find_module(modulename)) == NULL) {
		yyerror("module `%s' is not found", modulename);
		return (NULL);
	}
	if ((modref = calloc(1, sizeof(struct radiusd_module_ref))) == NULL) {
		yyerror("Out of memory: %s", strerror(errno));
		return (NULL);
	}
	modref->module = module;

	return (modref);
}

static void
radiusd_authentication_init(struct radiusd_authentication *auth)
{
	free(auth->auth);
	memset(auth, 0, sizeof(struct radiusd_authentication));
	TAILQ_INIT(&auth->deco);
}

static void
radiusd_client_init(struct radiusd_client *clnt)
{
	memset(clnt, 0, sizeof(struct radiusd_client));
	clnt->msgauth_required = true;
}

static const char *
default_module_path(const char *name)
{
	unsigned i;
	struct {
		const char *name;
		const char *path;
	} module_paths[] = {
		{ "bsdauth",	"/usr/libexec/radiusd/radiusd_bsdauth" },
		{ "eap2mschap",	"/usr/libexec/radiusd/radiusd_eap2mschap" },
		{ "file",	"/usr/libexec/radiusd/radiusd_file" },
		{ "ipcp",	"/usr/libexec/radiusd/radiusd_ipcp" },
		{ "radius",	"/usr/libexec/radiusd/radiusd_radius" },
		{ "standard",	"/usr/libexec/radiusd/radiusd_standard" }
	};

	for (i = 0; i < nitems(module_paths); i++) {
		if (strcmp(name, module_paths[i].name) == 0)
			return (module_paths[i].path);
	}

	return (NULL);
}
