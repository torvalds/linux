/*	$OpenBSD: parse.y,v 1.47 2025/09/16 15:06:02 sthen Exp $ */

/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2016 Sebastian Benoit <benno@openbsd.org>
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
#include <sys/queue.h>
#include <sys/stat.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parse.h"
#include "extern.h"

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
struct file	*pushfile(const char *);
int		 popfile(void);
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

struct authority_c	*conf_new_authority(struct acme_conf *, char *);
struct domain_c		*conf_new_domain(struct acme_conf *, char *);
struct keyfile		*conf_new_keyfile(struct acme_conf *, char *);
void			 clear_config(struct acme_conf *);
const char*		 kt2txt(enum keytype);
void			 print_config(struct acme_conf *);
int			 conf_check_file(char *);

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

static struct acme_conf		*conf;
static struct authority_c	*auth;
static struct domain_c		*domain;
static int			 errors = 0;

typedef struct {
	union {
		int64_t		 number;
		char		*string;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	AUTHORITY URL API ACCOUNT CONTACT
%token	DOMAIN ALTERNATIVE NAME NAMES CERT FULL CHAIN KEY SIGN WITH
%token	CHALLENGEDIR PROFILE
%token	YES NO
%token	INCLUDE
%token	ERROR
%token	RSA ECDSA
%token	INSECURE
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.string>	string
%type	<v.number>	keytype

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar varset '\n'
		| grammar '\n'
		| grammar authority '\n'
		| grammar domain '\n'
		| grammar error '\n'		{ file->errors++; }
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

varset		: STRING '=' string		{
			char *s = $1;
			if (conf->opts & ACME_OPT_VERBOSE)
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
				errx(EXIT_FAILURE, "cannot store variable");
			free($1);
			free($3);
		}
		;

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl		/* one newline or more */
		;

optcommanl	: ',' optnl
		| optnl
		;

authority	: AUTHORITY STRING {
			char *s;
			if ((s = strdup($2)) == NULL)
				err(EXIT_FAILURE, "strdup");
			if ((auth = conf_new_authority(conf, s)) == NULL) {
				free(s);
				yyerror("authority already defined");
				YYERROR;
			}
		} '{' optnl authorityopts_l '}' {
			if (auth->api == NULL) {
				yyerror("authority %s: no api URL specified",
				    auth->name);
				YYERROR;
			}
			if (auth->account == NULL) {
				yyerror("authority %s: no account key file "
				    "specified", auth->name);
				YYERROR;
			}
			auth = NULL;
		}
		;

authorityopts_l	: authorityopts_l authorityoptsl nl
		| authorityoptsl optnl
		;

authorityoptsl	: API URL STRING {
			char *s;
			if (auth->api != NULL) {
				yyerror("duplicate api");
				YYERROR;
			}
			if ((s = strdup($3)) == NULL)
				err(EXIT_FAILURE, "strdup");
			auth->api = s;
		}
		| ACCOUNT KEY STRING keytype{
			char *s;
			if (auth->account != NULL) {
				yyerror("duplicate account");
				YYERROR;
			}
			if ((s = strdup($3)) == NULL)
				err(EXIT_FAILURE, "strdup");
			auth->account = s;
			auth->keytype = $4;
		}
		| CONTACT STRING {
			char *s;
			if (auth->contact != NULL) {
				yyerror("duplicate contact");
				YYERROR;
			}
			if ((s = strdup($2)) == NULL)
				err(EXIT_FAILURE, "strdup");
			auth->contact = s;
		}
		| INSECURE {
			auth->insecure = 1;
		}
		;

domain		: DOMAIN STRING {
			char *s;
			if ((s = strdup($2)) == NULL)
				err(EXIT_FAILURE, "strdup");
			if (!domain_valid(s)) {
				yyerror("%s: bad domain syntax", s);
				free(s);
				YYERROR;
			}
			if ((domain = conf_new_domain(conf, s)) == NULL) {
				free(s);
				yyerror("domain already defined");
				YYERROR;
			}
		} '{' optnl domainopts_l '}' {
			if (domain->domain == NULL) {
				if ((domain->domain = strdup(domain->handle))
				    == NULL)
					err(EXIT_FAILURE, "strdup");
			}
			/* enforce minimum config here */
			if (domain->key == NULL) {
				yyerror("no domain key file specified for "
				    "domain %s", domain->domain);
				YYERROR;
			}
			if (domain->cert == NULL && domain->fullchain == NULL) {
				yyerror("at least certificate file or full "
				    "certificate chain file must be specified "
				    "for domain %s", domain->domain);
				YYERROR;
			}
			domain = NULL;
		}
		;

keytype		: RSA	{ $$ = KT_RSA; }
		| ECDSA	{ $$ = KT_ECDSA; }
		|	{ $$ = KT_RSA; }
		;

domainopts_l	: domainopts_l domainoptsl nl
		| domainoptsl optnl
		;

domainoptsl	: ALTERNATIVE NAMES '{' optnl altname_l '}'
		| DOMAIN NAME STRING {
			char *s;
			if (domain->domain != NULL) {
				yyerror("duplicate domain name");
				YYERROR;
			}
			if ((s = strdup($3)) == NULL)
				err(EXIT_FAILURE, "strdup");
			domain->domain = s;
		}
		| DOMAIN KEY STRING keytype {
			char *s;
			if (domain->key != NULL) {
				yyerror("duplicate key");
				YYERROR;
			}
			if ((s = strdup($3)) == NULL)
				err(EXIT_FAILURE, "strdup");
			if (!conf_check_file(s)) {
				free(s);
				YYERROR;
			}
			if ((conf_new_keyfile(conf, s)) == NULL) {
				free(s);
				yyerror("domain key file already used");
				YYERROR;
			}
			domain->key = s;
			domain->keytype = $4;
		}
		| DOMAIN CERT STRING {
			char *s;
			if (domain->cert != NULL) {
				yyerror("duplicate cert");
				YYERROR;
			}
			if ((s = strdup($3)) == NULL)
				err(EXIT_FAILURE, "strdup");
			if (s[0] != '/') {
				free(s);
				yyerror("not an absolute path");
				YYERROR;
			}
			if ((conf_new_keyfile(conf, s)) == NULL) {
				free(s);
				yyerror("domain cert file already used");
				YYERROR;
			}
			domain->cert = s;
		}
		| DOMAIN CHAIN CERT STRING {
			char *s;
			if (domain->chain != NULL) {
				yyerror("duplicate chain");
				YYERROR;
			}
			if ((s = strdup($4)) == NULL)
				err(EXIT_FAILURE, "strdup");
			if ((conf_new_keyfile(conf, s)) == NULL) {
				free(s);
				yyerror("domain chain file already used");
				YYERROR;
			}
			domain->chain = s;
		}
		| DOMAIN FULL CHAIN CERT STRING {
			char *s;
			if (domain->fullchain != NULL) {
				yyerror("duplicate full chain");
				YYERROR;
			}
			if ((s = strdup($5)) == NULL)
				err(EXIT_FAILURE, "strdup");
			if ((conf_new_keyfile(conf, s)) == NULL) {
				free(s);
				yyerror("domain full chain file already used");
				YYERROR;
			}
			domain->fullchain = s;
		}
		| SIGN WITH STRING {
			char *s;
			if (domain->auth != NULL) {
				yyerror("duplicate sign with");
				YYERROR;
			}
			if ((s = strdup($3)) == NULL)
				err(EXIT_FAILURE, "strdup");
			if (authority_find(conf, s) == NULL) {
				yyerror("sign with: unknown authority");
				free(s);
				YYERROR;
			}
			domain->auth = s;
		}
		| CHALLENGEDIR STRING {
			char *s;
			if (domain->challengedir != NULL) {
				yyerror("duplicate challengedir");
				YYERROR;
			}
			if ((s = strdup($2)) == NULL)
				err(EXIT_FAILURE, "strdup");
			domain->challengedir = s;
		}
		| PROFILE STRING {
			char *s;
			if (domain->profile != NULL) {
				yyerror("duplicate profile");
				YYERROR;
			}
			if ((s = strdup($2)) == NULL)
				err(EXIT_FAILURE, "strdup");
			domain->profile = s;
		}
		;

altname_l	: altname optcommanl altname_l
		| altname optnl
		;

altname		: STRING {
			char			*s;
			struct altname_c	*ac;
			if (!domain_valid($1)) {
				yyerror("bad domain syntax");
				YYERROR;
			}
			if ((ac = calloc(1, sizeof(struct altname_c))) == NULL)
				err(EXIT_FAILURE, "calloc");
			if ((s = strdup($1)) == NULL) {
				free(ac);
				err(EXIT_FAILURE, "strdup");
			}
			ac->domain = s;
			TAILQ_INSERT_TAIL(&domain->altname_list, ac, entry);
			domain->altname_count++;
			/*
			 * XXX we could check if altname is duplicate
			 * or identical to domain->domain
			*/
		}

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
		err(EXIT_FAILURE, "yyerror vasprintf");
	va_end(ap);
	fprintf(stderr, "%s:%d: %s\n", file->name, yylval.lineno, msg);
	free(msg);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return strcmp(k, ((const struct keywords *)e)->k_name);
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{"account",		ACCOUNT},
		{"alternative",		ALTERNATIVE},
		{"api",			API},
		{"authority",		AUTHORITY},
		{"certificate",		CERT},
		{"chain",		CHAIN},
		{"challengedir",	CHALLENGEDIR},
		{"contact",		CONTACT},
		{"domain",		DOMAIN},
		{"ecdsa",		ECDSA},
		{"full",		FULL},
		{"include",		INCLUDE},
		{"insecure",		INSECURE},
		{"key",			KEY},
		{"name",		NAME},
		{"names",		NAMES},
		{"profile",		PROFILE},
		{"rsa",			RSA},
		{"sign",		SIGN},
		{"url",			URL},
		{"with",		WITH},
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p != NULL)
		return p->k_val;
	else
		return STRING;
}

#define	START_EXPAND	1
#define	DONE_EXPAND	2

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
	return c;
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
			return quotec;
		}
		return c;
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
			return '\n';
		}
		while (c == EOF) {
			if (file == topfile || popfile() == EOF)
				return (EOF);
			c = igetc();
		}
	}
	return c;
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
	return ERROR;
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
				return 0;

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return findeol();
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
			return findeol();
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
				return 0;
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return 0;
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
				return findeol();
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return findeol();
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			err(EXIT_FAILURE, "%s", __func__);
		return STRING;
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return findeol();
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
			if (errstr != NULL) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return NUMBER;
		} else {
nodigits:
			while (p > buf + 1)
				lungetc((unsigned char)*--p);
			c = (unsigned char)*--p;
			if (c == '-')
				return c;
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
		if ((token = lookup(buf)) == STRING) {
			if ((yylval.v.string = strdup(buf)) == NULL)
				err(EXIT_FAILURE, "%s", __func__);
		}
		return token;
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return 0;
	return c;
}

struct file *
pushfile(const char *name)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		warn("%s", __func__);
		return NULL;
	}
	if ((nfile->name = strdup(name)) == NULL) {
		warn("%s", __func__);
		free(nfile);
		return NULL;
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		warn("%s: %s", __func__, nfile->name);
		free(nfile->name);
		free(nfile);
		return NULL;
	}
	nfile->lineno = TAILQ_EMPTY(&files) ? 1 : 0;
	nfile->ungetsize = 16;
	nfile->ungetbuf = malloc(nfile->ungetsize);
	if (nfile->ungetbuf == NULL) {
		warn("%s", __func__);
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return NULL;
	}
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return nfile;
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

struct acme_conf *
parse_config(const char *filename, int opts)
{
	struct sym	*sym, *next;

	if ((conf = calloc(1, sizeof(struct acme_conf))) == NULL)
		err(EXIT_FAILURE, "%s", __func__);
	conf->opts = opts;

	if ((file = pushfile(filename)) == NULL) {
		free(conf);
		return NULL;
	}
	topfile = file;

	TAILQ_INIT(&conf->authority_list);
	TAILQ_INIT(&conf->domain_list);

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((conf->opts & ACME_OPT_VERBOSE) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	if (errors != 0) {
		clear_config(conf);
		return NULL;
	}

	if (opts & ACME_OPT_CHECK) {
		if (opts & ACME_OPT_VERBOSE)
			print_config(conf);
		exit(0);
	}


	return conf;
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
		return -1;

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return -1;
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return -1;
	}
	sym->used = 0;
	sym->persist = persist;
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return 0;
}

int
cmdline_symset(char *s)
{
	char	*sym, *val;
	int	ret;

	if ((val = strrchr(s, '=')) == NULL)
		return -1;
	sym = strndup(s, val - s);
	if (sym == NULL)
		errx(EXIT_FAILURE, "%s: strndup", __func__);
	ret = symset(sym, val + 1, 1);
	free(sym);

	return ret;
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return sym->val;
		}
	}
	return NULL;
}

struct authority_c *
conf_new_authority(struct acme_conf *c, char *s)
{
	struct authority_c *a;

	a = authority_find(c, s);
	if (a != NULL)
		return NULL;
	if ((a = calloc(1, sizeof(struct authority_c))) == NULL)
		err(EXIT_FAILURE, "%s", __func__);
	TAILQ_INSERT_TAIL(&c->authority_list, a, entry);

	a->name = s;
	return a;
}

struct authority_c *
authority_find(struct acme_conf *c, char *s)
{
	struct authority_c	*a;

	TAILQ_FOREACH(a, &c->authority_list, entry) {
		if (strncmp(a->name, s, AUTH_MAXLEN) == 0) {
			return a;
		}
	}
	return NULL;
}

struct authority_c *
authority_find0(struct acme_conf *c)
{
	return (TAILQ_FIRST(&c->authority_list));
}

struct domain_c *
conf_new_domain(struct acme_conf *c, char *s)
{
	struct domain_c *d;

	d = domain_find_handle(c, s);
	if (d != NULL)
		return (NULL);
	if ((d = calloc(1, sizeof(struct domain_c))) == NULL)
		err(EXIT_FAILURE, "%s", __func__);
	TAILQ_INSERT_TAIL(&c->domain_list, d, entry);

	d->handle = s;
	TAILQ_INIT(&d->altname_list);

	return d;
}

struct domain_c *
domain_find_handle(struct acme_conf *c, char *s)
{
	struct domain_c	*d;

	TAILQ_FOREACH(d, &c->domain_list, entry) {
		if (strncmp(d->handle, s, DOMAIN_MAXLEN) == 0) {
			return d;
		}
	}
	return NULL;
}

struct keyfile *
conf_new_keyfile(struct acme_conf *c, char *s)
{
	struct keyfile *k;

	LIST_FOREACH(k, &c->used_key_list, entry) {
		if (strncmp(k->name, s, PATH_MAX) == 0) {
			return NULL;
		}
	}

	if ((k = calloc(1, sizeof(struct keyfile))) == NULL)
		err(EXIT_FAILURE, "%s", __func__);
	LIST_INSERT_HEAD(&c->used_key_list, k, entry);

	k->name = s;
	return k;
}

void
clear_config(struct acme_conf *xconf)
{
	struct authority_c	*a;
	struct domain_c		*d;
	struct altname_c	*ac;

	while ((a = TAILQ_FIRST(&xconf->authority_list)) != NULL) {
		TAILQ_REMOVE(&xconf->authority_list, a, entry);
		free(a);
	}
	while ((d = TAILQ_FIRST(&xconf->domain_list)) != NULL) {
		while ((ac = TAILQ_FIRST(&d->altname_list)) != NULL) {
			TAILQ_REMOVE(&d->altname_list, ac, entry);
			free(ac);
		}
		TAILQ_REMOVE(&xconf->domain_list, d, entry);
		free(d);
	}
	free(xconf);
}

const char*
kt2txt(enum keytype kt)
{
	switch (kt) {
	case KT_RSA:
		return "rsa";
	case KT_ECDSA:
		return "ecdsa";
	default:
		return "<unknown>";
	}
}

void
print_config(struct acme_conf *xconf)
{
	struct authority_c	*a;
	struct domain_c		*d;
	struct altname_c	*ac;
	int			 f;

	TAILQ_FOREACH(a, &xconf->authority_list, entry) {
		printf("authority %s {\n", a->name);
		if (a->api != NULL)
			printf("\tapi url \"%s\"\n", a->api);
		if (a->account != NULL)
			printf("\taccount key \"%s\" %s\n", a->account,
			    kt2txt(a->keytype));
		if (a->insecure)
			printf("\tinsecure\n");
		printf("}\n\n");
	}
	TAILQ_FOREACH(d, &xconf->domain_list, entry) {
		f = 0;
		printf("domain %s {\n", d->handle);
		if (d->domain != NULL)
			printf("\tdomain name \"%s\"\n", d->domain);
		TAILQ_FOREACH(ac, &d->altname_list, entry) {
			if (!f)
				printf("\talternative names {");
			if (ac->domain != NULL) {
				printf("%s%s", f ? ", " : " ", ac->domain);
				f = 1;
			}
		}
		if (f)
			printf(" }\n");
		if (d->key != NULL)
			printf("\tdomain key \"%s\" %s\n", d->key, kt2txt(
			    d->keytype));
		if (d->cert != NULL)
			printf("\tdomain certificate \"%s\"\n", d->cert);
		if (d->chain != NULL)
			printf("\tdomain chain certificate \"%s\"\n", d->chain);
		if (d->fullchain != NULL)
			printf("\tdomain full chain certificate \"%s\"\n",
			    d->fullchain);
		if (d->profile != NULL)
			printf("\tprofile \"%s\"\n", d->profile);
		if (d->auth != NULL)
			printf("\tsign with \"%s\"\n", d->auth);
		if (d->challengedir != NULL)
			printf("\tchallengedir \"%s\"\n", d->challengedir);
		printf("}\n\n");
	}
}

/*
 * This isn't RFC1035 compliant, but does the bare minimum in making
 * sure that we don't get bogus domain names on the command line, which
 * might otherwise screw up our directory structure.
 * Returns zero on failure, non-zero on success.
 */
int
domain_valid(const char *cp)
{

	for ( ; *cp != '\0'; cp++)
		if (!(*cp == '.' || *cp == '-' ||
		    *cp == '_' || isalnum((unsigned char)*cp)))
			return 0;
	return 1;
}

int
conf_check_file(char *s)
{
	struct stat st;

	if (s[0] != '/') {
		warnx("%s: not an absolute path", s);
		return 0;
	}
	if (stat(s, &st)) {
		if (errno == ENOENT)
			return 1;
		warn("cannot stat %s", s);
		return 0;
	}
	if (st.st_mode & (S_IRWXG | S_IRWXO)) {
		warnx("%s: group read/writable or world read/writable", s);
		return 0;
	}
	return 1;
}
