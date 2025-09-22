/*	$OpenBSD: parse.y,v 1.10 2024/11/11 15:19:31 florian Exp $	*/

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
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

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
#include "dhcpleased.h"
#include "frontend.h"

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

int	 symset(const char *, const char *, int);
char	*symget(const char *);

static struct dhcpleased_conf	*conf;
static int			 errors;

static struct iface_conf	*iface_conf;

struct iface_conf	*conf_get_iface(char *);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	DHCP_IFACE ERROR SEND VENDOR CLASS ID CLIENT IGNORE DNS ROUTES HOST NAME
%token	NO PREFER IPV6

%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.string>	string

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar varset '\n'
		| grammar dhcp_iface '\n'
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

nl		: '\n' optnl		/* one or more newlines */
		;

dhcp_iface	: DHCP_IFACE STRING {
			iface_conf = conf_get_iface($2);
		} '{' iface_block '}' {
			iface_conf = NULL;
		}
		;

iface_block	: optnl ifaceopts_l
		;

ifaceopts_l	: ifaceopts_l ifaceoptsl nl
		| ifaceoptsl optnl
		;

ifaceoptsl	: SEND VENDOR CLASS ID STRING {
			ssize_t len;
			char	buf[256];

			if (iface_conf->vc_id != NULL) {
				yyerror("vendor class id already set");
				YYERROR;
			}

			len = strnunvis(buf, $5, sizeof(buf));
			free($5);

			if (len == -1) {
				yyerror("invalid vendor class id");
				YYERROR;
			}
			if ((size_t)len >= sizeof(buf)) {
				yyerror("vendor class id too long");
				YYERROR;
			}

			iface_conf->vc_id_len = 2 + strlen(buf);
			iface_conf->vc_id = malloc(iface_conf->vc_id_len);
			if (iface_conf->vc_id == NULL) {
				yyerror("malloc");
				YYERROR;
			}
			iface_conf->vc_id[0] = DHO_DHCP_CLASS_IDENTIFIER;
			iface_conf->vc_id[1] = iface_conf->vc_id_len - 2;
			memcpy(&iface_conf->vc_id[2], buf,
			    iface_conf->vc_id_len - 2);
		}
		| SEND CLIENT ID STRING {
			size_t			 i;
			ssize_t			 len;
			int			 not_hex = 0, val;
			char			 buf[256], *hex, *p, excess;

			if (iface_conf->c_id != NULL) {
				yyerror("client-id already set");
				YYERROR;
			}

			/* parse as hex string including the type byte */
			if ((hex = strdup($4)) == NULL) {
				free($4);
				yyerror("malloc");
				YYERROR;
			}
			for (i = 0; (p = strsep(&hex, ":")) != NULL && i <
				 sizeof(buf); ) {
				if (sscanf(p, "%x%c", &val, &excess) != 1 ||
				    val < 0 || val > 0xff) {
					not_hex = 1;
					break;
				}
				buf[i++] = (val & 0xff);
			}
			if (p != NULL && i == sizeof(buf))
				not_hex = 1;
			free(hex);

			if (not_hex) {
				len = strnunvis(buf, $4, sizeof(buf));
				free($4);

				if (len == -1) {
					yyerror("invalid client-id");
					YYERROR;
				}
				if ((size_t)len >= sizeof(buf)) {
					yyerror("client-id too long");
					YYERROR;
				}
				iface_conf->c_id_len = 2 + len;
				iface_conf->c_id = malloc(iface_conf->c_id_len);
				if (iface_conf->c_id == NULL) {
					yyerror("malloc");
					YYERROR;
				}
				memcpy(&iface_conf->c_id[2], buf,
				    iface_conf->c_id_len - 2);
			} else {
				free($4);
				iface_conf->c_id_len = 2 + i;
				iface_conf->c_id = malloc(iface_conf->c_id_len);
				if (iface_conf->c_id == NULL) {
					yyerror("malloc");
					YYERROR;
				}
				memcpy(&iface_conf->c_id[2], buf,
				    iface_conf->c_id_len - 2);
			}
			iface_conf->c_id[0] = DHO_DHCP_CLIENT_IDENTIFIER;
			iface_conf->c_id[1] = iface_conf->c_id_len - 2;
		}
		| SEND HOST NAME STRING {
			if (iface_conf->h_name != NULL) {
				free($4);
				yyerror("host name already set");
				YYERROR;
			}
			if (strlen($4) > 255) {
				free($4);
				yyerror("host name too long");
				YYERROR;
			}
			iface_conf->h_name = $4;
		}
		| SEND NO HOST NAME {
			if (iface_conf->h_name != NULL) {
				yyerror("host name already set");
				YYERROR;
			}

			if ((iface_conf->h_name = strdup("")) == NULL) {
				yyerror("malloc");
				YYERROR;
			}
		}
		| IGNORE ROUTES {
			iface_conf->ignore |= IGN_ROUTES;
		}
		| IGNORE DNS {
			iface_conf->ignore |= IGN_DNS;
		}
		| IGNORE STRING {
			int res;

			if (iface_conf->ignore_servers_len >= MAX_SERVERS) {
				yyerror("too many servers to ignore");
				free($2);
				YYERROR;
			}
			res = inet_pton(AF_INET, $2,
			    &iface_conf->ignore_servers[
			    iface_conf->ignore_servers_len++]);

			if (res != 1) {
				yyerror("Invalid server IP %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
		}
		| PREFER IPV6 {
			iface_conf->prefer_ipv6 = 1;
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
		{"class",		CLASS},
		{"client",		CLIENT},
		{"dns",			DNS},
		{"host",		HOST},
		{"id",			ID},
		{"ignore",		IGNORE},
		{"interface",		DHCP_IFACE},
		{"ipv6",		IPV6},
		{"name",		NAME},
		{"no",			NO},
		{"prefer",		PREFER},
		{"routes",		ROUTES},
		{"send",		SEND},
		{"vendor",		VENDOR},
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

struct dhcpleased_conf *
parse_config(const char *filename)
{
	extern const char	 default_conffile[];
	struct sym		*sym, *next;

	conf = config_new_empty();

	file = pushfile(filename, 0);
	if (file == NULL) {
		/* no default config file is fine */
		if (errno == ENOENT && filename == default_conffile)
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


	SIMPLEQ_INSERT_TAIL(&conf->iface_list, iface, entry);

	return (iface);
}
