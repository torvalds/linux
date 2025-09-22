/*	$OpenBSD: parse.y,v 1.11 2025/04/26 18:05:55 florian Exp $	*/

/*
 * Copyright (c) 2018, 2024 Florian Obser <florian@openbsd.org>
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
#include <sys/socket.h>
#include <sys/stat.h>

#include <net/if.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <vis.h>

#include "log.h"
#include "dhcp6leased.h"
#include "frontend.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
struct file 	 *file, *topfile;
int		 check_file_secrecy(int, const char *);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 lookup(char *);
int		 igetc(void);

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

static struct dhcp6leased_conf	*conf;
static int			 errors;

static struct iface_conf	*iface_conf;
static struct iface_ia_conf	*iface_ia_conf;

struct iface_conf	*conf_get_iface(char *);
struct iface_pd_conf	*conf_get_pd_iface(char *, int);
void			 addressing_plan(struct iface_ia_conf *);
int			 fls64(uint64_t);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	ERROR DELEGATION FOR ON PREFIX REQUEST RAPID COMMIT

%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.string>	string

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar varset '\n'
		| grammar conf_main '\n'
		| grammar ia_pd '\n'
		| grammar error '\n'		{ file->errors++; }
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
			if (log_getverbose() == 1)
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

conf_main	: REQUEST RAPID COMMIT {
			conf->rapid_commit = 1;
		}
		;

ia_pd		: REQUEST PREFIX DELEGATION ON STRING FOR {
			iface_conf = conf_get_iface($5);
			iface_ia_conf = calloc(1, sizeof(*iface_ia_conf));
			if (iface_ia_conf == NULL)
				err(1, "%s: calloc", __func__);
			iface_ia_conf->id = iface_conf->ia_count++;
			if (iface_conf->ia_count > MAX_IA) {
				yyerror("Too many prefix delegation requests");
				YYERROR;
			}
			SIMPLEQ_INIT(&iface_ia_conf->iface_pd_list);
			SIMPLEQ_INSERT_TAIL(&iface_conf->iface_ia_list,
			    iface_ia_conf, entry);
		} iface_block {
			iface_conf = NULL;
			iface_ia_conf = NULL;
		}
		;

iface_block	: '{' optnl ifaceopts_l '}'
		| ifaceoptsl
		;

ifaceopts_l	: ifaceopts_l ifaceoptsl optnl
		| ifaceoptsl optnl
		;

ifaceoptsl	: STRING {
			struct iface_pd_conf	*iface_pd_conf;
			int			 prefixlen;
			char			*p;
			const char		*errstr;

			p = strchr($1, '/');
			if (p != NULL) {
				*p++ = '\0';
				prefixlen = strtonum(p, 0, 128, &errstr);
				if (errstr != NULL) {
					yyerror("error parsing interface "
					    "\"%s/%s\"", $1, p);
					free($1);
					YYERROR;
				}
			} else
				prefixlen = 64;
			if ((iface_pd_conf = conf_get_pd_iface($1, prefixlen))
			    == NULL) {
				yyerror("duplicate interface %s", $1);
				free($1);
				YYERROR;
			}
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
		{"commit",	COMMIT},
		{"delegation",	DELEGATION},
		{"for",		FOR},
		{"on",		ON},
		{"prefix",	PREFIX},
		{"rapid",	RAPID},
		{"request",	REQUEST},
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
		log_warn("%s", nfile->name);
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

struct dhcp6leased_conf *
parse_config(const char *filename)
{
	struct sym		*sym, *next;
	struct iface_conf	*iface;
	struct iface_ia_conf	*ia_conf;

	conf = config_new_empty();

	file = pushfile(filename, 0);
	if (file == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((log_getverbose() == 2) && !sym->used)
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
		config_clear(conf);
		return (NULL);
	}

	SIMPLEQ_FOREACH(iface, &conf->iface_list, entry) {
		SIMPLEQ_FOREACH(ia_conf, &iface->iface_ia_list, entry) {
			addressing_plan(ia_conf);
		}
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

struct iface_conf *
conf_get_iface(char *name)
{
	struct iface_conf	*iface;
	size_t			 n;

	SIMPLEQ_FOREACH(iface, &conf->iface_list, entry) {
		if (strcmp(name, iface->name) == 0)
			return (iface);
	}

	iface = calloc(1, sizeof(*iface));
	if (iface == NULL)
		errx(1, "%s: calloc", __func__);
	n = strlcpy(iface->name, name, sizeof(iface->name));
	if (n >= sizeof(iface->name))
		errx(1, "%s: name too long", __func__);
	SIMPLEQ_INIT(&iface->iface_ia_list);

	SIMPLEQ_INSERT_TAIL(&conf->iface_list, iface, entry);

	return (iface);
}

struct iface_pd_conf *
conf_get_pd_iface(char *name, int prefixlen)
{
	struct iface_ia_conf	*iface_ia;
	struct iface_pd_conf	*iface_pd;
	size_t			 n;

	if (strcmp(name, "reserve") != 0) {
		SIMPLEQ_FOREACH(iface_ia, &iface_conf->iface_ia_list,
		    entry) {
			SIMPLEQ_FOREACH(iface_pd, &iface_ia->iface_pd_list,
			    entry) {
				if (strcmp(name, iface_pd->name) == 0)
					return NULL;
			}
		}
	}

	iface_pd = calloc(1, sizeof(*iface_pd));
	if (iface_pd == NULL)
		err(1, "%s: calloc", __func__);
	n = strlcpy(iface_pd->name, name, sizeof(iface_pd->name));
	if (n >= sizeof(iface_pd->name))
		errx(1, "%s: name too long", __func__);
	iface_pd->prefix_len = prefixlen;

	SIMPLEQ_INSERT_TAIL(&iface_ia_conf->iface_pd_list, iface_pd, entry);

	return (iface_pd);
}

static inline uint64_t
get_shift(int plen)
{
	if (plen > 64)
		plen -= 64;

	return 1ULL << (64 - plen);
}

void
addressing_plan(struct iface_ia_conf *ia_conf)
{
	struct iface_pd_conf	*pd_conf;
	uint64_t		*p, lo_counter, hi_counter, lo_shift, hi_shift;
	int			 prev_plen = -1;

	lo_counter = hi_counter = 0;

	SIMPLEQ_FOREACH(pd_conf, &ia_conf->iface_pd_list, entry) {
		/* not the first prefix */
		if (ia_conf->prefix_len != 0) {
			lo_shift = hi_shift = 0;
			if (prev_plen > pd_conf->prefix_len) {
				if (pd_conf->prefix_len > 64)
					lo_shift =
					    get_shift(pd_conf->prefix_len);
				else
					hi_shift =
					    get_shift(pd_conf->prefix_len);
			} else  {
				if (prev_plen > 64)
					lo_shift = get_shift(prev_plen);
				else
					hi_shift = get_shift(prev_plen);
			}

			if (lo_shift != 0) {
				if (lo_counter > UINT64_MAX - lo_shift) {
					/* overflow */
					hi_counter++;
					lo_counter = 0;
				} else {
					lo_counter += lo_shift;
					/* remove all lower bits */
					lo_counter &= ~(lo_shift - 1);
				}
			} else {
				hi_counter += hi_shift;
				/* remove all lower bits */
				hi_counter &= ~(hi_shift - 1);
				lo_counter = 0;
			}

		} else
			ia_conf->prefix_len = pd_conf->prefix_len;

		p = (uint64_t *)&pd_conf->prefix_mask.s6_addr;
		*p |= htobe64(hi_counter);

		p = (uint64_t *)&pd_conf->prefix_mask.s6_addr[8];
		*p |= htobe64(lo_counter);

		prev_plen = pd_conf->prefix_len;
	}

	if (hi_counter != 0)
		ia_conf->prefix_len = 64 - fls64(hi_counter);
	else if (lo_counter != 0)
		ia_conf->prefix_len = 128 - fls64(lo_counter);
}

/* from NetBSD's sys/sys/bitops.h */
/*-
 * Copyright (c) 2007, 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas and Joerg Sonnenberger.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
int
fls64(uint64_t _n)
{
	int _v;

	if (!_n)
		return 0;

	_v = 64;
	if ((_n & 0xFFFFFFFF00000000ULL) == 0) {
		_n <<= 32;
		_v -= 32;
	}
	if ((_n & 0xFFFF000000000000ULL) == 0) {
		_n <<= 16;
		_v -= 16;
	}
	if ((_n & 0xFF00000000000000ULL) == 0) {
		_n <<= 8;
		_v -= 8;
	}
	if ((_n & 0xF000000000000000ULL) == 0) {
		_n <<= 4;
		_v -= 4;
	}
	if ((_n & 0xC000000000000000ULL) == 0) {
		_n <<= 2;
		_v -= 2;
	}
	if ((_n & 0x8000000000000000ULL) == 0) {
		//_n <<= 1;
		_v -= 1;
	}
	return _v;
}
