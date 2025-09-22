/*	$OpenBSD: parse.y,v 1.128 2022/02/27 20:30:30 bluhm Exp $	*/

/*
 * Copyright (c) 2020 Matthias Pressfreund <mpfr@fn.de>
 * Copyright (c) 2007 - 2015 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2008 Gilles Chehade <gilles@openbsd.org>
 * Copyright (c) 2006 Pierre-Yves Ritschard <pyr@openbsd.org>
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
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <sys/time.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <ifaddrs.h>
#include <syslog.h>

#include "httpd.h"
#include "http.h"

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

struct httpd		*conf = NULL;
static int		 errors = 0;
static int		 loadcfg = 0;
uint32_t		 last_server_id = 0;
uint32_t		 last_auth_id = 0;

static struct server	*srv = NULL, *parentsrv = NULL;
static struct server_config *srv_conf = NULL;
struct serverlist	 servers;
struct media_type	 media;

struct address	*host_v4(const char *);
struct address	*host_v6(const char *);
int		 host_dns(const char *, struct addresslist *,
		    int, struct portrange *, const char *, int);
int		 host_if(const char *, struct addresslist *,
		    int, struct portrange *, const char *, int);
int		 host(const char *, struct addresslist *,
		    int, struct portrange *, const char *, int);
struct server	*server_inherit(struct server *, struct server_config *,
		    struct server_config *);
int		 listen_on(const char *, int, struct portrange *);
int		 getservice(char *);
int		 is_if_in_group(const char *, const char *);
int		 get_fastcgi_dest(struct server_config *, const char *, char *);
void		 remove_locations(struct server_config *);

typedef struct {
	union {
		int64_t			 number;
		char			*string;
		struct timeval		 tv;
		struct portrange	 port;
		struct auth		 auth;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	ACCESS ALIAS AUTO BACKLOG BODY BUFFER CERTIFICATE CHROOT CIPHERS COMMON
%token	COMBINED CONNECTION DHE DIRECTORY ECDHE ERR FCGI INDEX IP KEY LIFETIME
%token	LISTEN LOCATION LOG LOGDIR MATCH MAXIMUM NO NODELAY OCSP ON PORT PREFORK
%token	PROTOCOLS REQUESTS ROOT SACK SERVER SOCKET STRIP STYLE SYSLOG TCP TICKET
%token	TIMEOUT TLS TYPE TYPES HSTS MAXAGE SUBDOMAINS DEFAULT PRELOAD REQUEST
%token	ERROR INCLUDE AUTHENTICATE WITH BLOCK DROP RETURN PASS REWRITE
%token	CA CLIENT CRL OPTIONAL PARAM FORWARDED FOUND NOT
%token	ERRDOCS GZIPSTATIC
%token	<v.string>	STRING
%token  <v.number>	NUMBER
%type	<v.port>	port
%type	<v.string>	fcgiport
%type	<v.number>	opttls optmatch optfound
%type	<v.tv>		timeout
%type	<v.string>	numberstring optstring
%type	<v.auth>	authopts

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar varset '\n'
		| grammar main '\n'
		| grammar server '\n'
		| grammar types '\n'
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

varset		: STRING '=' STRING	{
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

opttls		: /*empty*/	{ $$ = 0; }
		| TLS		{ $$ = 1; }
		;

main		: PREFORK NUMBER	{
			if (loadcfg)
				break;
			if ($2 <= 0 || $2 > PROC_MAX_INSTANCES) {
				yyerror("invalid number of preforked "
				    "servers: %lld", $2);
				YYERROR;
			}
			conf->sc_prefork_server = $2;
		}
		| CHROOT STRING		{
			conf->sc_chroot = $2;
		}
		| ERRDOCS STRING	{
			if ($2 != NULL && strlcpy(conf->sc_errdocroot, $2,
			    sizeof(conf->sc_errdocroot)) >=
			    sizeof(conf->sc_errdocroot)) {
				yyerror("errdoc root path too long");
				free($2);
				YYERROR;
			}
			free($2);
			conf->sc_custom_errdocs = 1;
		}
		| LOGDIR STRING		{
			conf->sc_logdir = $2;
		}
		| DEFAULT TYPE mediastring	{
			memcpy(&conf->sc_default_type, &media,
			    sizeof(struct media_type));
		}
		;

server		: SERVER optmatch STRING	{
			struct server		*s;
			struct sockaddr_un	*sun;

			if (!loadcfg) {
				free($3);
				YYACCEPT;
			}

			if ((s = calloc(1, sizeof (*s))) == NULL)
				fatal("out of memory");

			if (strlcpy(s->srv_conf.name, $3,
			    sizeof(s->srv_conf.name)) >=
			    sizeof(s->srv_conf.name)) {
				yyerror("server name truncated");
				free($3);
				free(s);
				YYERROR;
			}
			free($3);

			strlcpy(s->srv_conf.root, HTTPD_DOCROOT,
			    sizeof(s->srv_conf.root));
			strlcpy(s->srv_conf.index, HTTPD_INDEX,
			    sizeof(s->srv_conf.index));
			strlcpy(s->srv_conf.accesslog, HTTPD_ACCESS_LOG,
			    sizeof(s->srv_conf.accesslog));
			strlcpy(s->srv_conf.errorlog, HTTPD_ERROR_LOG,
			    sizeof(s->srv_conf.errorlog));
			s->srv_conf.id = ++last_server_id;
			s->srv_conf.parent_id = s->srv_conf.id;
			s->srv_s = -1;
			s->srv_conf.timeout.tv_sec = SERVER_TIMEOUT;
			s->srv_conf.requesttimeout.tv_sec =
			    SERVER_REQUESTTIMEOUT;
			s->srv_conf.maxrequests = SERVER_MAXREQUESTS;
			s->srv_conf.maxrequestbody = SERVER_MAXREQUESTBODY;
			s->srv_conf.flags = SRVFLAG_LOG;
			if ($2)
				s->srv_conf.flags |= SRVFLAG_SERVER_MATCH;
			s->srv_conf.logformat = LOG_FORMAT_COMMON;
			s->srv_conf.tls_protocols = TLS_PROTOCOLS_DEFAULT;
			if ((s->srv_conf.tls_cert_file =
			    strdup(HTTPD_TLS_CERT)) == NULL)
				fatal("out of memory");
			if ((s->srv_conf.tls_key_file =
			    strdup(HTTPD_TLS_KEY)) == NULL)
				fatal("out of memory");
			strlcpy(s->srv_conf.tls_ciphers,
			    HTTPD_TLS_CIPHERS,
			    sizeof(s->srv_conf.tls_ciphers));
			strlcpy(s->srv_conf.tls_dhe_params,
			    HTTPD_TLS_DHE_PARAMS,
			    sizeof(s->srv_conf.tls_dhe_params));
			strlcpy(s->srv_conf.tls_ecdhe_curves,
			    HTTPD_TLS_ECDHE_CURVES,
			    sizeof(s->srv_conf.tls_ecdhe_curves));

			sun = (struct sockaddr_un *)&s->srv_conf.fastcgi_ss;
			sun->sun_family = AF_UNIX;
			(void)strlcpy(sun->sun_path, HTTPD_FCGI_SOCKET,
			    sizeof(sun->sun_path));
			sun->sun_len = sizeof(struct sockaddr_un);

			s->srv_conf.hsts_max_age = SERVER_HSTS_DEFAULT_AGE;

			(void)strlcpy(s->srv_conf.errdocroot,
			    conf->sc_errdocroot,
			    sizeof(s->srv_conf.errdocroot));
			if (conf->sc_custom_errdocs)
				s->srv_conf.flags |= SRVFLAG_ERRDOCS;

			if (last_server_id == INT_MAX) {
				yyerror("too many servers defined");
				free(s);
				YYERROR;
			}
			srv = s;
			srv_conf = &srv->srv_conf;

			SPLAY_INIT(&srv->srv_clients);
			TAILQ_INIT(&srv->srv_hosts);
			TAILQ_INIT(&srv_conf->fcgiparams);

			TAILQ_INSERT_TAIL(&srv->srv_hosts, srv_conf, entry);
		} '{' optnl serveropts_l '}'	{
			struct server		*s, *sn;
			struct server_config	*a, *b;

			srv_conf = &srv->srv_conf;

			/* Check if the new server already exists. */
			if (server_match(srv, 1) != NULL) {
				yyerror("server \"%s\" defined twice",
				    srv->srv_conf.name);
				serverconfig_free(srv_conf);
				free(srv);
				YYABORT;
			}

			if (srv->srv_conf.ss.ss_family == AF_UNSPEC) {
				yyerror("listen address not specified");
				serverconfig_free(srv_conf);
				free(srv);
				YYERROR;
			}

			if ((s = server_match(srv, 0)) != NULL) {
				if ((s->srv_conf.flags & SRVFLAG_TLS) !=
				    (srv->srv_conf.flags & SRVFLAG_TLS)) {
					yyerror("server \"%s\": tls and "
					    "non-tls on same address/port",
					    srv->srv_conf.name);
					serverconfig_free(srv_conf);
					free(srv);
					YYERROR;
				}
				if (srv->srv_conf.flags & SRVFLAG_TLS &&
				    server_tls_cmp(s, srv) != 0) {
					yyerror("server \"%s\": tls "
					    "configuration mismatch on same "
					    "address/port",
					    srv->srv_conf.name);
					serverconfig_free(srv_conf);
					free(srv);
					YYERROR;
				}
			}

			if ((srv->srv_conf.flags & SRVFLAG_TLS) &&
			    srv->srv_conf.tls_protocols == 0) {
				yyerror("server \"%s\": no tls protocols",
				    srv->srv_conf.name);
				serverconfig_free(srv_conf);
				free(srv);
				YYERROR;
			}

			if (server_tls_load_keypair(srv) == -1) {
				/* Soft fail as there may be no certificate. */
				log_warnx("%s:%d: server \"%s\": failed to "
				    "load public/private keys", file->name,
				    yylval.lineno, srv->srv_conf.name);

				remove_locations(srv_conf);
				serverconfig_free(srv_conf);
				srv_conf = NULL;
				free(srv);
				srv = NULL;
				break;
			}

			if (server_tls_load_ca(srv) == -1) {
				yyerror("server \"%s\": failed to load "
				    "ca cert(s)", srv->srv_conf.name);
				serverconfig_free(srv_conf);
				free(srv);
				YYERROR;
			}

			if (server_tls_load_crl(srv) == -1) {
				yyerror("server \"%s\": failed to load crl(s)",
				    srv->srv_conf.name);
				serverconfig_free(srv_conf);
				free(srv);
				YYERROR;
			}

			if (server_tls_load_ocsp(srv) == -1) {
				yyerror("server \"%s\": failed to load "
				    "ocsp staple", srv->srv_conf.name);
				serverconfig_free(srv_conf);
				free(srv);
				YYERROR;
			}

			DPRINTF("adding server \"%s[%u]\"",
			    srv->srv_conf.name, srv->srv_conf.id);

			TAILQ_INSERT_TAIL(conf->sc_servers, srv, srv_entry);

			/*
			 * Add aliases and additional listen addresses as
			 * individual servers.
			 */
			TAILQ_FOREACH(a, &srv->srv_hosts, entry) {
				/* listen address */
				if (a->ss.ss_family == AF_UNSPEC)
					continue;
				TAILQ_FOREACH(b, &srv->srv_hosts, entry) {
					/* alias name */
					if (*b->name == '\0' ||
					    (b == &srv->srv_conf && b == a))
						continue;

					if ((sn = server_inherit(srv,
					    b, a)) == NULL) {
						serverconfig_free(srv_conf);
						free(srv);
						YYABORT;
					}

					DPRINTF("adding server \"%s[%u]\"",
					    sn->srv_conf.name, sn->srv_conf.id);

					TAILQ_INSERT_TAIL(conf->sc_servers,
					    sn, srv_entry);
				}
			}

			/* Remove temporary aliases */
			TAILQ_FOREACH_SAFE(a, &srv->srv_hosts, entry, b) {
				TAILQ_REMOVE(&srv->srv_hosts, a, entry);
				if (a == &srv->srv_conf)
					continue;
				serverconfig_free(a);
				free(a);
			}

			srv = NULL;
			srv_conf = NULL;
		}
		;

serveropts_l	: serveropts_l serveroptsl nl
		| serveroptsl optnl
		;

serveroptsl	: LISTEN ON STRING opttls port	{
			if (listen_on($3, $4, &$5) == -1) {
				free($3);
				YYERROR;
			}
			free($3);
		}
		| ALIAS optmatch STRING		{
			struct server_config	*alias;

			if (parentsrv != NULL) {
				yyerror("alias inside location");
				free($3);
				YYERROR;
			}

			if ((alias = calloc(1, sizeof(*alias))) == NULL)
				fatal("out of memory");

			if (strlcpy(alias->name, $3, sizeof(alias->name)) >=
			    sizeof(alias->name)) {
				yyerror("server alias truncated");
				free($3);
				free(alias);
				YYERROR;
			}
			free($3);

			if ($2)
				alias->flags |= SRVFLAG_SERVER_MATCH;

			TAILQ_INSERT_TAIL(&srv->srv_hosts, alias, entry);
		}
		| ERRDOCS STRING	{
			if (parentsrv != NULL) {
				yyerror("errdocs inside location");
				YYERROR;
			}
			if ($2 != NULL && strlcpy(srv->srv_conf.errdocroot, $2,
			    sizeof(srv->srv_conf.errdocroot)) >=
			    sizeof(srv->srv_conf.errdocroot)) {
				yyerror("errdoc root path too long");
				free($2);
				YYERROR;
			}
			free($2);
			srv->srv_conf.flags |= SRVFLAG_ERRDOCS;
		}
		| NO ERRDOCS		{
			if (parentsrv != NULL) {
				yyerror("errdocs inside location");
				YYERROR;
			}
			srv->srv_conf.flags &= ~SRVFLAG_ERRDOCS;
		}
		| tcpip			{
			if (parentsrv != NULL) {
				yyerror("tcp flags inside location");
				YYERROR;
			}
		}
		| connection		{
			if (parentsrv != NULL) {
				yyerror("connection options inside location");
				YYERROR;
			}
		}
		| tls			{
			struct server_config	*sc;
			int			 tls_flag = 0;

			if (parentsrv != NULL) {
				yyerror("tls configuration inside location");
				YYERROR;
			}

			/* Ensure that at least one server has TLS enabled. */
			TAILQ_FOREACH(sc, &srv->srv_hosts, entry) {
				tls_flag |= (sc->flags & SRVFLAG_TLS);
			}
			if (tls_flag == 0) {
				yyerror("tls options without tls listener");
				YYERROR;
			}
		}
		| request
		| root
		| directory
		| logformat
		| fastcgi
		| authenticate
		| gzip_static
		| filter
		| LOCATION optfound optmatch STRING	{
			struct server		*s;
			struct sockaddr_un	*sun;

			if (srv->srv_conf.ss.ss_family == AF_UNSPEC) {
				yyerror("listen address not specified");
				free($4);
				YYERROR;
			}

			if (parentsrv != NULL) {
				yyerror("location %s inside location", $4);
				free($4);
				YYERROR;
			}

			if (!loadcfg) {
				free($4);
				YYACCEPT;
			}

			if ((s = calloc(1, sizeof (*s))) == NULL)
				fatal("out of memory");

			if (strlcpy(s->srv_conf.location, $4,
			    sizeof(s->srv_conf.location)) >=
			    sizeof(s->srv_conf.location)) {
				yyerror("server location truncated");
				free($4);
				free(s);
				YYERROR;
			}
			free($4);

			if (strlcpy(s->srv_conf.name, srv->srv_conf.name,
			    sizeof(s->srv_conf.name)) >=
			    sizeof(s->srv_conf.name)) {
				yyerror("server name truncated");
				free(s);
				YYERROR;
			}

			sun = (struct sockaddr_un *)&s->srv_conf.fastcgi_ss;
			sun->sun_family = AF_UNIX;
			(void)strlcpy(sun->sun_path, HTTPD_FCGI_SOCKET,
			    sizeof(sun->sun_path));
			sun->sun_len = sizeof(struct sockaddr_un);

			s->srv_conf.id = ++last_server_id;
			/* A location entry uses the parent id */
			s->srv_conf.parent_id = srv->srv_conf.id;
			s->srv_conf.flags = SRVFLAG_LOCATION;
			if ($2 == 1) {
				s->srv_conf.flags &=
				    ~SRVFLAG_LOCATION_NOT_FOUND;
				s->srv_conf.flags |=
				    SRVFLAG_LOCATION_FOUND;
			} else if ($2 == -1) {
				s->srv_conf.flags &=
				    ~SRVFLAG_LOCATION_FOUND;
				s->srv_conf.flags |=
				    SRVFLAG_LOCATION_NOT_FOUND;
			}
			if ($3)
				s->srv_conf.flags |= SRVFLAG_LOCATION_MATCH;
			s->srv_s = -1;
			memcpy(&s->srv_conf.ss, &srv->srv_conf.ss,
			    sizeof(s->srv_conf.ss));
			s->srv_conf.port = srv->srv_conf.port;
			s->srv_conf.prefixlen = srv->srv_conf.prefixlen;
			s->srv_conf.tls_flags = srv->srv_conf.tls_flags;

			if (last_server_id == INT_MAX) {
				yyerror("too many servers/locations defined");
				free(s);
				YYERROR;
			}
			parentsrv = srv;
			srv = s;
			srv_conf = &srv->srv_conf;
			SPLAY_INIT(&srv->srv_clients);
		} '{' optnl serveropts_l '}'	{
			struct server	*s = NULL;
			uint32_t	 f;

			f = SRVFLAG_LOCATION_FOUND |
			    SRVFLAG_LOCATION_NOT_FOUND;

			TAILQ_FOREACH(s, conf->sc_servers, srv_entry) {
				/* Compare locations of same parent server */
				if ((s->srv_conf.flags & SRVFLAG_LOCATION) &&
				    s->srv_conf.parent_id ==
				    srv_conf->parent_id &&
				    (s->srv_conf.flags & f) ==
				    (srv_conf->flags & f) &&
				    strcmp(s->srv_conf.location,
				    srv_conf->location) == 0)
					break;
			}
			if (s != NULL) {
				yyerror("location \"%s\" defined twice",
				    srv->srv_conf.location);
				serverconfig_free(srv_conf);
				free(srv);
				YYABORT;
			}

			DPRINTF("adding location \"%s\" for \"%s[%u]\"",
			    srv->srv_conf.location,
			    srv->srv_conf.name, srv->srv_conf.id);

			TAILQ_INSERT_TAIL(conf->sc_servers, srv, srv_entry);

			srv = parentsrv;
			srv_conf = &parentsrv->srv_conf;
			parentsrv = NULL;
		}
		| DEFAULT TYPE mediastring	{
			srv_conf->flags |= SRVFLAG_DEFAULT_TYPE;
			memcpy(&srv_conf->default_type, &media,
			    sizeof(struct media_type));
		}
		| include
		| hsts				{
			if (parentsrv != NULL) {
				yyerror("hsts inside location");
				YYERROR;
			}
			srv->srv_conf.flags |= SRVFLAG_SERVER_HSTS;
		}
		;

optfound	: /* empty */	{ $$ = 0; }
		| FOUND		{ $$ = 1; }
		| NOT FOUND	{ $$ = -1; }
		;

hsts		: HSTS '{' optnl hstsflags_l '}'
		| HSTS hstsflags
		| HSTS
		;

hstsflags_l	: hstsflags optcommanl hstsflags_l
		| hstsflags optnl
		;

hstsflags	: MAXAGE NUMBER		{
			if ($2 < 0 || $2 > INT_MAX) {
				yyerror("invalid number of seconds: %lld", $2);
				YYERROR;
			}
			srv_conf->hsts_max_age = $2;
		}
		| SUBDOMAINS		{
			srv->srv_conf.hsts_flags |= HSTSFLAG_SUBDOMAINS;
		}
		| PRELOAD		{
			srv->srv_conf.hsts_flags |= HSTSFLAG_PRELOAD;
		}
		;

fastcgi		: NO FCGI		{
			srv_conf->flags &= ~SRVFLAG_FCGI;
			srv_conf->flags |= SRVFLAG_NO_FCGI;
		}
		| FCGI			{
			srv_conf->flags &= ~SRVFLAG_NO_FCGI;
			srv_conf->flags |= SRVFLAG_FCGI;
		}
		| FCGI			{
			srv_conf->flags &= ~SRVFLAG_NO_FCGI;
			srv_conf->flags |= SRVFLAG_FCGI;
		} '{' optnl fcgiflags_l '}'
		| FCGI			{
			srv_conf->flags &= ~SRVFLAG_NO_FCGI;
			srv_conf->flags |= SRVFLAG_FCGI;
		} fcgiflags
		;

fcgiflags_l	: fcgiflags optcommanl fcgiflags_l
		| fcgiflags optnl
		;

fcgiflags	: SOCKET STRING {
			struct sockaddr_un *sun;
			sun = (struct sockaddr_un *)&srv_conf->fastcgi_ss;
			memset(sun, 0, sizeof(*sun));
			sun->sun_family = AF_UNIX;
			if (strlcpy(sun->sun_path, $2, sizeof(sun->sun_path))
			    >= sizeof(sun->sun_path)) {
				yyerror("socket path too long");
				free($2);
				YYERROR;
			}
			srv_conf->fastcgi_ss.ss_len =
			    sizeof(struct sockaddr_un);
			free($2);
		}
		| SOCKET TCP STRING {
			if (get_fastcgi_dest(srv_conf, $3, FCGI_DEFAULT_PORT)
			    == -1) {
				free($3);
				YYERROR;
			}
			free($3);
		}
		| SOCKET TCP STRING fcgiport {
			if (get_fastcgi_dest(srv_conf, $3, $4) == -1) {
				free($3);
				free($4);
				YYERROR;
			}
			free($3);
			free($4);
		}
		| PARAM STRING STRING	{
			struct fastcgi_param	*param;

			if ((param = calloc(1, sizeof(*param))) == NULL)
				fatal("out of memory");

			if (strlcpy(param->name, $2, sizeof(param->name)) >=
			    sizeof(param->name)) {
				yyerror("fastcgi_param name truncated");
				free($2);
				free($3);
				free(param);
				YYERROR;
			}
			if (strlcpy(param->value, $3, sizeof(param->value)) >=
			    sizeof(param->value)) {
				yyerror("fastcgi_param value truncated");
				free($2);
				free($3);
				free(param);
				YYERROR;
			}
			free($2);
			free($3);

			DPRINTF("[%s,%s,%d]: adding param \"%s\" value \"%s\"",
			    srv_conf->location, srv_conf->name, srv_conf->id,
			    param->name, param->value);
			TAILQ_INSERT_HEAD(&srv_conf->fcgiparams, param, entry);
		}
		| STRIP NUMBER			{
			if ($2 < 0 || $2 > INT_MAX) {
				yyerror("invalid fastcgi strip number");
				YYERROR;
			}
			srv_conf->fcgistrip = $2;
		}
		;

connection	: CONNECTION '{' optnl conflags_l '}'
		| CONNECTION conflags
		;

conflags_l	: conflags optcommanl conflags_l
		| conflags optnl
		;

conflags	: TIMEOUT timeout		{
			memcpy(&srv_conf->timeout, &$2,
			    sizeof(struct timeval));
		}
		| REQUEST TIMEOUT timeout	{
			memcpy(&srv_conf->requesttimeout, &$3,
			    sizeof(struct timeval));
		}
		| MAXIMUM REQUESTS NUMBER	{
			srv_conf->maxrequests = $3;
		}
		| MAXIMUM REQUEST BODY NUMBER	{
			srv_conf->maxrequestbody = $4;
		}
		;

tls		: TLS '{' optnl tlsopts_l '}'
		| TLS tlsopts
		;

tlsopts_l	: tlsopts optcommanl tlsopts_l
		| tlsopts optnl
		;

tlsopts		: CERTIFICATE STRING		{
			free(srv_conf->tls_cert_file);
			if ((srv_conf->tls_cert_file = strdup($2)) == NULL)
				fatal("out of memory");
			free($2);
		}
		| KEY STRING			{
			free(srv_conf->tls_key_file);
			if ((srv_conf->tls_key_file = strdup($2)) == NULL)
				fatal("out of memory");
			free($2);
		}
		| OCSP STRING			{
			free(srv_conf->tls_ocsp_staple_file);
			if ((srv_conf->tls_ocsp_staple_file = strdup($2))
			    == NULL)
				fatal("out of memory");
			free($2);
		}
		| CIPHERS STRING		{
			if (strlcpy(srv_conf->tls_ciphers, $2,
			    sizeof(srv_conf->tls_ciphers)) >=
			    sizeof(srv_conf->tls_ciphers)) {
				yyerror("ciphers too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| CLIENT CA STRING tlsclientopt {
			srv_conf->tls_flags |= TLSFLAG_CA;
			free(srv_conf->tls_ca_file);
			if ((srv_conf->tls_ca_file = strdup($3)) == NULL)
				fatal("out of memory");
			free($3);
		}
		| DHE STRING			{
			if (strlcpy(srv_conf->tls_dhe_params, $2,
			    sizeof(srv_conf->tls_dhe_params)) >=
			    sizeof(srv_conf->tls_dhe_params)) {
				yyerror("dhe too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| ECDHE STRING			{
			if (strlcpy(srv_conf->tls_ecdhe_curves, $2,
			    sizeof(srv_conf->tls_ecdhe_curves)) >=
			    sizeof(srv_conf->tls_ecdhe_curves)) {
				yyerror("ecdhe too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| PROTOCOLS STRING		{
			if (tls_config_parse_protocols(
			    &srv_conf->tls_protocols, $2) != 0) {
				yyerror("invalid tls protocols");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| TICKET LIFETIME DEFAULT	{
			srv_conf->tls_ticket_lifetime = SERVER_DEF_TLS_LIFETIME;
		}
		| TICKET LIFETIME NUMBER	{
			if ($3 != 0 && $3 < SERVER_MIN_TLS_LIFETIME) {
				yyerror("ticket lifetime too small");
				YYERROR;
			}
			if ($3 > SERVER_MAX_TLS_LIFETIME) {
				yyerror("ticket lifetime too large");
				YYERROR;
			}
			srv_conf->tls_ticket_lifetime = $3;
		}
		| NO TICKET			{
			srv_conf->tls_ticket_lifetime = 0;
		}
		;

tlsclientopt	: /* empty */
		| tlsclientopt CRL STRING	{
			srv_conf->tls_flags = TLSFLAG_CRL;
			free(srv_conf->tls_crl_file);
			if ((srv_conf->tls_crl_file = strdup($3)) == NULL)
				fatal("out of memory");
			free($3);
		}
		| tlsclientopt OPTIONAL		{
			srv_conf->tls_flags |= TLSFLAG_OPTIONAL;
		}
		;
root		: ROOT rootflags
		| ROOT '{' optnl rootflags_l '}'
		;

rootflags_l	: rootflags optcommanl rootflags_l
		| rootflags optnl
		;

rootflags	: STRING		{
			if (strlcpy(srv->srv_conf.root, $1,
			    sizeof(srv->srv_conf.root)) >=
			    sizeof(srv->srv_conf.root)) {
				yyerror("document root too long");
				free($1);
				YYERROR;
			}
			free($1);
			srv->srv_conf.flags |= SRVFLAG_ROOT;
		}
		;

request		: REQUEST requestflags
		| REQUEST '{' optnl requestflags_l '}'
		;

requestflags_l	: requestflags optcommanl requestflags_l
		| requestflags optnl
		;

requestflags	: REWRITE STRING		{
			if (strlcpy(srv->srv_conf.path, $2,
			    sizeof(srv->srv_conf.path)) >=
			    sizeof(srv->srv_conf.path)) {
				yyerror("request path too long");
				free($2);
				YYERROR;
			}
			free($2);
			srv->srv_conf.flags |= SRVFLAG_PATH_REWRITE;
			srv->srv_conf.flags &= ~SRVFLAG_NO_PATH_REWRITE;
		}
		| NO REWRITE			{
			srv->srv_conf.flags |= SRVFLAG_NO_PATH_REWRITE;
			srv->srv_conf.flags &= ~SRVFLAG_PATH_REWRITE;
		}
		| STRIP NUMBER			{
			if ($2 < 0 || $2 > INT_MAX) {
				yyerror("invalid strip number");
				YYERROR;
			}
			srv->srv_conf.strip = $2;
		}
		;

authenticate	: NO AUTHENTICATE		{
			srv->srv_conf.flags |= SRVFLAG_NO_AUTH;
		}
		| AUTHENTICATE authopts		{
			struct auth	*auth;

			if ((auth = auth_add(conf->sc_auth, &$2)) == NULL) {
				yyerror("failed to add auth");
				YYERROR;
			}

			if (auth->auth_id == 0) {
				/* New htpasswd, get new Id */
				auth->auth_id = ++last_auth_id;
				if (last_auth_id == INT_MAX) {
					yyerror("too many auth ids defined");
					auth_free(conf->sc_auth, auth);
					YYERROR;
				}
			}

			srv->srv_conf.auth_id = auth->auth_id;
			srv->srv_conf.flags |= SRVFLAG_AUTH;
		}
		;

authopts	: STRING WITH STRING	{
			if (strlcpy(srv->srv_conf.auth_realm, $1,
			    sizeof(srv->srv_conf.auth_realm)) >=
			    sizeof(srv->srv_conf.auth_realm)) {
				yyerror("basic auth realm name too long");
				free($1);
				YYERROR;
			}
			free($1);
			if (strlcpy($$.auth_htpasswd, $3,
			    sizeof($$.auth_htpasswd)) >=
			    sizeof($$.auth_htpasswd)) {
				yyerror("password file name too long");
				free($3);
				YYERROR;
			}
			free($3);

		}
		| WITH STRING		{
			if (strlcpy($$.auth_htpasswd, $2,
			    sizeof($$.auth_htpasswd)) >=
			    sizeof($$.auth_htpasswd)) {
				yyerror("password file name too long");
				free($2);
				YYERROR;
			}
			free($2);
		};

directory	: DIRECTORY dirflags
		| DIRECTORY '{' optnl dirflags_l '}'
		;

dirflags_l	: dirflags optcommanl dirflags_l
		| dirflags optnl
		;

dirflags	: INDEX STRING		{
			if (strlcpy(srv_conf->index, $2,
			    sizeof(srv_conf->index)) >=
			    sizeof(srv_conf->index)) {
				yyerror("index file too long");
				free($2);
				YYERROR;
			}
			srv_conf->flags &= ~SRVFLAG_NO_INDEX;
			srv_conf->flags |= SRVFLAG_INDEX;
			free($2);
		}
		| NO INDEX		{
			srv_conf->flags &= ~SRVFLAG_INDEX;
			srv_conf->flags |= SRVFLAG_NO_INDEX;
		}
		| AUTO INDEX		{
			srv_conf->flags &= ~SRVFLAG_NO_AUTO_INDEX;
			srv_conf->flags |= SRVFLAG_AUTO_INDEX;
		}
		| NO AUTO INDEX		{
			srv_conf->flags &= ~SRVFLAG_AUTO_INDEX;
			srv_conf->flags |= SRVFLAG_NO_AUTO_INDEX;
		}
		;


logformat	: LOG logflags
		| LOG '{' optnl logflags_l '}'
		| NO LOG		{
			srv_conf->flags &= ~SRVFLAG_LOG;
			srv_conf->flags |= SRVFLAG_NO_LOG;
		}
		;

logflags_l	: logflags optcommanl logflags_l
		| logflags optnl
		;

logflags	: STYLE logstyle
		| SYSLOG		{
			srv_conf->flags &= ~SRVFLAG_NO_SYSLOG;
			srv_conf->flags |= SRVFLAG_SYSLOG;
		}
		| NO SYSLOG		{
			srv_conf->flags &= ~SRVFLAG_SYSLOG;
			srv_conf->flags |= SRVFLAG_NO_SYSLOG;
		}
		| ACCESS STRING		{
			if (strlcpy(srv_conf->accesslog, $2,
			    sizeof(srv_conf->accesslog)) >=
			    sizeof(srv_conf->accesslog)) {
				yyerror("access log name too long");
				free($2);
				YYERROR;
			}
			free($2);
			srv_conf->flags |= SRVFLAG_ACCESS_LOG;
		}
		| ERR STRING		{
			if (strlcpy(srv_conf->errorlog, $2,
			    sizeof(srv_conf->errorlog)) >=
			    sizeof(srv_conf->errorlog)) {
				yyerror("error log name too long");
				free($2);
				YYERROR;
			}
			free($2);
			srv_conf->flags |= SRVFLAG_ERROR_LOG;
		}
		;

logstyle	: COMMON		{
			srv_conf->flags &= ~SRVFLAG_NO_LOG;
			srv_conf->flags |= SRVFLAG_LOG;
			srv_conf->logformat = LOG_FORMAT_COMMON;
		}
		| COMBINED		{
			srv_conf->flags &= ~SRVFLAG_NO_LOG;
			srv_conf->flags |= SRVFLAG_LOG;
			srv_conf->logformat = LOG_FORMAT_COMBINED;
		}
		| CONNECTION		{
			srv_conf->flags &= ~SRVFLAG_NO_LOG;
			srv_conf->flags |= SRVFLAG_LOG;
			srv_conf->logformat = LOG_FORMAT_CONNECTION;
		}
		| FORWARDED		{
			srv_conf->flags &= ~SRVFLAG_NO_LOG;
			srv_conf->flags |= SRVFLAG_LOG;
			srv_conf->logformat = LOG_FORMAT_FORWARDED;
		}
		;

filter		: block RETURN NUMBER optstring	{
			if ($3 <= 0 || server_httperror_byid($3) == NULL) {
				yyerror("invalid return code: %lld", $3);
				free($4);
				YYERROR;
			}
			srv_conf->return_code = $3;

			if ($4 != NULL) {
				/* Only for 3xx redirection headers */
				if ($3 < 300 || $3 > 399) {
					yyerror("invalid return code for "
					    "location URI");
					free($4);
					YYERROR;
				}
				srv_conf->return_uri = $4;
				srv_conf->return_uri_len = strlen($4) + 1;
			}
		}
		| block DROP			{
			/* No return code, silently drop the connection */
			srv_conf->return_code = 0;
		}
		| block				{
			/* Forbidden */
			srv_conf->return_code = 403;
		}
		| PASS				{
			srv_conf->flags &= ~SRVFLAG_BLOCK;
			srv_conf->flags |= SRVFLAG_NO_BLOCK;
		}
		;

block		: BLOCK				{
			srv_conf->flags &= ~SRVFLAG_NO_BLOCK;
			srv_conf->flags |= SRVFLAG_BLOCK;
		}
		;

optmatch	: /* empty */		{ $$ = 0; }
		| MATCH			{ $$ = 1; }
		;

optstring	: /* empty */		{ $$ = NULL; }
		| STRING		{ $$ = $1; }
		;

fcgiport	: NUMBER		{
			if ($1 <= 0 || $1 > (int)USHRT_MAX) {
				yyerror("invalid port: %lld", $1);
				YYERROR;
			}
			if (asprintf(&$$, "%lld", $1) == -1) {
				yyerror("out of memory");
				YYERROR;
			}
		}
		| STRING		{
			if (getservice($1) <= 0) {
				yyerror("invalid port: %s", $1);
				free($1);
				YYERROR;
			}

			$$ = $1;
		}
		;

gzip_static	: NO GZIPSTATIC		{
			srv->srv_conf.flags &= ~SRVFLAG_GZIP_STATIC;
		}
		| GZIPSTATIC		{
			srv->srv_conf.flags |= SRVFLAG_GZIP_STATIC;
		}
		;

tcpip		: TCP '{' optnl tcpflags_l '}'
		| TCP tcpflags
		;

tcpflags_l	: tcpflags optcommanl tcpflags_l
		| tcpflags optnl
		;

tcpflags	: SACK			{ srv_conf->tcpflags |= TCPFLAG_SACK; }
		| NO SACK		{ srv_conf->tcpflags |= TCPFLAG_NSACK; }
		| NODELAY		{
			srv_conf->tcpflags |= TCPFLAG_NODELAY;
		}
		| NO NODELAY		{
			srv_conf->tcpflags |= TCPFLAG_NNODELAY;
		}
		| BACKLOG NUMBER	{
			if ($2 < 0 || $2 > SERVER_MAX_CLIENTS) {
				yyerror("invalid backlog: %lld", $2);
				YYERROR;
			}
			srv_conf->tcpbacklog = $2;
		}
		| SOCKET BUFFER NUMBER	{
			srv_conf->tcpflags |= TCPFLAG_BUFSIZ;
			if ((srv_conf->tcpbufsiz = $3) < 0) {
				yyerror("invalid socket buffer size: %lld", $3);
				YYERROR;
			}
		}
		| IP STRING NUMBER	{
			if ($3 < 0) {
				yyerror("invalid ttl: %lld", $3);
				free($2);
				YYERROR;
			}
			if (strcasecmp("ttl", $2) == 0) {
				srv_conf->tcpflags |= TCPFLAG_IPTTL;
				srv_conf->tcpipttl = $3;
			} else if (strcasecmp("minttl", $2) == 0) {
				srv_conf->tcpflags |= TCPFLAG_IPMINTTL;
				srv_conf->tcpipminttl = $3;
			} else {
				yyerror("invalid TCP/IP flag: %s", $2);
				free($2);
				YYERROR;
			}
			free($2);
		}
		;

types		: TYPES	'{' optnl mediaopts_l '}'
		;

mediaopts_l	: mediaopts_l mediaoptsl nl
		| mediaoptsl nl
		;

mediaoptsl	: mediastring medianames_l optsemicolon
		| include
		;

mediastring	: STRING '/' STRING	{
			if (strlcpy(media.media_type, $1,
			    sizeof(media.media_type)) >=
			    sizeof(media.media_type) ||
			    strlcpy(media.media_subtype, $3,
			    sizeof(media.media_subtype)) >=
			    sizeof(media.media_subtype)) {
				yyerror("media type too long");
				free($1);
				free($3);
				YYERROR;
			}
			free($1);
			free($3);
		}
		;

medianames_l	: medianames_l medianamesl
		| medianamesl
		;

medianamesl	: numberstring				{
			if (strlcpy(media.media_name, $1,
			    sizeof(media.media_name)) >=
			    sizeof(media.media_name)) {
				yyerror("media name too long");
				free($1);
				YYERROR;
			}
			free($1);

			if (!loadcfg)
				break;

			if (media_add(conf->sc_mediatypes, &media) == NULL) {
				yyerror("failed to add media type");
				YYERROR;
			}
		}
		;

port		: PORT NUMBER {
			if ($2 <= 0 || $2 > (int)USHRT_MAX) {
				yyerror("invalid port: %lld", $2);
				YYERROR;
			}
			$$.val[0] = htons($2);
			$$.op = 1;
		}
		| PORT STRING {
			int	 val;

			if ((val = getservice($2)) == -1) {
				yyerror("invalid port: %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			$$.val[0] = val;
			$$.op = 1;
		}
		;

timeout		: NUMBER
		{
			if ($1 < 0) {
				yyerror("invalid timeout: %lld", $1);
				YYERROR;
			}
			$$.tv_sec = $1;
			$$.tv_usec = 0;
		}
		;

numberstring	: NUMBER		{
			char *s;
			if (asprintf(&s, "%lld", $1) == -1) {
				yyerror("asprintf: number");
				YYERROR;
			}
			$$ = s;
		}
		| STRING
		;

optsemicolon	: ';'
		|
		;

optnl		: '\n' optnl
		|
		;

optcommanl	: ',' optnl
		| nl
		;

nl		: '\n' optnl
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
		{ "access",		ACCESS },
		{ "alias",		ALIAS },
		{ "authenticate",	AUTHENTICATE},
		{ "auto",		AUTO },
		{ "backlog",		BACKLOG },
		{ "block",		BLOCK },
		{ "body",		BODY },
		{ "buffer",		BUFFER },
		{ "ca",			CA },
		{ "certificate",	CERTIFICATE },
		{ "chroot",		CHROOT },
		{ "ciphers",		CIPHERS },
		{ "client",		CLIENT },
		{ "combined",		COMBINED },
		{ "common",		COMMON },
		{ "connection",		CONNECTION },
		{ "crl",		CRL },
		{ "default",		DEFAULT },
		{ "dhe",		DHE },
		{ "directory",		DIRECTORY },
		{ "drop",		DROP },
		{ "ecdhe",		ECDHE },
		{ "errdocs",		ERRDOCS },
		{ "error",		ERR },
		{ "fastcgi",		FCGI },
		{ "forwarded",		FORWARDED },
		{ "found",		FOUND },
		{ "gzip-static",	GZIPSTATIC },
		{ "hsts",		HSTS },
		{ "include",		INCLUDE },
		{ "index",		INDEX },
		{ "ip",			IP },
		{ "key",		KEY },
		{ "lifetime",		LIFETIME },
		{ "listen",		LISTEN },
		{ "location",		LOCATION },
		{ "log",		LOG },
		{ "logdir",		LOGDIR },
		{ "match",		MATCH },
		{ "max",		MAXIMUM },
		{ "max-age",		MAXAGE },
		{ "no",			NO },
		{ "nodelay",		NODELAY },
		{ "not",		NOT },
		{ "ocsp",		OCSP },
		{ "on",			ON },
		{ "optional",		OPTIONAL },
		{ "param",		PARAM },
		{ "pass",		PASS },
		{ "port",		PORT },
		{ "prefork",		PREFORK },
		{ "preload",		PRELOAD },
		{ "protocols",		PROTOCOLS },
		{ "request",		REQUEST },
		{ "requests",		REQUESTS },
		{ "return",		RETURN },
		{ "rewrite",		REWRITE },
		{ "root",		ROOT },
		{ "sack",		SACK },
		{ "server",		SERVER },
		{ "socket",		SOCKET },
		{ "strip",		STRIP },
		{ "style",		STYLE },
		{ "subdomains",		SUBDOMAINS },
		{ "syslog",		SYSLOG },
		{ "tcp",		TCP },
		{ "ticket",		TICKET },
		{ "timeout",		TIMEOUT },
		{ "tls",		TLS },
		{ "type",		TYPE },
		{ "types",		TYPES },
		{ "with",		WITH }
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
	x != ',' && x != ';' && x != '/'))

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
parse_config(const char *filename, struct httpd *x_conf)
{
	struct sym		*sym, *next;
	struct media_type	 dflt = HTTPD_DEFAULT_TYPE;

	conf = x_conf;
	if (config_init(conf) == -1) {
		log_warn("%s: cannot initialize configuration", __func__);
		return (-1);
	}

	/* Set default media type */
	memcpy(&conf->sc_default_type, &dflt, sizeof(struct media_type));

	errors = 0;

	if ((file = pushfile(filename, 0)) == NULL)
		return (-1);

	topfile = file;
	setservent(1);

	yyparse();
	errors = file->errors;
	while (popfile() != EOF)
		;

	endservent();
	endprotoent();

	/* Free macros */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	return (errors ? -1 : 0);
}

int
load_config(const char *filename, struct httpd *x_conf)
{
	struct sym		*sym, *next;
	struct http_mediatype	 mediatypes[] = MEDIA_TYPES;
	struct media_type	 m;
	int			 i;

	conf = x_conf;
	conf->sc_flags = 0;

	loadcfg = 1;
	errors = 0;
	last_server_id = 0;
	last_auth_id = 0;

	srv = NULL;

	if ((file = pushfile(filename, 0)) == NULL)
		return (-1);

	topfile = file;
	setservent(1);

	yyparse();
	errors = file->errors;
	popfile();

	endservent();
	endprotoent();

	/* Free macros and check which have not been used. */
	for (sym = TAILQ_FIRST(&symhead); sym != NULL; sym = next) {
		next = TAILQ_NEXT(sym, entry);
		if ((conf->sc_opts & HTTPD_OPT_VERBOSE) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	if (TAILQ_EMPTY(conf->sc_servers)) {
		log_warnx("no actions, nothing to do");
		errors++;
	}

	if (RB_EMPTY(conf->sc_mediatypes)) {
		/* Add default media types */
		for (i = 0; mediatypes[i].media_name != NULL; i++) {
			(void)strlcpy(m.media_name, mediatypes[i].media_name,
			    sizeof(m.media_name));
			(void)strlcpy(m.media_type, mediatypes[i].media_type,
			    sizeof(m.media_type));
			(void)strlcpy(m.media_subtype,
			    mediatypes[i].media_subtype,
			    sizeof(m.media_subtype));
			m.media_encoding = NULL;

			if (media_add(conf->sc_mediatypes, &m) == NULL) {
				log_warnx("failed to add default media \"%s\"",
				    m.media_name);
				errors++;
			}
		}
	}

	return (errors ? -1 : 0);
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

struct address *
host_v4(const char *s)
{
	struct in_addr		 ina;
	struct sockaddr_in	*sain;
	struct address		*h;

	memset(&ina, 0, sizeof(ina));
	if (inet_pton(AF_INET, s, &ina) != 1)
		return (NULL);

	if ((h = calloc(1, sizeof(*h))) == NULL)
		fatal(__func__);
	sain = (struct sockaddr_in *)&h->ss;
	sain->sin_len = sizeof(struct sockaddr_in);
	sain->sin_family = AF_INET;
	sain->sin_addr.s_addr = ina.s_addr;
	if (sain->sin_addr.s_addr == INADDR_ANY)
		h->prefixlen = 0; /* 0.0.0.0 address */
	else
		h->prefixlen = -1; /* host address */
	return (h);
}

struct address *
host_v6(const char *s)
{
	struct addrinfo		 hints, *res;
	struct sockaddr_in6	*sa_in6;
	struct address		*h = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM; /* dummy */
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, "0", &hints, &res) == 0) {
		if ((h = calloc(1, sizeof(*h))) == NULL)
			fatal(__func__);
		sa_in6 = (struct sockaddr_in6 *)&h->ss;
		sa_in6->sin6_len = sizeof(struct sockaddr_in6);
		sa_in6->sin6_family = AF_INET6;
		memcpy(&sa_in6->sin6_addr,
		    &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr,
		    sizeof(sa_in6->sin6_addr));
		sa_in6->sin6_scope_id =
		    ((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id;
		if (memcmp(&sa_in6->sin6_addr, &in6addr_any,
		    sizeof(sa_in6->sin6_addr)) == 0)
			h->prefixlen = 0; /* any address */
		else
			h->prefixlen = -1; /* host address */
		freeaddrinfo(res);
	}

	return (h);
}

int
host_dns(const char *s, struct addresslist *al, int max,
    struct portrange *port, const char *ifname, int ipproto)
{
	struct addrinfo		 hints, *res0, *res;
	int			 error, cnt = 0;
	struct sockaddr_in	*sain;
	struct sockaddr_in6	*sin6;
	struct address		*h;

	if ((cnt = host_if(s, al, max, port, ifname, ipproto)) != 0)
		return (cnt);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /* DUMMY */
	hints.ai_flags = AI_ADDRCONFIG;
	error = getaddrinfo(s, NULL, &hints, &res0);
	if (error == EAI_AGAIN || error == EAI_NODATA || error == EAI_NONAME)
		return (0);
	if (error) {
		log_warnx("%s: could not parse \"%s\": %s", __func__, s,
		    gai_strerror(error));
		return (-1);
	}

	for (res = res0; res && cnt < max; res = res->ai_next) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;
		if ((h = calloc(1, sizeof(*h))) == NULL)
			fatal(__func__);

		if (port != NULL)
			memcpy(&h->port, port, sizeof(h->port));
		if (ifname != NULL) {
			if (strlcpy(h->ifname, ifname, sizeof(h->ifname)) >=
			    sizeof(h->ifname))
				log_warnx("%s: interface name truncated",
				    __func__);
			freeaddrinfo(res0);
			free(h);
			return (-1);
		}
		if (ipproto != -1)
			h->ipproto = ipproto;
		h->ss.ss_family = res->ai_family;
		h->prefixlen = -1; /* host address */

		if (res->ai_family == AF_INET) {
			sain = (struct sockaddr_in *)&h->ss;
			sain->sin_len = sizeof(struct sockaddr_in);
			sain->sin_addr.s_addr = ((struct sockaddr_in *)
			    res->ai_addr)->sin_addr.s_addr;
		} else {
			sin6 = (struct sockaddr_in6 *)&h->ss;
			sin6->sin6_len = sizeof(struct sockaddr_in6);
			memcpy(&sin6->sin6_addr, &((struct sockaddr_in6 *)
			    res->ai_addr)->sin6_addr, sizeof(struct in6_addr));
		}

		TAILQ_INSERT_HEAD(al, h, entry);
		cnt++;
	}
	if (cnt == max && res) {
		log_warnx("%s: %s resolves to more than %d hosts", __func__,
		    s, max);
	}
	freeaddrinfo(res0);
	return (cnt);
}

int
host_if(const char *s, struct addresslist *al, int max,
    struct portrange *port, const char *ifname, int ipproto)
{
	struct ifaddrs		*ifap, *p;
	struct sockaddr_in	*sain;
	struct sockaddr_in6	*sin6;
	struct address		*h;
	int			 cnt = 0, af;

	if (getifaddrs(&ifap) == -1)
		fatal("getifaddrs");

	/* First search for IPv4 addresses */
	af = AF_INET;

 nextaf:
	for (p = ifap; p != NULL && cnt < max; p = p->ifa_next) {
		if (p->ifa_addr == NULL ||
		    p->ifa_addr->sa_family != af ||
		    (strcmp(s, p->ifa_name) != 0 &&
		    !is_if_in_group(p->ifa_name, s)))
			continue;
		if ((h = calloc(1, sizeof(*h))) == NULL)
			fatal("calloc");

		if (port != NULL)
			memcpy(&h->port, port, sizeof(h->port));
		if (ifname != NULL) {
			if (strlcpy(h->ifname, ifname, sizeof(h->ifname)) >=
			    sizeof(h->ifname))
				log_warnx("%s: interface name truncated",
				    __func__);
			freeifaddrs(ifap);
			free(h);
			return (-1);
		}
		if (ipproto != -1)
			h->ipproto = ipproto;
		h->ss.ss_family = af;
		h->prefixlen = -1; /* host address */

		if (af == AF_INET) {
			sain = (struct sockaddr_in *)&h->ss;
			sain->sin_len = sizeof(struct sockaddr_in);
			sain->sin_addr.s_addr = ((struct sockaddr_in *)
			    p->ifa_addr)->sin_addr.s_addr;
		} else {
			sin6 = (struct sockaddr_in6 *)&h->ss;
			sin6->sin6_len = sizeof(struct sockaddr_in6);
			memcpy(&sin6->sin6_addr, &((struct sockaddr_in6 *)
			    p->ifa_addr)->sin6_addr, sizeof(struct in6_addr));
			sin6->sin6_scope_id = ((struct sockaddr_in6 *)
			    p->ifa_addr)->sin6_scope_id;
		}

		TAILQ_INSERT_HEAD(al, h, entry);
		cnt++;
	}
	if (af == AF_INET) {
		/* Next search for IPv6 addresses */
		af = AF_INET6;
		goto nextaf;
	}

	if (cnt > max) {
		log_warnx("%s: %s resolves to more than %d hosts", __func__,
		    s, max);
	}
	freeifaddrs(ifap);
	return (cnt);
}

int
host(const char *s, struct addresslist *al, int max,
    struct portrange *port, const char *ifname, int ipproto)
{
	struct address *h;

	h = host_v4(s);

	/* IPv6 address? */
	if (h == NULL)
		h = host_v6(s);

	if (h != NULL) {
		if (port != NULL)
			memcpy(&h->port, port, sizeof(h->port));
		if (ifname != NULL) {
			if (strlcpy(h->ifname, ifname, sizeof(h->ifname)) >=
			    sizeof(h->ifname)) {
				log_warnx("%s: interface name truncated",
				    __func__);
				free(h);
				return (-1);
			}
		}
		if (ipproto != -1)
			h->ipproto = ipproto;

		TAILQ_INSERT_HEAD(al, h, entry);
		return (1);
	}

	return (host_dns(s, al, max, port, ifname, ipproto));
}

struct server *
server_inherit(struct server *src, struct server_config *alias,
    struct server_config *addr)
{
	struct server	*dst, *s, *dstl;

	if ((dst = calloc(1, sizeof(*dst))) == NULL)
		fatal("out of memory");

	/* Copy the source server and assign a new Id */
	memcpy(&dst->srv_conf, &src->srv_conf, sizeof(dst->srv_conf));
	if ((dst->srv_conf.tls_cert_file =
	    strdup(src->srv_conf.tls_cert_file)) == NULL)
		fatal("out of memory");
	if ((dst->srv_conf.tls_key_file =
	    strdup(src->srv_conf.tls_key_file)) == NULL)
		fatal("out of memory");
	if (src->srv_conf.tls_ocsp_staple_file != NULL) {
		if ((dst->srv_conf.tls_ocsp_staple_file =
		    strdup(src->srv_conf.tls_ocsp_staple_file)) == NULL)
			fatal("out of memory");
	}

	if (src->srv_conf.return_uri != NULL &&
	    (dst->srv_conf.return_uri =
	    strdup(src->srv_conf.return_uri)) == NULL)
		fatal("out of memory");

	dst->srv_conf.id = ++last_server_id;
	dst->srv_conf.parent_id = dst->srv_conf.id;
	dst->srv_s = -1;

	if (last_server_id == INT_MAX) {
		yyerror("too many servers defined");
		serverconfig_free(&dst->srv_conf);
		free(dst);
		return (NULL);
	}

	/* Now set alias and listen address */
	strlcpy(dst->srv_conf.name, alias->name, sizeof(dst->srv_conf.name));
	memcpy(&dst->srv_conf.ss, &addr->ss, sizeof(dst->srv_conf.ss));
	dst->srv_conf.port = addr->port;
	dst->srv_conf.prefixlen = addr->prefixlen;
	if (addr->flags & SRVFLAG_TLS)
		dst->srv_conf.flags |= SRVFLAG_TLS;
	else
		dst->srv_conf.flags &= ~SRVFLAG_TLS;

	/* Don't inherit the "match" option, use it from the alias */
	dst->srv_conf.flags &= ~SRVFLAG_SERVER_MATCH;
	dst->srv_conf.flags |= (alias->flags & SRVFLAG_SERVER_MATCH);

	if (server_tls_load_keypair(dst) == -1)
		log_warnx("%s:%d: server \"%s\": failed to "
		    "load public/private keys", file->name,
		    yylval.lineno, dst->srv_conf.name);

	if (server_tls_load_ca(dst) == -1) {
		yyerror("failed to load ca cert(s) for server %s",
		    dst->srv_conf.name);
		serverconfig_free(&dst->srv_conf);
		return NULL;
	}

	if (server_tls_load_crl(dst) == -1) {
		yyerror("failed to load crl(s) for server %s",
		    dst->srv_conf.name);
		serverconfig_free(&dst->srv_conf);
		free(dst);
		return NULL;
	}

	if (server_tls_load_ocsp(dst) == -1) {
		yyerror("failed to load ocsp staple "
		    "for server %s", dst->srv_conf.name);
		serverconfig_free(&dst->srv_conf);
		free(dst);
		return (NULL);
	}

	/* Check if the new server already exists */
	if (server_match(dst, 1) != NULL) {
		yyerror("server \"%s\" defined twice",
		    dst->srv_conf.name);
		serverconfig_free(&dst->srv_conf);
		free(dst);
		return (NULL);
	}

	/* Copy all the locations of the source server */
	TAILQ_FOREACH(s, conf->sc_servers, srv_entry) {
		if (!(s->srv_conf.flags & SRVFLAG_LOCATION &&
		    s->srv_conf.parent_id == src->srv_conf.parent_id))
			continue;

		if ((dstl = calloc(1, sizeof(*dstl))) == NULL)
			fatal("out of memory");

		memcpy(&dstl->srv_conf, &s->srv_conf, sizeof(dstl->srv_conf));
		strlcpy(dstl->srv_conf.name, alias->name,
		    sizeof(dstl->srv_conf.name));

		/* Copy the new Id and listen address */
		dstl->srv_conf.id = ++last_server_id;
		dstl->srv_conf.parent_id = dst->srv_conf.id;
		memcpy(&dstl->srv_conf.ss, &addr->ss,
		    sizeof(dstl->srv_conf.ss));
		dstl->srv_conf.port = addr->port;
		dstl->srv_conf.prefixlen = addr->prefixlen;
		dstl->srv_s = -1;

		DPRINTF("adding location \"%s\" for \"%s[%u]\"",
		    dstl->srv_conf.location,
		    dstl->srv_conf.name, dstl->srv_conf.id);

		TAILQ_INSERT_TAIL(conf->sc_servers, dstl, srv_entry);
	}

	return (dst);
}

int
listen_on(const char *addr, int tls, struct portrange *port)
{
	struct addresslist	 al;
	struct address		*h;
	struct server_config	*s_conf, *alias = NULL;

	if (parentsrv != NULL) {
		yyerror("listen %s inside location", addr);
		return (-1);
	}

	TAILQ_INIT(&al);
	if (strcmp("*", addr) == 0) {
		if (host("0.0.0.0", &al, 1, port, NULL, -1) <= 0) {
			yyerror("invalid listen ip: %s",
			    "0.0.0.0");
			return (-1);
		}
		if (host("::", &al, 1, port, NULL, -1) <= 0) {
			yyerror("invalid listen ip: %s", "::");
			return (-1);
		}
	} else {
		if (host(addr, &al, HTTPD_MAX_ALIAS_IP, port, NULL,
		    -1) <= 0) {
			yyerror("invalid listen ip: %s", addr);
			return (-1);
		}
	}

	while ((h = TAILQ_FIRST(&al)) != NULL) {
		if (srv->srv_conf.ss.ss_family != AF_UNSPEC) {
			if ((alias = calloc(1,
			    sizeof(*alias))) == NULL)
				fatal("out of memory");
				/* Add as an IP-based alias. */
			s_conf = alias;
		} else
			s_conf = &srv->srv_conf;
		memcpy(&s_conf->ss, &h->ss, sizeof(s_conf->ss));
		s_conf->prefixlen = h->prefixlen;
		/* Set the default port to 80 or 443 */
		if (!h->port.op)
			s_conf->port = htons(tls ?
			    HTTPS_PORT : HTTP_PORT);
		else
			s_conf->port = h->port.val[0];

		if (tls)
			s_conf->flags |= SRVFLAG_TLS;

		if (alias != NULL) {
			/*
			 * IP-based; use name match flags from
			 * parent
			 */
			alias->flags &= ~SRVFLAG_SERVER_MATCH;
			alias->flags |= srv->srv_conf.flags &
			    SRVFLAG_SERVER_MATCH;
			TAILQ_INSERT_TAIL(&srv->srv_hosts,
			    alias, entry);
		}
		TAILQ_REMOVE(&al, h, entry);
		free(h);
	}

	return (0);
}

int
getservice(char *n)
{
	struct servent	*s;
	const char	*errstr;
	long long	 llval;

	llval = strtonum(n, 0, UINT16_MAX, &errstr);
	if (errstr) {
		s = getservbyname(n, "tcp");
		if (s == NULL)
			s = getservbyname(n, "udp");
		if (s == NULL)
			return (-1);
		return (s->s_port);
	}

	return (htons((unsigned short)llval));
}

int
is_if_in_group(const char *ifname, const char *groupname)
{
	unsigned int		 len;
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;
	int			 s;
	int			 ret = 0;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "socket");

	memset(&ifgr, 0, sizeof(ifgr));
	if (strlcpy(ifgr.ifgr_name, ifname, IFNAMSIZ) >= IFNAMSIZ)
		err(1, "IFNAMSIZ");
	if (ioctl(s, SIOCGIFGROUP, (caddr_t)&ifgr) == -1) {
		if (errno == EINVAL || errno == ENOTTY)
			goto end;
		err(1, "SIOCGIFGROUP");
	}

	len = ifgr.ifgr_len;
	ifgr.ifgr_groups = calloc(len / sizeof(struct ifg_req),
	    sizeof(struct ifg_req));
	if (ifgr.ifgr_groups == NULL)
		err(1, "getifgroups");
	if (ioctl(s, SIOCGIFGROUP, (caddr_t)&ifgr) == -1)
		err(1, "SIOCGIFGROUP");

	ifg = ifgr.ifgr_groups;
	for (; ifg && len >= sizeof(struct ifg_req); ifg++) {
		len -= sizeof(struct ifg_req);
		if (strcmp(ifg->ifgrq_group, groupname) == 0) {
			ret = 1;
			break;
		}
	}
	free(ifgr.ifgr_groups);

end:
	close(s);
	return (ret);
}

int
get_fastcgi_dest(struct server_config *xsrv_conf, const char *node, char *port)
{
	struct addrinfo		 hints, *res;
	int			 s;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((s = getaddrinfo(node, port, &hints, &res)) != 0) {
		yyerror("getaddrinfo: %s\n", gai_strerror(s));
		return -1;
	}

	memset(&(xsrv_conf)->fastcgi_ss, 0, sizeof(xsrv_conf->fastcgi_ss));
	memcpy(&(xsrv_conf)->fastcgi_ss, res->ai_addr, res->ai_addrlen);

	freeaddrinfo(res);

	return (0);
}

void
remove_locations(struct server_config *xsrv_conf)
{
	struct server *s, *next;

	TAILQ_FOREACH_SAFE(s, conf->sc_servers, srv_entry, next) {
		if (!(s->srv_conf.flags & SRVFLAG_LOCATION &&
		    s->srv_conf.parent_id == xsrv_conf->parent_id))
			continue;
		TAILQ_REMOVE(conf->sc_servers, s, srv_entry);
		serverconfig_free(&s->srv_conf);
		free(s);
	}
}
