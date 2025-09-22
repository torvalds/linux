/*	$OpenBSD: parse.y,v 1.37 2023/07/18 13:06:33 claudio Exp $	*/

/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2007, 2008 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <tls.h>
#include <unistd.h>

#include "ypldap.h"
#include "log.h"

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
int		 symset(const char *, const char *, int);
char		*symget(const char *);

struct env		*conf = NULL;
struct idm		*idm = NULL;
static int		 errors = 0;

typedef struct {
	union {
		int64_t		 number;
		char		*string;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	SERVER FILTER ATTRIBUTE BASEDN BINDDN GROUPDN BINDCRED MAPS CHANGE DOMAIN PROVIDE
%token	USER GROUP TO EXPIRE HOME SHELL GECOS UID GID INTERVAL
%token	PASSWD NAME FIXED LIST GROUPNAME GROUPPASSWD GROUPGID MAP
%token	INCLUDE DIRECTORY CLASS PORT ERROR GROUPMEMBERS LDAPS TLS CAFILE
%token	BIND LOCAL PORTMAP BINDEXT CERTFILE KEYFILE
%token	<v.string>	STRING
%token  <v.number>	NUMBER
%type	<v.number>	opcode attribute
%type	<v.number>	port
%type	<v.number>	ssl

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar include '\n'
		| grammar varset '\n'
		| grammar directory '\n'
		| grammar main '\n'
		| grammar error '\n'			{ file->errors++; }
		;

nl		: '\n' optnl
		;

optnl		: '\n' optnl
		| /* empty */
		;


include		: INCLUDE STRING			{
			struct file	*nfile;

			if ((nfile = pushfile($2, 1)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

varset		: STRING '=' STRING			{
			char *s = $1;
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

port		: PORT STRING				{
			struct servent *servent;

			servent = getservbyname($2, "tcp");
			if (servent == NULL) {
				yyerror("port %s is invalid", $2);
				free($2);
				YYERROR;
			}
			$$ = ntohs(servent->s_port);
			free($2);
		}
		| PORT NUMBER				{
			if ($2 <= 0 || $2 > (int)USHRT_MAX) {
				yyerror("invalid port: %lld", $2);
				YYERROR;
			}
			$$ = $2;
		}
		| /* empty */				{
			$$ = 0;
		}
		;

opcode		: GROUP					{ $$ = 0; }
		| PASSWD				{ $$ = 1; }
		;


attribute	: NAME					{ $$ = 0; }
		| PASSWD				{ $$ = 1; }
		| UID					{ $$ = 2; }
		| GID					{ $$ = 3; }
		| CLASS					{ $$ = 4; }
		| CHANGE				{ $$ = 5; }
		| EXPIRE				{ $$ = 6; }
		| GECOS					{ $$ = 7; }
		| HOME					{ $$ = 8; }
		| SHELL					{ $$ = 9; }
		| GROUPNAME				{ $$ = 10; }
		| GROUPPASSWD				{ $$ = 11; }
		| GROUPGID				{ $$ = 12; }
		| GROUPMEMBERS				{ $$ = 13; }
		;

diropt		: BINDDN STRING				{
			if (idm->idm_bindext != 0) {
				yyerror("can't specify multiple bind types");
				free($2);
				YYERROR;
			}
			idm->idm_flags |= F_NEEDAUTH;
			if (strlcpy(idm->idm_binddn, $2,
			    sizeof(idm->idm_binddn)) >=
			    sizeof(idm->idm_binddn)) {
				yyerror("directory binddn truncated");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| BINDCRED STRING			{
			if (idm->idm_bindext != 0) {
				yyerror("can't specify multiple bind types");
				free($2);
				YYERROR;
			}
			idm->idm_flags |= F_NEEDAUTH;
			if (strlcpy(idm->idm_bindcred, $2,
			    sizeof(idm->idm_bindcred)) >=
			    sizeof(idm->idm_bindcred)) {
				yyerror("directory bindcred truncated");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| BINDEXT STRING			{
			if (idm->idm_flags & F_NEEDAUTH) {
				yyerror("can't specify multiple bind types");
				free($2);
				YYERROR;
			}
			idm->idm_flags |= F_NEEDAUTH;
			idm->idm_bindext = 1;
			if (strlcpy(idm->idm_bindextid, $2,
			    sizeof(idm->idm_bindextid)) >=
			    sizeof(idm->idm_bindextid)) {
				yyerror("directory bindext truncated");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| BINDEXT				{
			if (idm->idm_flags & F_NEEDAUTH) {
				yyerror("can't specify multiple bind types");
				YYERROR;
			}
			idm->idm_flags |= F_NEEDAUTH;
			idm->idm_bindext = 1;
			idm->idm_bindextid[0] = '\0';
		}
		| CERTFILE STRING			{
			if (idm->idm_tls_config == NULL) {
				yyerror("can't set cert file without tls"
				    " enabled");
				free($2);
				YYERROR;
			}
			if (tls_config_set_cert_file(idm->idm_tls_config, $2)
			    == -1) {
				yyerror("tls set cert file failed: %s",
				    tls_config_error(
				    idm->idm_tls_config));
				free($2);
				YYERROR;
			}
		}
		| KEYFILE STRING			{
			if (idm->idm_tls_config == NULL) {
				yyerror("can't set key file without tls"
				    " enabled");
				free($2);
				YYERROR;
			}
			if (tls_config_set_key_file(idm->idm_tls_config, $2)
			    == -1) {
				yyerror("tls set key file failed: %s",
				    tls_config_error(
				    idm->idm_tls_config));
				free($2);
				YYERROR;
			}
		}
		| BASEDN STRING			{
			if (strlcpy(idm->idm_basedn, $2,
			    sizeof(idm->idm_basedn)) >=
			    sizeof(idm->idm_basedn)) {
				yyerror("directory basedn truncated");
				free($2);
				YYERROR;
			}
			free($2);
		} 
		| GROUPDN STRING		{
			if(strlcpy(idm->idm_groupdn, $2,
			    sizeof(idm->idm_groupdn)) >=
			    sizeof(idm->idm_groupdn)) {
				yyerror("directory groupdn truncated");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| opcode FILTER STRING			{
			if (strlcpy(idm->idm_filters[$1], $3,
			    sizeof(idm->idm_filters[$1])) >=
			    sizeof(idm->idm_filters[$1])) {
				yyerror("filter truncated");
				free($3);
				YYERROR;
			}
			free($3);
		}
		| ATTRIBUTE attribute MAPS TO STRING	{
			if (strlcpy(idm->idm_attrs[$2], $5,
			    sizeof(idm->idm_attrs[$2])) >=
			    sizeof(idm->idm_attrs[$2])) {
				yyerror("attribute truncated");
				free($5);
				YYERROR;
			}
			free($5);
		}
		| FIXED ATTRIBUTE attribute STRING	{
			if (strlcpy(idm->idm_attrs[$3], $4,
			    sizeof(idm->idm_attrs[$3])) >=
			    sizeof(idm->idm_attrs[$3])) {
				yyerror("attribute truncated");
				free($4);
				YYERROR;
			}
			idm->idm_flags |= F_FIXED_ATTR($3);
			free($4);
		}
		| LIST attribute MAPS TO STRING	{
			if (strlcpy(idm->idm_attrs[$2], $5,
			    sizeof(idm->idm_attrs[$2])) >=
			    sizeof(idm->idm_attrs[$2])) {
				yyerror("attribute truncated");
				free($5);
				YYERROR;
			}
			idm->idm_list |= F_LIST($2);
			free($5);
		}
		;

ssl		: /* empty */				{ $$ = 0; }
		| LDAPS					{ $$ = F_SSL; }
		| TLS					{ $$ = F_STARTTLS; }
		;

directory	: DIRECTORY STRING port ssl {
			if ((idm = calloc(1, sizeof(*idm))) == NULL)
				fatal(NULL);
			idm->idm_id = conf->sc_maxid++;

			if (strlcpy(idm->idm_name, $2,
			    sizeof(idm->idm_name)) >=
			    sizeof(idm->idm_name)) {
				yyerror("attribute truncated");
				free($2);
				YYERROR;
			}
			free($2);

			idm->idm_port = $3;

			if ($4 != 0) {
				if (tls_init()) {
					yyerror("tls init failed");
					YYERROR;
				}

				idm->idm_flags |= $4;
				idm->idm_tls_config = tls_config_new();
				if (idm->idm_tls_config == NULL) {
					yyerror("tls config failed");
					YYERROR;
				}

				if (tls_config_set_protocols(
				    idm->idm_tls_config,
				    TLS_PROTOCOLS_ALL) == -1) {
					yyerror("tls set protocols failed: %s",
					    tls_config_error(
					    idm->idm_tls_config));
					tls_config_free(idm->idm_tls_config);
					idm->idm_tls_config = NULL;
					YYERROR;
				}
				if (tls_config_set_ciphers(idm->idm_tls_config,
				    "compat") == -1) {
					yyerror("tls set ciphers failed: %s",
					    tls_config_error(
					    idm->idm_tls_config));
					tls_config_free(idm->idm_tls_config);
					idm->idm_tls_config = NULL;
					YYERROR;
				}

				if (tls_config_set_ca_file(idm->idm_tls_config,
				    conf->sc_cafile) == -1) {
					yyerror("tls set CA bundle failed: %s",
					    tls_config_error(
					    idm->idm_tls_config));
					tls_config_free(idm->idm_tls_config);
					idm->idm_tls_config = NULL;
					YYERROR;
				}
			}

		} '{' optnl diropts '}'			{
			TAILQ_INSERT_TAIL(&conf->sc_idms, idm, idm_entry);
			idm = NULL;
		}
		;

main		: INTERVAL NUMBER			{
			conf->sc_conf_tv.tv_sec = $2;
			conf->sc_conf_tv.tv_usec = 0;
		}
		| DOMAIN STRING				{
			if (strlcpy(conf->sc_domainname, $2,
			    sizeof(conf->sc_domainname)) >=
			    sizeof(conf->sc_domainname)) {
				yyerror("domainname truncated");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| PROVIDE MAP STRING			{
			if (strcmp($3, "passwd.byname") == 0)
				conf->sc_flags |= YPMAP_PASSWD_BYNAME;
			else if (strcmp($3, "passwd.byuid") == 0)
				conf->sc_flags |= YPMAP_PASSWD_BYUID;
			else if (strcmp($3, "master.passwd.byname") == 0)
				conf->sc_flags |= YPMAP_MASTER_PASSWD_BYNAME;
			else if (strcmp($3, "master.passwd.byuid") == 0)
				conf->sc_flags |= YPMAP_MASTER_PASSWD_BYUID;
			else if (strcmp($3, "group.byname") == 0)
				conf->sc_flags |= YPMAP_GROUP_BYNAME;
			else if (strcmp($3, "group.bygid") == 0)
				conf->sc_flags |= YPMAP_GROUP_BYGID;
			else if (strcmp($3, "netid.byname") == 0)
				conf->sc_flags |= YPMAP_NETID_BYNAME;
			else {
				yyerror("unsupported map type: %s", $3);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| CAFILE STRING				{
			free(conf->sc_cafile);
			conf->sc_cafile = $2;
		}
		| BIND LOCAL				{
			conf->sc_bind_mode = BIND_MODE_LOCAL;
		}
		| BIND PORTMAP				{
			conf->sc_bind_mode = BIND_MODE_PORTMAP;
		}
		;

diropts		: diropts diropt nl
		| diropt optnl
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
		{ "attribute",		ATTRIBUTE },
		{ "basedn",		BASEDN },
		{ "bind",		BIND },
		{ "bindcred",		BINDCRED },
		{ "binddn",		BINDDN },
		{ "bindext",		BINDEXT },
		{ "cafile",		CAFILE },
		{ "certfile",		CERTFILE },
		{ "change",		CHANGE },
		{ "class",		CLASS },
		{ "directory",		DIRECTORY },
		{ "domain",		DOMAIN },
		{ "expire",		EXPIRE },
		{ "filter",		FILTER },
		{ "fixed",		FIXED },
		{ "gecos",		GECOS },
		{ "gid",		GID },
		{ "group",		GROUP },
		{ "groupdn",		GROUPDN },
		{ "groupgid",		GROUPGID },
		{ "groupmembers",	GROUPMEMBERS },
		{ "groupname",		GROUPNAME },
		{ "grouppasswd",	GROUPPASSWD },
		{ "home",		HOME },
		{ "include",		INCLUDE },
		{ "interval",		INTERVAL },
		{ "keyfile",		KEYFILE },
		{ "ldaps",		LDAPS },
		{ "list",		LIST },
		{ "local",		LOCAL },
		{ "map",		MAP },
		{ "maps",		MAPS },
		{ "name",		NAME },
		{ "passwd",		PASSWD },
		{ "port",		PORT },
		{ "portmap",		PORTMAP },
		{ "provide",		PROVIDE },
		{ "server",		SERVER },
		{ "shell",		SHELL },
		{ "tls",		TLS },
		{ "to",			TO },
		{ "uid",		UID },
		{ "user",		USER },
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
	x != '{' && x != '}' && x != '<' && x != '>' && \
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

int
parse_config(struct env *x_conf, const char *filename, int opts)
{
	struct sym	*sym, *next;

	conf = x_conf;
	bzero(conf, sizeof(*conf));

	TAILQ_INIT(&conf->sc_idms);
	conf->sc_conf_tv.tv_sec = DEFAULT_INTERVAL;
	conf->sc_conf_tv.tv_usec = 0;
	conf->sc_cafile = strdup(tls_default_ca_cert_file());
	if (conf->sc_cafile == NULL) {
		log_warn("%s", __func__);
		return (-1);
	}
	conf->sc_bind_mode = BIND_MODE_PORTMAP;

	errors = 0;

	if ((file = pushfile(filename, 1)) == NULL) {
		return (-1);
	}
	topfile = file;

	/*
	 * parse configuration
	 */
	setservent(1);
	yyparse();
	endservent();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((opts & YPLDAP_OPT_VERBOSE) && !sym->used)
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
		return (-1);
	}

	return (0);
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
