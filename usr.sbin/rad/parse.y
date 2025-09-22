/*	$OpenBSD: parse.y,v 1.23 2024/05/17 06:50:14 florian Exp $	*/

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

#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <net/if.h>

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

#include "log.h"
#include "rad.h"
#include "frontend.h"

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

void	 clear_config(struct rad_conf *xconf);

static struct rad_conf		*conf;
static struct ra_options_conf	*ra_options;
static int			 errors;

static struct ra_iface_conf	*ra_iface_conf;
static struct ra_prefix_conf	*ra_prefix_conf;
static struct ra_pref64_conf	*ra_pref64_conf;

struct ra_prefix_conf	*conf_get_ra_prefix(struct in6_addr*, int);
struct ra_pref64_conf	*conf_get_ra_pref64(struct in6_addr*, int);
struct ra_iface_conf	*conf_get_ra_iface(char *);
void			 copy_dns_options(const struct ra_options_conf *,
			    struct ra_options_conf *);
void			 copy_pref64_options(const struct ra_options_conf *,
			    struct ra_options_conf *);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	RA_IFACE YES NO INCLUDE ERROR
%token	DEFAULT ROUTER HOP LIMIT MANAGED ADDRESS
%token	CONFIGURATION OTHER LIFETIME REACHABLE TIME RETRANS TIMER
%token	AUTO PREFIX VALID PREFERENCE PREFERRED LIFETIME ONLINK AUTONOMOUS
%token	ADDRESS_CONFIGURATION DNS NAMESERVER SEARCH MTU NAT64 HIGH MEDIUM LOW
%token	SOURCE LINK_LAYER

%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.number>	yesno
%type	<v.string>	string

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar { ra_options = &conf->ra_options; } conf_main '\n'
		| grammar varset '\n'
		| grammar ra_iface '\n'
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

yesno		: YES	{ $$ = 1; }
		| NO	{ $$ = 0; }
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

conf_main	: ra_opt_block {
			ra_options = &conf->ra_options;
		}
		;

ra_opt_block	: DEFAULT ROUTER yesno {
			ra_options->dfr = $3;
		}
		| HOP LIMIT NUMBER {
			ra_options->cur_hl = $3;
		}
		| MANAGED ADDRESS CONFIGURATION yesno {
			ra_options->m_flag = $4;
		}
		| OTHER CONFIGURATION yesno {
			ra_options->o_flag = $3;
		}
		| ROUTER LIFETIME NUMBER {
			ra_options->router_lifetime = $3;
		}
		| ROUTER PREFERENCE HIGH {
			ra_options->rtpref = ND_RA_FLAG_RTPREF_HIGH;
		}
		| ROUTER PREFERENCE MEDIUM {
			ra_options->rtpref = ND_RA_FLAG_RTPREF_MEDIUM;
		}
		| ROUTER PREFERENCE LOW {
			ra_options->rtpref = ND_RA_FLAG_RTPREF_LOW;
		}
		| REACHABLE TIME NUMBER {
			ra_options->reachable_time = $3;
		}
		| RETRANS TIMER NUMBER {
			ra_options->retrans_timer = $3;
		}
		| SOURCE LINK_LAYER ADDRESS yesno {
			ra_options->source_link_addr = $4;
		}
		| MTU NUMBER {
			ra_options->mtu = $2;
		}
		| NAT64 PREFIX STRING {
			struct in6_addr	 addr;
			int		 prefixlen;
			char		*p;
			const char	*errstr;

			memset(&addr, 0, sizeof(addr));
			p = strchr($3, '/');
			if (p != NULL) {
				*p++ = '\0';
				prefixlen = strtonum(p, 0, 128, &errstr);
				if (errstr != NULL) {
					yyerror("error parsing prefix "
					    "\"%s/%s\"", $3, p);
					free($3);
					YYERROR;
				}
			} else
				prefixlen = 96;

			switch (prefixlen) {
			case 96:
			case 64:
			case 56:
			case 48:
			case 40:
			case 32:
				break;
			default:
				yyerror("invalid nat64 prefix length: %d",
				    prefixlen);
				YYERROR;
				break;
			}
			if(inet_pton(AF_INET6, $3, &addr) == 0) {
				yyerror("error parsing prefix \"%s/%d\"", $3,
				    prefixlen);
				free($3);
				YYERROR;
			}
			mask_prefix(&addr, prefixlen);
			ra_pref64_conf = conf_get_ra_pref64(&addr, prefixlen);
		} ra_pref64_block {
			ra_pref64_conf = NULL;
		}
		| DNS dns_block
		;

optnl		: '\n' optnl		/* zero or more newlines */
		| /*empty*/
		;

nl		: '\n' optnl		/* one or more newlines */
		;

ra_iface	: RA_IFACE STRING {
			ra_iface_conf = conf_get_ra_iface($2);
			/* set auto prefix defaults */
			ra_iface_conf->autoprefix = conf_get_ra_prefix(NULL, 0);
			ra_options = &ra_iface_conf->ra_options;
		} ra_iface_block {
			ra_iface_conf = NULL;
			ra_options = &conf->ra_options;
		}
		;

ra_iface_block	: '{' optnl ra_ifaceopts_l '}'
		| '{' optnl '}'
		| /* empty */
		;

ra_ifaceopts_l	: ra_ifaceopts_l ra_ifaceoptsl nl
		| ra_ifaceoptsl optnl
		;

ra_ifaceoptsl	: NO AUTO PREFIX {
			free(ra_iface_conf->autoprefix);
			ra_iface_conf->autoprefix = NULL;
		}
		| AUTO PREFIX {
			if (ra_iface_conf->autoprefix == NULL)
				ra_iface_conf->autoprefix =
				    conf_get_ra_prefix(NULL, 0);
			ra_prefix_conf = ra_iface_conf->autoprefix;
		} ra_prefix_block {
			ra_prefix_conf = NULL;
		}
		| PREFIX STRING {
			struct in6_addr	 addr;
			int		 prefixlen;
			char		*p;
			const char	*errstr;

			memset(&addr, 0, sizeof(addr));
			p = strchr($2, '/');
			if (p != NULL) {
				*p++ = '\0';
				prefixlen = strtonum(p, 0, 128, &errstr);
				if (errstr != NULL) {
					yyerror("error parsing prefix "
					    "\"%s/%s\"", $2, p);
					free($2);
					YYERROR;
				}
			} else
				prefixlen = 64;
			if(inet_pton(AF_INET6, $2, &addr) == 0) {
				yyerror("error parsing prefix \"%s/%d\"", $2,
				    prefixlen);
				free($2);
				YYERROR;
			}
			mask_prefix(&addr, prefixlen);
			ra_prefix_conf = conf_get_ra_prefix(&addr, prefixlen);
		} ra_prefix_block {
			ra_prefix_conf = NULL;
		}
		| ra_opt_block
		;

ra_prefix_block	: '{' optnl ra_prefixopts_l '}'
		| '{' optnl '}'
		| /* empty */
		;

ra_prefixopts_l	: ra_prefixopts_l ra_prefixoptsl nl
		| ra_prefixoptsl optnl
		;

ra_prefixoptsl	: VALID LIFETIME NUMBER {
			ra_prefix_conf->vltime = $3;
		}
		| PREFERRED LIFETIME NUMBER {
			ra_prefix_conf->pltime = $3;
		}
		| ONLINK yesno {
			ra_prefix_conf->lflag = $2;
		}
		| AUTONOMOUS ADDRESS_CONFIGURATION yesno {
			ra_prefix_conf->aflag = $3;
		}
		;

ra_pref64_block	: '{' optnl ra_pref64opts_l '}'
		| '{' optnl '}'
		| /* empty */
		;

ra_pref64opts_l	: ra_pref64opts_l ra_pref64optsl nl
		| ra_pref64optsl optnl
		;

ra_pref64optsl	: LIFETIME NUMBER {
			if ($2 < 0 || $2 > 65528) {
				yyerror("Invalid nat64 prefix lifetime: %lld",
				    $2);
				YYERROR;
			}
			ra_pref64_conf->ltime = $2;
		}
		;

dns_block	: '{' optnl dnsopts_l '}'
		| '{' optnl '}'
		| /* empty */
		;

dnsopts_l	: dnsopts_l dnsoptsl nl
		| dnsoptsl optnl
		;

dnsoptsl	: LIFETIME NUMBER {
			ra_options->rdns_lifetime = $2;
		}
		| NAMESERVER nserver_block
		| SEARCH search_block
		;
nserver_block	: '{' optnl nserveropts_l '}'
			| '{' optnl '}'
			| nserveroptsl
			| /* empty */
			;

nserveropts_l	: nserveropts_l nserveroptsl optnl
		| nserveroptsl optnl
		;

nserveroptsl	: STRING {
			struct ra_rdnss_conf	*ra_rdnss_conf;
			struct in6_addr		 addr;

			memset(&addr, 0, sizeof(addr));
			if (inet_pton(AF_INET6, $1, &addr)
			    != 1) {
				yyerror("error parsing nameserver address %s",
				    $1);
				free($1);
				YYERROR;
			}
			if ((ra_rdnss_conf = calloc(1, sizeof(*ra_rdnss_conf)))
			    == NULL)
				err(1, "%s", __func__);
			memcpy(&ra_rdnss_conf->rdnss, &addr, sizeof(addr));
			SIMPLEQ_INSERT_TAIL(&ra_options->ra_rdnss_list,
			    ra_rdnss_conf, entry);
			ra_options->rdnss_count++;
		}
		;
search_block	: '{' optnl searchopts_l '}'
		| '{' optnl '}'
		| searchoptsl
		| /* empty */
		;

searchopts_l	: searchopts_l searchoptsl optnl
		| searchoptsl optnl
		;

searchoptsl	: STRING {
			struct ra_dnssl_conf	*ra_dnssl_conf;
			size_t			 len;

			if ((ra_dnssl_conf = calloc(1,
			    sizeof(*ra_dnssl_conf))) == NULL)
				err(1, "%s", __func__);

			if ((len = strlcpy(ra_dnssl_conf->search, $1,
			    sizeof(ra_dnssl_conf->search))) >=
			    sizeof(ra_dnssl_conf->search)) {
				yyerror("search string too long");
				free($1);
				YYERROR;
			}
			if (ra_dnssl_conf->search[len] != '.') {
				if ((len = strlcat(ra_dnssl_conf->search, ".",
				    sizeof(ra_dnssl_conf->search))) >
				    sizeof(ra_dnssl_conf->search)) {
					yyerror("search string too long");
					free($1);
					YYERROR;
				}
			}
			SIMPLEQ_INSERT_TAIL(&ra_options->ra_dnssl_list,
			    ra_dnssl_conf, entry);
			ra_options->dnssl_len += len + 1;
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
		{"address",		ADDRESS},
		{"address-configuration",	ADDRESS_CONFIGURATION},
		{"auto",		AUTO},
		{"autonomous",		AUTONOMOUS},
		{"configuration",	CONFIGURATION},
		{"default",		DEFAULT},
		{"dns",			DNS},
		{"high",		HIGH},
		{"hop",			HOP},
		{"include",		INCLUDE},
		{"interface",		RA_IFACE},
		{"lifetime",		LIFETIME},
		{"limit",		LIMIT},
		{"link-layer",		LINK_LAYER},
		{"low",			LOW},
		{"managed",		MANAGED},
		{"medium",		MEDIUM},
		{"mtu",			MTU},
		{"nameserver",		NAMESERVER},
		{"nat64",		NAT64},
		{"no",			NO},
		{"on-link",		ONLINK},
		{"other",		OTHER},
		{"preference",		PREFERENCE},
		{"preferred",		PREFERRED},
		{"prefix",		PREFIX},
		{"reachable",		REACHABLE},
		{"retrans",		RETRANS},
		{"router",		ROUTER},
		{"search",		SEARCH},
		{"source",		SOURCE},
		{"time",		TIME},
		{"timer",		TIMER},
		{"valid",		VALID},
		{"yes",			YES},
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

struct rad_conf *
parse_config(char *filename)
{
	struct sym		*sym, *next;
	struct ra_iface_conf	*iface;

	conf = config_new_empty();
	ra_options = NULL;

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

	if (!SIMPLEQ_EMPTY(&conf->ra_options.ra_rdnss_list) ||
	    !SIMPLEQ_EMPTY(&conf->ra_options.ra_dnssl_list)) {
		SIMPLEQ_FOREACH(iface, &conf->ra_iface_list, entry)
			copy_dns_options(&conf->ra_options,
			    &iface->ra_options);
	}

	if (!SIMPLEQ_EMPTY(&conf->ra_options.ra_pref64_list)) {
		SIMPLEQ_FOREACH(iface, &conf->ra_iface_list, entry)
			copy_pref64_options(&conf->ra_options,
			    &iface->ra_options);
	}

	return (conf);
}

void
copy_dns_options(const struct ra_options_conf *src, struct ra_options_conf *dst)
{
	struct ra_rdnss_conf	*ra_rdnss, *nra_rdnss;
	struct ra_dnssl_conf	*ra_dnssl, *nra_dnssl;

	if (SIMPLEQ_EMPTY(&dst->ra_rdnss_list)) {
		SIMPLEQ_FOREACH(ra_rdnss, &src->ra_rdnss_list, entry) {
			if ((nra_rdnss = calloc(1, sizeof(*nra_rdnss))) == NULL)
				err(1, "%s", __func__);
			memcpy(nra_rdnss, ra_rdnss, sizeof(*nra_rdnss));
			SIMPLEQ_INSERT_TAIL(&dst->ra_rdnss_list, nra_rdnss,
			    entry);
		}
		dst->rdnss_count = src->rdnss_count;
	}
	if (SIMPLEQ_EMPTY(&dst->ra_dnssl_list)) {
		SIMPLEQ_FOREACH(ra_dnssl, &src->ra_dnssl_list, entry) {
			if ((nra_dnssl = calloc(1, sizeof(*nra_dnssl))) == NULL)
				err(1, "%s", __func__);
			memcpy(nra_dnssl, ra_dnssl, sizeof(*nra_dnssl));
			SIMPLEQ_INSERT_TAIL(&dst->ra_dnssl_list, nra_dnssl,
			    entry);
		}
		dst->dnssl_len = src->dnssl_len;
	}
}

void
copy_pref64_options(const struct ra_options_conf *src, struct ra_options_conf
    *dst)
{
	struct ra_pref64_conf	*pref64, *npref64;

	SIMPLEQ_FOREACH(pref64, &src->ra_pref64_list, entry) {
		if ((npref64 = calloc(1, sizeof(*npref64))) == NULL)
			err(1, "%s", __func__);
		memcpy(npref64, pref64, sizeof(*npref64));
		SIMPLEQ_INSERT_TAIL(&dst->ra_pref64_list, npref64, entry);
	}
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

struct ra_prefix_conf *
conf_get_ra_prefix(struct in6_addr *addr, int prefixlen)
{
	struct ra_prefix_conf	*prefix;

	if (addr == NULL) {
		if (ra_iface_conf->autoprefix != NULL)
			return (ra_iface_conf->autoprefix);
	} else {
		SIMPLEQ_FOREACH(prefix, &ra_iface_conf->ra_prefix_list, entry) {
			if (prefix->prefixlen == prefixlen && memcmp(addr,
			    &prefix->prefix, sizeof(*addr)) == 0)
				return (prefix);
		}
	}

	prefix = calloc(1, sizeof(*prefix));
	if (prefix == NULL)
		err(1, "%s", __func__);
	prefix->prefixlen = prefixlen;
	prefix->vltime = ADV_VALID_LIFETIME;
	prefix->pltime = ADV_PREFERRED_LIFETIME;
	prefix->lflag = 1;
	prefix->aflag = 1;

	if (addr == NULL)
		ra_iface_conf->autoprefix = prefix;
	else {
		prefix->prefix = *addr;
		SIMPLEQ_INSERT_TAIL(&ra_iface_conf->ra_prefix_list, prefix,
		    entry);
	}

	return (prefix);
}

struct ra_pref64_conf *
conf_get_ra_pref64(struct in6_addr *addr, int prefixlen)
{
	struct ra_pref64_conf	*pref64;

	SIMPLEQ_FOREACH(pref64, &ra_options->ra_pref64_list, entry) {
		if (pref64->prefixlen == prefixlen && memcmp(addr,
		    &pref64->prefix, sizeof(*addr)) == 0)
			return (pref64);
	}

	pref64 = calloc(1, sizeof(*pref64));
	if (pref64 == NULL)
		err(1, "%s", __func__);
	pref64->prefixlen = prefixlen;
	pref64->ltime = ADV_DEFAULT_LIFETIME;
	pref64->prefix = *addr;
	SIMPLEQ_INSERT_TAIL(&ra_options->ra_pref64_list, pref64, entry);

	return (pref64);
}

struct ra_iface_conf *
conf_get_ra_iface(char *name)
{
	struct ra_iface_conf	*iface;
	size_t			 n;

	SIMPLEQ_FOREACH(iface, &conf->ra_iface_list, entry) {
		if (strcmp(name, iface->name) == 0)
			return (iface);
	}

	iface = calloc(1, sizeof(*iface));
	if (iface == NULL)
		errx(1, "%s: calloc", __func__);
	n = strlcpy(iface->name, name, sizeof(iface->name));
	if (n >= sizeof(iface->name))
		errx(1, "%s: name too long", __func__);

	/* Inherit attributes set in global section. */
	iface->ra_options = conf->ra_options;

	SIMPLEQ_INIT(&iface->ra_prefix_list);
	SIMPLEQ_INIT(&iface->ra_options.ra_rdnss_list);
	iface->ra_options.rdnss_count = 0;
	SIMPLEQ_INIT(&iface->ra_options.ra_dnssl_list);
	iface->ra_options.dnssl_len = 0;
	SIMPLEQ_INIT(&iface->ra_options.ra_pref64_list);

	SIMPLEQ_INSERT_TAIL(&conf->ra_iface_list, iface, entry);

	return (iface);
}

void
clear_config(struct rad_conf *xconf)
{
	struct ra_iface_conf	*iface;

	free_dns_options(&xconf->ra_options);

	while((iface = SIMPLEQ_FIRST(&xconf->ra_iface_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&xconf->ra_iface_list, entry);
		free_ra_iface_conf(iface);
	}

	free(xconf);
}
