/*	$OpenBSD: parse.y,v 1.31 2025/09/15 08:43:51 florian Exp $	*/

/*
 * Copyright (c) 2018 Florian Obser <florian@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "log.h"
#include "unwind.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t	 		 ungetpos;
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

int	 symset(const char *, const char *, int);
char	*symget(const char *);
int	 check_pref_uniq(enum uw_resolver_type);

static struct uw_conf		*conf;
static int			 errors;

void			 clear_config(struct uw_conf *xconf);
struct sockaddr_storage	*host_ip(const char *);

typedef struct {
	union {
		int64_t				 number;
		char				*string;
		struct force_tree		 force;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	INCLUDE ERROR
%token	FORWARDER DOT PORT ODOT_FORWARDER ODOT_AUTOCONF ODOT_DHCP
%token	AUTHENTICATION NAME PREFERENCE RECURSOR AUTOCONF DHCP STUB
%token	BLOCK LIST LOG FORCE ACCEPT BOGUS

%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.number>	port dot prefopt log acceptbogus
%type	<v.string>	string authname
%type	<v.force>	force_list

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar varset '\n'
		| grammar uw_pref '\n'
		| grammar uw_forwarder '\n'
		| grammar block_list '\n'
		| grammar force '\n'
		| grammar error '\n'		{ file->errors++; }
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2, 0)) == NULL) {
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
			if (cmd_opts & OPT_VERBOSE)
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


optnl		: '\n' optnl		/* zero or more newlines */
		| /*empty*/
		;

block_list		: BLOCK LIST STRING log {
				if (conf->blocklist_file != NULL) {
					yyerror("block list already "
					    "configured");
					free($3);
					YYERROR;
				} else {
					conf->blocklist_file = strdup($3);
					if (conf->blocklist_file == NULL)
						err(1, "strdup");
					free($3);
					conf->blocklist_log = $4;
				}
			}
			;

uw_pref			: PREFERENCE {
				conf->res_pref.len = 0;
				memset(conf->enabled_resolvers, 0,
				    sizeof(conf->enabled_resolvers));
			} pref_block
			;

pref_block		: '{' optnl prefopts_l '}'
			| prefoptsl
			;

prefopts_l		: prefopts_l prefoptsl optnl
			| prefoptsl optnl
			;

prefoptsl		: prefopt {
				if (!check_pref_uniq($1))
					YYERROR;
				if (conf->res_pref.len >= UW_RES_NONE) {
					yyerror("preference list too long");
					YYERROR;
				}
				conf->res_pref.types[conf->res_pref.len++] = $1;
				conf->enabled_resolvers[$1] = 1;
			}
			;

prefopt			: DOT			{ $$ = UW_RES_DOT; }
			| FORWARDER		{ $$ = UW_RES_FORWARDER; }
			| ODOT_FORWARDER	{ $$ = UW_RES_ODOT_FORWARDER; }
			| RECURSOR		{ $$ = UW_RES_RECURSOR; }
			| AUTOCONF		{ $$ = UW_RES_AUTOCONF; }
			| DHCP			{ $$ = UW_RES_AUTOCONF; }
			| ODOT_AUTOCONF		{ $$ = UW_RES_ODOT_AUTOCONF; }
			| ODOT_DHCP		{ $$ = UW_RES_ODOT_AUTOCONF; }
			| STUB			{ $$ = UW_RES_ASR; }
			;

uw_forwarder		: FORWARDER forwarder_block
			;

forwarder_block		: '{' optnl forwarderopts_l '}'
			| forwarderoptsl
			;

forwarderopts_l		: forwarderopts_l forwarderoptsl optnl
			| forwarderoptsl optnl
			;

forwarderoptsl		: STRING port authname dot {
				struct uw_forwarder *uw_fwd;
				struct sockaddr_storage *ss;

				if ((ss = host_ip($1)) == NULL) {
					yyerror("%s is not an ip-address", $1);
					free($1);
					YYERROR;
				}
				free(ss);

				if ((uw_fwd = calloc(1, sizeof(*uw_fwd))) ==
				    NULL)
					err(1, NULL);

				if ($2 < 0 || $2 > (int)USHRT_MAX) {
					yyerror("invalid port: %lld", $2);
					free($1);
					free(uw_fwd);
					YYERROR;
				}
				if ($2 == 0)
					uw_fwd->port = $4 == DOT ? 853 : 53;
				else
					uw_fwd->port = $2;

				if ($3 != NULL && $4 == 0) {
					yyerror("authentication name can only "
					    "be used with DoT");
					free($1);
					free(uw_fwd);
					YYERROR;
				}

				if (strlcpy(uw_fwd->ip, $1, sizeof(uw_fwd->ip))
				    >= sizeof(uw_fwd->ip)) {
					free(uw_fwd);
					yyerror("forwarder %s too long", $1);
					free($1);
					YYERROR;
				}

				if ($4 == DOT && $3 != NULL) {
					if (strlcpy(uw_fwd->auth_name, $3,
					    sizeof(uw_fwd->auth_name))
					    >= sizeof(uw_fwd->auth_name)) {
						free(uw_fwd);
						yyerror("authentication name "
						    "%s too long", $3);
						free($1);
						YYERROR;
					}
				}

				if ($4 == DOT)
					TAILQ_INSERT_TAIL(
					    &conf->uw_dot_forwarder_list,
					    uw_fwd, entry);
				else {
					TAILQ_INSERT_TAIL(
					    &conf->uw_forwarder_list,
					    uw_fwd, entry);
				}
				free($1);
			}
	;

port	:	PORT NUMBER	{ $$ = $2; }
	|	/* empty */	{ $$ = 0; }
	;

authname:	AUTHENTICATION NAME STRING	{ $$ = $3; }
	|	/* empty */			{ $$ = NULL; }
	;

dot	:	DOT				{ $$ = DOT; }
	|	/* empty */			{ $$ = 0; }
	;

log	:	LOG				{ $$ = 1; }
	|	/* empty */			{ $$ = 0; }
	;

force	:	FORCE acceptbogus prefopt '{' force_list optnl '}' {
			struct force_tree_entry *n, *nxt;
			int error = 0;

			RB_FOREACH_SAFE(n, force_tree, &$5, nxt) {
				n->acceptbogus = $2;
				n->type = $3;
				conf->force_resolvers[$3] = 1;
				RB_REMOVE(force_tree, &$5, n);
				if (RB_INSERT(force_tree, &conf->force,
				    n)) {
					yyerror("%s already in an force "
					    "list", n->domain);
					error = 1;
				}
			}
			if (error)
				YYERROR;
		}
	;

acceptbogus:	ACCEPT BOGUS	{ $$ = 1; }
	|	/* empty */	{ $$ = 0; }
	;

force_list:	force_list optnl STRING {
			struct force_tree_entry	*e;
			size_t				 len;

			len = strlen($3);
			e = calloc(1, sizeof(*e));
			if (e == NULL)
				err(1, NULL);
			if (strlcpy(e->domain, $3, sizeof(e->domain)) >=
			    sizeof(e->domain)) {
				yyerror("force %s too long", $3);
				free($3);
				YYERROR;
			}
			free($3);
			if (len == 0 || e->domain[len-1] != '.') {
				if (strlcat(e->domain, ".",
				    sizeof((e->domain))) >=
				    sizeof((e->domain))) {
					yyerror("force %s too long", $3);
					YYERROR;
				}
			}
			if (RB_INSERT(force_tree, &$$, e) != NULL) {
				log_warnx("duplicate force %s", e->domain);
				free(e);
			}
		}
	|	/* empty */ {
			RB_INIT(&$$);
		}
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
	/* This has to be sorted always. */
	static const struct keywords keywords[] = {
		{"DoT",			DOT},
		{"accept",		ACCEPT},
		{"authentication",	AUTHENTICATION},
		{"autoconf",		AUTOCONF},
		{"block",		BLOCK},
		{"bogus",		BOGUS},
		{"dhcp",		DHCP},
		{"dot",			DOT},
		{"force",		FORCE},
		{"forwarder",		FORWARDER},
		{"include",		INCLUDE},
		{"list",		LIST},
		{"log",			LOG},
		{"name",		NAME},
		{"oDoT-autoconf",	ODOT_AUTOCONF},
		{"oDoT-dhcp",		ODOT_DHCP},
		{"oDoT-forwarder",	ODOT_FORWARDER},
		{"port",		PORT},
		{"preference",		PREFERENCE},
		{"recursor",		RECURSOR},
		{"stub",		STUB},
		{"tls",			DOT},
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
			err(1, "lungetc");
		file->ungetbuf = p;
		file->ungetsize *= 2;
	}
	file->ungetbuf[file->ungetpos++] = c;
}

int
findeol(void)
{
	int	c;

	/* Skip to either EOF or the first real EOL. */
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
			err(1, "yylex: strdup");
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
				err(1, "yylex: strdup");
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
		log_warn("calloc");
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		log_warn("strdup");
		free(nfile);
		return (NULL);
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
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
		log_warn("malloc");
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

struct uw_conf *
parse_config(char *filename)
{
	static enum uw_resolver_type	 default_res_pref[] = {
	    UW_RES_DOT,
	    UW_RES_ODOT_FORWARDER,
	    UW_RES_FORWARDER,
	    UW_RES_RECURSOR,
	    UW_RES_ODOT_AUTOCONF,
	    UW_RES_AUTOCONF,
	    UW_RES_ASR};
	struct sym			*sym, *next;
	int				 i;

	conf = config_new_empty();

	memcpy(&conf->res_pref.types, &default_res_pref,
	    sizeof(default_res_pref));
	conf->res_pref.len = nitems(default_res_pref);
	for (i = 0; i < conf->res_pref.len; i++)
		conf->enabled_resolvers[conf->res_pref.types[i]] = 1;

	file = pushfile(filename != NULL ? filename : _PATH_CONF_FILE, 0);
	if (file == NULL) {
		/* no default config file is fine */
		if (errno == ENOENT && filename == NULL)
			return (conf);
		log_warn("%s", filename);
		free(conf);
		return (NULL);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((cmd_opts & OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not used\n",
			    sym->nam);
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

void
clear_config(struct uw_conf *xconf)
{
	struct uw_forwarder	*uw_forwarder;

	while ((uw_forwarder = TAILQ_FIRST(&xconf->uw_forwarder_list)) !=
	    NULL) {
		TAILQ_REMOVE(&xconf->uw_forwarder_list, uw_forwarder, entry);
		free(uw_forwarder);
	}
	while ((uw_forwarder = TAILQ_FIRST(&xconf->uw_dot_forwarder_list)) !=
	    NULL) {
		TAILQ_REMOVE(&xconf->uw_dot_forwarder_list, uw_forwarder,
		    entry);
		free(uw_forwarder);
	}

	free(xconf);
}

struct sockaddr_storage *
host_ip(const char *s)
{
	struct addrinfo	 hints, *res;
	struct sockaddr_storage	*ss = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, "0", &hints, &res) == 0) {
		if (res->ai_family == AF_INET ||
		    res->ai_family == AF_INET6) {
			if ((ss = calloc(1, sizeof(*ss))) == NULL)
				fatal(NULL);
			memcpy(ss, res->ai_addr, res->ai_addrlen);
		}
		freeaddrinfo(res);
	}

	return (ss);
}

int
check_pref_uniq(enum uw_resolver_type type)
{
	int	 i;

	for (i = 0; i < conf->res_pref.len; i++)
		if (conf->res_pref.types[i] == type) {
			yyerror("%s is already in the preference list",
			    uw_resolver_type_str[type]);
			return (0);
		}

	return (1);
}
