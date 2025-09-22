/*	$OpenBSD: parse.y,v 1.91 2024/06/03 06:14:32 anton Exp $	*/

/*
 * Copyright (c) 2007, 2008, 2012 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/utsname.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include <openssl/sha.h>

#include <ber.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#include "application.h"
#include "log.h"
#include "mib.h"
#include "snmpd.h"
#include "snmp.h"

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

struct oid_sym {
	char			*descriptor;
	char			 file[PATH_MAX];
	int			 lineno;
};

struct object_sym {
	struct oid_sym		 oid;
	char			*name;
	int			 isint;
	union {
		int32_t		 intval;
		char		*sval;
	};
};

struct trapcmd_sym {
	struct oid_sym	 oid;
	struct trapcmd	*cmd;
};

struct trapaddress_sym {
	struct oid_sym	 oid;
	struct trap_address *tr;
};

struct snmpd			*conf = NULL;
static int			 errors = 0;
static struct usmuser		*user = NULL;
static int			 mibparsed = 0;

static struct oid_sym		*blocklist = NULL;
static size_t			 nblocklist = 0;
static struct oid_sym		 sysoid = {};
static struct object_sym	*objects = NULL;
static size_t			 nobjects = 0;
static struct trapcmd_sym	*trapcmds = NULL;
static size_t			 ntrapcmds = 0;
static struct trapaddress_sym	*trapaddresses = NULL;
static size_t			 ntrapaddresses = 0;

static uint8_t			 engineid[SNMPD_MAXENGINEIDLEN];
static int32_t			 enginepen;
static size_t			 engineidlen;

static unsigned char		 sha256[SHA256_DIGEST_LENGTH];

int		 resolve_oid(struct ber_oid *, struct oid_sym *);
int		 resolve_oids(void);
int		 host(const char *, const char *, int, int,
		    struct sockaddr_storage *, int);
int		 listen_add(struct sockaddr_storage *, int, int);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
		struct host	*host;
		struct timeval	 tv;
		struct oid_sym	 oid;
		struct agentx_master ax;
		struct {
			int		 type;
			void		*data;
			long long	 value;
		}		 data;
		enum usmauth	 auth;
		enum usmpriv	 enc;
	} v;
	int lineno;
} YYSTYPE;

#define axm_opts	axm_fd
#define AXM_PATH	1 << 0
#define AXM_OWNER	1 << 1
#define AXM_GROUP	1 << 2
#define AXM_MODE	1 << 3

%}

%token	INCLUDE
%token	LISTEN ON READ WRITE NOTIFY SNMPV1 SNMPV2 SNMPV3
%token	AGENTX PATH OWNER GROUP MODE
%token	ENGINEID PEN OPENBSD IP4 IP6 MAC TEXT OCTETS AGENTID HOSTHASH
%token	SYSTEM CONTACT DESCR LOCATION NAME OBJECTID SERVICES RTFILTER
%token	READONLY READWRITE OCTETSTRING INTEGER COMMUNITY TRAP RECEIVER
%token	SECLEVEL NONE AUTH ENC USER AUTHKEY ENCKEY ERROR
%token	HANDLE DEFAULT SRCADDR TCP UDP BLOCKLIST PORT
%token	MIB DIRECTORY
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.string>	usmuser community optcommunity
%type	<v.number>	listenproto listenflag listenflags
%type	<v.string>	srcaddr port
%type	<v.number>	optwrite yesno seclevel
%type	<v.data>	cmd hostauth hostauthv3 usmauthopts usmauthopt
%type	<v.oid>		oid hostoid trapoid
%type	<v.auth>	auth
%type	<v.enc>		enc
%type	<v.ax>		agentxopts agentxopt

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar varset '\n'
		| grammar main '\n'
		| grammar system '\n'
		| grammar object '\n'
		| grammar mib '\n'
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

yesno		:  STRING			{
			if (!strcmp($1, "yes"))
				$$ = 1;
			else if (!strcmp($1, "no"))
				$$ = 0;
			else {
				yyerror("syntax error, "
				    "either yes or no expected");
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

main		: LISTEN ON listen_udptcp
		| AGENTX agentxopts {
			struct agentx_master *master, *test;
			struct group *grp;

			if ((master = malloc(sizeof(*master))) == NULL) {
				yyerror("malloc");
				YYERROR;
			}
			master->axm_fd = -1;
			master->axm_sun.sun_len = sizeof(master->axm_sun);
			master->axm_sun.sun_family = AF_UNIX;
			if ($2.axm_opts & AXM_PATH)
				strlcpy(master->axm_sun.sun_path,
				    $2.axm_sun.sun_path,
				    sizeof(master->axm_sun.sun_path));
			else
				strlcpy(master->axm_sun.sun_path,
				    AGENTX_MASTER_PATH,
				    sizeof(master->axm_sun.sun_path));
			master->axm_owner = $2.axm_opts & AXM_OWNER ?
			    $2.axm_owner : 0;
			if ($2.axm_opts & AXM_GROUP)
				master->axm_group = $2.axm_group;
			else {
				if ((grp = getgrnam(AGENTX_GROUP)) == NULL) {
					yyerror("agentx: group %s not found",
					    AGENTX_GROUP);
					YYERROR;
				}
				master->axm_group = grp->gr_gid;
			}
			master->axm_mode = $2.axm_opts & AXM_MODE ?
			    $2.axm_mode : 0660;

			TAILQ_FOREACH(test, &conf->sc_agentx_masters,
			    axm_entry) {
				if (strcmp(test->axm_sun.sun_path,
				    master->axm_sun.sun_path) == 0) {
					yyerror("agentx: duplicate entry: %s",
					    test->axm_sun.sun_path);
					YYERROR;
				}
			}

			TAILQ_INSERT_TAIL(&conf->sc_agentx_masters, master,
			    axm_entry);
		}
		| engineid_local {
			if (conf->sc_engineid_len != 0) {
				yyerror("Redefinition of engineid");
				YYERROR;
			}
			memcpy(conf->sc_engineid, engineid, engineidlen);
			conf->sc_engineid_len = engineidlen;
		}
		| READONLY COMMUNITY STRING	{
			if (strlcpy(conf->sc_rdcommunity, $3,
			    sizeof(conf->sc_rdcommunity)) >=
			    sizeof(conf->sc_rdcommunity)) {
				yyerror("r/o community name too long");
				free($3);
				YYERROR;
			}
			free($3);
		}
		| READWRITE COMMUNITY STRING	{
			if (strlcpy(conf->sc_rwcommunity, $3,
			    sizeof(conf->sc_rwcommunity)) >=
			    sizeof(conf->sc_rwcommunity)) {
				yyerror("r/w community name too long");
				free($3);
				YYERROR;
			}
			free($3);
		}
		| TRAP COMMUNITY STRING		{
			if (strlcpy(conf->sc_trcommunity, $3,
			    sizeof(conf->sc_trcommunity)) >=
			    sizeof(conf->sc_trcommunity)) {
				yyerror("trap community name too long");
				free($3);
				YYERROR;
			}
			free($3);
		}
		| TRAP RECEIVER	host
		| TRAP HANDLE trapoid cmd {
			struct trapcmd_sym *ttrapcmds;

			if ((ttrapcmds = recallocarray(trapcmds, ntrapcmds,
			    ntrapcmds + 1, sizeof(*trapcmds))) == NULL) {
				yyerror("malloc");
				free($3.descriptor);
				free($4.data);
				YYERROR;
			}

			trapcmds = ttrapcmds;
			trapcmds[ntrapcmds].oid = $3;
			trapcmds[ntrapcmds++].cmd = $4.data;
		}
		| BLOCKLIST oid {
			struct oid_sym *tblocklist;

			if ((tblocklist = recallocarray(blocklist, nblocklist,
			    nblocklist + 1, sizeof(*blocklist))) == NULL) {
				yyerror("malloc");
				free($2.descriptor);
				YYERROR;
			}
			blocklist = tblocklist;
			blocklist[nblocklist++] = $2;
		}
		| RTFILTER yesno		{
			conf->sc_rtfilter = $2;
		}
		| seclevel {
			conf->sc_min_seclevel = $1;
		}
		| USER STRING			{
			const char *errstr;
			user = usm_newuser($2, &errstr);
			if (user == NULL) {
				yyerror("%s", errstr);
				free($2);
				YYERROR;
			}
		} userspecs {
			const char *errstr;
			if (usm_checkuser(user, &errstr) < 0) {
				yyerror("%s", errstr);
				YYERROR;
			}
			user = NULL;
		}
		;

listenproto	: /* empty */			{ $$ = SOCK_DGRAM; }
		| UDP 				{ $$ = SOCK_DGRAM; }
		| TCP				{ $$ = SOCK_STREAM; }
		;

listenflags	: /* empty */ { $$ = 0; }
		| listenflags listenflag { $$ |= $2; }
		;

listenflag	: READ { $$ = ADDRESS_FLAG_READ; }
		| WRITE { $$ = ADDRESS_FLAG_WRITE; }
		| NOTIFY { $$ = ADDRESS_FLAG_NOTIFY; }
		| SNMPV1 { $$ = ADDRESS_FLAG_SNMPV1; }
		| SNMPV2 { $$ = ADDRESS_FLAG_SNMPV2; }
		| SNMPV3 { $$ = ADDRESS_FLAG_SNMPV3; }
		;

listen_udptcp	: listenproto STRING port listenflags	{
			struct sockaddr_storage ss[16];
			int nhosts, j;
			char *address[2], *port = $3;
			size_t addresslen = 1, i;

			if (port == NULL) {
				if (($4 & ADDRESS_FLAG_PERM) ==
				    ADDRESS_FLAG_NOTIFY)
					port = SNMPTRAP_PORT;
				else
					port = SNMP_PORT;
			}

			if (strcmp($2, "any") == 0) {
				addresslen = 2;
				address[0] = "0.0.0.0";
				address[1] = "::";
			} else {
				addresslen = 1;
				address[0] = $2;
			}

			for (i = 0; i < addresslen; i++) {
				if ((nhosts = host(address[i], port, AF_UNSPEC,
				    $1, ss, nitems(ss))) < 1) {
					yyerror("invalid address: %s", $2);
					free($2);
					free($3);
					YYERROR;
				}
				if (nhosts > (int)nitems(ss))
					log_warn("%s:%s resolves to more than "
					    "%zu hosts", $2, port, nitems(ss));

				for (j = 0; j < nhosts; j++) {
					if (listen_add(&(ss[j]), $1, $4) == -1) {
						yyerror("calloc");
						YYERROR;
					}
				}
			}
			free($2);
			free($3);
		}
		;

port		: /* empty */			{
			$$ = NULL;
		}
		| PORT STRING			{
			$$ = $2;
		}
		| PORT NUMBER			{
			char *number;

			if ($2 > UINT16_MAX) {
				yyerror("port number too large");
				YYERROR;
			}
			if ($2 < 1) {
				yyerror("port number too small");
				YYERROR;
			}
			if (asprintf(&number, "%"PRId64, $2) == -1) {
				yyerror("malloc");
				YYERROR;
			}
			$$ = number;
		}
		;

agentxopt	: PATH STRING			{
			if ($2[0] != '/') {
				yyerror("agentx path: must be absolute");
				YYERROR;
			}
			if (strlcpy($$.axm_sun.sun_path, $2,
			    sizeof($$.axm_sun.sun_path)) >=
			    sizeof($$.axm_sun.sun_path)) {
				yyerror("agentx path: too long");
				YYERROR;
			}
			$$.axm_opts = AXM_PATH;
		}
		| OWNER NUMBER			{
			if ($2 > UID_MAX) {
				yyerror("agentx owner: too large");
				YYERROR;
			}
			$$.axm_owner = $2;
			$$.axm_opts = AXM_OWNER;
		}
		| OWNER STRING			{
			struct passwd *pw;
			const char *errstr;

			$$.axm_owner = strtonum($2, 0, UID_MAX, &errstr);
			if (errstr != NULL && errno == ERANGE) {
				yyerror("agentx owner: %s", errstr);
				YYERROR;
			}
			if ((pw = getpwnam($2)) == NULL) {
				yyerror("agentx owner: user not found");
				YYERROR;
			}
			$$.axm_owner = pw->pw_uid;
			$$.axm_opts = AXM_OWNER;
		}
		| GROUP NUMBER			{
			if ($2 > GID_MAX) {
				yyerror("agentx group: too large");
				YYERROR;
			}
			$$.axm_group = $2;
			$$.axm_opts = AXM_GROUP;
		}
		| GROUP STRING			{
			struct group *gr;
			const char *errstr;

			$$.axm_group = strtonum($2, 0, GID_MAX, &errstr);
			if (errstr != NULL && errno == ERANGE) {
				yyerror("agentx group: %s", errstr);
				YYERROR;
			}
			if ((gr = getgrnam($2)) == NULL) {
				yyerror("agentx group: group not found");
				YYERROR;
			}
			$$.axm_group = gr->gr_gid;
			$$.axm_opts = AXM_GROUP;
		}
		| MODE NUMBER			{
			long mode;
			char *endptr, str[21];

			snprintf(str, sizeof(str), "%lld", $2);
			errno = 0;
			mode = strtol(str, &endptr, 8);
			if (errno != 0 || endptr[0] != '\0' ||
			    mode <= 0 || mode > 0777) {
				yyerror("agentx mode: invalid");
				YYERROR;
			}
			$$.axm_mode = mode;
			$$.axm_opts = AXM_MODE;
		}
		;

agentxopts	: /* empty */			{
			bzero(&$$, sizeof($$));
		}
		| agentxopts agentxopt {
			if ($$.axm_opts & $2.axm_opts) {
				yyerror("agentx: duplicate option");
				YYERROR;
			}
			switch ($2.axm_opts) {
			case AXM_PATH:
				strlcpy($$.axm_sun.sun_path,
				    $2.axm_sun.sun_path,
				    sizeof($$.axm_sun.sun_path));
				break;
			case AXM_OWNER:
				$$.axm_owner = $2.axm_owner;
				break;
			case AXM_GROUP:
				$$.axm_group = $2.axm_group;
				break;
			case AXM_MODE:
				$$.axm_mode = $2.axm_mode;
				break;
			}
			$$.axm_opts |= $2.axm_opts;
		}
		;

enginefmt	: IP4 STRING			{
			struct in_addr addr;

			engineid[engineidlen++] = SNMP_ENGINEID_FMT_IPv4;
			if (inet_pton(AF_INET, $2, &addr) != 1) {
				yyerror("Invalid ipv4 address: %s", $2);
				free($2);
				YYERROR;
			}
			memcpy(engineid + engineidlen, &addr,
			    sizeof(engineid) - engineidlen);
			engineid[0] |= SNMP_ENGINEID_NEW;
			engineidlen += sizeof(addr);
			free($2);
		}
		| IP6 STRING			{
			struct in6_addr addr;

			engineid[engineidlen++] = SNMP_ENGINEID_FMT_IPv6;
			if (inet_pton(AF_INET6, $2, &addr) != 1) {
				yyerror("Invalid ipv6 address: %s", $2);
				free($2);
				YYERROR;
			}
			memcpy(engineid + engineidlen, &addr,
			    sizeof(engineid) - engineidlen);
			engineid[0] |= SNMP_ENGINEID_NEW;
			engineidlen += sizeof(addr);
			free($2);
		}
		| MAC STRING			{
			size_t i;

			if (strlen($2) != 5 * 3 + 2) {
				yyerror("Invalid mac address: %s", $2);
				free($2);
				YYERROR;
			}
			engineid[engineidlen++] = SNMP_ENGINEID_FMT_MAC;
			for (i = 0; i < 6; i++) {
				if (fromhexstr(engineid + engineidlen + i,
				    $2 + (i * 3), 2) == NULL ||
				    $2[i * 3 + 2] != (i < 5 ? ':' : '\0')) {
					yyerror("Invalid mac address: %s", $2);
					free($2);
					YYERROR;
				}
			}
			engineid[0] |= SNMP_ENGINEID_NEW;
			engineidlen += 6;
			free($2);
		}
		| TEXT STRING			{
			size_t i, fmtstart;

			engineid[engineidlen++] = SNMP_ENGINEID_FMT_TEXT;
			for (i = 0, fmtstart = engineidlen;
			    i < sizeof(engineid) - engineidlen && $2[i] != '\0';
			    i++) {
				if (!isprint($2[i])) {
					yyerror("invalid text character");
					free($2);
					YYERROR;
				}
				engineid[fmtstart + i] = $2[i];
				engineidlen++;
			}
			if (i == 0 || $2[i] != '\0') {
				yyerror("Invalid text length: %s", $2);
				free($2);
				YYERROR;
			}
			engineid[0] |= SNMP_ENGINEID_NEW;
			free($2);
		}
		| OCTETS STRING			{
			if (strlen($2) / 2 > sizeof(engineid) - 1) {
				yyerror("Invalid octets length: %s", $2);
				free($2);
				YYERROR;
			}

			engineid[engineidlen++] = SNMP_ENGINEID_FMT_OCT;
			if (fromhexstr(engineid + engineidlen, $2,
			    strlen($2)) == NULL) {
				yyerror("Invalid octets: %s", $2);
				free($2);
				YYERROR;
			}
			engineidlen += strlen($2) / 2;
			engineid[0] |= SNMP_ENGINEID_NEW;
			free($2);
		}
		| AGENTID STRING		{
			if (strlen($2) / 2 != 8) {
				yyerror("Invalid agentid length: %s", $2);
				free($2);
				YYERROR;
			}

			if (fromhexstr(engineid + engineidlen, $2,
			    strlen($2)) == NULL) {
				yyerror("Invalid agentid: %s", $2);
				free($2);
				YYERROR;
			}
			engineidlen += 8;
			engineid[0] |= SNMP_ENGINEID_OLD;
			free($2);
		}
		| HOSTHASH STRING		{
			if (enginepen != PEN_OPENBSD) {
				yyerror("hosthash only allowed for pen "
				    "openbsd");
				YYERROR;
			}
			engineid[engineidlen++] = SNMP_ENGINEID_FMT_HH;
			memcpy(engineid + engineidlen,
			    SHA256($2, strlen($2), sha256),
			    sizeof(engineid) - engineidlen);
			engineidlen = sizeof(engineid);
			engineid[0] |= SNMP_ENGINEID_NEW;
			free($2);
		}
		| NUMBER STRING			{
			if (enginepen == PEN_OPENBSD) {
				yyerror("%lld is only allowed when pen is not "
				    "openbsd", $1);
				YYERROR;
			}

			if ($1 < 128 || $1 > 255) {
				yyerror("Invalid format number: %lld\n", $1);
				YYERROR;
			}
			if (strlen($2) / 2 > sizeof(engineid) - 1) {
				yyerror("Invalid octets length: %s", $2);
				free($2);
				YYERROR;
			}

			engineid[engineidlen++] = (uint8_t)$1;
			if (fromhexstr(engineid + engineidlen, $2,
			    strlen($2)) == NULL) {
				yyerror("Invalid octets: %s", $2);
				free($2);
				YYERROR;
			}
			engineidlen += strlen($2) / 2;
			engineid[0] |= SNMP_ENGINEID_NEW;
			free($2);
		}
		;

enginefmt_local	: enginefmt
		| HOSTHASH			{
			char hostname[HOST_NAME_MAX + 1];

			if (enginepen != PEN_OPENBSD) {
				yyerror("hosthash only allowed for pen "
				    "openbsd");
				YYERROR;
			}

			if (gethostname(hostname, sizeof(hostname)) == -1) {
				yyerror("gethostname: %s", strerror(errno));
				YYERROR;
			}

			engineid[engineidlen++] = SNMP_ENGINEID_FMT_HH;
			memcpy(engineid + engineidlen,
			    SHA256(hostname, strlen(hostname), sha256),
			    sizeof(engineid) - engineidlen);
			engineidlen = sizeof(engineid);
			engineid[0] |= SNMP_ENGINEID_NEW;
		}
		;

pen		: /* empty */			{
			enginepen = PEN_OPENBSD;
		}
		| PEN NUMBER			{
			if ($2 > INT32_MAX) {
				yyerror("pen number too large");
				YYERROR;
			}
			if ($2 <= 0) {
				yyerror("pen number too small");
				YYERROR;
			}
			enginepen = $2;
		}
		| PEN OPENBSD			{
			enginepen = PEN_OPENBSD;
		}
		;

engineid_local	: ENGINEID pen			{
			uint32_t npen = htonl(enginepen);

			memcpy(engineid, &npen, sizeof(enginepen));
			engineidlen = sizeof(enginepen);
		} enginefmt_local

system		: SYSTEM sysmib
		;

sysmib		: CONTACT STRING		{
			if (conf->sc_system.sys_contact[0] != '\0') {
				yyerror("system contact already defined");
				free($2);
				YYERROR;
			}
			if (strlcpy(conf->sc_system.sys_contact, $2,
			    sizeof(conf->sc_system.sys_contact)) >=
			    sizeof(conf->sc_system.sys_contact)) {
				yyerror("system contact too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| DESCR STRING			{
			if (conf->sc_system.sys_descr[0] != '\0') {
				yyerror("system description already defined");
				free($2);
				YYERROR;
			}
			if (strlcpy(conf->sc_system.sys_descr, $2,
			    sizeof(conf->sc_system.sys_descr)) >=
			    sizeof(conf->sc_system.sys_descr)) {
				yyerror("system description too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| LOCATION STRING		{
			if (conf->sc_system.sys_location[0] != '\0') {
				yyerror("system location already defined");
				free($2);
				YYERROR;
			}
			if (strlcpy(conf->sc_system.sys_location, $2,
			    sizeof(conf->sc_system.sys_location)) >=
			    sizeof(conf->sc_system.sys_location)) {
				yyerror("system location too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| NAME STRING			{
			if (conf->sc_system.sys_name[0] != '\0') {
				yyerror("system name already defined");
				free($2);
				YYERROR;
			}
			if (strlcpy(conf->sc_system.sys_name, $2,
			    sizeof(conf->sc_system.sys_name)) >=
			    sizeof(conf->sc_system.sys_name)) {
				yyerror("system name too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| OBJECTID oid			{
			if (sysoid.descriptor != NULL) {
				yyerror("system oid already defined");
				free($2.descriptor);
				YYERROR;
			}
			sysoid = $2;
		}
		| SERVICES NUMBER		{
			if (conf->sc_system.sys_services != -1) {
				yyerror("system services already defined");
				YYERROR;
			}
			if ($2 < 0) {
				yyerror("system services too small");
				YYERROR;
			} else if ($2 > 127) {
				yyerror("system services too large");
				YYERROR;
			}
			conf->sc_system.sys_services = $2;
		}
		;

object		: OBJECTID oid NAME STRING optwrite 	{
			struct object_sym *tobjects;

			if ((tobjects = recallocarray(objects, nobjects,
			    nobjects + 1, sizeof(*objects))) == NULL) {
				yyerror("malloc");
				free($2.descriptor);
				free($4);
			}
			objects = tobjects;
			nobjects++;
			objects[nobjects - 1].oid = $2;
			objects[nobjects - 1].name = $4;
		} objectvalue
		;

objectvalue	: INTEGER NUMBER			{
			if ($2 < INT32_MIN) {
				yyerror("number too small");
				YYERROR;
			}
			if ($2 > INT32_MAX) {
				yyerror("number too large");
				YYERROR;
			}
			objects[nobjects - 1].isint = 1;
			objects[nobjects - 1].intval = $2;
		}
		| OCTETSTRING STRING			{
			objects[nobjects - 1].isint = 0;
			objects[nobjects - 1].sval = $2;
		}
		;

optwrite	: READONLY				{ $$ = 0; }
		| READWRITE				{ $$ = 1; }
		;

oid		: STRING				{
			$$.descriptor = $1;
			strlcpy($$.file, file->name, sizeof($$.file));
			$$.lineno = file->lineno;
		}
		;

trapoid		: oid					{ $$ = $1; }
		| DEFAULT				{
			if (($$.descriptor = strdup("1.3")) == NULL) {
				yyerror("malloc");
				YYERROR;
			}
			strlcpy($$.file, file->name, sizeof($$.file));
			$$.lineno = file->lineno;
		}
		;

hostoid		: /* empty */				{
			$$.descriptor = NULL;
			strlcpy($$.file, file->name, sizeof($$.file));
			$$.lineno = file->lineno;
		}
		| OBJECTID oid				{ $$ = $2; }
		;

usmuser		: USER STRING				{
			if (strlen($2) > SNMPD_MAXUSERNAMELEN) {
				yyerror("User name too long: %s", $2);
				free($2);
				YYERROR;
			}
			$$ = $2;
		}
		;

community	: COMMUNITY STRING			{
			if (strlen($2) > SNMPD_MAXCOMMUNITYLEN) {
				yyerror("Community too long: %s", $2);
				free($2);
				YYERROR;
			}
			$$ = $2;
		}
		;

optcommunity	: /* empty */				{ $$ = NULL; }
		| community				{ $$ = $1; }
		;

usmauthopt	: usmuser				{
			$$.data = $1;
			$$.value = -1;
		}
		| seclevel				{
			$$.data = 0;
			$$.value = $1;
		}
		;

usmauthopts	: /* empty */				{
			$$.data = NULL;
			$$.value = -1;
		}
		| usmauthopts usmauthopt		{
			if ($2.data != NULL) {
				if ($$.data != NULL) {
					yyerror("user redefined");
					free($2.data);
					YYERROR;
				}
				$$.data = $2.data;
			} else {
				if ($$.value != -1) {
					yyerror("seclevel redefined");
					YYERROR;
				}
				$$.value = $2.value;
			}
		}
		;

hostauthv3	: usmauthopts				{
			if ($1.data == NULL) {
				yyerror("user missing");
				YYERROR;
			}
			$$.data = $1.data;
			$$.value = $1.value;
		}
		;

hostauth	: hostauthv3				{
			$$.type = SNMP_V3;
			$$.data = $1.data;
			$$.value = $1.value;
		}
		| SNMPV2 optcommunity			{
			$$.type = SNMP_V2;
			$$.data = $2;
		}
		| SNMPV3 hostauthv3			{
			$$.type = SNMP_V3;
			$$.data = $2.data;
			$$.value = $2.value;
		}
		;

srcaddr		: /* empty */				{ $$ = NULL; }
		| SRCADDR STRING			{ $$ = $2; }
		;

hostdef		: STRING hostoid hostauth srcaddr	{
			struct sockaddr_storage ss, ssl = {};
			struct trap_address *tr;
			struct trapaddress_sym *ttrapaddresses;

			if (host($1, SNMPTRAP_PORT, AF_UNSPEC, SOCK_DGRAM,
			    &ss, 1) <= 0) {
				yyerror("invalid host: %s", $1);
				free($1);
				free($2.descriptor);
				free($3.data);
				free($4);
				YYERROR;
			}
			free($1);
			if ($4 != NULL) {
				if (host($4, "0", ss.ss_family, SOCK_DGRAM,
				    &ss, 1) <= 0) {
					yyerror("invalid source-address: %s",
					    $4);
					free($2.descriptor);
					free($3.data);
					free($4);
					YYERROR;
				}
				free($4);
			}

			ttrapaddresses = reallocarray(trapaddresses,
			    ntrapaddresses + 1, sizeof(*trapaddresses));
			if (ttrapaddresses == NULL) {
				yyerror("malloc");
				free($2.descriptor);
				free($3.data);
				YYERROR;
			}
			trapaddresses = ttrapaddresses;
			ntrapaddresses++;

			if ((tr = calloc(1, sizeof(*tr))) == NULL) {
				yyerror("calloc");
				free($2.descriptor);
				free($3.data);
				ntrapaddresses--;
				YYERROR;
			}

			tr->ta_ss = ss;
			tr->ta_sslocal = ssl;
			tr->ta_version = $3.type;
			if ($3.type == SNMP_V2) {
				(void)strlcpy(tr->ta_community, $3.data,
				    sizeof(tr->ta_community));
				free($3.data);
			} else {
				tr->ta_usmusername = $3.data;
				tr->ta_seclevel = $3.value;
			}

			trapaddresses[ntrapaddresses - 1].oid = $2;
			trapaddresses[ntrapaddresses - 1].tr = tr;
		}
		;

hostlist	: /* empty */
		| hostlist comma hostdef
		;

host		: hostdef
		| '{' hostlist '}'
		;

comma		: /* empty */
		| ','
		;

seclevel	: SECLEVEL NONE	{ $$ = 0; }
		| SECLEVEL AUTH	{ $$ = SNMP_MSGFLAG_AUTH; }
		| SECLEVEL ENC	{ $$ = SNMP_MSGFLAG_AUTH | SNMP_MSGFLAG_PRIV; }
		;

userspecs	: /* empty */
		| userspecs userspec
		;

userspec	: AUTHKEY STRING		{
			user->uu_authkey = $2;
		}
		| AUTH auth			{
			user->uu_auth = $2;
		}
		| ENCKEY STRING			{
			user->uu_privkey = $2;
		}
		| ENC enc			{
			user->uu_priv = $2;
		}
		;

auth		: STRING			{
			if (strcasecmp($1, "hmac-md5") == 0 ||
			    strcasecmp($1, "hmac-md5-96") == 0)
				$$ = AUTH_MD5;
			else if (strcasecmp($1, "hmac-sha1") == 0 ||
			     strcasecmp($1, "hmac-sha1-96") == 0)
				$$ = AUTH_SHA1;
			else if (strcasecmp($1, "hmac-sha224") == 0 ||
			    strcasecmp($1, "usmHMAC128SHA224AuthProtocol") == 0)
				$$ = AUTH_SHA224;
			else if (strcasecmp($1, "hmac-sha256") == 0 ||
			    strcasecmp($1, "usmHMAC192SHA256AuthProtocol") == 0)
				$$ = AUTH_SHA256;
			else if (strcasecmp($1, "hmac-sha384") == 0 ||
			    strcasecmp($1, "usmHMAC256SHA384AuthProtocol") == 0)
				$$ = AUTH_SHA384;
			else if (strcasecmp($1, "hmac-sha512") == 0 ||
			    strcasecmp($1, "usmHMAC384SHA512AuthProtocol") == 0)
				$$ = AUTH_SHA512;
			else {
				yyerror("syntax error, bad auth hmac");
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

enc		: STRING			{
			if (strcasecmp($1, "des") == 0 ||
			    strcasecmp($1, "cbc-des") == 0)
				$$ = PRIV_DES;
			else if (strcasecmp($1, "aes") == 0 ||
			    strcasecmp($1, "cfb128-aes-128") == 0)
				$$ = PRIV_AES;
			else {
				yyerror("syntax error, bad encryption cipher");
				free($1);
				YYERROR;
			}
			free($1);

		}
		;

cmd		: STRING		{
			struct trapcmd	*cmd;
			size_t		 span, limit;
			char		*pos, **args, **args2;
			int		 nargs = 32;		/* XXX */

			if ((cmd = calloc(1, sizeof(*cmd))) == NULL ||
			    (args = calloc(nargs, sizeof(char *))) == NULL) {
				free(cmd);
				free($1);
				YYERROR;
			}

			pos = $1;
			limit = strlen($1);

			while (pos < $1 + limit &&
			    (span = strcspn(pos, " \t")) != 0) {
				pos[span] = '\0';
				args[cmd->cmd_argc] = strdup(pos);
				if (args[cmd->cmd_argc] == NULL) {
					trapcmd_free(cmd);
					free(args);
					free($1);
					YYERROR;
				}
				cmd->cmd_argc++;
				if (cmd->cmd_argc >= nargs - 1) {
					nargs *= 2;
					args2 = calloc(nargs, sizeof(char *));
					if (args2 == NULL) {
						trapcmd_free(cmd);
						free(args);
						free($1);
						YYERROR;
					}
					args = args2;
				}
				pos += span + 1;
			}
			free($1);
			cmd->cmd_argv = args;
			$$.data = cmd;
		}
		;

mib		: MIB DIRECTORY STRING		{
			mib_parsedir($3);
			mibparsed = 1;
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
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "agentid",			AGENTID },
		{ "agentx",			AGENTX },
		{ "auth",			AUTH },
		{ "authkey",			AUTHKEY },
		{ "blocklist",			BLOCKLIST },
		{ "community",			COMMUNITY },
		{ "contact",			CONTACT },
		{ "default",			DEFAULT },
		{ "description",		DESCR },
		{ "directory",			DIRECTORY },
		{ "enc",			ENC },
		{ "enckey",			ENCKEY },
		{ "engineid",			ENGINEID },
		{ "filter-routes",		RTFILTER },
		{ "group",			GROUP },
		{ "handle",			HANDLE },
		{ "hosthash",			HOSTHASH },
		{ "include",			INCLUDE },
		{ "integer",			INTEGER },
		{ "ipv4",			IP4 },
		{ "ipv6",			IP6 },
		{ "listen",			LISTEN },
		{ "location",			LOCATION },
		{ "mac",			MAC },
		{ "mib",			MIB },
		{ "mode",			MODE },
		{ "name",			NAME },
		{ "none",			NONE },
		{ "notify",			NOTIFY },
		{ "octets",			OCTETS },
		{ "oid",			OBJECTID },
		{ "on",				ON },
		{ "openbsd",			OPENBSD },
		{ "owner",			OWNER },
		{ "path",			PATH },
		{ "pen",			PEN },
		{ "port",			PORT },
		{ "read",			READ },
		{ "read-only",			READONLY },
		{ "read-write",			READWRITE },
		{ "receiver",			RECEIVER },
		{ "seclevel",			SECLEVEL },
		{ "services",			SERVICES },
		{ "snmpv1",			SNMPV1 },
		{ "snmpv2c",			SNMPV2 },
		{ "snmpv3",			SNMPV3 },
		{ "source-address",		SRCADDR },
		{ "string",			OCTETSTRING },
		{ "system",			SYSTEM },
		{ "tcp",			TCP },
		{ "text",			TEXT },
		{ "trap",			TRAP },
		{ "udp",			UDP },
		{ "user",			USER },
		{ "write",			WRITE }
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
		else c = getc(file->stream);

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
			yyerror("reached end of file while parsing quoted string");
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
	if (c == '\t' || c == ' ') {
		/* Compress blanks to a single space. */
		do {
			c = getc(file->stream);
		} while (c == '\t' || c == ' ');
		ungetc(c, file->stream);
		c = ' ';
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
resolve_oid(struct ber_oid *dst, struct oid_sym *src)
{
	struct file f = { .name = src->file, };
	const char *error;

	file = &f;
	yylval.lineno = src->lineno;

	if ((error = mib_string2oid(src->descriptor, dst)) != NULL) {
		if (smi_string2oid(src->descriptor, dst) == -1) {
			yyerror("%s", error);
			free(src->descriptor);
			return -1;
		}
		yyerror("deprecated oid format");
	}
	free(src->descriptor);

	return 0;
}

int
resolve_oids(void)
{
	struct file f;
	struct ber_oid oid;
	const char *error;
	size_t i;

	conf->sc_blocklist = calloc(nblocklist, sizeof(*conf->sc_blocklist));
	if (conf->sc_blocklist == NULL)
		fatal("malloc");

	conf->sc_nblocklist = nblocklist;
	for (i = 0; i < nblocklist; i++) {
		if (resolve_oid(&conf->sc_blocklist[i], &blocklist[i]) == -1)
			return -1;
	}
	free(blocklist);

	if (sysoid.descriptor != NULL) {
		if (resolve_oid(&conf->sc_system.sys_oid, &sysoid) == -1)
			return -1;
	}

	for (i = 0; i < nobjects; i++) {
		if (resolve_oid(&oid, &objects[i].oid) == -1)
			return -1;
		file = &f;
		f.name = objects[i].oid.file;
		yylval.lineno = objects[i].oid.lineno;
		if ((error = smi_insert(&oid, objects[i].name)) != NULL) {
			yyerror("%s", error);
			return -1;
		}
		if (objects[i].isint) {
			if ((error = appl_internal_object_int(
			    &oid, objects[i].intval)) != NULL) {
				yyerror("%s", error);
				return -1;
			}
		} else {
			if ((error = appl_internal_object_string(
			    &oid, objects[i].sval)) != NULL) {
				yyerror("%s", error);
				return -1;
			}
		}
		free(objects[i].name);
	}
	free(objects);

	for (i = 0; i < ntrapcmds; i++) {
		if (resolve_oid(
		    &trapcmds[i].cmd->cmd_oid, &trapcmds[i].oid) == -1)
			return -1;
		f.name = trapcmds[i].oid.file;
		yylval.lineno = trapcmds[i].oid.lineno;
		file = &f;
		if (trapcmd_add(trapcmds[i].cmd) != 0) {
			yyerror("duplicate oid");
			return -1;
		}
	}
	free(trapcmds);

	for (i = 0; i < ntrapaddresses; i++) {
		if (trapaddresses[i].oid.descriptor == NULL)
			trapaddresses[i].tr->ta_oid.bo_n = 0;
		else {
			if (resolve_oid(&trapaddresses[i].tr->ta_oid,
			    &trapaddresses[i].oid) == -1)
				return -1;
		}
		TAILQ_INSERT_TAIL(&conf->sc_trapreceivers,
		    trapaddresses[i].tr, entry);
	}
	free(trapaddresses);

	return 0;
}

struct snmpd *
parse_config(const char *filename, u_int flags)
{
	struct sockaddr_storage ss;
	struct utsname u;
	struct sym	*sym, *next;
	struct address	*h;
	struct trap_address	*tr;
	const struct usmuser	*up;
	const char	*errstr;
	char		 hostname[HOST_NAME_MAX + 1];
	int		 found;
	uint32_t	 npen = htonl(PEN_OPENBSD);

	if ((conf = calloc(1, sizeof(*conf))) == NULL) {
		log_warn("%s", __func__);
		return (NULL);
	}

	conf->sc_system.sys_services = -1;
	conf->sc_flags = flags;

	conf->sc_oidfmt =
	    flags & SNMPD_F_NONAMES ?  MIB_OIDNUMERIC : MIB_OIDSYMBOLIC;

	conf->sc_confpath = filename;
	TAILQ_INIT(&conf->sc_addresses);
	TAILQ_INIT(&conf->sc_agentx_masters);
	TAILQ_INIT(&conf->sc_trapreceivers);
	conf->sc_min_seclevel = SNMP_MSGFLAG_AUTH | SNMP_MSGFLAG_PRIV;

	if ((file = pushfile(filename, 0)) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;
	setservent(1);

	yyparse();
	errors = file->errors;
	popfile();

	endservent();

	if (errors) {
		free(conf);
		return (NULL);
	}

	if (!mibparsed)
		mib_parsedir("/usr/share/snmp/mibs");
	mib_resolve();

	if (resolve_oids() == -1) {
		free(conf);
		return NULL;
	}

	if (uname(&u) == -1)
		fatal("uname");

	if (conf->sc_system.sys_descr[0] == '\0')
		snprintf(conf->sc_system.sys_descr,
		    sizeof(conf->sc_system.sys_descr), "%s %s %s %s %s",
		    u.sysname, u.nodename, u.release, u.version, u.machine);
	if (conf->sc_system.sys_oid.bo_n == 0)
		conf->sc_system.sys_oid = OID(MIB_SYSOID_DEFAULT);
	if (conf->sc_system.sys_contact[0] == '\0')
		snprintf(conf->sc_system.sys_contact,
		    sizeof(conf->sc_system.sys_contact), "root@%s", u.nodename);
	if (conf->sc_system.sys_name[0] == '\0')
		snprintf(conf->sc_system.sys_name,
		    sizeof(conf->sc_system.sys_name), "%s", u.nodename);
	if (conf->sc_system.sys_services == -1)
		conf->sc_system.sys_services = 0;


	/* Must be identical to enginefmt_local:HOSTHASH */
	if (conf->sc_engineid_len == 0) {
		if (gethostname(hostname, sizeof(hostname)) == -1)
			fatal("gethostname");
		memcpy(conf->sc_engineid, &npen, sizeof(npen));
		conf->sc_engineid_len += sizeof(npen);
		conf->sc_engineid[conf->sc_engineid_len++] |=
		    SNMP_ENGINEID_FMT_HH;
		memcpy(conf->sc_engineid + conf->sc_engineid_len,
		    SHA256(hostname, strlen(hostname), sha256),
		    sizeof(conf->sc_engineid) - conf->sc_engineid_len);
		conf->sc_engineid_len = sizeof(conf->sc_engineid);
		conf->sc_engineid[0] |= SNMP_ENGINEID_NEW;
	}

	/* Setup default listen addresses */
	if (TAILQ_EMPTY(&conf->sc_addresses)) {
		if (host("0.0.0.0", SNMP_PORT, AF_INET, SOCK_DGRAM,
		    &ss, 1) != 1)
			fatal("Unexpected resolving of 0.0.0.0");
		if (listen_add(&ss, SOCK_DGRAM, 0) == -1)
			fatal("calloc");
		if (host("::", SNMP_PORT, AF_INET6, SOCK_DGRAM, &ss, 1) != 1)
			fatal("Unexpected resolving of ::");
		if (listen_add(&ss, SOCK_DGRAM, 0) == -1)
			fatal("calloc");
	}

	if ((up = usm_check_mincred(conf->sc_min_seclevel, &errstr)) != NULL)
		fatalx("user '%s': %s", up->uu_name, errstr);

	found = 0;
	TAILQ_FOREACH(h, &conf->sc_addresses, entry) {
		if (h->flags & ADDRESS_FLAG_NOTIFY)
			found = 1;
	}
	if (ntrapcmds && !found) {
		log_warnx("trap handler needs at least one notify listener");
		free(conf);
		return (NULL);
	}
	if (!ntrapcmds && found) {
		log_warnx("notify listener needs at least one trap handler");
		free(conf);
		return (NULL);
	}

	TAILQ_FOREACH(tr, &conf->sc_trapreceivers, entry) {
		if (tr->ta_version == SNMP_V2 &&
		    tr->ta_community[0] == '\0' &&
		    conf->sc_trcommunity[0] == '\0') {
			log_warnx("trap receiver: missing community");
			free(conf);
			return (NULL);
		}
		if (tr->ta_version == SNMP_V3) {
			tr->ta_usmuser = usm_finduser(tr->ta_usmusername);
			if (tr->ta_usmuser == NULL) {
				log_warnx("trap receiver: user not defined: %s",
				    tr->ta_usmusername);
				free(conf);
				return (NULL);
			}
		}
	}

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((conf->sc_flags & SNMPD_F_VERBOSE) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
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

int
host(const char *s, const char *port, int family, int type,
    struct sockaddr_storage *ss, int max)
{
	struct addrinfo		 hints, *res0, *res;
	int			 error, i;

	bzero(&hints, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = type;
	/*
	 * Without AI_NUMERICHOST getaddrinfo might not resolve ip addresses
	 * for families not specified in the "family" statement in resolv.conf.
	 */
	hints.ai_flags = AI_NUMERICHOST;
	error = getaddrinfo(s, port, &hints, &res0);
	if (error == EAI_NONAME) {
		hints.ai_flags = 0;
		error = getaddrinfo(s, port, &hints, &res0);
	}
	if (error == EAI_AGAIN || error == EAI_NODATA || error == EAI_NONAME)
		return 0;
	if (error) {
		log_warnx("Could not parse \"%s\": %s", s, gai_strerror(error));
		return -1;
	}

	for (i = 0, res = res0; res; res = res->ai_next, i++) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;
		if (i >= max)
			continue;

		bcopy(res->ai_addr, &(ss[i]), res->ai_addrlen);
	}
	freeaddrinfo(res0);

	return i;
}

int
listen_add(struct sockaddr_storage *ss, int type, int flags)
{
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	struct address *h;

	if ((h = calloc(1, sizeof(*h))) == NULL)
		return -1;
	bcopy(ss, &(h->ss), sizeof(*ss));
	if (ss->ss_family == AF_INET) {
		sin = (struct sockaddr_in *)ss;
		h->port = ntohs(sin->sin_port);
	} else {
		sin6 = (struct sockaddr_in6*)ss;
		h->port = ntohs(sin6->sin6_port);
	}
	h->type = type;
	if (((h->flags = flags) & ADDRESS_FLAG_PERM) == 0) {
		if (h->port == 162)
			h->flags |= ADDRESS_FLAG_NOTIFY;
		else
			h->flags |= ADDRESS_FLAG_READ | ADDRESS_FLAG_WRITE;
	}
	if ((h->flags & ADDRESS_FLAG_MPS) == 0)
		h->flags |= ADDRESS_FLAG_SNMPV3;
	TAILQ_INSERT_TAIL(&(conf->sc_addresses), h, entry);

	return 0;
}
