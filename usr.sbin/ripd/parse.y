/*	$OpenBSD: parse.y,v 1.48 2021/10/15 15:01:28 naddy Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "ripd.h"
#include "rip.h"
#include "ripe.h"
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

static struct {
	u_int8_t		 auth_key[MAX_SIMPLE_AUTH_LEN];
	struct auth_md_head	 md_list;
	enum auth_type		 auth_type;
	u_int8_t		 auth_keyid;
	u_int8_t		 cost;
} *defs, globaldefs, ifacedefs;

struct iface	*iface = NULL;
static struct ripd_conf	*conf;
static int		 errors = 0;

struct iface	*conf_get_if(struct kif *);
void		 clear_config(struct ripd_conf *);
int		 check_file_secrecy(int, const char *);
u_int32_t	 get_rtr_id(void);
int		 host(const char *, struct in_addr *, struct in_addr *);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	SPLIT_HORIZON TRIGGERED_UPDATES FIBPRIORITY FIBUPDATE
%token	REDISTRIBUTE RDOMAIN
%token	AUTHKEY AUTHTYPE AUTHMD AUTHMDKEYID
%token	INTERFACE RTLABEL
%token	COST PASSIVE
%token	YES NO
%token	DEMOTE
%token	ERROR
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.number>	yesno no
%type	<v.string>	string

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar conf_main '\n'
		| grammar varset '\n'
		| grammar interface '\n'
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

yesno		: YES	{ $$ = 1; }
		| NO	{ $$ = 0; }
		;

no		: /* empty */	{ $$ = 0; }
		| NO		{ $$ = 1; }

varset		: STRING '=' string {
			char *s = $1;
			if (conf->opts & RIPD_OPT_VERBOSE)
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

conf_main	: SPLIT_HORIZON STRING {
			/* clean flags first */
			conf->options &= ~(OPT_SPLIT_HORIZON |
			    OPT_SPLIT_POISONED);
			if (!strcmp($2, "none"))
				/* nothing */ ;
			else if (!strcmp($2, "simple"))
				conf->options |= OPT_SPLIT_HORIZON;
			else if (!strcmp($2, "poisoned"))
				conf->options |= OPT_SPLIT_POISONED;
			else {
				yyerror("unknown split horizon type");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| TRIGGERED_UPDATES yesno {
			if ($2 == 1)
				conf->options |= OPT_TRIGGERED_UPDATES;
			else
				conf->options &= ~OPT_TRIGGERED_UPDATES;
		}
		| RDOMAIN NUMBER {
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rdomain");
				YYERROR;
			}
			conf->rdomain = $2;
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
				conf->flags |= RIPD_FLAG_NO_FIB_UPDATE;
			else
				conf->flags &= ~RIPD_FLAG_NO_FIB_UPDATE;
		}
		| no REDISTRIBUTE STRING {
			struct redistribute	*r;

			if ((r = calloc(1, sizeof(*r))) == NULL)
				fatal(NULL);
			if (!strcmp($3, "static"))
				r->type = REDIST_STATIC;
			else if (!strcmp($3, "connected"))
				r->type = REDIST_CONNECTED;
			else if (!strcmp($3, "default"))
				r->type = REDIST_DEFAULT;
			else if (host($3, &r->addr, &r->mask))
				r->type = REDIST_ADDR;
			else {
				yyerror("unknown redistribute type");
				free($3);
				free(r);
				YYERROR;
			}

			if ($1)
				r->type |= REDIST_NO;

			SIMPLEQ_INSERT_TAIL(&conf->redist_list, r,
			    entry);

			conf->redistribute |= REDISTRIBUTE_ON;
			free($3);
		}
		| no REDISTRIBUTE RTLABEL STRING {
			struct redistribute	*r;

			if ((r = calloc(1, sizeof(*r))) == NULL)
				fatal(NULL);
			r->type = REDIST_LABEL;
			r->label = rtlabel_name2id($4);
			if ($1)
				r->type |= REDIST_NO;
			free($4);

			SIMPLEQ_INSERT_TAIL(&conf->redist_list, r, entry);
			conf->redistribute |= REDISTRIBUTE_ON;
		}
		| defaults
		;

authmd		: AUTHMD NUMBER STRING {
			if ($2 < MIN_MD_ID || $2 > MAX_MD_ID) {
				yyerror("auth-md key-id out of range "
				    "(%d-%d)", MIN_MD_ID, MAX_MD_ID);
				free($3);
				YYERROR;
			}
			if (md_list_add(&defs->md_list, $2, $3) == -1) {
				yyerror("auth-md key length out of range "
				    "(max length %d)", MD5_DIGEST_LENGTH);
				free($3);
				YYERROR;
			}
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
			bzero(defs->auth_key, MAX_SIMPLE_AUTH_LEN);
			memcpy(defs->auth_key, $2, strlen($2));
			free($2);
		}
		;

defaults	: COST NUMBER {
			if ($2 < 1 || $2 > INFINITY) {
				yyerror("cost out of range (%d-%d)", 1,
				    INFINITY);
				YYERROR;
			}
			defs->cost = $2;
		}
		| authtype
		| authkey
		| authmdkeyid
		| authmd
		;

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl
		;

interface	: INTERFACE STRING {
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
			md_list_copy(&ifacedefs.md_list, &defs->md_list);
			defs = &ifacedefs;
		} interface_block {
			iface->cost = defs->cost;
			iface->auth_type = defs->auth_type;
			iface->auth_keyid = defs->auth_keyid;
			memcpy(iface->auth_key, defs->auth_key,
			    sizeof(iface->auth_key));
			md_list_copy(&iface->auth_md_list, &defs->md_list);
			md_list_clr(&defs->md_list);
			defs = &globaldefs;
		}
		;

interface_block	: /* empty */
		| '{' optnl interfaceopts_l '}'
		| '{' optnl '}'
		;

interfaceopts_l	: interfaceopts_l interfaceoptsl nl
		| interfaceoptsl optnl
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
			    conf->opts & RIPD_OPT_FORCE_DEMOTE) == -1) {
				yyerror("error initializing group \"%s\"",
				    iface->demote_group);
				YYERROR;
			}
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
	    {"auth-key",		AUTHKEY},
	    {"auth-md",			AUTHMD},
	    {"auth-md-keyid",		AUTHMDKEYID},
	    {"auth-type",		AUTHTYPE},
	    {"cost",			COST},
	    {"demote",			DEMOTE},
	    {"fib-priority",		FIBPRIORITY},
	    {"fib-update",		FIBUPDATE},
	    {"interface",		INTERFACE},
	    {"no",			NO},
	    {"passive",			PASSIVE},
	    {"rdomain",			RDOMAIN},
	    {"redistribute",		REDISTRIBUTE},
	    {"rtlabel",			RTLABEL},
	    {"split-horizon",		SPLIT_HORIZON},
	    {"triggered-updates",	TRIGGERED_UPDATES},
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

struct ripd_conf *
parse_config(char *filename, int opts)
{
	struct sym	*sym, *next;

	if ((conf = calloc(1, sizeof(struct ripd_conf))) == NULL)
		fatal("parse_config");

	bzero(&globaldefs, sizeof(globaldefs));
	defs = &globaldefs;
	TAILQ_INIT(&defs->md_list);
	defs->cost = DEFAULT_COST;
	defs->auth_type = AUTH_NONE;
	conf->opts = opts;
	conf->options = OPT_SPLIT_POISONED;
	conf->fib_priority = RTP_RIP;
	SIMPLEQ_INIT(&conf->redist_list);

	if ((file = pushfile(filename, !(conf->opts & RIPD_OPT_NOACTION))) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((conf->opts & RIPD_OPT_VERBOSE2) && !sym->used)
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

	LIST_FOREACH(i, &conf->iface_list, entry)
		if (i->ifindex == kif->ifindex) {
			yyerror("interface %s already configured",
			    kif->ifname);
			return (NULL);
		}

	i = if_new(kif);
	i->auth_keyid = 1;
	i->passive = 0;

	return (i);
}

void
clear_config(struct ripd_conf *xconf)
{
	struct iface	*i;

	while ((i = LIST_FIRST(&conf->iface_list)) != NULL) {
		LIST_REMOVE(i, entry);
		if_del(i);
	}

	free(xconf);
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
