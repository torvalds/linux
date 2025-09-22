/*	$OpenBSD: parse.y,v 1.482 2025/02/27 14:15:35 claudio Exp $ */

/*
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
 * Copyright (c) 2016, 2017 Job Snijders <job@openbsd.org>
 * Copyright (c) 2016 Peter Hessler <phessler@openbsd.org>
 * Copyright (c) 2017, 2018 Sebastian Benoit <benno@openbsd.org>
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
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_ipsp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <endian.h>
#include <err.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "bgpd.h"
#include "session.h"
#include "rde.h"
#include "log.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

#define MACRO_NAME_LEN		128

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
int		 expand_macro(void);

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

struct filter_rib_l {
	struct filter_rib_l	*next;
	char			 name[PEER_DESCR_LEN];
};

struct filter_peers_l {
	struct filter_peers_l	*next;
	struct filter_peers	 p;
};

struct filter_prefix_l {
	struct filter_prefix_l	*next;
	struct filter_prefix	 p;
};

struct filter_prefixlen {
	enum comp_ops		op;
	int			len_min;
	int			len_max;
};

struct filter_as_l {
	struct filter_as_l	*next;
	struct filter_as	 a;
};

struct filter_match_l {
	struct filter_match	 m;
	struct filter_prefix_l	*prefix_l;
	struct filter_as_l	*as_l;
	struct filter_prefixset	*prefixset;
} fmopts;

struct aspa_tas_l {
	struct aspa_tas_l	*next;
	uint32_t		 as;
	uint32_t		 num;
};

struct flowspec_context {
	uint8_t			*components[FLOWSPEC_TYPE_MAX];
	uint16_t		 complen[FLOWSPEC_TYPE_MAX];
	uint8_t			 aid;
	uint8_t			 type;
	uint8_t			 addr_type;
};

struct peer	*alloc_peer(void);
struct peer	*new_peer(void);
struct peer	*new_group(void);
int		 add_mrtconfig(enum mrt_type, char *, int, struct peer *,
		    char *);
struct rde_rib	*add_rib(char *);
struct rde_rib	*find_rib(char *);
int		 rib_add_fib(struct rde_rib *, u_int);
int		 get_id(struct peer *);
int		 merge_prefixspec(struct filter_prefix *,
		    struct filter_prefixlen *);
int		 expand_rule(struct filter_rule *, struct filter_rib_l *,
		    struct filter_peers_l *, struct filter_match_l *,
		    struct filter_set_head *);
int		 str2key(char *, char *, size_t);
int		 neighbor_consistent(struct peer *);
int		 merge_filterset(struct filter_set_head *, struct filter_set *);
void		 optimize_filters(struct filter_head *);
struct filter_rule	*get_rule(enum action_types);

int		 parsecommunity(struct community *, int, char *);
int		 parseextcommunity(struct community *, char *,
		    char *);
static int	 new_as_set(char *);
static void	 add_as_set(uint32_t);
static void	 done_as_set(void);
static struct prefixset	*new_prefix_set(char *, int);
static void	 add_roa_set(struct prefixset_item *, uint32_t, uint8_t,
		    time_t);
static struct rtr_config	*get_rtr(struct bgpd_addr *);
static int	 insert_rtr(struct rtr_config *);
static int	 merge_aspa_set(uint32_t, struct aspa_tas_l *, time_t);
static int	 map_tos(char *, int *);
static int	 getservice(char *);
static int	 parse_flags(char *);
static struct flowspec_config	*flow_to_flowspec(struct flowspec_context *);
static void	 flow_free(struct flowspec_context *);
static int	 push_prefix(struct bgpd_addr *, uint8_t);
static int	 push_binop(uint8_t, long long);
static int	 push_unary_numop(enum comp_ops, long long);
static int	 push_binary_numop(enum comp_ops, long long, long long);
static int	 geticmptypebyname(char *, uint8_t);
static int	 geticmpcodebyname(u_long, char *, uint8_t);
static int	 merge_auth_conf(struct auth_config *, struct auth_config *);

static struct bgpd_config	*conf;
static struct network_head	*netconf;
static struct peer_head		*new_peers, *cur_peers;
static struct rtr_config_head	*cur_rtrs;
static struct peer		*curpeer;
static struct peer		*curgroup;
static struct rde_rib		*currib;
static struct l3vpn		*curvpn;
static struct prefixset		*curpset, *curoset;
static struct roa_tree		*curroatree;
static struct rtr_config	*currtr;
static struct filter_head	*filter_l;
static struct filter_head	*peerfilter_l;
static struct filter_head	*groupfilter_l;
static struct filter_rule	*curpeer_filter[2];
static struct filter_rule	*curgroup_filter[2];
static struct flowspec_context	*curflow;
static int			 noexpires;

typedef struct {
	union {
		long long		 number;
		char			*string;
		struct bgpd_addr	 addr;
		uint8_t			 u8;
		struct filter_rib_l	*filter_rib;
		struct filter_peers_l	*filter_peers;
		struct filter_match_l	 filter_match;
		struct filter_prefixset	*filter_prefixset;
		struct filter_prefix_l	*filter_prefix;
		struct filter_as_l	*filter_as;
		struct filter_set	*filter_set;
		struct filter_set_head	*filter_set_head;
		struct aspa_tas_l	*aspa_elm;
		struct {
			struct bgpd_addr	prefix;
			uint8_t			len;
		}			prefix;
		struct filter_prefixlen	prefixlen;
		struct prefixset_item	*prefixset_item;
		struct auth_config	authconf;
		struct {
			enum auth_enc_alg	enc_alg;
			uint8_t			enc_key_len;
			char			enc_key[IPSEC_ENC_KEY_LEN];
		}			encspec;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	AS ROUTERID HOLDTIME YMIN LISTEN ON FIBUPDATE FIBPRIORITY RTABLE
%token	NONE UNICAST VPN RD EXPORT EXPORTTRGT IMPORTTRGT DEFAULTROUTE
%token	RDE RIB EVALUATE IGNORE COMPARE RTR PORT MINVERSION STALETIME
%token	GROUP NEIGHBOR NETWORK
%token	EBGP IBGP
%token	FLOWSPEC PROTO FLAGS FRAGMENT TOS LENGTH ICMPTYPE CODE
%token	LOCALAS REMOTEAS DESCR LOCALADDR MULTIHOP PASSIVE MAXPREFIX RESTART
%token	ANNOUNCE REFRESH AS4BYTE CONNECTRETRY ENHANCED ADDPATH EXTENDED
%token	SEND RECV PLUS POLICY ROLE GRACEFUL NOTIFICATION MESSAGE
%token	DEMOTE ENFORCE NEIGHBORAS ASOVERRIDE REFLECTOR DEPEND DOWN
%token	DUMP IN OUT SOCKET RESTRICTED
%token	LOG TRANSPARENT FILTERED
%token	TCP MD5SIG PASSWORD KEY TTLSECURITY
%token	ALLOW DENY MATCH
%token	QUICK
%token	FROM TO ANY
%token	CONNECTED STATIC
%token	COMMUNITY EXTCOMMUNITY LARGECOMMUNITY DELETE
%token	MAXCOMMUNITIES MAXEXTCOMMUNITIES MAXLARGECOMMUNITIES
%token	PREFIX PREFIXLEN PREFIXSET
%token	ASPASET ROASET ORIGINSET OVS AVS EXPIRES
%token	ASSET SOURCEAS TRANSITAS PEERAS PROVIDERAS CUSTOMERAS MAXASLEN MAXASSEQ
%token	SET LOCALPREF MED METRIC NEXTHOP REJECT BLACKHOLE NOMODIFY SELF
%token	PREPEND_SELF PREPEND_PEER PFTABLE WEIGHT RTLABEL ORIGIN PRIORITY
%token	ERROR INCLUDE
%token	IPSEC ESP AH SPI IKE
%token	IPV4 IPV6 EVPN
%token	QUALIFY VIA
%token	NE LE GE XRANGE LONGER MAXLEN MAX
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.number>		asnumber as4number as4number_any optnumber
%type	<v.number>		espah af safi restart origincode nettype
%type	<v.number>		yesno inout restricted expires
%type	<v.number>		yesnoenforce enforce
%type	<v.number>		validity aspa_validity
%type	<v.number>		addpathextra addpathmax
%type	<v.number>		port proto_item tos length flag icmptype
%type	<v.string>		string
%type	<v.addr>		address
%type	<v.prefix>		prefix addrspec
%type	<v.prefixset_item>	prefixset_item
%type	<v.u8>			action quick direction delete community
%type	<v.filter_rib>		filter_rib_h filter_rib_l filter_rib
%type	<v.filter_peers>	filter_peer filter_peer_l filter_peer_h
%type	<v.filter_match>	filter_match filter_elm filter_match_h
%type	<v.filter_as>		filter_as filter_as_l filter_as_h
%type	<v.filter_as>		filter_as_t filter_as_t_l filter_as_l_h
%type	<v.prefixlen>		prefixlenop
%type	<v.filter_set>		filter_set_opt
%type	<v.filter_set_head>	filter_set filter_set_l
%type	<v.filter_prefix>	filter_prefix filter_prefix_l filter_prefix_h
%type	<v.filter_prefix>	filter_prefix_m
%type	<v.u8>			unaryop equalityop binaryop filter_as_type
%type	<v.authconf>		authconf
%type	<v.encspec>		encspec
%type	<v.aspa_elm>		aspa_tas aspa_tas_l
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar varset '\n'
		| grammar include '\n'
		| grammar as_set '\n'
		| grammar prefixset '\n'
		| grammar roa_set '\n'
		| grammar aspa_set '\n'
		| grammar origin_set '\n'
		| grammar rtr '\n'
		| grammar rib '\n'
		| grammar network '\n'
		| grammar flowspec '\n'
		| grammar mrtdump '\n'
		| grammar conf_main '\n'
		| grammar l3vpn '\n'
		| grammar neighbor '\n'
		| grammar group '\n'
		| grammar filterrule '\n'
		| grammar error '\n'		{ file->errors++; }
		;

asnumber	: NUMBER			{
			/*
			 * According to iana 65535 and 4294967295 are reserved
			 * but enforcing this is not duty of the parser.
			 */
			if ($1 < 0 || $1 > UINT_MAX) {
				yyerror("AS too big: max %u", UINT_MAX);
				YYERROR;
			}
		}

as4number	: STRING			{
			const char	*errstr;
			char		*dot;
			uint32_t	 uvalh = 0, uval;

			if ((dot = strchr($1, '.')) != NULL) {
				*dot++ = '\0';
				uvalh = strtonum($1, 0, USHRT_MAX, &errstr);
				if (errstr) {
					yyerror("number %s is %s", $1, errstr);
					free($1);
					YYERROR;
				}
				uval = strtonum(dot, 0, USHRT_MAX, &errstr);
				if (errstr) {
					yyerror("number %s is %s", dot, errstr);
					free($1);
					YYERROR;
				}
				free($1);
			} else {
				yyerror("AS %s is bad", $1);
				free($1);
				YYERROR;
			}
			if (uvalh == 0 && (uval == AS_TRANS || uval == 0)) {
				yyerror("AS %u is reserved and may not be used",
				    uval);
				YYERROR;
			}
			$$ = uval | (uvalh << 16);
		}
		| asnumber {
			if ($1 == AS_TRANS || $1 == 0) {
				yyerror("AS %u is reserved and may not be used",
				    (uint32_t)$1);
				YYERROR;
			}
			$$ = $1;
		}
		;

as4number_any	: STRING			{
			const char	*errstr;
			char		*dot;
			uint32_t	 uvalh = 0, uval;

			if ((dot = strchr($1, '.')) != NULL) {
				*dot++ = '\0';
				uvalh = strtonum($1, 0, USHRT_MAX, &errstr);
				if (errstr) {
					yyerror("number %s is %s", $1, errstr);
					free($1);
					YYERROR;
				}
				uval = strtonum(dot, 0, USHRT_MAX, &errstr);
				if (errstr) {
					yyerror("number %s is %s", dot, errstr);
					free($1);
					YYERROR;
				}
				free($1);
			} else {
				yyerror("AS %s is bad", $1);
				free($1);
				YYERROR;
			}
			$$ = uval | (uvalh << 16);
		}
		| asnumber {
			$$ = $1;
		}
		;

string		: string STRING			{
			if (asprintf(&$$, "%s %s", $1, $2) == -1)
				fatal("string: asprintf");
			free($1);
			free($2);
		}
		| STRING
		;

yesno		: STRING			{
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

varset		: STRING '=' string		{
			char *s = $1;
			if (strlen($1) >= MACRO_NAME_LEN) {
				yyerror("macro name to long, max %d characters",
				    MACRO_NAME_LEN - 1);
				free($1);
				free($3);
				YYERROR;
			}
			do {
				if (isalnum((unsigned char)*s) || *s == '_')
					continue;
				yyerror("macro name can only contain "
					    "alphanumerics and '_'");
				free($1);
				free($3);
				YYERROR;
			} while (*++s);

			if (cmd_opts & BGPD_OPT_VERBOSE)
				printf("%s = \"%s\"\n", $1, $3);
			if (symset($1, $3, 0) == -1)
				fatal("cannot store variable");
			free($1);
			free($3);
		}
		;

include		: INCLUDE STRING		{
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

as_set		: ASSET STRING '{' optnl	{
			if (strlen($2) >= SET_NAME_LEN) {
				yyerror("as-set name %s too long", $2);
				free($2);
				YYERROR;
			}
			if (new_as_set($2) != 0) {
				free($2);
				YYERROR;
			}
			free($2);
		} as_set_l optnl '}' {
			done_as_set();
		}
		| ASSET STRING '{' optnl '}'	{
			if (new_as_set($2) != 0) {
				free($2);
				YYERROR;
			}
			free($2);
		}

as_set_l	: as4number_any			{ add_as_set($1); }
		| as_set_l comma as4number_any	{ add_as_set($3); }

prefixset	: PREFIXSET STRING '{' optnl		{
			if ((curpset = new_prefix_set($2, 0)) == NULL) {
				free($2);
				YYERROR;
			}
			free($2);
		} prefixset_l optnl '}'			{
			SIMPLEQ_INSERT_TAIL(&conf->prefixsets, curpset, entry);
			curpset = NULL;
		}
		| PREFIXSET STRING '{' optnl '}'	{
			if ((curpset = new_prefix_set($2, 0)) == NULL) {
				free($2);
				YYERROR;
			}
			free($2);
			SIMPLEQ_INSERT_TAIL(&conf->prefixsets, curpset, entry);
			curpset = NULL;
		}

prefixset_l	: prefixset_item			{
			struct prefixset_item	*psi;
			if ($1->p.op != OP_NONE)
				curpset->sflags |= PREFIXSET_FLAG_OPS;
			psi = RB_INSERT(prefixset_tree, &curpset->psitems, $1);
			if (psi != NULL) {
				if (cmd_opts & BGPD_OPT_VERBOSE2)
					log_warnx("warning: duplicate entry in "
					    "prefixset \"%s\" for %s/%u",
					    curpset->name,
					    log_addr(&$1->p.addr), $1->p.len);
				free($1);
			}
		}
		| prefixset_l comma prefixset_item	{
			struct prefixset_item	*psi;
			if ($3->p.op != OP_NONE)
				curpset->sflags |= PREFIXSET_FLAG_OPS;
			psi = RB_INSERT(prefixset_tree, &curpset->psitems, $3);
			if (psi != NULL) {
				if (cmd_opts & BGPD_OPT_VERBOSE2)
					log_warnx("warning: duplicate entry in "
					    "prefixset \"%s\" for %s/%u",
					    curpset->name,
					    log_addr(&$3->p.addr), $3->p.len);
				free($3);
			}
		}
		;

prefixset_item	: prefix prefixlenop			{
			if ($2.op != OP_NONE && $2.op != OP_RANGE) {
				yyerror("unsupported prefixlen operation in "
				    "prefix-set");
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				fatal(NULL);
			memcpy(&$$->p.addr, &$1.prefix, sizeof($$->p.addr));
			$$->p.len = $1.len;
			if (merge_prefixspec(&$$->p, &$2) == -1) {
				free($$);
				YYERROR;
			}
		}
		;

roa_set		: ROASET '{' optnl		{
			curroatree = &conf->roa;
		} roa_set_l optnl '}'			{
			curroatree = NULL;
		}
		| ROASET '{' optnl '}'		/* nothing */
		;

origin_set	: ORIGINSET STRING '{' optnl		{
			if ((curoset = new_prefix_set($2, 1)) == NULL) {
				free($2);
				YYERROR;
			}
			curroatree = &curoset->roaitems;
			noexpires = 1;
			free($2);
		} roa_set_l optnl '}'			{
			SIMPLEQ_INSERT_TAIL(&conf->originsets, curoset, entry);
			curoset = NULL;
			curroatree = NULL;
			noexpires = 0;
		}
		| ORIGINSET STRING '{' optnl '}'		{
			if ((curoset = new_prefix_set($2, 1)) == NULL) {
				free($2);
				YYERROR;
			}
			free($2);
			SIMPLEQ_INSERT_TAIL(&conf->originsets, curoset, entry);
			curoset = NULL;
			curroatree = NULL;
		}
		;

expires		: /* empty */	{
			$$ = 0;
		}
		| EXPIRES NUMBER	{
			if (noexpires) {
				yyerror("syntax error, expires not allowed");
				YYERROR;
			}
			$$ = $2;
		}

roa_set_l	: prefixset_item SOURCEAS as4number_any	expires		{
			if ($1->p.len_min != $1->p.len) {
				yyerror("unsupported prefixlen operation in "
				    "roa-set");
				free($1);
				YYERROR;
			}
			add_roa_set($1, $3, $1->p.len_max, $4);
			free($1);
		}
		| roa_set_l comma prefixset_item SOURCEAS as4number_any	expires {
			if ($3->p.len_min != $3->p.len) {
				yyerror("unsupported prefixlen operation in "
				    "roa-set");
				free($3);
				YYERROR;
			}
			add_roa_set($3, $5, $3->p.len_max, $6);
			free($3);
		}
		;

aspa_set	: ASPASET '{' optnl aspa_set_l optnl '}'
		| ASPASET '{' optnl '}'
		;

aspa_set_l	: aspa_elm
		| aspa_set_l comma aspa_elm
		;

aspa_elm	: CUSTOMERAS as4number expires PROVIDERAS '{' optnl
		    aspa_tas_l optnl '}' {
			int rv;
			struct aspa_tas_l *a, *n;

			rv = merge_aspa_set($2, $7, $3);

			for (a = $7; a != NULL; a = n) {
				n = a->next;
				free(a);
			}

			if (rv == -1)
				YYERROR;
		}
		;

aspa_tas_l	: aspa_tas			{ $$ = $1; }
		| aspa_tas_l comma aspa_tas	{
			$3->next = $1;
			$3->num = $1->num + 1;
			$$ = $3;
		}
		;

aspa_tas	: as4number_any {
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				fatal(NULL);
			$$->as = $1;
			$$->num = 1;
		}
		| as4number_any af {
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				fatal(NULL);
			$$->as = $1;
			$$->num = 1;
		}
		;

rtr		: RTR address	{
			currtr = get_rtr(&$2);
			currtr->remote_port = RTR_PORT;
			if (insert_rtr(currtr) == -1) {
				free(currtr);
				YYERROR;
			}
			currtr = NULL;
		}
		| RTR address	{
			currtr = get_rtr(&$2);
			currtr->remote_port = RTR_PORT;
		} '{' optnl rtropt_l optnl '}' {
			if (insert_rtr(currtr) == -1) {
				free(currtr);
				YYERROR;
			}
			currtr = NULL;
		}
		;

rtropt_l	: rtropt
		| rtropt_l optnl rtropt
		;

rtropt		: DESCR STRING		{
			if (strlcpy(currtr->descr, $2,
			    sizeof(currtr->descr)) >=
			    sizeof(currtr->descr)) {
				yyerror("descr \"%s\" too long: max %zu",
				    $2, sizeof(currtr->descr) - 1);
				free($2);
				YYERROR;
			}
			free($2);
		}
		| LOCALADDR address	{
			if ($2.aid != currtr->remote_addr.aid) {
				yyerror("Bad address family %s for "
				    "local-addr", aid2str($2.aid));
				YYERROR;
			}
			currtr->local_addr = $2;
		}
		| PORT port {
			currtr->remote_port = $2;
		}
		| MINVERSION NUMBER {
			if ($2 < 0 || $2 > RTR_MAX_VERSION) {
				yyerror("min-version must be between %u and %u",
				    0, RTR_MAX_VERSION);
				YYERROR;
			}
			currtr->min_version = $2;
		}
		| authconf {
			if (merge_auth_conf(&currtr->auth, &$1) == 0)
				YYERROR;
		}
		;

conf_main	: AS as4number		{
			conf->as = $2;
			if ($2 > USHRT_MAX)
				conf->short_as = AS_TRANS;
			else
				conf->short_as = $2;
		}
		| AS as4number asnumber {
			conf->as = $2;
			conf->short_as = $3;
		}
		| ROUTERID address		{
			if ($2.aid != AID_INET) {
				yyerror("router-id must be an IPv4 address");
				YYERROR;
			}
			conf->bgpid = ntohl($2.v4.s_addr);
		}
		| HOLDTIME NUMBER	{
			if ($2 < MIN_HOLDTIME || $2 > USHRT_MAX) {
				yyerror("holdtime must be between %u and %u",
				    MIN_HOLDTIME, USHRT_MAX);
				YYERROR;
			}
			conf->holdtime = $2;
		}
		| HOLDTIME YMIN NUMBER	{
			if ($3 < MIN_HOLDTIME || $3 > USHRT_MAX) {
				yyerror("holdtime must be between %u and %u",
				    MIN_HOLDTIME, USHRT_MAX);
				YYERROR;
			}
			conf->min_holdtime = $3;
		}
		| STALETIME NUMBER	{
			if ($2 < MIN_HOLDTIME || $2 > USHRT_MAX) {
				yyerror("staletime must be between %u and %u",
				    MIN_HOLDTIME, USHRT_MAX);
				YYERROR;
			}
			conf->staletime = $2;
		}
		| LISTEN ON address	{
			struct listen_addr	*la;
			struct sockaddr		*sa;

			if ((la = calloc(1, sizeof(struct listen_addr))) ==
			    NULL)
				fatal("parse conf_main listen on calloc");

			la->fd = -1;
			la->reconf = RECONF_REINIT;
			sa = addr2sa(&$3, BGP_PORT, &la->sa_len);
			memcpy(&la->sa, sa, la->sa_len);
			TAILQ_INSERT_TAIL(conf->listen_addrs, la, entry);
		}
		| LISTEN ON address PORT port	{
			struct listen_addr	*la;
			struct sockaddr		*sa;

			if ((la = calloc(1, sizeof(struct listen_addr))) ==
			    NULL)
				fatal("parse conf_main listen on calloc");

			la->fd = -1;
			la->reconf = RECONF_REINIT;
			sa = addr2sa(&$3, $5, &la->sa_len);
			memcpy(&la->sa, sa, la->sa_len);
			TAILQ_INSERT_TAIL(conf->listen_addrs, la, entry);
		}
		| FIBPRIORITY NUMBER		{
			if (!kr_check_prio($2)) {
				yyerror("fib-priority %lld out of range", $2);
				YYERROR;
			}
			conf->fib_priority = $2;
		}
		| FIBUPDATE yesno		{
			struct rde_rib *rr;
			rr = find_rib("Loc-RIB");
			if (rr == NULL)
				fatalx("RTABLE cannot find the main RIB!");

			if ($2 == 0)
				rr->flags |= F_RIB_NOFIBSYNC;
			else
				rr->flags &= ~F_RIB_NOFIBSYNC;
		}
		| TRANSPARENT yesno	{
			if ($2 == 1)
				conf->flags |= BGPD_FLAG_DECISION_TRANS_AS;
			else
				conf->flags &= ~BGPD_FLAG_DECISION_TRANS_AS;
		}
		| REJECT ASSET yesno	{
			if ($3 == 1)
				conf->flags &= ~BGPD_FLAG_PERMIT_AS_SET;
			else
				conf->flags |= BGPD_FLAG_PERMIT_AS_SET;
		}
		| LOG STRING		{
			if (!strcmp($2, "updates"))
				conf->log |= BGPD_LOG_UPDATES;
			else {
				free($2);
				YYERROR;
			}
			free($2);
		}
		| DUMP STRING STRING optnumber		{
			int action;

			if ($4 < 0 || $4 > INT_MAX) {
				yyerror("bad timeout");
				free($2);
				free($3);
				YYERROR;
			}
			if (!strcmp($2, "table"))
				action = MRT_TABLE_DUMP;
			else if (!strcmp($2, "table-mp"))
				action = MRT_TABLE_DUMP_MP;
			else if (!strcmp($2, "table-v2"))
				action = MRT_TABLE_DUMP_V2;
			else {
				yyerror("unknown mrt dump type");
				free($2);
				free($3);
				YYERROR;
			}
			free($2);
			if (add_mrtconfig(action, $3, $4, NULL, NULL) == -1) {
				free($3);
				YYERROR;
			}
			free($3);
		}
		| DUMP RIB STRING STRING STRING optnumber		{
			int action;

			if ($6 < 0 || $6 > INT_MAX) {
				yyerror("bad timeout");
				free($3);
				free($4);
				free($5);
				YYERROR;
			}
			if (!strcmp($4, "table"))
				action = MRT_TABLE_DUMP;
			else if (!strcmp($4, "table-mp"))
				action = MRT_TABLE_DUMP_MP;
			else if (!strcmp($4, "table-v2"))
				action = MRT_TABLE_DUMP_V2;
			else {
				yyerror("unknown mrt dump type");
				free($3);
				free($4);
				free($5);
				YYERROR;
			}
			free($4);
			if (add_mrtconfig(action, $5, $6, NULL, $3) == -1) {
				free($3);
				free($5);
				YYERROR;
			}
			free($3);
			free($5);
		}
		| RDE STRING EVALUATE		{
			if (!strcmp($2, "route-age"))
				conf->flags |= BGPD_FLAG_DECISION_ROUTEAGE;
			else {
				yyerror("unknown route decision type");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| RDE STRING IGNORE		{
			if (!strcmp($2, "route-age"))
				conf->flags &= ~BGPD_FLAG_DECISION_ROUTEAGE;
			else {
				yyerror("unknown route decision type");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| RDE MED COMPARE STRING	{
			if (!strcmp($4, "always"))
				conf->flags |= BGPD_FLAG_DECISION_MED_ALWAYS;
			else if (!strcmp($4, "strict"))
				conf->flags &= ~BGPD_FLAG_DECISION_MED_ALWAYS;
			else {
				yyerror("rde med compare: "
				    "unknown setting \"%s\"", $4);
				free($4);
				YYERROR;
			}
			free($4);
		}
		| RDE EVALUATE STRING {
			if (!strcmp($3, "all"))
				conf->flags |= BGPD_FLAG_DECISION_ALL_PATHS;
			else if (!strcmp($3, "default"))
				conf->flags &= ~BGPD_FLAG_DECISION_ALL_PATHS;
			else {
				yyerror("rde evaluate: "
				    "unknown setting \"%s\"", $3);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| RDE RIB STRING INCLUDE FILTERED {
			if (strcmp($3, "Loc-RIB") != 0) {
				yyerror("include filtered only supported in "
				    "Loc-RIB");
				YYERROR;
			}
			conf->filtered_in_locrib = 1;
		}
		| NEXTHOP QUALIFY VIA STRING	{
			if (!strcmp($4, "bgp"))
				conf->flags |= BGPD_FLAG_NEXTHOP_BGP;
			else if (!strcmp($4, "default"))
				conf->flags |= BGPD_FLAG_NEXTHOP_DEFAULT;
			else {
				yyerror("nexthop depend on: "
				    "unknown setting \"%s\"", $4);
				free($4);
				YYERROR;
			}
			free($4);
		}
		| RTABLE NUMBER {
			struct rde_rib *rr;
			if ($2 > RT_TABLEID_MAX) {
				yyerror("rtable %llu too big: max %u", $2,
				    RT_TABLEID_MAX);
				YYERROR;
			}
			if (!ktable_exists($2, NULL)) {
				yyerror("rtable id %lld does not exist", $2);
				YYERROR;
			}
			rr = find_rib("Loc-RIB");
			if (rr == NULL)
				fatalx("RTABLE cannot find the main RIB!");
			rr->rtableid = $2;
		}
		| CONNECTRETRY NUMBER {
			if ($2 > USHRT_MAX || $2 < 1) {
				yyerror("invalid connect-retry");
				YYERROR;
			}
			conf->connectretry = $2;
		}
		| SOCKET STRING	restricted {
			if (strlen($2) >=
			    sizeof(((struct sockaddr_un *)0)->sun_path)) {
				yyerror("socket path too long");
				YYERROR;
			}
			if ($3) {
				free(conf->rcsock);
				conf->rcsock = $2;
			} else {
				free(conf->csock);
				conf->csock = $2;
			}
		}
		;

rib		: RDE RIB STRING {
			if ((currib = add_rib($3)) == NULL) {
				free($3);
				YYERROR;
			}
			free($3);
		} ribopts {
			currib = NULL;
		}

ribopts		: fibupdate
		| RTABLE NUMBER fibupdate {
			if ($2 > RT_TABLEID_MAX) {
				yyerror("rtable %llu too big: max %u", $2,
				    RT_TABLEID_MAX);
				YYERROR;
			}
			if (rib_add_fib(currib, $2) == -1)
				YYERROR;
		}
		| yesno EVALUATE {
			if ($1) {
				yyerror("bad rde rib definition");
				YYERROR;
			}
			currib->flags |= F_RIB_NOEVALUATE;
		}
		;

fibupdate	: /* empty */
		| FIBUPDATE yesno {
			if ($2 == 0)
				currib->flags |= F_RIB_NOFIBSYNC;
			else
				currib->flags &= ~F_RIB_NOFIBSYNC;
		}
		;

mrtdump		: DUMP STRING inout STRING optnumber	{
			int action;

			if ($5 < 0 || $5 > INT_MAX) {
				yyerror("bad timeout");
				free($2);
				free($4);
				YYERROR;
			}
			if (!strcmp($2, "all"))
				action = $3 ? MRT_ALL_IN : MRT_ALL_OUT;
			else if (!strcmp($2, "updates"))
				action = $3 ? MRT_UPDATE_IN : MRT_UPDATE_OUT;
			else {
				yyerror("unknown mrt msg dump type");
				free($2);
				free($4);
				YYERROR;
			}
			if (add_mrtconfig(action, $4, $5, curpeer, NULL) ==
			    -1) {
				free($2);
				free($4);
				YYERROR;
			}
			free($2);
			free($4);
		}
		;

network		: NETWORK prefix filter_set	{
			struct network	*n, *m;

			if ((n = calloc(1, sizeof(struct network))) == NULL)
				fatal("new_network");
			memcpy(&n->net.prefix, &$2.prefix,
			    sizeof(n->net.prefix));
			n->net.prefixlen = $2.len;
			filterset_move($3, &n->net.attrset);
			free($3);
			TAILQ_FOREACH(m, netconf, entry) {
				if (n->net.type == m->net.type &&
				    n->net.prefixlen == m->net.prefixlen &&
				    prefix_compare(&n->net.prefix,
				    &m->net.prefix, n->net.prefixlen) == 0)
					yyerror("duplicate prefix "
					    "in network statement");
			}

			TAILQ_INSERT_TAIL(netconf, n, entry);
		}
		| NETWORK PREFIXSET STRING filter_set	{
			struct prefixset *ps;
			struct network	*n;
			if ((ps = find_prefixset($3, &conf->prefixsets))
			    == NULL) {
				yyerror("prefix-set '%s' not defined", $3);
				free($3);
				filterset_free($4);
				free($4);
				YYERROR;
			}
			if (ps->sflags & PREFIXSET_FLAG_OPS) {
				yyerror("prefix-set %s has prefixlen operators "
				    "and cannot be used in network statements.",
				    ps->name);
				free($3);
				filterset_free($4);
				free($4);
				YYERROR;
			}
			if ((n = calloc(1, sizeof(struct network))) == NULL)
				fatal("new_network");
			strlcpy(n->net.psname, ps->name, sizeof(n->net.psname));
			filterset_move($4, &n->net.attrset);
			n->net.type = NETWORK_PREFIXSET;
			TAILQ_INSERT_TAIL(netconf, n, entry);
			free($3);
			free($4);
		}
		| NETWORK af RTLABEL STRING filter_set	{
			struct network	*n;

			if ((n = calloc(1, sizeof(struct network))) == NULL)
				fatal("new_network");
			if (afi2aid($2, SAFI_UNICAST, &n->net.prefix.aid) ==
			    -1) {
				yyerror("unknown address family");
				filterset_free($5);
				free($5);
				YYERROR;
			}
			n->net.type = NETWORK_RTLABEL;
			n->net.rtlabel = rtlabel_name2id($4);
			filterset_move($5, &n->net.attrset);
			free($5);

			TAILQ_INSERT_TAIL(netconf, n, entry);
		}
		| NETWORK af PRIORITY NUMBER filter_set	{
			struct network	*n;
			if (!kr_check_prio($4)) {
				yyerror("priority %lld out of range", $4);
				YYERROR;
			}

			if ((n = calloc(1, sizeof(struct network))) == NULL)
				fatal("new_network");
			if (afi2aid($2, SAFI_UNICAST, &n->net.prefix.aid) ==
			    -1) {
				yyerror("unknown address family");
				filterset_free($5);
				free($5);
				YYERROR;
			}
			n->net.type = NETWORK_PRIORITY;
			n->net.priority = $4;
			filterset_move($5, &n->net.attrset);
			free($5);

			TAILQ_INSERT_TAIL(netconf, n, entry);
		}
		| NETWORK af nettype filter_set	{
			struct network	*n;

			if ((n = calloc(1, sizeof(struct network))) == NULL)
				fatal("new_network");
			if (afi2aid($2, SAFI_UNICAST, &n->net.prefix.aid) ==
			    -1) {
				yyerror("unknown address family");
				filterset_free($4);
				free($4);
				YYERROR;
			}
			n->net.type = $3 ? NETWORK_STATIC : NETWORK_CONNECTED;
			filterset_move($4, &n->net.attrset);
			free($4);

			TAILQ_INSERT_TAIL(netconf, n, entry);
		}
		;

flowspec	: FLOWSPEC af {
			if ((curflow = calloc(1, sizeof(*curflow))) == NULL)
				fatal("new_flowspec");
			curflow->aid = $2;
		} flow_rules filter_set {
			struct flowspec_config *f;

			f = flow_to_flowspec(curflow);
			if (f == NULL) {
				yyerror("out of memory");
				free($5);
				flow_free(curflow);
				curflow = NULL;
				YYERROR;
			}
			filterset_move($5, &f->attrset);
			free($5);
			flow_free(curflow);
			curflow = NULL;

			if (RB_INSERT(flowspec_tree, &conf->flowspecs, f) !=
			    NULL) {
				yyerror("duplicate flowspec definition");
				flowspec_free(f);
				YYERROR;
			}
		}
		;

proto		: PROTO proto_item
		| PROTO '{' optnl proto_list optnl '}'
		;

proto_list	: proto_item				{
			curflow->type = FLOWSPEC_TYPE_PROTO;
			if (push_unary_numop(OP_EQ, $1) == -1)
				YYERROR;
		}
		| proto_list comma proto_item		{
			curflow->type = FLOWSPEC_TYPE_PROTO;
			if (push_unary_numop(OP_EQ, $3) == -1)
				YYERROR;
		}
		;

proto_item	: STRING				{
			struct protoent *p;

			p = getprotobyname($1);
			if (p == NULL) {
				yyerror("unknown protocol %s", $1);
				free($1);
				YYERROR;
			}
			$$ = p->p_proto;
			free($1);
		}
		| NUMBER				{
			if ($1 < 0 || $1 > 255) {
				yyerror("protocol outside range");
				YYERROR;
			}
			$$ = $1;
		}
		;

from		: FROM {
			curflow->type = FLOWSPEC_TYPE_SRC_PORT;
			curflow->addr_type = FLOWSPEC_TYPE_SOURCE;
		} ipportspec
		;

to		: TO {
			curflow->type = FLOWSPEC_TYPE_DST_PORT;
			curflow->addr_type = FLOWSPEC_TYPE_DEST;
		} ipportspec
		;

ipportspec	: ipspec
		| ipspec PORT portspec
		| PORT portspec
		;

ipspec		: ANY
		| prefix			{
			if (push_prefix(&$1.prefix, $1.len) == -1)
				YYERROR;
		}
		;

portspec	: port_item
		| '{' optnl port_list optnl '}'
		;

port_list	: port_item
		| port_list comma port_item
		;

port_item	: port				{
			if (push_unary_numop(OP_EQ, $1) == -1)
				YYERROR;
		}
		| unaryop port			{
			if (push_unary_numop($1, $2) == -1)
				YYERROR;
		}
		| port binaryop port		{
			if (push_binary_numop($2, $1, $3))
				YYERROR;
		}
		;

port		: NUMBER			{
			if ($1 < 1 || $1 > USHRT_MAX) {
				yyerror("port must be between %u and %u",
				    1, USHRT_MAX);
				YYERROR;
			}
			$$ = $1;
		}
		| STRING			{
			if (($$ = getservice($1)) == -1) {
				yyerror("unknown port '%s'", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

flow_rules	: /* empty */
		| flow_rules_l
		;

flow_rules_l	: flowrule
		| flow_rules_l flowrule
		;

flowrule	: from
		| to
		| FLAGS {
			curflow->type = FLOWSPEC_TYPE_TCP_FLAGS;
		} flags
		| FRAGMENT {
			curflow->type = FLOWSPEC_TYPE_FRAG;
		} flags;
		| icmpspec
		| LENGTH lengthspec {
			curflow->type = FLOWSPEC_TYPE_PKT_LEN;
		}
		| proto
		| TOS tos {
			curflow->type = FLOWSPEC_TYPE_DSCP;
			if (push_unary_numop(OP_EQ, $2 >> 2) == -1)
				YYERROR;
		}
		;

flags		: flag '/' flag			{
			if (($1 & $3) != $1) {
				yyerror("bad flag combination, "
				    "check bit not in mask");
				YYERROR;
			}
			if (push_binop(FLOWSPEC_OP_BIT_MATCH, $1) == -1)
				YYERROR;
			/* check if extra mask op is needed */
			if ($3 & ~$1) {
				if (push_binop(FLOWSPEC_OP_BIT_NOT |
				    FLOWSPEC_OP_AND, $3 & ~$1) == -1)
					YYERROR;
			}
		}
		| '/' flag			{
			if (push_binop(FLOWSPEC_OP_BIT_NOT, $2) == -1)
				YYERROR;
		}
		| flag				{
			if (push_binop(0, $1) == -1)
				YYERROR;
		}
		| ANY		/* nothing */
		;

flag		: STRING {
			if (($$ = parse_flags($1)) < 0) {
				yyerror("bad flags %s", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

icmpspec	: ICMPTYPE icmp_item
		| ICMPTYPE '{' optnl icmp_list optnl '}'
		;

icmp_list	: icmp_item
		| icmp_list comma icmp_item
		;

icmp_item	: icmptype			{
			curflow->type = FLOWSPEC_TYPE_ICMP_TYPE;
			if (push_unary_numop(OP_EQ, $1) == -1)
				YYERROR;
		}
		| icmptype CODE STRING {
			int code;

			if ((code = geticmpcodebyname($1, $3, curflow->aid)) ==
			    -1) {
				yyerror("unknown icmp-code %s", $3);
				free($3);
				YYERROR;
			}
			free($3);

			curflow->type = FLOWSPEC_TYPE_ICMP_TYPE;
			if (push_unary_numop(OP_EQ, $1) == -1)
				YYERROR;
			curflow->type = FLOWSPEC_TYPE_ICMP_CODE;
			if (push_unary_numop(OP_EQ, code) == -1)
				YYERROR;
		}
		| icmptype CODE NUMBER {
			if ($3 < 0 || $3 > 255) {
				yyerror("illegal icmp-code %lld", $3);
				YYERROR;
			}
			curflow->type = FLOWSPEC_TYPE_ICMP_TYPE;
			if (push_unary_numop(OP_EQ, $1) == -1)
				YYERROR;
			curflow->type = FLOWSPEC_TYPE_ICMP_CODE;
			if (push_unary_numop(OP_EQ, $3) == -1)
				YYERROR;
		}
		;

icmptype	: STRING {
			int type;

			if ((type = geticmptypebyname($1, curflow->aid)) ==
			    -1) {
				yyerror("unknown icmp-type %s", $1);
				free($1);
				YYERROR;
			}
			$$ = type;
			free($1);
		}
		| NUMBER {
			if ($1 < 0 || $1 > 255) {
				yyerror("illegal icmp-type %lld", $1);
				YYERROR;
			}
			$$ = $1;
		}
		;

tos		: STRING		{
			int val;
			char *end;

			if (map_tos($1, &val))
				$$ = val;
			else if ($1[0] == '0' && $1[1] == 'x') {
				errno = 0;
				$$ = strtoul($1, &end, 16);
				if (errno || *end != '\0')
					$$ = 256;
			} else
				$$ = 256;
			if ($$ < 0 || $$ > 255) {
				yyerror("illegal tos value %s", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		| NUMBER		{
			if ($$ < 0 || $$ > 255) {
				yyerror("illegal tos value %lld", $1);
				YYERROR;
			}
			$$ = $1;
		}
		;

lengthspec	: length_item
		| '{' optnl length_list optnl '}'
		;

length_list	: length_item
		| length_list comma length_item
		;

length_item	: length			{
			if (push_unary_numop(OP_EQ, $1) == -1)
				YYERROR;
		}
		| unaryop length		{
			if (push_unary_numop($1, $2) == -1)
				YYERROR;
		}
		| length binaryop length	{
			if (push_binary_numop($2, $1, $3) == -1)
				YYERROR;
		}
		;

length		: NUMBER			{
			if ($$ < 0 || $$ > USHRT_MAX) {
				yyerror("illegal ptk length value %lld", $1);
				YYERROR;
			}
			$$ = $1;
		}

inout		: IN		{ $$ = 1; }
		| OUT		{ $$ = 0; }
		;

restricted	: /* empty */	{ $$ = 0; }
		| RESTRICTED	{ $$ = 1; }
		;

address		: STRING		{
			uint8_t	len;

			if (!host($1, &$$, &len)) {
				yyerror("could not parse address spec \"%s\"",
				    $1);
				free($1);
				YYERROR;
			}
			free($1);

			if (($$.aid == AID_INET && len != 32) ||
			    ($$.aid == AID_INET6 && len != 128)) {
				/* unreachable */
				yyerror("got prefixlen %u, expected %u",
				    len, $$.aid == AID_INET ? 32 : 128);
				YYERROR;
			}
		}
		;

prefix		: STRING '/' NUMBER	{
			char	*s;
			if ($3 < 0 || $3 > 128) {
				yyerror("bad prefixlen %lld", $3);
				free($1);
				YYERROR;
			}
			if (asprintf(&s, "%s/%lld", $1, $3) == -1)
				fatal(NULL);
			free($1);

			if (!host(s, &$$.prefix, &$$.len)) {
				yyerror("could not parse address \"%s\"", s);
				free(s);
				YYERROR;
			}
			free(s);
		}
		| NUMBER '/' NUMBER	{
			char	*s;

			/* does not match IPv6 */
			if ($1 < 0 || $1 > 255 || $3 < 0 || $3 > 32) {
				yyerror("bad prefix %lld/%lld", $1, $3);
				YYERROR;
			}
			if (asprintf(&s, "%lld/%lld", $1, $3) == -1)
				fatal(NULL);

			if (!host(s, &$$.prefix, &$$.len)) {
				yyerror("could not parse address \"%s\"", s);
				free(s);
				YYERROR;
			}
			free(s);
		}
		;

addrspec	: address	{
			memcpy(&$$.prefix, &$1, sizeof(struct bgpd_addr));
			if ($$.prefix.aid == AID_INET)
				$$.len = 32;
			else
				$$.len = 128;
		}
		| prefix
		;

optnumber	: /* empty */		{ $$ = 0; }
		| NUMBER
		;

l3vpn		: VPN STRING ON STRING			{
			u_int rdomain, label;

			if (get_mpe_config($4, &rdomain, &label) == -1) {
				if ((cmd_opts & BGPD_OPT_NOACTION) == 0) {
					yyerror("troubles getting config of %s",
					    $4);
					free($4);
					free($2);
					YYERROR;
				}
			}

			if (!(curvpn = calloc(1, sizeof(struct l3vpn))))
				fatal(NULL);
			strlcpy(curvpn->ifmpe, $4, IFNAMSIZ);

			if (strlcpy(curvpn->descr, $2,
			    sizeof(curvpn->descr)) >=
			    sizeof(curvpn->descr)) {
				yyerror("descr \"%s\" too long: max %zu",
				    $2, sizeof(curvpn->descr) - 1);
				free($2);
				free($4);
				free(curvpn);
				curvpn = NULL;
				YYERROR;
			}
			free($2);
			free($4);

			TAILQ_INIT(&curvpn->import);
			TAILQ_INIT(&curvpn->export);
			TAILQ_INIT(&curvpn->net_l);
			curvpn->label = label;
			curvpn->rtableid = rdomain;
			netconf = &curvpn->net_l;
		} '{' l3vpnopts_l '}'	{
			/* insert into list */
			SIMPLEQ_INSERT_TAIL(&conf->l3vpns, curvpn, entry);
			curvpn = NULL;
			netconf = &conf->networks;
		}
		;

l3vpnopts_l	: /* empty */
		| l3vpnopts_l '\n'
		| l3vpnopts_l l3vpnopts '\n'
		| l3vpnopts_l error '\n'
		;

l3vpnopts	: RD STRING {
			struct community	ext;

			memset(&ext, 0, sizeof(ext));
			if (parseextcommunity(&ext, "rt", $2) == -1) {
				free($2);
				YYERROR;
			}
			free($2);
			/*
			 * RD is almost encoded like an ext-community,
			 * but only almost so convert here.
			 */
			if (community_to_rd(&ext, &curvpn->rd) == -1) {
				yyerror("bad encoding of rd");
				YYERROR;
			}
		}
		| EXPORTTRGT STRING STRING	{
			struct filter_set	*set;

			if ((set = calloc(1, sizeof(struct filter_set))) ==
			    NULL)
				fatal(NULL);
			set->type = ACTION_SET_COMMUNITY;
			if (parseextcommunity(&set->action.community,
			    $2, $3) == -1) {
				free($3);
				free($2);
				free(set);
				YYERROR;
			}
			free($3);
			free($2);
			TAILQ_INSERT_TAIL(&curvpn->export, set, entry);
		}
		| IMPORTTRGT STRING STRING	{
			struct filter_set	*set;

			if ((set = calloc(1, sizeof(struct filter_set))) ==
			    NULL)
				fatal(NULL);
			set->type = ACTION_SET_COMMUNITY;
			if (parseextcommunity(&set->action.community,
			    $2, $3) == -1) {
				free($3);
				free($2);
				free(set);
				YYERROR;
			}
			free($3);
			free($2);
			TAILQ_INSERT_TAIL(&curvpn->import, set, entry);
		}
		| FIBUPDATE yesno		{
			if ($2 == 0)
				curvpn->flags |= F_RIB_NOFIBSYNC;
			else
				curvpn->flags &= ~F_RIB_NOFIBSYNC;
		}
		| network
		;

neighbor	: { curpeer = new_peer(); }
		    NEIGHBOR addrspec {
			memcpy(&curpeer->conf.remote_addr, &$3.prefix,
			    sizeof(curpeer->conf.remote_addr));
			curpeer->conf.remote_masklen = $3.len;
			if (($3.prefix.aid == AID_INET && $3.len != 32) ||
			    ($3.prefix.aid == AID_INET6 && $3.len != 128))
				curpeer->conf.template = 1;
			if (get_id(curpeer)) {
				yyerror("get_id failed");
				YYERROR;
			}
		}
		    peeropts_h {
			uint8_t		aid;

			if (curpeer_filter[0] != NULL)
				TAILQ_INSERT_TAIL(peerfilter_l,
				    curpeer_filter[0], entry);
			if (curpeer_filter[1] != NULL)
				TAILQ_INSERT_TAIL(peerfilter_l,
				    curpeer_filter[1], entry);
			curpeer_filter[0] = NULL;
			curpeer_filter[1] = NULL;

			/*
			 * Check if any MP capa is set, if none is set and
			 * and the default AID was not disabled via none then
			 * enable it. Finally fixup the disabled AID.
			 */
			for (aid = AID_MIN; aid < AID_MAX; aid++) {
				if (curpeer->conf.capabilities.mp[aid] > 0)
					break;
			}
			if (aid == AID_MAX &&
			    curpeer->conf.capabilities.mp[
			    curpeer->conf.remote_addr.aid] != -1)
				curpeer->conf.capabilities.mp[
				    curpeer->conf.remote_addr.aid] = 1;
			for (aid = AID_MIN; aid < AID_MAX; aid++) {
				if (curpeer->conf.capabilities.mp[aid] == -1)
					curpeer->conf.capabilities.mp[aid] = 0;
			}

			if (neighbor_consistent(curpeer) == -1) {
				free(curpeer);
				YYERROR;
			}
			if (RB_INSERT(peer_head, new_peers, curpeer) != NULL)
				fatalx("%s: peer tree is corrupt", __func__);
			curpeer = curgroup;
		}
		;

group		: GROUP string			{
			curgroup = curpeer = new_group();
			if (strlcpy(curgroup->conf.group, $2,
			    sizeof(curgroup->conf.group)) >=
			    sizeof(curgroup->conf.group)) {
				yyerror("group name \"%s\" too long: max %zu",
				    $2, sizeof(curgroup->conf.group) - 1);
				free($2);
				free(curgroup);
				YYERROR;
			}
			free($2);
			if (get_id(curgroup)) {
				yyerror("get_id failed");
				free(curgroup);
				YYERROR;
			}
		} '{' groupopts_l '}'		{
			if (curgroup_filter[0] != NULL)
				TAILQ_INSERT_TAIL(groupfilter_l,
				    curgroup_filter[0], entry);
			if (curgroup_filter[1] != NULL)
				TAILQ_INSERT_TAIL(groupfilter_l,
				    curgroup_filter[1], entry);
			curgroup_filter[0] = NULL;
			curgroup_filter[1] = NULL;

			free(curgroup);
			curgroup = NULL;
		}
		;

groupopts_l	: /* empty */
		| groupopts_l '\n'
		| groupopts_l peeropts '\n'
		| groupopts_l neighbor '\n'
		| groupopts_l error '\n'
		;

addpathextra	: /* empty */		{ $$ = 0; }
		| PLUS NUMBER		{
			if ($2 < 1 || $2 > USHRT_MAX) {
				yyerror("additional paths must be between "
				    "%u and %u", 1, USHRT_MAX);
				YYERROR;
			}
			$$ = $2;
		}
		;

addpathmax	: /* empty */		{ $$ = 0; }
		| MAX NUMBER		{
			if ($2 < 1 || $2 > USHRT_MAX) {
				yyerror("maximum additional paths must be "
				    "between %u and %u", 1, USHRT_MAX);
				YYERROR;
			}
			$$ = $2;
		}
		;

peeropts_h	: '{' '\n' peeropts_l '}'
		| '{' peeropts '}'
		| /* empty */
		;

peeropts_l	: /* empty */
		| peeropts_l '\n'
		| peeropts_l peeropts '\n'
		| peeropts_l error '\n'
		;

peeropts	: REMOTEAS as4number	{
			curpeer->conf.remote_as = $2;
		}
		| LOCALAS as4number	{
			curpeer->conf.local_as = $2;
			if ($2 > USHRT_MAX)
				curpeer->conf.local_short_as = AS_TRANS;
			else
				curpeer->conf.local_short_as = $2;
		}
		| LOCALAS as4number asnumber {
			curpeer->conf.local_as = $2;
			curpeer->conf.local_short_as = $3;
		}
		| DESCR string		{
			if (strlcpy(curpeer->conf.descr, $2,
			    sizeof(curpeer->conf.descr)) >=
			    sizeof(curpeer->conf.descr)) {
				yyerror("descr \"%s\" too long: max %zu",
				    $2, sizeof(curpeer->conf.descr) - 1);
				free($2);
				YYERROR;
			}
			free($2);
		}
		| LOCALADDR address	{
			if ($2.aid == AID_INET)
				memcpy(&curpeer->conf.local_addr_v4, &$2,
				    sizeof(curpeer->conf.local_addr_v4));
			else if ($2.aid == AID_INET6)
				memcpy(&curpeer->conf.local_addr_v6, &$2,
				    sizeof(curpeer->conf.local_addr_v6));
			else {
				yyerror("Unsupported address family %s for "
				    "local-addr", aid2str($2.aid));
				YYERROR;
			}
		}
		| yesno LOCALADDR	{
			if ($1) {
				yyerror("bad local-address definition");
				YYERROR;
			}
			memset(&curpeer->conf.local_addr_v4, 0,
			    sizeof(curpeer->conf.local_addr_v4));
			memset(&curpeer->conf.local_addr_v6, 0,
			    sizeof(curpeer->conf.local_addr_v6));
		}
		| MULTIHOP NUMBER	{
			if ($2 < 2 || $2 > 255) {
				yyerror("invalid multihop distance %lld", $2);
				YYERROR;
			}
			curpeer->conf.distance = $2;
		}
		| PASSIVE		{
			curpeer->conf.passive = 1;
		}
		| DOWN			{
			curpeer->conf.down = 1;
		}
		| DOWN STRING		{
			curpeer->conf.down = 1;
			if (strlcpy(curpeer->conf.reason, $2,
				sizeof(curpeer->conf.reason)) >=
				sizeof(curpeer->conf.reason)) {
				    yyerror("shutdown reason too long");
				    free($2);
				    YYERROR;
			}
			free($2);
		}
		| RIB STRING	{
			if (!find_rib($2)) {
				yyerror("rib \"%s\" does not exist.", $2);
				free($2);
				YYERROR;
			}
			if (strlcpy(curpeer->conf.rib, $2,
			    sizeof(curpeer->conf.rib)) >=
			    sizeof(curpeer->conf.rib)) {
				yyerror("rib name \"%s\" too long: max %zu",
				    $2, sizeof(curpeer->conf.rib) - 1);
				free($2);
				YYERROR;
			}
			free($2);
		}
		| HOLDTIME NUMBER	{
			if ($2 < MIN_HOLDTIME || $2 > USHRT_MAX) {
				yyerror("holdtime must be between %u and %u",
				    MIN_HOLDTIME, USHRT_MAX);
				YYERROR;
			}
			curpeer->conf.holdtime = $2;
		}
		| HOLDTIME YMIN NUMBER	{
			if ($3 < MIN_HOLDTIME || $3 > USHRT_MAX) {
				yyerror("holdtime must be between %u and %u",
				    MIN_HOLDTIME, USHRT_MAX);
				YYERROR;
			}
			curpeer->conf.min_holdtime = $3;
		}
		| STALETIME NUMBER	{
			if ($2 < MIN_HOLDTIME || $2 > USHRT_MAX) {
				yyerror("staletime must be between %u and %u",
				    MIN_HOLDTIME, USHRT_MAX);
				YYERROR;
			}
			curpeer->conf.staletime = $2;
		}
		| ANNOUNCE af safi enforce {
			uint8_t		aid, safi;
			uint16_t	afi;

			if ($3 == SAFI_NONE) {
				for (aid = AID_MIN; aid < AID_MAX; aid++) {
					if (aid2afi(aid, &afi, &safi) == -1 ||
					    afi != $2)
						continue;
					curpeer->conf.capabilities.mp[aid] = -1;
				}
			} else {
				if (afi2aid($2, $3, &aid) == -1) {
					yyerror("unknown AFI/SAFI pair");
					YYERROR;
				}
				if ($4)
					curpeer->conf.capabilities.mp[aid] = 2;
				else
					curpeer->conf.capabilities.mp[aid] = 1;
			}
		}
		| ANNOUNCE EVPN enforce {
			if ($3)
				curpeer->conf.capabilities.mp[AID_EVPN] = 2;
			else
				curpeer->conf.capabilities.mp[AID_EVPN] = 1;
		}
		| ANNOUNCE REFRESH yesnoenforce {
			curpeer->conf.capabilities.refresh = $3;
		}
		| ANNOUNCE ENHANCED REFRESH yesnoenforce {
			curpeer->conf.capabilities.enhanced_rr = $4;
		}
		| ANNOUNCE RESTART yesnoenforce {
			curpeer->conf.capabilities.grestart.restart = $3;
		}
		| ANNOUNCE GRACEFUL NOTIFICATION yesno {
			curpeer->conf.capabilities.grestart.grnotification = $4;
		}
		| ANNOUNCE AS4BYTE yesnoenforce {
			curpeer->conf.capabilities.as4byte = $3;
		}
		| ANNOUNCE ADDPATH RECV yesnoenforce {
			int8_t *ap = curpeer->conf.capabilities.add_path;
			uint8_t i;

			for (i = AID_MIN; i < AID_MAX; i++) {
				if ($4) {
					if ($4 == 2)
						ap[i] |= CAPA_AP_RECV_ENFORCE;
					ap[i] |= CAPA_AP_RECV;
				} else
					ap[i] &= ~CAPA_AP_RECV;
			}
		}
		| ANNOUNCE ADDPATH SEND STRING addpathextra addpathmax enforce {
			int8_t *ap = curpeer->conf.capabilities.add_path;
			enum addpath_mode mode;
			u_int8_t i;

			if (!strcmp($4, "no")) {
				free($4);
				if ($5 != 0 || $6 != 0 || $7 != 0) {
					yyerror("no additional option allowed "
					    "for 'add-path send no'");
					YYERROR;
				}
				mode = ADDPATH_EVAL_NONE;
			} else if (!strcmp($4, "all")) {
				free($4);
				if ($5 != 0 || $6 != 0) {
					yyerror("no additional option allowed "
					    "for 'add-path send all'");
					YYERROR;
				}
				mode = ADDPATH_EVAL_ALL;
			} else if (!strcmp($4, "best")) {
				free($4);
				mode = ADDPATH_EVAL_BEST;
			} else if (!strcmp($4, "ecmp")) {
				free($4);
				mode = ADDPATH_EVAL_ECMP;
			} else if (!strcmp($4, "as-wide-best")) {
				free($4);
				mode = ADDPATH_EVAL_AS_WIDE;
			} else {
				yyerror("announce add-path send: "
				    "unknown mode \"%s\"", $4);
				free($4);
				YYERROR;
			}
			for (i = AID_MIN; i < AID_MAX; i++) {
				if (mode != ADDPATH_EVAL_NONE) {
					if ($7)
						ap[i] |= CAPA_AP_SEND_ENFORCE;
					ap[i] |= CAPA_AP_SEND;
				} else
					ap[i] &= ~CAPA_AP_SEND;
			}
			curpeer->conf.eval.mode = mode;
			curpeer->conf.eval.extrapaths = $5;
			curpeer->conf.eval.maxpaths = $6;
		}
		| ANNOUNCE POLICY yesnoenforce {
			curpeer->conf.capabilities.policy = $3;
		}
		| ANNOUNCE EXTENDED MESSAGE yesnoenforce {
			curpeer->conf.capabilities.ext_msg = $4;
		}
		| ANNOUNCE EXTENDED NEXTHOP yesnoenforce {
			curpeer->conf.capabilities.ext_nh[AID_VPN_IPv4] =
			    curpeer->conf.capabilities.ext_nh[AID_INET] = $4;
		}
		| ROLE STRING {
			if (strcmp($2, "provider") == 0) {
				curpeer->conf.role = ROLE_PROVIDER;
			} else if (strcmp($2, "rs") == 0) {
				curpeer->conf.role = ROLE_RS;
			} else if (strcmp($2, "rs-client") == 0) {
				curpeer->conf.role = ROLE_RS_CLIENT;
			} else if (strcmp($2, "customer") == 0) {
				curpeer->conf.role = ROLE_CUSTOMER;
			} else if (strcmp($2, "peer") == 0) {
				curpeer->conf.role = ROLE_PEER;
			} else {
				yyerror("syntax error, one of none, provider, "
				    "rs, rs-client, customer, peer expected");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| ROLE NONE {
			curpeer->conf.role = ROLE_NONE;
		}
		| EXPORT NONE {
			curpeer->conf.export_type = EXPORT_NONE;
		}
		| EXPORT DEFAULTROUTE {
			curpeer->conf.export_type = EXPORT_DEFAULT_ROUTE;
		}
		| ENFORCE NEIGHBORAS yesno {
			if ($3)
				curpeer->conf.enforce_as = ENFORCE_AS_ON;
			else
				curpeer->conf.enforce_as = ENFORCE_AS_OFF;
		}
		| ENFORCE LOCALAS yesno {
			if ($3)
				curpeer->conf.enforce_local_as = ENFORCE_AS_ON;
			else
				curpeer->conf.enforce_local_as = ENFORCE_AS_OFF;
		}
		| ASOVERRIDE yesno {
			if ($2) {
				struct filter_rule	*r;
				struct filter_set	*s;

				if ((s = calloc(1, sizeof(struct filter_set)))
				    == NULL)
					fatal(NULL);
				s->type = ACTION_SET_AS_OVERRIDE;

				r = get_rule(s->type);
				if (merge_filterset(&r->set, s) == -1)
					YYERROR;
			}
		}
		| MAXPREFIX NUMBER restart {
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("bad maximum number of prefixes");
				YYERROR;
			}
			curpeer->conf.max_prefix = $2;
			curpeer->conf.max_prefix_restart = $3;
		}
		| MAXPREFIX NUMBER OUT restart {
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("bad maximum number of prefixes");
				YYERROR;
			}
			curpeer->conf.max_out_prefix = $2;
			curpeer->conf.max_out_prefix_restart = $4;
		}
		| authconf {
			if (merge_auth_conf(&curpeer->auth_conf, &$1) == 0)
				YYERROR;
		}
		| TTLSECURITY yesno	{
			curpeer->conf.ttlsec = $2;
		}
		| SET filter_set_opt	{
			struct filter_rule	*r;

			r = get_rule($2->type);
			if (merge_filterset(&r->set, $2) == -1)
				YYERROR;
		}
		| SET '{' optnl filter_set_l optnl '}'	{
			struct filter_rule	*r;
			struct filter_set	*s;

			while ((s = TAILQ_FIRST($4)) != NULL) {
				TAILQ_REMOVE($4, s, entry);
				r = get_rule(s->type);
				if (merge_filterset(&r->set, s) == -1)
					YYERROR;
			}
			free($4);
		}
		| mrtdump
		| REFLECTOR		{
			if ((conf->flags & BGPD_FLAG_REFLECTOR) &&
			    conf->clusterid != 0) {
				yyerror("only one route reflector "
				    "cluster allowed");
				YYERROR;
			}
			conf->flags |= BGPD_FLAG_REFLECTOR;
			curpeer->conf.reflector_client = 1;
		}
		| REFLECTOR address	{
			if ($2.aid != AID_INET) {
				yyerror("route reflector cluster-id must be "
				    "an IPv4 address");
				YYERROR;
			}
			if ((conf->flags & BGPD_FLAG_REFLECTOR) &&
			    conf->clusterid != ntohl($2.v4.s_addr)) {
				yyerror("only one route reflector "
				    "cluster allowed");
				YYERROR;
			}
			conf->flags |= BGPD_FLAG_REFLECTOR;
			curpeer->conf.reflector_client = 1;
			conf->clusterid = ntohl($2.v4.s_addr);
		}
		| DEPEND ON STRING	{
			if (strlcpy(curpeer->conf.if_depend, $3,
			    sizeof(curpeer->conf.if_depend)) >=
			    sizeof(curpeer->conf.if_depend)) {
				yyerror("interface name \"%s\" too long: "
				    "max %zu", $3,
				    sizeof(curpeer->conf.if_depend) - 1);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| DEMOTE STRING		{
			if (strlcpy(curpeer->conf.demote_group, $2,
			    sizeof(curpeer->conf.demote_group)) >=
			    sizeof(curpeer->conf.demote_group)) {
				yyerror("demote group name \"%s\" too long: "
				    "max %zu", $2,
				    sizeof(curpeer->conf.demote_group) - 1);
				free($2);
				YYERROR;
			}
			free($2);
			if (carp_demote_init(curpeer->conf.demote_group,
			    cmd_opts & BGPD_OPT_FORCE_DEMOTE) == -1) {
				yyerror("error initializing group \"%s\"",
				    curpeer->conf.demote_group);
				YYERROR;
			}
		}
		| TRANSPARENT yesno	{
			if ($2 == 1)
				curpeer->conf.flags |= PEERFLAG_TRANS_AS;
			else
				curpeer->conf.flags &= ~PEERFLAG_TRANS_AS;
		}
		| LOG STRING		{
			if (!strcmp($2, "updates"))
				curpeer->conf.flags |= PEERFLAG_LOG_UPDATES;
			else if (!strcmp($2, "no"))
				curpeer->conf.flags &= ~PEERFLAG_LOG_UPDATES;
			else {
				free($2);
				YYERROR;
			}
			free($2);
		}
		| REJECT ASSET yesno	{
			if ($3 == 1)
				curpeer->conf.flags &= ~PEERFLAG_PERMIT_AS_SET;
			else
				curpeer->conf.flags |= PEERFLAG_PERMIT_AS_SET;
		}
		| PORT port {
			curpeer->conf.remote_port = $2;
		}
		| RDE EVALUATE STRING {
			if (!strcmp($3, "all"))
				curpeer->conf.flags |= PEERFLAG_EVALUATE_ALL;
			else if (!strcmp($3, "default"))
				curpeer->conf.flags &= ~PEERFLAG_EVALUATE_ALL;
			else {
				yyerror("rde evaluate: "
				    "unknown setting \"%s\"", $3);
				free($3);
				YYERROR;
			}
			free($3);
		}
		;

restart		: /* nada */		{ $$ = 0; }
		| RESTART NUMBER	{
			if ($2 < 1 || $2 > USHRT_MAX) {
				yyerror("restart out of range. 1 to %u minutes",
				    USHRT_MAX);
				YYERROR;
			}
			$$ = $2;
		}
		;

af		: IPV4	{ $$ = AFI_IPv4; }
		| IPV6	{ $$ = AFI_IPv6; }
		;

safi		: NONE		{ $$ = SAFI_NONE; }
		| UNICAST	{ $$ = SAFI_UNICAST; }
		| VPN		{ $$ = SAFI_MPLSVPN; }
		| FLOWSPEC	{ $$ = SAFI_FLOWSPEC; }
		;

nettype		: STATIC	{ $$ = 1; }
		| CONNECTED	{ $$ = 0; }
		;

authconf	: TCP MD5SIG PASSWORD string {
			memset(&$$, 0, sizeof($$));
			if (strlcpy($$.md5key, $4, sizeof($$.md5key)) >=
			    sizeof($$.md5key)) {
				yyerror("tcp md5sig password too long: max %zu",
				    sizeof($$.md5key) - 1);
				free($4);
				YYERROR;
			}
			$$.method = AUTH_MD5SIG;
			$$.md5key_len = strlen($4);
			free($4);
		}
		| TCP MD5SIG KEY string {
			memset(&$$, 0, sizeof($$));
			if (str2key($4, $$.md5key, sizeof($$.md5key)) == -1) {
				free($4);
				YYERROR;
			}
			$$.method = AUTH_MD5SIG;
			$$.md5key_len = strlen($4) / 2;
			free($4);
		}
		| IPSEC espah IKE {
			memset(&$$, 0, sizeof($$));
			if ($2)
				$$.method = AUTH_IPSEC_IKE_ESP;
			else
				$$.method = AUTH_IPSEC_IKE_AH;
		}
		| IPSEC espah inout SPI NUMBER STRING STRING encspec {
			enum auth_alg	auth_alg;
			uint8_t		keylen;

			memset(&$$, 0, sizeof($$));
			if (!strcmp($6, "sha1")) {
				auth_alg = AUTH_AALG_SHA1HMAC;
				keylen = 20;
			} else if (!strcmp($6, "md5")) {
				auth_alg = AUTH_AALG_MD5HMAC;
				keylen = 16;
			} else {
				yyerror("unknown auth algorithm \"%s\"", $6);
				free($6);
				free($7);
				YYERROR;
			}
			free($6);

			if (strlen($7) / 2 != keylen) {
				yyerror("auth key len: must be %u bytes, "
				    "is %zu bytes", keylen, strlen($7) / 2);
				free($7);
				YYERROR;
			}

			if ($2)
				$$.method = AUTH_IPSEC_MANUAL_ESP;
			else {
				if ($8.enc_alg) {
					yyerror("\"ipsec ah\" doesn't take "
					    "encryption keys");
					free($7);
					YYERROR;
				}
				$$.method = AUTH_IPSEC_MANUAL_AH;
			}

			if ($5 <= SPI_RESERVED_MAX || $5 > UINT_MAX) {
				yyerror("bad spi number %lld", $5);
				free($7);
				YYERROR;
			}

			if ($3 == 1) {
				if (str2key($7, $$.auth_key_in,
				    sizeof($$.auth_key_in)) == -1) {
					free($7);
					YYERROR;
				}
				$$.spi_in = $5;
				$$.auth_alg_in = auth_alg;
				$$.enc_alg_in = $8.enc_alg;
				memcpy(&$$.enc_key_in, &$8.enc_key,
				    sizeof($$.enc_key_in));
				$$.enc_keylen_in = $8.enc_key_len;
				$$.auth_keylen_in = keylen;
			} else {
				if (str2key($7, $$.auth_key_out,
				    sizeof($$.auth_key_out)) == -1) {
					free($7);
					YYERROR;
				}
				$$.spi_out = $5;
				$$.auth_alg_out = auth_alg;
				$$.enc_alg_out = $8.enc_alg;
				memcpy(&$$.enc_key_out, &$8.enc_key,
				    sizeof($$.enc_key_out));
				$$.enc_keylen_out = $8.enc_key_len;
				$$.auth_keylen_out = keylen;
			}
			free($7);
		}
		;

espah		: ESP		{ $$ = 1; }
		| AH		{ $$ = 0; }
		;

encspec		: /* nada */	{
			memset(&$$, 0, sizeof($$));
		}
		| STRING STRING {
			memset(&$$, 0, sizeof($$));
			if (!strcmp($1, "3des") || !strcmp($1, "3des-cbc")) {
				$$.enc_alg = AUTH_EALG_3DESCBC;
				$$.enc_key_len = 21; /* XXX verify */
			} else if (!strcmp($1, "aes") ||
			    !strcmp($1, "aes-128-cbc")) {
				$$.enc_alg = AUTH_EALG_AES;
				$$.enc_key_len = 16;
			} else {
				yyerror("unknown enc algorithm \"%s\"", $1);
				free($1);
				free($2);
				YYERROR;
			}
			free($1);

			if (strlen($2) / 2 != $$.enc_key_len) {
				yyerror("enc key length wrong: should be %u "
				    "bytes, is %zu bytes",
				    $$.enc_key_len * 2, strlen($2));
				free($2);
				YYERROR;
			}

			if (str2key($2, $$.enc_key, sizeof($$.enc_key)) == -1) {
				free($2);
				YYERROR;
			}
			free($2);
		}
		;

filterrule	: action quick filter_rib_h direction filter_peer_h
				filter_match_h filter_set
		{
			struct filter_rule	 r;
			struct filter_rib_l	 *rb, *rbnext;

			memset(&r, 0, sizeof(r));
			r.action = $1;
			r.quick = $2;
			r.dir = $4;
			if ($3) {
				if (r.dir != DIR_IN) {
					yyerror("rib only allowed on \"from\" "
					    "rules.");

					for (rb = $3; rb != NULL; rb = rbnext) {
						rbnext = rb->next;
						free(rb);
					}
					YYERROR;
				}
			}
			if (expand_rule(&r, $3, $5, &$6, $7) == -1)
				YYERROR;
		}
		;

action		: ALLOW		{ $$ = ACTION_ALLOW; }
		| DENY		{ $$ = ACTION_DENY; }
		| MATCH		{ $$ = ACTION_NONE; }
		;

quick		: /* empty */	{ $$ = 0; }
		| QUICK		{ $$ = 1; }
		;

direction	: FROM		{ $$ = DIR_IN; }
		| TO		{ $$ = DIR_OUT; }
		;

filter_rib_h	: /* empty */			{ $$ = NULL; }
		| RIB filter_rib		{ $$ = $2; }
		| RIB '{' optnl filter_rib_l optnl '}'	{ $$ = $4; }

filter_rib_l	: filter_rib			{ $$ = $1; }
		| filter_rib_l comma filter_rib	{
			$3->next = $1;
			$$ = $3;
		}
		;

filter_rib	: STRING	{
			if (!find_rib($1)) {
				yyerror("rib \"%s\" does not exist.", $1);
				free($1);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_rib_l))) ==
			    NULL)
				fatal(NULL);
			$$->next = NULL;
			if (strlcpy($$->name, $1, sizeof($$->name)) >=
			    sizeof($$->name)) {
				yyerror("rib name \"%s\" too long: "
				    "max %zu", $1, sizeof($$->name) - 1);
				free($1);
				free($$);
				YYERROR;
			}
			free($1);
		}
		;

filter_peer_h	: filter_peer
		| '{' optnl filter_peer_l optnl '}'	{ $$ = $3; }
		;

filter_peer_l	: filter_peer				{ $$ = $1; }
		| filter_peer_l comma filter_peer	{
			$3->next = $1;
			$$ = $3;
		}
		;

filter_peer	: ANY		{
			if (($$ = calloc(1, sizeof(struct filter_peers_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.peerid = $$->p.groupid = 0;
			$$->next = NULL;
		}
		| address	{
			struct peer *p;

			if (($$ = calloc(1, sizeof(struct filter_peers_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.remote_as = $$->p.groupid = $$->p.peerid = 0;
			$$->next = NULL;
			RB_FOREACH(p, peer_head, new_peers)
				if (!memcmp(&p->conf.remote_addr,
				    &$1, sizeof(p->conf.remote_addr))) {
					$$->p.peerid = p->conf.id;
					break;
				}
			if ($$->p.peerid == 0) {
				yyerror("no such peer: %s", log_addr(&$1));
				free($$);
				YYERROR;
			}
		}
		| AS as4number	{
			if (($$ = calloc(1, sizeof(struct filter_peers_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.groupid = $$->p.peerid = 0;
			$$->p.remote_as = $2;
		}
		| GROUP STRING	{
			struct peer *p;

			if (($$ = calloc(1, sizeof(struct filter_peers_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.remote_as = $$->p.peerid = 0;
			$$->next = NULL;
			RB_FOREACH(p, peer_head, new_peers)
				if (!strcmp(p->conf.group, $2)) {
					$$->p.groupid = p->conf.groupid;
					break;
				}
			if ($$->p.groupid == 0) {
				yyerror("no such group: \"%s\"", $2);
				free($2);
				free($$);
				YYERROR;
			}
			free($2);
		}
		| EBGP {
			if (($$ = calloc(1, sizeof(struct filter_peers_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.ebgp = 1;
		}
		| IBGP {
			if (($$ = calloc(1, sizeof(struct filter_peers_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.ibgp = 1;
		}
		;

filter_prefix_h	: IPV4 prefixlenop			 {
			if ($2.op == OP_NONE) {
				$2.op = OP_RANGE;
				$2.len_min = 0;
				$2.len_max = -1;
			}
			if (($$ = calloc(1, sizeof(struct filter_prefix_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.addr.aid = AID_INET;
			if (merge_prefixspec(&$$->p, &$2) == -1) {
				free($$);
				YYERROR;
			}
		}
		| IPV6 prefixlenop			{
			if ($2.op == OP_NONE) {
				$2.op = OP_RANGE;
				$2.len_min = 0;
				$2.len_max = -1;
			}
			if (($$ = calloc(1, sizeof(struct filter_prefix_l))) ==
			    NULL)
				fatal(NULL);
			$$->p.addr.aid = AID_INET6;
			if (merge_prefixspec(&$$->p, &$2) == -1) {
				free($$);
				YYERROR;
			}
		}
		| PREFIX filter_prefix			{ $$ = $2; }
		| PREFIX '{' filter_prefix_m '}'	{ $$ = $3; }
		;

filter_prefix_m	: filter_prefix_l
		| '{' filter_prefix_l '}'		{ $$ = $2; }
		| '{' filter_prefix_l '}' filter_prefix_m
		{
			struct filter_prefix_l	*p;

			/* merge, both can be lists */
			for (p = $2; p != NULL && p->next != NULL; p = p->next)
				;	/* nothing */
			if (p != NULL)
				p->next = $4;
			$$ = $2;
		}

filter_prefix_l	: filter_prefix			{ $$ = $1; }
		| filter_prefix_l comma filter_prefix	{
			$3->next = $1;
			$$ = $3;
		}
		;

filter_prefix	: prefix prefixlenop			{
			if (($$ = calloc(1, sizeof(struct filter_prefix_l))) ==
			    NULL)
				fatal(NULL);
			memcpy(&$$->p.addr, &$1.prefix,
			    sizeof($$->p.addr));
			$$->p.len = $1.len;

			if (merge_prefixspec(&$$->p, &$2) == -1) {
				free($$);
				YYERROR;
			}
		}
		;

filter_as_h	: filter_as_t
		| '{' filter_as_t_l '}'		{ $$ = $2; }
		;

filter_as_t_l	: filter_as_t
		| filter_as_t_l comma filter_as_t		{
			struct filter_as_l	*a;

			/* merge, both can be lists */
			for (a = $1; a != NULL && a->next != NULL; a = a->next)
				;	/* nothing */
			if (a != NULL)
				a->next = $3;
			$$ = $1;
		}
		;

filter_as_t	: filter_as_type filter_as			{
			$$ = $2;
			$$->a.type = $1;
		}
		| filter_as_type '{' filter_as_l_h '}'	{
			struct filter_as_l	*a;

			$$ = $3;
			for (a = $$; a != NULL; a = a->next)
				a->a.type = $1;
		}
		| filter_as_type ASSET STRING {
			if (as_sets_lookup(&conf->as_sets, $3) == NULL) {
				yyerror("as-set \"%s\" not defined", $3);
				free($3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_as_l))) ==
			    NULL)
				fatal(NULL);
			$$->a.type = $1;
			$$->a.flags = AS_FLAG_AS_SET_NAME;
			if (strlcpy($$->a.name, $3, sizeof($$->a.name)) >=
			    sizeof($$->a.name)) {
				yyerror("as-set name \"%s\" too long: "
				    "max %zu", $3, sizeof($$->a.name) - 1);
				free($3);
				free($$);
				YYERROR;
			}
			free($3);
		}
		;

filter_as_l_h	: filter_as_l
		| '{' filter_as_l '}'			{ $$ = $2; }
		| '{' filter_as_l '}' filter_as_l_h
		{
			struct filter_as_l	*a;

			/* merge, both can be lists */
			for (a = $2; a != NULL && a->next != NULL; a = a->next)
				;	/* nothing */
			if (a != NULL)
				a->next = $4;
			$$ = $2;
		}
		;

filter_as_l	: filter_as
		| filter_as_l comma filter_as	{
			$3->next = $1;
			$$ = $3;
		}
		;

filter_as	: as4number_any		{
			if (($$ = calloc(1, sizeof(struct filter_as_l))) ==
			    NULL)
				fatal(NULL);
			$$->a.as_min = $1;
			$$->a.as_max = $1;
			$$->a.op = OP_EQ;
		}
		| NEIGHBORAS		{
			if (($$ = calloc(1, sizeof(struct filter_as_l))) ==
			    NULL)
				fatal(NULL);
			$$->a.flags = AS_FLAG_NEIGHBORAS;
		}
		| equalityop as4number_any	{
			if (($$ = calloc(1, sizeof(struct filter_as_l))) ==
			    NULL)
				fatal(NULL);
			$$->a.op = $1;
			$$->a.as_min = $2;
			$$->a.as_max = $2;
		}
		| as4number_any binaryop as4number_any {
			if (($$ = calloc(1, sizeof(struct filter_as_l))) ==
			    NULL)
				fatal(NULL);
			if ($1 >= $3) {
				yyerror("start AS is bigger than end");
				YYERROR;
			}
			$$->a.op = $2;
			$$->a.as_min = $1;
			$$->a.as_max = $3;
		}
		;

filter_match_h	: /* empty */			{
			memset(&$$, 0, sizeof($$));
		}
		| {
			memset(&fmopts, 0, sizeof(fmopts));
		}
		    filter_match		{
			memcpy(&$$, &fmopts, sizeof($$));
		}
		;

filter_match	: filter_elm
		| filter_match filter_elm
		;

filter_elm	: filter_prefix_h	{
			if (fmopts.prefix_l != NULL) {
				yyerror("\"prefix\" already specified");
				YYERROR;
			}
			if (fmopts.m.prefixset.name[0] != '\0') {
				yyerror("\"prefix-set\" already specified, "
				    "cannot be used with \"prefix\" in the "
				    "same filter rule");
				YYERROR;
			}
			fmopts.prefix_l = $1;
		}
		| filter_as_h		{
			if (fmopts.as_l != NULL) {
				yyerror("AS filters already specified");
				YYERROR;
			}
			fmopts.as_l = $1;
		}
		| MAXASLEN NUMBER	{
			if (fmopts.m.aslen.type != ASLEN_NONE) {
				yyerror("AS length filters already specified");
				YYERROR;
			}
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("bad max-as-len %lld", $2);
				YYERROR;
			}
			fmopts.m.aslen.type = ASLEN_MAX;
			fmopts.m.aslen.aslen = $2;
		}
		| MAXASSEQ NUMBER	{
			if (fmopts.m.aslen.type != ASLEN_NONE) {
				yyerror("AS length filters already specified");
				YYERROR;
			}
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("bad max-as-seq %lld", $2);
				YYERROR;
			}
			fmopts.m.aslen.type = ASLEN_SEQ;
			fmopts.m.aslen.aslen = $2;
		}
		| community STRING	{
			int i;
			for (i = 0; i < MAX_COMM_MATCH; i++) {
				if (fmopts.m.community[i].flags == 0)
					break;
			}
			if (i >= MAX_COMM_MATCH) {
				yyerror("too many \"community\" filters "
				    "specified");
				free($2);
				YYERROR;
			}
			if (parsecommunity(&fmopts.m.community[i], $1, $2) ==
			    -1) {
				free($2);
				YYERROR;
			}
			free($2);
		}
		| EXTCOMMUNITY STRING STRING {
			int i;
			for (i = 0; i < MAX_COMM_MATCH; i++) {
				if (fmopts.m.community[i].flags == 0)
					break;
			}
			if (i >= MAX_COMM_MATCH) {
				yyerror("too many \"community\" filters "
				    "specified");
				free($2);
				free($3);
				YYERROR;
			}
			if (parseextcommunity(&fmopts.m.community[i],
			    $2, $3) == -1) {
				free($2);
				free($3);
				YYERROR;
			}
			free($2);
			free($3);
		}
		| EXTCOMMUNITY OVS STRING {
			int i;
			for (i = 0; i < MAX_COMM_MATCH; i++) {
				if (fmopts.m.community[i].flags == 0)
					break;
			}
			if (i >= MAX_COMM_MATCH) {
				yyerror("too many \"community\" filters "
				    "specified");
				free($3);
				YYERROR;
			}
			if (parseextcommunity(&fmopts.m.community[i],
			    "ovs", $3) == -1) {
				free($3);
				YYERROR;
			}
			free($3);
		}
		| MAXCOMMUNITIES NUMBER {
			if ($2 < 0 || $2 > INT16_MAX) {
				yyerror("bad max-comunities %lld", $2);
				YYERROR;
			}
			if (fmopts.m.maxcomm != 0) {
				yyerror("%s already specified",
				    "max-communities");
				YYERROR;
			}
			/*
			 * Offset by 1 since 0 means not used.
			 * The match function then uses >= to compensate.
			 */
			fmopts.m.maxcomm = $2 + 1;
		}
		| MAXEXTCOMMUNITIES NUMBER {
			if ($2 < 0 || $2 > INT16_MAX) {
				yyerror("bad max-ext-communities %lld", $2);
				YYERROR;
			}
			if (fmopts.m.maxextcomm != 0) {
				yyerror("%s already specified",
				    "max-ext-communities");
				YYERROR;
			}
			fmopts.m.maxextcomm = $2 + 1;
		}
		| MAXLARGECOMMUNITIES NUMBER {
			if ($2 < 0 || $2 > INT16_MAX) {
				yyerror("bad max-large-communities %lld", $2);
				YYERROR;
			}
			if (fmopts.m.maxlargecomm != 0) {
				yyerror("%s already specified",
				    "max-large-communities");
				YYERROR;
			}
			fmopts.m.maxlargecomm = $2 + 1;
		}
		| NEXTHOP address	{
			if (fmopts.m.nexthop.flags) {
				yyerror("nexthop already specified");
				YYERROR;
			}
			fmopts.m.nexthop.addr = $2;
			fmopts.m.nexthop.flags = FILTER_NEXTHOP_ADDR;
		}
		| NEXTHOP NEIGHBOR	{
			if (fmopts.m.nexthop.flags) {
				yyerror("nexthop already specified");
				YYERROR;
			}
			fmopts.m.nexthop.flags = FILTER_NEXTHOP_NEIGHBOR;
		}
		| PREFIXSET STRING prefixlenop {
			struct prefixset *ps;
			if (fmopts.prefix_l != NULL) {
				yyerror("\"prefix\" already specified, cannot "
				    "be used with \"prefix-set\" in the same "
				    "filter rule");
				free($2);
				YYERROR;
			}
			if (fmopts.m.prefixset.name[0] != '\0') {
				yyerror("prefix-set filter already specified");
				free($2);
				YYERROR;
			}
			if ((ps = find_prefixset($2, &conf->prefixsets))
			    == NULL) {
				yyerror("prefix-set '%s' not defined", $2);
				free($2);
				YYERROR;
			}
			if (strlcpy(fmopts.m.prefixset.name, $2,
			    sizeof(fmopts.m.prefixset.name)) >=
			    sizeof(fmopts.m.prefixset.name)) {
				yyerror("prefix-set name too long");
				free($2);
				YYERROR;
			}
			if (!($3.op == OP_NONE ||
			    ($3.op == OP_RANGE &&
			     $3.len_min == -1 && $3.len_max == -1))) {
				yyerror("prefix-sets can only use option "
				    "or-longer");
				free($2);
				YYERROR;
			}
			if ($3.op == OP_RANGE &&
			    ps->sflags & PREFIXSET_FLAG_OPS) {
				yyerror("prefix-set %s contains prefixlen "
				    "operators and cannot be used with an "
				    "or-longer filter", $2);
				free($2);
				YYERROR;
			}
			if ($3.op == OP_RANGE && $3.len_min == -1 &&
			    $3.len_min == -1)
				fmopts.m.prefixset.flags |=
				    PREFIXSET_FLAG_LONGER;
			fmopts.m.prefixset.flags |= PREFIXSET_FLAG_FILTER;
			free($2);
		}
		| ORIGINSET STRING {
			if (fmopts.m.originset.name[0] != '\0') {
				yyerror("origin-set filter already specified");
				free($2);
				YYERROR;
			}
			if (find_prefixset($2, &conf->originsets) == NULL) {
				yyerror("origin-set '%s' not defined", $2);
				free($2);
				YYERROR;
			}
			if (strlcpy(fmopts.m.originset.name, $2,
			    sizeof(fmopts.m.originset.name)) >=
			    sizeof(fmopts.m.originset.name)) {
				yyerror("origin-set name too long");
				free($2);
				YYERROR;
			}
			free($2);
		}
		| OVS validity		{
			if (fmopts.m.ovs.is_set) {
				yyerror("ovs filter already specified");
				YYERROR;
			}
			fmopts.m.ovs.validity = $2;
			fmopts.m.ovs.is_set = 1;
		}
		| AVS aspa_validity		{
			if (fmopts.m.avs.is_set) {
				yyerror("avs filter already specified");
				YYERROR;
			}
			fmopts.m.avs.validity = $2;
			fmopts.m.avs.is_set = 1;
		}
		;

prefixlenop	: /* empty */		{ memset(&$$, 0, sizeof($$)); }
		| LONGER				{
			memset(&$$, 0, sizeof($$));
			$$.op = OP_RANGE;
			$$.len_min = -1;
			$$.len_max = -1;
		}
		| MAXLEN NUMBER				{
			memset(&$$, 0, sizeof($$));
			if ($2 < 0 || $2 > 128) {
				yyerror("prefixlen must be >= 0 and <= 128");
				YYERROR;
			}

			$$.op = OP_RANGE;
			$$.len_min = -1;
			$$.len_max = $2;
		}
		| PREFIXLEN unaryop NUMBER		{
			int min, max;

			memset(&$$, 0, sizeof($$));
			if ($3 < 0 || $3 > 128) {
				yyerror("prefixlen must be >= 0 and <= 128");
				YYERROR;
			}
			/*
			 * convert the unary operation into the equivalent
			 * range check
			 */
			$$.op = OP_RANGE;

			switch ($2) {
			case OP_NE:
				$$.op = $2;
			case OP_EQ:
				min = max = $3;
				break;
			case OP_LT:
				if ($3 == 0) {
					yyerror("prefixlen must be > 0");
					YYERROR;
				}
				$3 -= 1;
			case OP_LE:
				min = -1;
				max = $3;
				break;
			case OP_GT:
				$3 += 1;
			case OP_GE:
				min = $3;
				max = -1;
				break;
			default:
				yyerror("unknown prefixlen operation");
				YYERROR;
			}
			$$.len_min = min;
			$$.len_max = max;
		}
		| PREFIXLEN NUMBER binaryop NUMBER	{
			memset(&$$, 0, sizeof($$));
			if ($2 < 0 || $2 > 128 || $4 < 0 || $4 > 128) {
				yyerror("prefixlen must be < 128");
				YYERROR;
			}
			if ($2 > $4) {
				yyerror("start prefixlen is bigger than end");
				YYERROR;
			}
			$$.op = $3;
			$$.len_min = $2;
			$$.len_max = $4;
		}
		;

filter_as_type	: AS		{ $$ = AS_ALL; }
		| SOURCEAS	{ $$ = AS_SOURCE; }
		| TRANSITAS	{ $$ = AS_TRANSIT; }
		| PEERAS	{ $$ = AS_PEER; }
		;

filter_set	: /* empty */	{ $$ = NULL; }
		| SET filter_set_opt	{
			if (($$ = calloc(1, sizeof(struct filter_set_head))) ==
			    NULL)
				fatal(NULL);
			TAILQ_INIT($$);
			TAILQ_INSERT_TAIL($$, $2, entry);
		}
		| SET '{' optnl filter_set_l optnl '}'	{ $$ = $4; }
		;

filter_set_l	: filter_set_l comma filter_set_opt	{
			$$ = $1;
			if (merge_filterset($$, $3) == 1)
				YYERROR;
		}
		| filter_set_opt {
			if (($$ = calloc(1, sizeof(struct filter_set_head))) ==
			    NULL)
				fatal(NULL);
			TAILQ_INIT($$);
			TAILQ_INSERT_TAIL($$, $1, entry);
		}
		;

community	: COMMUNITY		{ $$ = COMMUNITY_TYPE_BASIC; }
		| LARGECOMMUNITY	{ $$ = COMMUNITY_TYPE_LARGE; }
		;

delete		: /* empty */	{ $$ = 0; }
		| DELETE	{ $$ = 1; }
		;

enforce		: /* empty */	{ $$ = 0; }
		| ENFORCE	{ $$ = 2; }
		;

yesnoenforce	: yesno		{ $$ = $1; }
		| ENFORCE	{ $$ = 2; }
		;

filter_set_opt	: LOCALPREF NUMBER		{
			if ($2 < -INT_MAX || $2 > UINT_MAX) {
				yyerror("bad localpref %lld", $2);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if ($2 >= 0) {
				$$->type = ACTION_SET_LOCALPREF;
				$$->action.metric = $2;
			} else {
				$$->type = ACTION_SET_RELATIVE_LOCALPREF;
				$$->action.relative = $2;
			}
		}
		| LOCALPREF '+' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad localpref +%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_LOCALPREF;
			$$->action.relative = $3;
		}
		| LOCALPREF '-' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad localpref -%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_LOCALPREF;
			$$->action.relative = -$3;
		}
		| MED NUMBER			{
			if ($2 < -INT_MAX || $2 > UINT_MAX) {
				yyerror("bad metric %lld", $2);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if ($2 >= 0) {
				$$->type = ACTION_SET_MED;
				$$->action.metric = $2;
			} else {
				$$->type = ACTION_SET_RELATIVE_MED;
				$$->action.relative = $2;
			}
		}
		| MED '+' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad metric +%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_MED;
			$$->action.relative = $3;
		}
		| MED '-' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad metric -%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_MED;
			$$->action.relative = -$3;
		}
		| METRIC NUMBER			{	/* alias for MED */
			if ($2 < -INT_MAX || $2 > UINT_MAX) {
				yyerror("bad metric %lld", $2);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if ($2 >= 0) {
				$$->type = ACTION_SET_MED;
				$$->action.metric = $2;
			} else {
				$$->type = ACTION_SET_RELATIVE_MED;
				$$->action.relative = $2;
			}
		}
		| METRIC '+' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad metric +%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_MED;
			$$->action.metric = $3;
		}
		| METRIC '-' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad metric -%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_MED;
			$$->action.relative = -$3;
		}
		| WEIGHT NUMBER			{
			if ($2 < -INT_MAX || $2 > UINT_MAX) {
				yyerror("bad weight %lld", $2);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if ($2 > 0) {
				$$->type = ACTION_SET_WEIGHT;
				$$->action.metric = $2;
			} else {
				$$->type = ACTION_SET_RELATIVE_WEIGHT;
				$$->action.relative = $2;
			}
		}
		| WEIGHT '+' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad weight +%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_WEIGHT;
			$$->action.relative = $3;
		}
		| WEIGHT '-' NUMBER		{
			if ($3 < 0 || $3 > INT_MAX) {
				yyerror("bad weight -%lld", $3);
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_RELATIVE_WEIGHT;
			$$->action.relative = -$3;
		}
		| NEXTHOP address		{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_NEXTHOP;
			memcpy(&$$->action.nexthop, &$2,
			    sizeof($$->action.nexthop));
		}
		| NEXTHOP BLACKHOLE		{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_NEXTHOP_BLACKHOLE;
		}
		| NEXTHOP REJECT		{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_NEXTHOP_REJECT;
		}
		| NEXTHOP NOMODIFY		{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_NEXTHOP_NOMODIFY;
		}
		| NEXTHOP SELF		{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_NEXTHOP_SELF;
		}
		| PREPEND_SELF NUMBER		{
			if ($2 < 0 || $2 > 128) {
				yyerror("bad number of prepends");
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_PREPEND_SELF;
			$$->action.prepend = $2;
		}
		| PREPEND_PEER NUMBER		{
			if ($2 < 0 || $2 > 128) {
				yyerror("bad number of prepends");
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_PREPEND_PEER;
			$$->action.prepend = $2;
		}
		| ASOVERRIDE			{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_AS_OVERRIDE;
		}
		| PFTABLE STRING		{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_PFTABLE;
			if (!(cmd_opts & BGPD_OPT_NOACTION) &&
			    pftable_exists($2) != 0) {
				yyerror("pftable name does not exist");
				free($2);
				free($$);
				YYERROR;
			}
			if (strlcpy($$->action.pftable, $2,
			    sizeof($$->action.pftable)) >=
			    sizeof($$->action.pftable)) {
				yyerror("pftable name too long");
				free($2);
				free($$);
				YYERROR;
			}
			if (pftable_add($2) != 0) {
				yyerror("Couldn't register table");
				free($2);
				free($$);
				YYERROR;
			}
			free($2);
		}
		| RTLABEL STRING		{
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_RTLABEL;
			if (strlcpy($$->action.rtlabel, $2,
			    sizeof($$->action.rtlabel)) >=
			    sizeof($$->action.rtlabel)) {
				yyerror("rtlabel name too long");
				free($2);
				free($$);
				YYERROR;
			}
			free($2);
		}
		| community delete STRING	{
			uint8_t f1, f2, f3;

			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if ($2)
				$$->type = ACTION_DEL_COMMUNITY;
			else
				$$->type = ACTION_SET_COMMUNITY;

			if (parsecommunity(&$$->action.community, $1, $3) ==
			    -1) {
				free($3);
				free($$);
				YYERROR;
			}
			free($3);
			/* Don't allow setting of any match */
			f1 = $$->action.community.flags >> 8;
			f2 = $$->action.community.flags >> 16;
			f3 = $$->action.community.flags >> 24;
			if (!$2 && (f1 == COMMUNITY_ANY ||
			    f2 == COMMUNITY_ANY || f3 == COMMUNITY_ANY)) {
				yyerror("'*' is not allowed in set community");
				free($$);
				YYERROR;
			}
		}
		| EXTCOMMUNITY delete STRING STRING {
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if ($2)
				$$->type = ACTION_DEL_COMMUNITY;
			else
				$$->type = ACTION_SET_COMMUNITY;

			if (parseextcommunity(&$$->action.community,
			    $3, $4) == -1) {
				free($3);
				free($4);
				free($$);
				YYERROR;
			}
			free($3);
			free($4);
		}
		| EXTCOMMUNITY delete OVS STRING {
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if ($2)
				$$->type = ACTION_DEL_COMMUNITY;
			else
				$$->type = ACTION_SET_COMMUNITY;

			if (parseextcommunity(&$$->action.community,
			    "ovs", $4) == -1) {
				free($4);
				free($$);
				YYERROR;
			}
			free($4);
		}
		| ORIGIN origincode {
			if (($$ = calloc(1, sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			$$->type = ACTION_SET_ORIGIN;
			$$->action.origin = $2;
		}
		;

origincode	: STRING	{
			if (!strcmp($1, "egp"))
				$$ = ORIGIN_EGP;
			else if (!strcmp($1, "igp"))
				$$ = ORIGIN_IGP;
			else if (!strcmp($1, "incomplete"))
				$$ = ORIGIN_INCOMPLETE;
			else {
				yyerror("unknown origin \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		};

validity	: STRING	{
			if (!strcmp($1, "not-found"))
				$$ = ROA_NOTFOUND;
			else if (!strcmp($1, "invalid"))
				$$ = ROA_INVALID;
			else if (!strcmp($1, "valid"))
				$$ = ROA_VALID;
			else {
				yyerror("unknown roa validity \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		};

aspa_validity	: STRING	{
			if (!strcmp($1, "unknown"))
				$$ = ASPA_UNKNOWN;
			else if (!strcmp($1, "invalid"))
				$$ = ASPA_INVALID;
			else if (!strcmp($1, "valid"))
				$$ = ASPA_VALID;
			else {
				yyerror("unknown aspa validity \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		};

optnl		: /* empty */
		| '\n' optnl
		;

comma		: /* empty */
		| ','
		| '\n' optnl
		| ',' '\n' optnl
		;

unaryop		: '='		{ $$ = OP_EQ; }
		| NE		{ $$ = OP_NE; }
		| LE		{ $$ = OP_LE; }
		| '<'		{ $$ = OP_LT; }
		| GE		{ $$ = OP_GE; }
		| '>'		{ $$ = OP_GT; }
		;

equalityop	: '='		{ $$ = OP_EQ; }
		| NE		{ $$ = OP_NE; }
		;

binaryop	: '-'		{ $$ = OP_RANGE; }
		| XRANGE	{ $$ = OP_XRANGE; }
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
		{ "AS",			AS },
		{ "EVPN",		EVPN },
		{ "IPv4",		IPV4 },
		{ "IPv6",		IPV6 },
		{ "add-path",		ADDPATH },
		{ "ah",			AH },
		{ "allow",		ALLOW },
		{ "announce",		ANNOUNCE },
		{ "any",		ANY },
		{ "as-4byte",		AS4BYTE },
		{ "as-override",	ASOVERRIDE },
		{ "as-set",		ASSET },
		{ "aspa-set",		ASPASET },
		{ "avs",		AVS },
		{ "blackhole",		BLACKHOLE },
		{ "community",		COMMUNITY },
		{ "compare",		COMPARE },
		{ "connect-retry",	CONNECTRETRY },
		{ "connected",		CONNECTED },
		{ "customer-as",	CUSTOMERAS },
		{ "default-route",	DEFAULTROUTE },
		{ "delete",		DELETE },
		{ "demote",		DEMOTE },
		{ "deny",		DENY },
		{ "depend",		DEPEND },
		{ "descr",		DESCR },
		{ "down",		DOWN },
		{ "dump",		DUMP },
		{ "ebgp",		EBGP },
		{ "enforce",		ENFORCE },
		{ "enhanced",		ENHANCED },
		{ "esp",		ESP },
		{ "evaluate",		EVALUATE },
		{ "expires",		EXPIRES },
		{ "export",		EXPORT },
		{ "export-target",	EXPORTTRGT },
		{ "ext-community",	EXTCOMMUNITY },
		{ "extended",		EXTENDED },
		{ "fib-priority",	FIBPRIORITY },
		{ "fib-update",		FIBUPDATE },
		{ "filtered",		FILTERED },
		{ "flags",		FLAGS },
		{ "flowspec",		FLOWSPEC },
		{ "fragment",		FRAGMENT },
		{ "from",		FROM },
		{ "graceful",		GRACEFUL },
		{ "group",		GROUP },
		{ "holdtime",		HOLDTIME },
		{ "ibgp",		IBGP },
		{ "ignore",		IGNORE },
		{ "ike",		IKE },
		{ "import-target",	IMPORTTRGT },
		{ "in",			IN },
		{ "include",		INCLUDE },
		{ "inet",		IPV4 },
		{ "inet6",		IPV6 },
		{ "ipsec",		IPSEC },
		{ "key",		KEY },
		{ "large-community",	LARGECOMMUNITY },
		{ "listen",		LISTEN },
		{ "local-address",	LOCALADDR },
		{ "local-as",		LOCALAS },
		{ "localpref",		LOCALPREF },
		{ "log",		LOG },
		{ "match",		MATCH },
		{ "max",		MAX },
		{ "max-as-len",		MAXASLEN },
		{ "max-as-seq",		MAXASSEQ },
		{ "max-communities",	MAXCOMMUNITIES },
		{ "max-ext-communities",	MAXEXTCOMMUNITIES },
		{ "max-large-communities",	MAXLARGECOMMUNITIES },
		{ "max-prefix",		MAXPREFIX },
		{ "maxlen",		MAXLEN },
		{ "md5sig",		MD5SIG },
		{ "med",		MED },
		{ "message",		MESSAGE },
		{ "metric",		METRIC },
		{ "min",		YMIN },
		{ "min-version",	MINVERSION },
		{ "multihop",		MULTIHOP },
		{ "neighbor",		NEIGHBOR },
		{ "neighbor-as",	NEIGHBORAS },
		{ "network",		NETWORK },
		{ "nexthop",		NEXTHOP },
		{ "no-modify",		NOMODIFY },
		{ "none",		NONE },
		{ "notification",	NOTIFICATION },
		{ "on",			ON },
		{ "or-longer",		LONGER },
		{ "origin",		ORIGIN },
		{ "origin-set",		ORIGINSET },
		{ "out",		OUT },
		{ "ovs",		OVS },
		{ "passive",		PASSIVE },
		{ "password",		PASSWORD },
		{ "peer-as",		PEERAS },
		{ "pftable",		PFTABLE },
		{ "plus",		PLUS },
		{ "policy",		POLICY },
		{ "port",		PORT },
		{ "prefix",		PREFIX },
		{ "prefix-set",		PREFIXSET },
		{ "prefixlen",		PREFIXLEN },
		{ "prepend-neighbor",	PREPEND_PEER },
		{ "prepend-self",	PREPEND_SELF },
		{ "priority",		PRIORITY },
		{ "proto",		PROTO },
		{ "provider-as",	PROVIDERAS },
		{ "qualify",		QUALIFY },
		{ "quick",		QUICK },
		{ "rd",			RD },
		{ "rde",		RDE },
		{ "recv",		RECV },
		{ "refresh",		REFRESH },
		{ "reject",		REJECT },
		{ "remote-as",		REMOTEAS },
		{ "restart",		RESTART },
		{ "restricted",		RESTRICTED },
		{ "rib",		RIB },
		{ "roa-set",		ROASET },
		{ "role",		ROLE },
		{ "route-reflector",	REFLECTOR },
		{ "router-id",		ROUTERID },
		{ "rtable",		RTABLE },
		{ "rtlabel",		RTLABEL },
		{ "rtr",		RTR },
		{ "self",		SELF },
		{ "send",		SEND },
		{ "set",		SET },
		{ "socket",		SOCKET },
		{ "source-as",		SOURCEAS },
		{ "spi",		SPI },
		{ "staletime",		STALETIME },
		{ "static",		STATIC },
		{ "tcp",		TCP },
		{ "to",			TO },
		{ "tos",		TOS },
		{ "transit-as",		TRANSITAS },
		{ "transparent-as",	TRANSPARENT },
		{ "ttl-security",	TTLSECURITY },
		{ "unicast",		UNICAST },
		{ "via",		VIA },
		{ "vpn",		VPN },
		{ "weight",		WEIGHT },
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, nitems(keywords), sizeof(keywords[0]), kw_cmp);

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
expand_macro(void)
{
	char	 buf[MACRO_NAME_LEN];
	char	*p, *val;
	int	 c;

	p = buf;
	while (1) {
		if ((c = lgetc('$')) == EOF)
			return (ERROR);
		if (p + 1 >= buf + sizeof(buf) - 1) {
			yyerror("macro name too long");
			return (ERROR);
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
		return (ERROR);
	}
	p = val + strlen(val) - 1;
	lungetc(DONE_EXPAND);
	while (p >= val) {
		lungetc((unsigned char)*p);
		p--;
	}
	lungetc(START_EXPAND);
	return (0);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p;
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
		c = expand_macro();
		if (c != 0)
			return (c);
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
				yyerror("syntax error: unterminated quote");
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
	case '!':
		next = lgetc(0);
		if (next == '=')
			return (NE);
		lungetc(next);
		break;
	case '<':
		next = lgetc(0);
		if (next == '=')
			return (LE);
		lungetc(next);
		break;
	case '>':
		next = lgetc(0);
		if (next == '<')
			return (XRANGE);
		else if (next == '=')
			return (GE);
		lungetc(next);
		break;
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x == ',' || x == '/' || x == '}' || x == '=')

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
			if (c == '$' && !expanding) {
				c = expand_macro();
				if (c != 0)
					return (c);
			} else
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

int
check_file_secrecy(int fd, const char *fname)
{
	struct stat	st;

	if (fstat(fd, &st)) {
		log_warn("cannot stat %s", fname);
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
	}
	if (secret &&
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

static void
init_config(struct bgpd_config *c)
{
	u_int rdomid;

	c->min_holdtime = MIN_HOLDTIME;
	c->holdtime = INTERVAL_HOLD;
	c->staletime = INTERVAL_STALE;
	c->connectretry = INTERVAL_CONNECTRETRY;
	c->bgpid = get_bgpid();
	c->fib_priority = kr_default_prio();
	c->default_tableid = getrtable();
	if (!ktable_exists(c->default_tableid, &rdomid))
		fatalx("current routing table %u does not exist",
		    c->default_tableid);
	if (rdomid != c->default_tableid)
		fatalx("current routing table %u is not a routing domain",
		    c->default_tableid);

	if (asprintf(&c->csock, "%s.%d", SOCKET_NAME, c->default_tableid) == -1)
		fatal(NULL);
}

struct bgpd_config *
parse_config(const char *filename, struct peer_head *ph,
    struct rtr_config_head *rh)
{
	struct sym		*sym, *next;
	struct rde_rib		*rr;
	struct network		*n;
	int			 errors = 0;

	conf = new_config();
	init_config(conf);

	if ((filter_l = calloc(1, sizeof(struct filter_head))) == NULL)
		fatal(NULL);
	if ((peerfilter_l = calloc(1, sizeof(struct filter_head))) == NULL)
		fatal(NULL);
	if ((groupfilter_l = calloc(1, sizeof(struct filter_head))) == NULL)
		fatal(NULL);
	TAILQ_INIT(filter_l);
	TAILQ_INIT(peerfilter_l);
	TAILQ_INIT(groupfilter_l);

	curpeer = NULL;
	curgroup = NULL;

	cur_peers = ph;
	cur_rtrs = rh;
	new_peers = &conf->peers;
	netconf = &conf->networks;

	if ((rr = add_rib("Adj-RIB-In")) == NULL)
		fatal("add_rib failed");
	rr->flags = F_RIB_NOFIB | F_RIB_NOEVALUATE;
	if ((rr = add_rib("Loc-RIB")) == NULL)
		fatal("add_rib failed");
	rib_add_fib(rr, conf->default_tableid);
	rr->flags = F_RIB_LOCAL;

	if ((file = pushfile(filename, 1)) == NULL)
		goto errors;
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* check that we dont try to announce our own routes */
	TAILQ_FOREACH(n, netconf, entry)
		if (n->net.priority == conf->fib_priority) {
			errors++;
			logit(LOG_CRIT, "network priority %d == fib-priority "
			    "%d is not allowed.",
			    n->net.priority, conf->fib_priority);
		}

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((cmd_opts & BGPD_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro \"%s\" not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	if (!conf->as) {
		log_warnx("configuration error: AS not given");
		errors++;
	}

	/* clear the globals */
	curpeer = NULL;
	curgroup = NULL;
	cur_peers = NULL;
	new_peers = NULL;
	netconf = NULL;
	curflow = NULL;

	if (errors) {
errors:
		while ((rr = SIMPLEQ_FIRST(&ribnames)) != NULL) {
			SIMPLEQ_REMOVE_HEAD(&ribnames, entry);
			free(rr);
		}

		filterlist_free(filter_l);
		filterlist_free(peerfilter_l);
		filterlist_free(groupfilter_l);

		free_config(conf);
		return (NULL);
	}

	/* Create default listeners if none where specified. */
	if (TAILQ_EMPTY(conf->listen_addrs)) {
		struct listen_addr *la;

		if ((la = calloc(1, sizeof(struct listen_addr))) == NULL)
			fatal("setup_listeners calloc");
		la->fd = -1;
		la->flags = DEFAULT_LISTENER;
		la->reconf = RECONF_REINIT;
		la->sa_len = sizeof(struct sockaddr_in);
		((struct sockaddr_in *)&la->sa)->sin_family = AF_INET;
		((struct sockaddr_in *)&la->sa)->sin_addr.s_addr =
		    htonl(INADDR_ANY);
		((struct sockaddr_in *)&la->sa)->sin_port = htons(BGP_PORT);
		TAILQ_INSERT_TAIL(conf->listen_addrs, la, entry);

		if ((la = calloc(1, sizeof(struct listen_addr))) == NULL)
			fatal("setup_listeners calloc");
		la->fd = -1;
		la->flags = DEFAULT_LISTENER;
		la->reconf = RECONF_REINIT;
		la->sa_len = sizeof(struct sockaddr_in6);
		((struct sockaddr_in6 *)&la->sa)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)&la->sa)->sin6_port = htons(BGP_PORT);
		TAILQ_INSERT_TAIL(conf->listen_addrs, la, entry);
	}

	/* update clusterid in case it was not set explicitly */
	if ((conf->flags & BGPD_FLAG_REFLECTOR) && conf->clusterid == 0)
		conf->clusterid = conf->bgpid;

	/*
	 * Concatenate filter list and static group and peer filtersets
	 * together. Static group sets come first then peer sets
	 * last normal filter rules.
	 */
	TAILQ_CONCAT(conf->filters, groupfilter_l, entry);
	TAILQ_CONCAT(conf->filters, peerfilter_l, entry);
	TAILQ_CONCAT(conf->filters, filter_l, entry);

	optimize_filters(conf->filters);

	free(filter_l);
	free(peerfilter_l);
	free(groupfilter_l);

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
		fatal("%s: strndup", __func__);
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

static int
cmpcommunity(struct community *a, struct community *b)
{
	if (a->flags > b->flags)
		return 1;
	if (a->flags < b->flags)
		return -1;
	if (a->data1 > b->data1)
		return 1;
	if (a->data1 < b->data1)
		return -1;
	if (a->data2 > b->data2)
		return 1;
	if (a->data2 < b->data2)
		return -1;
	if (a->data3 > b->data3)
		return 1;
	if (a->data3 < b->data3)
		return -1;
	return 0;
}

static int
getcommunity(char *s, int large, uint32_t *val, uint32_t *flag)
{
	long long	 max = USHRT_MAX;
	const char	*errstr;

	*flag = 0;
	*val = 0;
	if (strcmp(s, "*") == 0) {
		*flag = COMMUNITY_ANY;
		return 0;
	} else if (strcmp(s, "neighbor-as") == 0) {
		*flag = COMMUNITY_NEIGHBOR_AS;
		return 0;
	} else if (strcmp(s, "local-as") == 0) {
		*flag = COMMUNITY_LOCAL_AS;
		return 0;
	}
	if (large)
		max = UINT_MAX;
	*val = strtonum(s, 0, max, &errstr);
	if (errstr) {
		yyerror("Community %s is %s (max: %lld)", s, errstr, max);
		return -1;
	}
	return 0;
}

static void
setcommunity(struct community *c, uint32_t as, uint32_t data,
    uint32_t asflag, uint32_t dataflag)
{
	c->flags = COMMUNITY_TYPE_BASIC;
	c->flags |= asflag << 8;
	c->flags |= dataflag << 16;
	c->data1 = as;
	c->data2 = data;
	c->data3 = 0;
}

static int
parselargecommunity(struct community *c, char *s)
{
	char *p, *q;
	uint32_t dflag1, dflag2, dflag3;

	if ((p = strchr(s, ':')) == NULL) {
		yyerror("Bad community syntax");
		return (-1);
	}
	*p++ = 0;

	if ((q = strchr(p, ':')) == NULL) {
		yyerror("Bad community syntax");
		return (-1);
	}
	*q++ = 0;

	if (getcommunity(s, 1, &c->data1, &dflag1) == -1 ||
	    getcommunity(p, 1, &c->data2, &dflag2) == -1 ||
	    getcommunity(q, 1, &c->data3, &dflag3) == -1)
		return (-1);
	c->flags = COMMUNITY_TYPE_LARGE;
	c->flags |= dflag1 << 8;
	c->flags |= dflag2 << 16;
	c->flags |= dflag3 << 24;
	return (0);
}

int
parsecommunity(struct community *c, int type, char *s)
{
	char *p;
	uint32_t as, data, asflag, dataflag;

	if (type == COMMUNITY_TYPE_LARGE)
		return parselargecommunity(c, s);

	/* Well-known communities */
	if (strcasecmp(s, "GRACEFUL_SHUTDOWN") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_GRACEFUL_SHUTDOWN, 0, 0);
		return (0);
	} else if (strcasecmp(s, "NO_EXPORT") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_EXPORT, 0, 0);
		return (0);
	} else if (strcasecmp(s, "NO_ADVERTISE") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_ADVERTISE, 0, 0);
		return (0);
	} else if (strcasecmp(s, "NO_EXPORT_SUBCONFED") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_EXPSUBCONFED, 0, 0);
		return (0);
	} else if (strcasecmp(s, "NO_PEER") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_PEER, 0, 0);
		return (0);
	} else if (strcasecmp(s, "BLACKHOLE") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_BLACKHOLE, 0, 0);
		return (0);
	}

	if ((p = strchr(s, ':')) == NULL) {
		yyerror("Bad community syntax");
		return (-1);
	}
	*p++ = 0;

	if (getcommunity(s, 0, &as, &asflag) == -1 ||
	    getcommunity(p, 0, &data, &dataflag) == -1)
		return (-1);
	setcommunity(c, as, data, asflag, dataflag);
	return (0);
}

static int
parsesubtype(char *name, int *type, int *subtype)
{
	const struct ext_comm_pairs *cp;
	int found = 0;

	for (cp = iana_ext_comms; cp->subname != NULL; cp++) {
		if (strcmp(name, cp->subname) == 0) {
			if (found == 0) {
				*type = cp->type;
				*subtype = cp->subtype;
			}
			found++;
		}
	}
	if (found > 1)
		*type = -1;
	return (found);
}

static int
parseextvalue(int type, char *s, uint32_t *v, uint32_t *flag)
{
	const char	*errstr;
	char		*p;
	struct in_addr	 ip;
	uint32_t	 uvalh, uval;

	if (type != -1) {
		/* nothing */
	} else if (strcmp(s, "neighbor-as") == 0) {
		*flag = COMMUNITY_NEIGHBOR_AS;
		*v = 0;
		return EXT_COMMUNITY_TRANS_TWO_AS;
	} else if (strcmp(s, "local-as") == 0) {
		*flag = COMMUNITY_LOCAL_AS;
		*v = 0;
		return EXT_COMMUNITY_TRANS_TWO_AS;
	} else if ((p = strchr(s, '.')) == NULL) {
		/* AS_PLAIN number (4 or 2 byte) */
		strtonum(s, 0, USHRT_MAX, &errstr);
		if (errstr == NULL)
			type = EXT_COMMUNITY_TRANS_TWO_AS;
		else
			type = EXT_COMMUNITY_TRANS_FOUR_AS;
	} else if (strchr(p + 1, '.') == NULL) {
		/* AS_DOT number (4-byte) */
		type = EXT_COMMUNITY_TRANS_FOUR_AS;
	} else {
		/* more than one dot -> IP address */
		type = EXT_COMMUNITY_TRANS_IPV4;
	}

	switch (type & EXT_COMMUNITY_VALUE) {
	case EXT_COMMUNITY_TRANS_TWO_AS:
		uval = strtonum(s, 0, USHRT_MAX, &errstr);
		if (errstr) {
			yyerror("Bad ext-community %s is %s", s, errstr);
			return (-1);
		}
		*v = uval;
		break;
	case EXT_COMMUNITY_TRANS_FOUR_AS:
		if ((p = strchr(s, '.')) == NULL) {
			uval = strtonum(s, 0, UINT_MAX, &errstr);
			if (errstr) {
				yyerror("Bad ext-community %s is %s", s,
				    errstr);
				return (-1);
			}
			*v = uval;
			break;
		}
		*p++ = '\0';
		uvalh = strtonum(s, 0, USHRT_MAX, &errstr);
		if (errstr) {
			yyerror("Bad ext-community %s is %s", s, errstr);
			return (-1);
		}
		uval = strtonum(p, 0, USHRT_MAX, &errstr);
		if (errstr) {
			yyerror("Bad ext-community %s is %s", p, errstr);
			return (-1);
		}
		*v = uval | (uvalh << 16);
		break;
	case EXT_COMMUNITY_TRANS_IPV4:
		if (inet_pton(AF_INET, s, &ip) != 1) {
			yyerror("Bad ext-community %s not parseable", s);
			return (-1);
		}
		*v = ntohl(ip.s_addr);
		break;
	default:
		fatalx("%s: unexpected type %d", __func__, type);
	}
	return (type);
}

int
parseextcommunity(struct community *c, char *t, char *s)
{
	const struct ext_comm_pairs *cp;
	char		*p, *ep;
	uint64_t	 ullval;
	uint32_t	 uval, uval2, dflag1 = 0, dflag2 = 0;
	int		 type = 0, subtype = 0;

	if (strcmp(t, "*") == 0 && strcmp(s, "*") == 0) {
		c->flags = COMMUNITY_TYPE_EXT;
		c->flags |= COMMUNITY_ANY << 24;
		return (0);
	}
	if (parsesubtype(t, &type, &subtype) == 0) {
		yyerror("Bad ext-community unknown type");
		return (-1);
	}

	switch (type) {
	case EXT_COMMUNITY_TRANS_TWO_AS:
	case EXT_COMMUNITY_TRANS_FOUR_AS:
	case EXT_COMMUNITY_TRANS_IPV4:
	case EXT_COMMUNITY_GEN_TWO_AS:
	case EXT_COMMUNITY_GEN_FOUR_AS:
	case EXT_COMMUNITY_GEN_IPV4:
	case -1:
		if (strcmp(s, "*") == 0) {
			dflag1 = COMMUNITY_ANY;
			break;
		}
		if ((p = strchr(s, ':')) == NULL) {
			yyerror("Bad ext-community %s", s);
			return (-1);
		}
		*p++ = '\0';
		if ((type = parseextvalue(type, s, &uval, &dflag1)) == -1)
			return (-1);

		switch (type) {
		case EXT_COMMUNITY_TRANS_TWO_AS:
		case EXT_COMMUNITY_GEN_TWO_AS:
			if (getcommunity(p, 1, &uval2, &dflag2) == -1)
				return (-1);
			break;
		case EXT_COMMUNITY_TRANS_IPV4:
		case EXT_COMMUNITY_TRANS_FOUR_AS:
		case EXT_COMMUNITY_GEN_IPV4:
		case EXT_COMMUNITY_GEN_FOUR_AS:
			if (getcommunity(p, 0, &uval2, &dflag2) == -1)
				return (-1);
			break;
		default:
			fatalx("parseextcommunity: unexpected result");
		}

		c->data1 = uval;
		c->data2 = uval2;
		break;
	case EXT_COMMUNITY_TRANS_OPAQUE:
	case EXT_COMMUNITY_TRANS_EVPN:
		if (strcmp(s, "*") == 0) {
			dflag1 = COMMUNITY_ANY;
			break;
		}
		errno = 0;
		ullval = strtoull(s, &ep, 0);
		if (s[0] == '\0' || *ep != '\0') {
			yyerror("Bad ext-community bad value");
			return (-1);
		}
		if (errno == ERANGE && ullval > EXT_COMMUNITY_OPAQUE_MAX) {
			yyerror("Bad ext-community value too big");
			return (-1);
		}
		c->data1 = ullval >> 32;
		c->data2 = ullval;
		break;
	case EXT_COMMUNITY_NON_TRANS_OPAQUE:
		if (subtype == EXT_COMMUNITY_SUBTYPE_OVS) {
			if (strcmp(s, "valid") == 0) {
				c->data2 = EXT_COMMUNITY_OVS_VALID;
				break;
			} else if (strcmp(s, "invalid") == 0) {
				c->data2 = EXT_COMMUNITY_OVS_INVALID;
				break;
			} else if (strcmp(s, "not-found") == 0) {
				c->data2 = EXT_COMMUNITY_OVS_NOTFOUND;
				break;
			} else if (strcmp(s, "*") == 0) {
				dflag1 = COMMUNITY_ANY;
				break;
			}
		}
		yyerror("Bad ext-community %s", s);
		return (-1);
	}

	c->data3 = type << 8 | subtype;

	/* special handling of ext-community rt * since type is not known */
	if (dflag1 == COMMUNITY_ANY && type == -1) {
		c->flags = COMMUNITY_TYPE_EXT;
		c->flags |= dflag1 << 8;
		return (0);
	}

	/* verify type/subtype combo */
	for (cp = iana_ext_comms; cp->subname != NULL; cp++) {
		if (cp->type == type && cp->subtype == subtype) {
			c->flags = COMMUNITY_TYPE_EXT;
			c->flags |= dflag1 << 8;
			c->flags |= dflag2 << 16;
			return (0);
		}
	}

	yyerror("Bad ext-community bad format for type");
	return (-1);
}

struct peer *
alloc_peer(void)
{
	struct peer	*p;

	if ((p = calloc(1, sizeof(struct peer))) == NULL)
		fatal("new_peer");

	/* some sane defaults */
	p->state = STATE_NONE;
	p->reconf_action = RECONF_REINIT;
	p->conf.distance = 1;
	p->conf.export_type = EXPORT_UNSET;
	p->conf.capabilities.refresh = 1;
	p->conf.capabilities.grestart.restart = 1;
	p->conf.capabilities.as4byte = 1;
	p->conf.capabilities.policy = 1;
	p->conf.local_as = conf->as;
	p->conf.local_short_as = conf->short_as;
	p->conf.remote_port = BGP_PORT;

	if (conf->flags & BGPD_FLAG_DECISION_TRANS_AS)
		p->conf.flags |= PEERFLAG_TRANS_AS;
	if (conf->flags & BGPD_FLAG_DECISION_ALL_PATHS)
		p->conf.flags |= PEERFLAG_EVALUATE_ALL;
	if (conf->flags & BGPD_FLAG_PERMIT_AS_SET)
		p->conf.flags |= PEERFLAG_PERMIT_AS_SET;

	return (p);
}

struct peer *
new_peer(void)
{
	struct peer		*p;

	p = alloc_peer();

	if (curgroup != NULL) {
		p->conf = curgroup->conf;
		p->auth_conf = curgroup->auth_conf;
		p->conf.groupid = curgroup->conf.id;
	}
	return (p);
}

struct peer *
new_group(void)
{
	return (alloc_peer());
}

int
add_mrtconfig(enum mrt_type type, char *name, int timeout, struct peer *p,
    char *rib)
{
	struct mrt	*m, *n;

	LIST_FOREACH(m, conf->mrt, entry) {
		if ((rib && strcmp(rib, m->rib)) ||
		    (!rib && *m->rib))
			continue;
		if (p == NULL) {
			if (m->peer_id != 0 || m->group_id != 0)
				continue;
		} else {
			if (m->peer_id != p->conf.id ||
			    m->group_id != p->conf.groupid)
				continue;
		}
		if (m->type == type) {
			yyerror("only one mrtdump per type allowed.");
			return (-1);
		}
	}

	if ((n = calloc(1, sizeof(struct mrt_config))) == NULL)
		fatal("add_mrtconfig");

	n->type = type;
	n->state = MRT_STATE_OPEN;
	if (strlcpy(MRT2MC(n)->name, name, sizeof(MRT2MC(n)->name)) >=
	    sizeof(MRT2MC(n)->name)) {
		yyerror("filename \"%s\" too long: max %zu",
		    name, sizeof(MRT2MC(n)->name) - 1);
		free(n);
		return (-1);
	}
	MRT2MC(n)->ReopenTimerInterval = timeout;
	if (p != NULL) {
		if (curgroup == p) {
			n->peer_id = 0;
			n->group_id = p->conf.id;
		} else {
			n->peer_id = p->conf.id;
			n->group_id = p->conf.groupid;
		}
	}
	if (rib) {
		if (!find_rib(rib)) {
			yyerror("rib \"%s\" does not exist.", rib);
			free(n);
			return (-1);
		}
		if (strlcpy(n->rib, rib, sizeof(n->rib)) >=
		    sizeof(n->rib)) {
			yyerror("rib name \"%s\" too long: max %zu",
			    name, sizeof(n->rib) - 1);
			free(n);
			return (-1);
		}
	}

	LIST_INSERT_HEAD(conf->mrt, n, entry);

	return (0);
}

struct rde_rib *
add_rib(char *name)
{
	struct rde_rib	*rr;

	if ((rr = find_rib(name)) == NULL) {
		if ((rr = calloc(1, sizeof(*rr))) == NULL) {
			log_warn("add_rib");
			return (NULL);
		}
		if (strlcpy(rr->name, name, sizeof(rr->name)) >=
		    sizeof(rr->name)) {
			yyerror("rib name \"%s\" too long: max %zu",
			    name, sizeof(rr->name) - 1);
			free(rr);
			return (NULL);
		}
		rr->flags = F_RIB_NOFIB;
		SIMPLEQ_INSERT_TAIL(&ribnames, rr, entry);
	}
	return (rr);
}

struct rde_rib *
find_rib(char *name)
{
	struct rde_rib	*rr;

	SIMPLEQ_FOREACH(rr, &ribnames, entry) {
		if (!strcmp(rr->name, name))
			return (rr);
	}
	return (NULL);
}

int
rib_add_fib(struct rde_rib *rr, u_int rtableid)
{
	u_int	rdom;

	if (!ktable_exists(rtableid, &rdom)) {
		yyerror("rtable id %u does not exist", rtableid);
		return (-1);
	}
	/*
	 * conf->default_tableid is also a rdomain because that is checked
	 * in init_config()
	 */
	if (rdom != conf->default_tableid) {
		log_warnx("rtable %u does not belong to rdomain %u",
		    rtableid, conf->default_tableid);
		return (-1);
	}
	rr->rtableid = rtableid;
	rr->flags &= ~F_RIB_NOFIB;
	return (0);
}

struct prefixset *
find_prefixset(char *name, struct prefixset_head *p)
{
	struct prefixset *ps;

	SIMPLEQ_FOREACH(ps, p, entry) {
		if (!strcmp(ps->name, name))
			return (ps);
	}
	return (NULL);
}

int
get_id(struct peer *newpeer)
{
	static uint32_t id = PEER_ID_STATIC_MIN;
	struct peer	*p = NULL;

	/* check if the peer already existed before */
	if (newpeer->conf.remote_addr.aid) {
		/* neighbor */
		if (cur_peers)
			RB_FOREACH(p, peer_head, cur_peers)
				if (p->conf.remote_masklen ==
				    newpeer->conf.remote_masklen &&
				    memcmp(&p->conf.remote_addr,
				    &newpeer->conf.remote_addr,
				    sizeof(p->conf.remote_addr)) == 0)
					break;
		if (p) {
			newpeer->conf.id = p->conf.id;
			return (0);
		}
	} else {
		/* group */
		if (cur_peers)
			RB_FOREACH(p, peer_head, cur_peers)
				if (strcmp(p->conf.group,
				    newpeer->conf.group) == 0)
					break;
		if (p) {
			newpeer->conf.id = p->conf.groupid;
			return (0);
		}
	}

	/* else new one */
	if (id < PEER_ID_STATIC_MAX) {
		newpeer->conf.id = id++;
		return (0);
	}

	return (-1);
}

int
merge_prefixspec(struct filter_prefix *p, struct filter_prefixlen *pl)
{
	uint8_t max_len = 0;

	switch (p->addr.aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		max_len = 32;
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		max_len = 128;
		break;
	}

	if (pl->op == OP_NONE) {
		p->len_min = p->len_max = p->len;
		return (0);
	}

	if (pl->len_min == -1)
		pl->len_min = p->len;
	if (pl->len_max == -1)
		pl->len_max = max_len;

	if (pl->len_max > max_len) {
		yyerror("prefixlen %d too big, limit %d",
		    pl->len_max, max_len);
		return (-1);
	}
	if (pl->len_min > pl->len_max) {
		yyerror("prefixlen %d too big, limit %d",
		    pl->len_min, pl->len_max);
		return (-1);
	}
	if (pl->len_min < p->len) {
		yyerror("prefixlen %d smaller than prefix, limit %d",
		    pl->len_min, p->len);
		return (-1);
	}

	p->op = pl->op;
	p->len_min = pl->len_min;
	p->len_max = pl->len_max;
	return (0);
}

int
expand_rule(struct filter_rule *rule, struct filter_rib_l *rib,
    struct filter_peers_l *peer, struct filter_match_l *match,
    struct filter_set_head *set)
{
	struct filter_rule	*r;
	struct filter_rib_l	*rb, *rbnext;
	struct filter_peers_l	*p, *pnext;
	struct filter_prefix_l	*prefix, *prefix_next;
	struct filter_as_l	*a, *anext;
	struct filter_set	*s;

	rb = rib;
	do {
		p = peer;
		do {
			a = match->as_l;
			do {
				prefix = match->prefix_l;
				do {
					if ((r = calloc(1,
					    sizeof(struct filter_rule))) ==
						 NULL) {
						log_warn("expand_rule");
						return (-1);
					}

					memcpy(r, rule, sizeof(*r));
					memcpy(&r->match, match,
					    sizeof(struct filter_match));
					filterset_copy(set, &r->set);

					if (rb != NULL)
						strlcpy(r->rib, rb->name,
						    sizeof(r->rib));

					if (p != NULL)
						memcpy(&r->peer, &p->p,
						    sizeof(r->peer));

					if (prefix != NULL)
						memcpy(&r->match.prefix,
						    &prefix->p,
						    sizeof(r->match.prefix));

					if (a != NULL)
						memcpy(&r->match.as, &a->a,
						    sizeof(struct filter_as));

					TAILQ_INSERT_TAIL(filter_l, r, entry);

					if (prefix != NULL)
						prefix = prefix->next;
				} while (prefix != NULL);

				if (a != NULL)
					a = a->next;
			} while (a != NULL);

			if (p != NULL)
				p = p->next;
		} while (p != NULL);

		if (rb != NULL)
			rb = rb->next;
	} while (rb != NULL);

	for (rb = rib; rb != NULL; rb = rbnext) {
		rbnext = rb->next;
		free(rb);
	}

	for (p = peer; p != NULL; p = pnext) {
		pnext = p->next;
		free(p);
	}

	for (a = match->as_l; a != NULL; a = anext) {
		anext = a->next;
		free(a);
	}

	for (prefix = match->prefix_l; prefix != NULL; prefix = prefix_next) {
		prefix_next = prefix->next;
		free(prefix);
	}

	if (set != NULL) {
		while ((s = TAILQ_FIRST(set)) != NULL) {
			TAILQ_REMOVE(set, s, entry);
			free(s);
		}
		free(set);
	}

	return (0);
}

static int
h2i(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else
		return -1;
}

int
str2key(char *s, char *dest, size_t max_len)
{
	size_t	i;

	if (strlen(s) / 2 > max_len) {
		yyerror("key too long");
		return (-1);
	}

	if (strlen(s) % 2) {
		yyerror("key must be of even length");
		return (-1);
	}

	for (i = 0; i < strlen(s) / 2; i++) {
		int hi, lo;

		hi = h2i(s[2 * i]);
		lo = h2i(s[2 * i + 1]);
		if (hi == -1 || lo == -1) {
			yyerror("key must be specified in hex");
			return (-1);
		}
		dest[i] = (hi << 4) | lo;
	}

	return (0);
}

int
neighbor_consistent(struct peer *p)
{
	struct bgpd_addr *local_addr;
	struct peer *xp;

	switch (p->conf.remote_addr.aid) {
	case AID_INET:
		local_addr = &p->conf.local_addr_v4;
		break;
	case AID_INET6:
		local_addr = &p->conf.local_addr_v6;
		break;
	default:
		yyerror("Bad address family for remote-addr");
		return (-1);
	}

	/* with any form of ipsec local-address is required */
	if ((p->auth_conf.method == AUTH_IPSEC_IKE_ESP ||
	    p->auth_conf.method == AUTH_IPSEC_IKE_AH ||
	    p->auth_conf.method == AUTH_IPSEC_MANUAL_ESP ||
	    p->auth_conf.method == AUTH_IPSEC_MANUAL_AH) &&
	    local_addr->aid == AID_UNSPEC) {
		yyerror("neighbors with any form of IPsec configured "
		    "need local-address to be specified");
		return (-1);
	}

	/* with static keying we need both directions */
	if ((p->auth_conf.method == AUTH_IPSEC_MANUAL_ESP ||
	    p->auth_conf.method == AUTH_IPSEC_MANUAL_AH) &&
	    (!p->auth_conf.spi_in || !p->auth_conf.spi_out)) {
		yyerror("with manual keyed IPsec, SPIs and keys "
		    "for both directions are required");
		return (-1);
	}

	if (!conf->as) {
		yyerror("AS needs to be given before neighbor definitions");
		return (-1);
	}

	/* set default values if they where undefined */
	p->conf.ebgp = (p->conf.remote_as != conf->as);
	if (p->conf.enforce_as == ENFORCE_AS_UNDEF)
		p->conf.enforce_as = p->conf.ebgp ?
		    ENFORCE_AS_ON : ENFORCE_AS_OFF;
	if (p->conf.enforce_local_as == ENFORCE_AS_UNDEF)
		p->conf.enforce_local_as = ENFORCE_AS_ON;

	if (p->conf.remote_as == 0 && !p->conf.template) {
		yyerror("peer AS may not be zero");
		return (-1);
	}

	/* EBGP neighbors are not allowed in route reflector clusters */
	if (p->conf.reflector_client && p->conf.ebgp) {
		yyerror("EBGP neighbors are not allowed in route "
		    "reflector clusters");
		return (-1);
	}

	/* BGP role and RFC 9234 role are only valid for EBGP neighbors */
	if (!p->conf.ebgp) {
		p->conf.role = ROLE_NONE;
		p->conf.capabilities.policy = 0;
	} else if (p->conf.role == ROLE_NONE) {
		/* no role, no policy capability */
		p->conf.capabilities.policy = 0;
	}

	/* check for duplicate peer definitions */
	RB_FOREACH(xp, peer_head, new_peers)
		if (xp->conf.remote_masklen ==
		    p->conf.remote_masklen &&
		    memcmp(&xp->conf.remote_addr,
		    &p->conf.remote_addr,
		    sizeof(p->conf.remote_addr)) == 0)
			break;
	if (xp != NULL) {
		char *descr = log_fmt_peer(&p->conf);
		yyerror("duplicate %s", descr);
		free(descr);
		return (-1);
	}

	return (0);
}

static void
filterset_add(struct filter_set_head *sh, struct filter_set *s)
{
	struct filter_set	*t;

	TAILQ_FOREACH(t, sh, entry) {
		if (s->type < t->type) {
			TAILQ_INSERT_BEFORE(t, s, entry);
			return;
		}
		if (s->type == t->type) {
			switch (s->type) {
			case ACTION_SET_COMMUNITY:
			case ACTION_DEL_COMMUNITY:
				switch (cmpcommunity(&s->action.community,
				    &t->action.community)) {
				case -1:
					TAILQ_INSERT_BEFORE(t, s, entry);
					return;
				case 0:
					break;
				case 1:
					continue;
				}
				break;
			case ACTION_SET_NEXTHOP:
				/* only last nexthop per AF matters */
				if (s->action.nexthop.aid <
				    t->action.nexthop.aid) {
					TAILQ_INSERT_BEFORE(t, s, entry);
					return;
				} else if (s->action.nexthop.aid ==
				    t->action.nexthop.aid) {
					t->action.nexthop = s->action.nexthop;
					break;
				}
				continue;
			case ACTION_SET_NEXTHOP_BLACKHOLE:
			case ACTION_SET_NEXTHOP_REJECT:
			case ACTION_SET_NEXTHOP_NOMODIFY:
			case ACTION_SET_NEXTHOP_SELF:
				/* set it only once */
				break;
			case ACTION_SET_LOCALPREF:
			case ACTION_SET_MED:
			case ACTION_SET_WEIGHT:
				/* only last set matters */
				t->action.metric = s->action.metric;
				break;
			case ACTION_SET_RELATIVE_LOCALPREF:
			case ACTION_SET_RELATIVE_MED:
			case ACTION_SET_RELATIVE_WEIGHT:
				/* sum all relative numbers */
				t->action.relative += s->action.relative;
				break;
			case ACTION_SET_ORIGIN:
				/* only last set matters */
				t->action.origin = s->action.origin;
				break;
			case ACTION_PFTABLE:
				/* only last set matters */
				strlcpy(t->action.pftable, s->action.pftable,
				    sizeof(t->action.pftable));
				break;
			case ACTION_RTLABEL:
				/* only last set matters */
				strlcpy(t->action.rtlabel, s->action.rtlabel,
				    sizeof(t->action.rtlabel));
				break;
			default:
				break;
			}
			free(s);
			return;
		}
	}

	TAILQ_INSERT_TAIL(sh, s, entry);
}

int
merge_filterset(struct filter_set_head *sh, struct filter_set *s)
{
	struct filter_set	*t;

	TAILQ_FOREACH(t, sh, entry) {
		/*
		 * need to cycle across the full list because even
		 * if types are not equal filterset_cmp() may return 0.
		 */
		if (filterset_cmp(s, t) == 0) {
			if (s->type == ACTION_SET_COMMUNITY)
				yyerror("community is already set");
			else if (s->type == ACTION_DEL_COMMUNITY)
				yyerror("community will already be deleted");
			else
				yyerror("redefining set parameter %s",
				    filterset_name(s->type));
			return (-1);
		}
	}

	filterset_add(sh, s);
	return (0);
}

static int
filter_equal(struct filter_rule *fa, struct filter_rule *fb)
{
	if (fa == NULL || fb == NULL)
		return 0;
	if (fa->action != fb->action || fa->quick != fb->quick ||
	    fa->dir != fb->dir)
		return 0;
	if (memcmp(&fa->peer, &fb->peer, sizeof(fa->peer)))
		return 0;
	if (memcmp(&fa->match, &fb->match, sizeof(fa->match)))
		return 0;

	return 1;
}

/* do a basic optimization by folding equal rules together */
void
optimize_filters(struct filter_head *fh)
{
	struct filter_rule *r, *nr;

	TAILQ_FOREACH_SAFE(r, fh, entry, nr) {
		while (filter_equal(r, nr)) {
			struct filter_set	*t;

			while ((t = TAILQ_FIRST(&nr->set)) != NULL) {
				TAILQ_REMOVE(&nr->set, t, entry);
				filterset_add(&r->set, t);
			}

			TAILQ_REMOVE(fh, nr, entry);
			free(nr);
			nr = TAILQ_NEXT(r, entry);
		}
	}
}

struct filter_rule *
get_rule(enum action_types type)
{
	struct filter_rule	*r;
	int			 out;

	switch (type) {
	case ACTION_SET_PREPEND_SELF:
	case ACTION_SET_NEXTHOP_NOMODIFY:
	case ACTION_SET_NEXTHOP_SELF:
		out = 1;
		break;
	default:
		out = 0;
		break;
	}
	r = (curpeer == curgroup) ? curgroup_filter[out] : curpeer_filter[out];
	if (r == NULL) {
		if ((r = calloc(1, sizeof(struct filter_rule))) == NULL)
			fatal(NULL);
		r->quick = 0;
		r->dir = out ? DIR_OUT : DIR_IN;
		r->action = ACTION_NONE;
		TAILQ_INIT(&r->set);
		if (curpeer == curgroup) {
			/* group */
			r->peer.groupid = curgroup->conf.id;
			curgroup_filter[out] = r;
		} else {
			/* peer */
			r->peer.peerid = curpeer->conf.id;
			curpeer_filter[out] = r;
		}
	}
	return (r);
}

struct set_table *curset;
static int
new_as_set(char *name)
{
	struct as_set *aset;

	if (as_sets_lookup(&conf->as_sets, name) != NULL) {
		yyerror("as-set \"%s\" already exists", name);
		return -1;
	}

	aset = as_sets_new(&conf->as_sets, name, 0, sizeof(uint32_t));
	if (aset == NULL)
		fatal(NULL);

	curset = aset->set;
	return 0;
}

static void
add_as_set(uint32_t as)
{
	if (curset == NULL)
		fatalx("%s: bad mojo jojo", __func__);

	if (set_add(curset, &as, 1) != 0)
		fatal(NULL);
}

static void
done_as_set(void)
{
	curset = NULL;
}

static struct prefixset *
new_prefix_set(char *name, int is_roa)
{
	const char *type = "prefix-set";
	struct prefixset_head *sets = &conf->prefixsets;
	struct prefixset *pset;

	if (is_roa) {
		type = "origin-set";
		sets = &conf->originsets;
	}

	if (find_prefixset(name, sets) != NULL) {
		yyerror("%s \"%s\" already exists", type, name);
		return NULL;
	}
	if ((pset = calloc(1, sizeof(*pset))) == NULL)
		fatal("prefixset");
	if (strlcpy(pset->name, name, sizeof(pset->name)) >=
	    sizeof(pset->name)) {
		yyerror("%s \"%s\" too long: max %zu", type,
		    name, sizeof(pset->name) - 1);
		free(pset);
		return NULL;
	}
	RB_INIT(&pset->psitems);
	RB_INIT(&pset->roaitems);
	return pset;
}

static void
add_roa_set(struct prefixset_item *npsi, uint32_t as, uint8_t max,
    time_t expires)
{
	struct roa *roa, *r;

	if ((roa = calloc(1, sizeof(*roa))) == NULL)
		fatal("add_roa_set");

	roa->aid = npsi->p.addr.aid;
	roa->prefixlen = npsi->p.len;
	roa->maxlen = max;
	roa->asnum = as;
	roa->expires = expires;
	switch (roa->aid) {
	case AID_INET:
		roa->prefix.inet = npsi->p.addr.v4;
		break;
	case AID_INET6:
		roa->prefix.inet6 = npsi->p.addr.v6;
		break;
	default:
		fatalx("Bad address family for roa_set address");
	}

	r = RB_INSERT(roa_tree, curroatree, roa);
	if (r != NULL) {
		/* just ignore duplicates */
		if (r->expires != 0 && expires != 0 && expires > r->expires)
			r->expires = expires;
		free(roa);
	}
}

static struct rtr_config *
get_rtr(struct bgpd_addr *addr)
{
	struct rtr_config *n;

	n = calloc(1, sizeof(*n));
	if (n == NULL) {
		yyerror("out of memory");
		return NULL;
	}

	n->remote_addr = *addr;
	strlcpy(n->descr, log_addr(addr), sizeof(currtr->descr));

	return n;
}

static int
insert_rtr(struct rtr_config *new)
{
	static uint32_t id;
	struct rtr_config *r;

	if (id == UINT32_MAX) {
		yyerror("out of rtr session IDs");
		return -1;
	}

	SIMPLEQ_FOREACH(r, &conf->rtrs, entry)
		if (memcmp(&r->remote_addr, &new->remote_addr,
		    sizeof(r->remote_addr)) == 0 &&
		    r->remote_port == new->remote_port) {
			yyerror("duplicate rtr session to %s:%u",
			    log_addr(&new->remote_addr), new->remote_port);
			return -1;
		}

	if (cur_rtrs)
		SIMPLEQ_FOREACH(r, cur_rtrs, entry)
			if (memcmp(&r->remote_addr, &new->remote_addr,
			    sizeof(r->remote_addr)) == 0 &&
			    r->remote_port == new->remote_port) {
				new->id = r->id;
				break;
			}

	if (new->id == 0)
		new->id = ++id;

	SIMPLEQ_INSERT_TAIL(&conf->rtrs, currtr, entry);

	return 0;
}

static int
merge_aspa_set(uint32_t as, struct aspa_tas_l *tas, time_t expires)
{
	struct aspa_set	*aspa, needle = { .as = as };
	uint32_t i, num, *newtas;

	aspa = RB_FIND(aspa_tree, &conf->aspa, &needle);
	if (aspa == NULL) {
		if ((aspa = calloc(1, sizeof(*aspa))) == NULL) {
			yyerror("out of memory");
			return -1;
		}
		aspa->as = as;
		aspa->expires = expires;
		RB_INSERT(aspa_tree, &conf->aspa, aspa);
	}

	if (MAX_ASPA_SPAS_COUNT - aspa->num <= tas->num) {
		yyerror("too many providers for customer-as %u", as);
		return -1;
	}
	num = aspa->num + tas->num;
	newtas = recallocarray(aspa->tas, aspa->num, num, sizeof(uint32_t));
	if (newtas == NULL) {
		yyerror("out of memory");
		return -1;
	}
	/* fill starting at the end since the tas list is reversed */
	if (num > 0) {
		for (i = num - 1; tas; tas = tas->next, i--)
			newtas[i] = tas->as;
	}

	aspa->num = num;
	aspa->tas = newtas;

	/* take the longest expiry time, same logic as for ROA entries */
	if (aspa->expires != 0 && expires != 0 && expires > aspa->expires)
		aspa->expires = expires;

	return 0;
}

static int
kw_casecmp(const void *k, const void *e)
{
	return (strcasecmp(k, ((const struct keywords *)e)->k_name));
}

static int
map_tos(char *s, int *val)
{
	/* DiffServ Codepoints and other TOS mappings */
	const struct keywords	 toswords[] = {
		{ "af11",		IPTOS_DSCP_AF11 },
		{ "af12",		IPTOS_DSCP_AF12 },
		{ "af13",		IPTOS_DSCP_AF13 },
		{ "af21",		IPTOS_DSCP_AF21 },
		{ "af22",		IPTOS_DSCP_AF22 },
		{ "af23",		IPTOS_DSCP_AF23 },
		{ "af31",		IPTOS_DSCP_AF31 },
		{ "af32",		IPTOS_DSCP_AF32 },
		{ "af33",		IPTOS_DSCP_AF33 },
		{ "af41",		IPTOS_DSCP_AF41 },
		{ "af42",		IPTOS_DSCP_AF42 },
		{ "af43",		IPTOS_DSCP_AF43 },
		{ "critical",		IPTOS_PREC_CRITIC_ECP },
		{ "cs0",		IPTOS_DSCP_CS0 },
		{ "cs1",		IPTOS_DSCP_CS1 },
		{ "cs2",		IPTOS_DSCP_CS2 },
		{ "cs3",		IPTOS_DSCP_CS3 },
		{ "cs4",		IPTOS_DSCP_CS4 },
		{ "cs5",		IPTOS_DSCP_CS5 },
		{ "cs6",		IPTOS_DSCP_CS6 },
		{ "cs7",		IPTOS_DSCP_CS7 },
		{ "ef",			IPTOS_DSCP_EF },
		{ "inetcontrol",	IPTOS_PREC_INTERNETCONTROL },
		{ "lowdelay",		IPTOS_LOWDELAY },
		{ "netcontrol",		IPTOS_PREC_NETCONTROL },
		{ "reliability",	IPTOS_RELIABILITY },
		{ "throughput",		IPTOS_THROUGHPUT },
	};
	const struct keywords	*p;

	p = bsearch(s, toswords, nitems(toswords), sizeof(toswords[0]),
	    kw_casecmp);

	if (p) {
		*val = p->k_val;
		return (1);
	}
	return (0);
}

static int
getservice(char *n)
{
	struct servent	*s;

	s = getservbyname(n, "tcp");
	if (s == NULL)
		s = getservbyname(n, "udp");
	if (s == NULL)
		return -1;
	return s->s_port;
}

static int
parse_flags(char *s)
{
	const char *flags = FLOWSPEC_TCP_FLAG_STRING;
	char *p, *q;
	uint8_t f = 0;

	if (curflow->type == FLOWSPEC_TYPE_FRAG) {
		if (curflow->aid == AID_INET)
			flags = FLOWSPEC_FRAG_STRING4;
		else
			flags = FLOWSPEC_FRAG_STRING6;
	}

	for (p = s; *p; p++) {
		if ((q = strchr(flags, *p)) == NULL)
			return -1;
		f |= 1 << (q - flags);
	}
	return (f ? f : 0xff);
}

static void
component_finish(int type, uint8_t *data, int len)
{
	uint8_t *last;
	int i;

	switch (type) {
	case FLOWSPEC_TYPE_DEST:
	case FLOWSPEC_TYPE_SOURCE:
		/* nothing to do */
		return;
	default:
		break;
	}

	i = 0;
	do {
		last = data + i;
		i += FLOWSPEC_OP_LEN(*last) + 1;
	} while (i < len);
	*last |= FLOWSPEC_OP_EOL;
}

static struct flowspec_config *
flow_to_flowspec(struct flowspec_context *ctx)
{
	struct flowspec_config *f;
	int i, len = 0;
	uint8_t aid;

	switch (ctx->aid) {
	case AID_INET:
		aid = AID_FLOWSPECv4;
		break;
	case AID_INET6:
		aid = AID_FLOWSPECv6;
		break;
	default:
		return NULL;
	}

	for (i = FLOWSPEC_TYPE_MIN; i < FLOWSPEC_TYPE_MAX; i++)
		if (ctx->components[i] != NULL)
			len += ctx->complen[i] + 1;

	f = flowspec_alloc(aid, len);
	if (f == NULL)
		return NULL;

	len = 0;
	for (i = FLOWSPEC_TYPE_MIN; i < FLOWSPEC_TYPE_MAX; i++)
		if (ctx->components[i] != NULL) {
			f->flow->data[len++] = i;
			component_finish(i, ctx->components[i],
			    ctx->complen[i]);
			memcpy(f->flow->data + len, ctx->components[i],
			    ctx->complen[i]);
			len += ctx->complen[i];
		}

	return f;
}

static void
flow_free(struct flowspec_context *ctx)
{
	int i;

	for (i = 0; i < FLOWSPEC_TYPE_MAX; i++)
		free(ctx->components[i]);
	free(ctx);
}

static int
push_prefix(struct bgpd_addr *addr, uint8_t len)
{
	void *data;
	uint8_t *comp;
	int complen, l;

	if (curflow->components[curflow->addr_type] != NULL) {
		yyerror("flowspec address already set");
		return -1;
	}

	if (curflow->aid != addr->aid) {
		yyerror("wrong address family for flowspec address");
		return -1;
	}

	switch (curflow->aid) {
	case AID_INET:
		complen = PREFIX_SIZE(len);
		data = &addr->v4;
		break;
	case AID_INET6:
		/* IPv6 includes an offset byte */
		complen = PREFIX_SIZE(len) + 1;
		data = &addr->v6;
		break;
	default:
		yyerror("unsupported address family for flowspec address");
		return -1;
	}
	comp = malloc(complen);
	if (comp == NULL) {
		yyerror("out of memory");
		return -1;
	}

	l = 0;
	comp[l++] = len;
	if (curflow->aid == AID_INET6)
		comp[l++] = 0;
	memcpy(comp + l, data, complen - l);

	curflow->complen[curflow->addr_type] = complen;
	curflow->components[curflow->addr_type] = comp;

	return 0;
}

static int
push_binop(uint8_t binop, long long val)
{
	uint8_t *comp;
	int complen;
	uint8_t u8;

	if (val < 0 || val > 0xff) {
		yyerror("unsupported value for flowspec bin_op");
		return -1;
	}
	u8 = val;

	complen = curflow->complen[curflow->type];
	comp = realloc(curflow->components[curflow->type],
	    complen + 2);
	if (comp == NULL) {
		yyerror("out of memory");
		return -1;
	}

	comp[complen++] = binop;
	comp[complen++] = u8;
	curflow->complen[curflow->type] = complen;
	curflow->components[curflow->type] = comp;

	return 0;
}

static uint8_t
component_numop(enum comp_ops op, int and, int len)
{
	uint8_t flag = 0;

	switch (op) {
	case OP_EQ:
		flag |= FLOWSPEC_OP_NUM_EQ;
		break;
	case OP_NE:
		flag |= FLOWSPEC_OP_NUM_NOT;
		break;
	case OP_LE:
		flag |= FLOWSPEC_OP_NUM_LE;
		break;
	case OP_LT:
		flag |= FLOWSPEC_OP_NUM_LT;
		break;
	case OP_GE:
		flag |= FLOWSPEC_OP_NUM_GE;
		break;
	case OP_GT:
		flag |= FLOWSPEC_OP_NUM_GT;
		break;
	default:
		fatalx("unsupported op");
	}

	switch (len) {
	case 2:
		flag |= 1 << FLOWSPEC_OP_LEN_SHIFT;
		break;
	case 4:
		flag |= 2 << FLOWSPEC_OP_LEN_SHIFT;
		break;
	case 8:
		flag |= 3 << FLOWSPEC_OP_LEN_SHIFT;
		break;
	}

	if (and)
		flag |= FLOWSPEC_OP_AND;

	return flag;
}

static int
push_numop(enum comp_ops op, int and, long long val)
{
	uint8_t *comp;
	void *data;
	uint32_t u32;
	uint16_t u16;
	uint8_t u8;
	int len, complen;

	if (val < 0 || val > 0xffffffff) {
		yyerror("unsupported value for flowspec num_op");
		return -1;
	} else if (val <= 255) {
		len = 1;
		u8 = val;
		data = &u8;
	} else if (val <= 0xffff) {
		len = 2;
		u16 = htons(val);
		data = &u16;
	} else {
		len = 4;
		u32 = htonl(val);
		data = &u32;
	}

	complen = curflow->complen[curflow->type];
	comp = realloc(curflow->components[curflow->type],
	    complen + len + 1);
	if (comp == NULL) {
		yyerror("out of memory");
		return -1;
	}

	comp[complen++] = component_numop(op, and, len);
	memcpy(comp + complen, data, len);
	complen += len;
	curflow->complen[curflow->type] = complen;
	curflow->components[curflow->type] = comp;

	return 0;
}

static int
push_unary_numop(enum comp_ops op, long long val)
{
	return push_numop(op, 0, val);
}

static int
push_binary_numop(enum comp_ops op, long long min, long long max)
{
	switch (op) {
	case OP_RANGE:
		if (push_numop(OP_GE, 0, min) == -1)
			return -1;
		return push_numop(OP_LE, 1, max);
	case OP_XRANGE:
		if (push_numop(OP_LT, 0, min) == -1)
			return -1;
		return push_numop(OP_GT, 0, max);
	default:
		yyerror("unsupported binary flowspec num_op");
		return -1;
	}
}

struct icmptypeent {
	const char *name;
	u_int8_t type;
};

struct icmpcodeent {
	const char *name;
	u_int8_t type;
	u_int8_t code;
};

static const struct icmptypeent icmp_type[] = {
	{ "echoreq",	ICMP_ECHO },
	{ "echorep",	ICMP_ECHOREPLY },
	{ "unreach",	ICMP_UNREACH },
	{ "squench",	ICMP_SOURCEQUENCH },
	{ "redir",	ICMP_REDIRECT },
	{ "althost",	ICMP_ALTHOSTADDR },
	{ "routeradv",	ICMP_ROUTERADVERT },
	{ "routersol",	ICMP_ROUTERSOLICIT },
	{ "timex",	ICMP_TIMXCEED },
	{ "paramprob",	ICMP_PARAMPROB },
	{ "timereq",	ICMP_TSTAMP },
	{ "timerep",	ICMP_TSTAMPREPLY },
	{ "inforeq",	ICMP_IREQ },
	{ "inforep",	ICMP_IREQREPLY },
	{ "maskreq",	ICMP_MASKREQ },
	{ "maskrep",	ICMP_MASKREPLY },
	{ "trace",	ICMP_TRACEROUTE },
	{ "dataconv",	ICMP_DATACONVERR },
	{ "mobredir",	ICMP_MOBILE_REDIRECT },
	{ "ipv6-where",	ICMP_IPV6_WHEREAREYOU },
	{ "ipv6-here",	ICMP_IPV6_IAMHERE },
	{ "mobregreq",	ICMP_MOBILE_REGREQUEST },
	{ "mobregrep",	ICMP_MOBILE_REGREPLY },
	{ "skip",	ICMP_SKIP },
	{ "photuris",	ICMP_PHOTURIS },
};

static const struct icmptypeent icmp6_type[] = {
	{ "unreach",	ICMP6_DST_UNREACH },
	{ "toobig",	ICMP6_PACKET_TOO_BIG },
	{ "timex",	ICMP6_TIME_EXCEEDED },
	{ "paramprob",	ICMP6_PARAM_PROB },
	{ "echoreq",	ICMP6_ECHO_REQUEST },
	{ "echorep",	ICMP6_ECHO_REPLY },
	{ "groupqry",	ICMP6_MEMBERSHIP_QUERY },
	{ "listqry",	MLD_LISTENER_QUERY },
	{ "grouprep",	ICMP6_MEMBERSHIP_REPORT },
	{ "listenrep",	MLD_LISTENER_REPORT },
	{ "groupterm",	ICMP6_MEMBERSHIP_REDUCTION },
	{ "listendone", MLD_LISTENER_DONE },
	{ "routersol",	ND_ROUTER_SOLICIT },
	{ "routeradv",	ND_ROUTER_ADVERT },
	{ "neighbrsol", ND_NEIGHBOR_SOLICIT },
	{ "neighbradv", ND_NEIGHBOR_ADVERT },
	{ "redir",	ND_REDIRECT },
	{ "routrrenum", ICMP6_ROUTER_RENUMBERING },
	{ "wrureq",	ICMP6_WRUREQUEST },
	{ "wrurep",	ICMP6_WRUREPLY },
	{ "fqdnreq",	ICMP6_FQDN_QUERY },
	{ "fqdnrep",	ICMP6_FQDN_REPLY },
	{ "niqry",	ICMP6_NI_QUERY },
	{ "nirep",	ICMP6_NI_REPLY },
	{ "mtraceresp",	MLD_MTRACE_RESP },
	{ "mtrace",	MLD_MTRACE },
	{ "listenrepv2", MLDV2_LISTENER_REPORT },
};

static const struct icmpcodeent icmp_code[] = {
	{ "net-unr",		ICMP_UNREACH,	ICMP_UNREACH_NET },
	{ "host-unr",		ICMP_UNREACH,	ICMP_UNREACH_HOST },
	{ "proto-unr",		ICMP_UNREACH,	ICMP_UNREACH_PROTOCOL },
	{ "port-unr",		ICMP_UNREACH,	ICMP_UNREACH_PORT },
	{ "needfrag",		ICMP_UNREACH,	ICMP_UNREACH_NEEDFRAG },
	{ "srcfail",		ICMP_UNREACH,	ICMP_UNREACH_SRCFAIL },
	{ "net-unk",		ICMP_UNREACH,	ICMP_UNREACH_NET_UNKNOWN },
	{ "host-unk",		ICMP_UNREACH,	ICMP_UNREACH_HOST_UNKNOWN },
	{ "isolate",		ICMP_UNREACH,	ICMP_UNREACH_ISOLATED },
	{ "net-prohib",		ICMP_UNREACH,	ICMP_UNREACH_NET_PROHIB },
	{ "host-prohib",	ICMP_UNREACH,	ICMP_UNREACH_HOST_PROHIB },
	{ "net-tos",		ICMP_UNREACH,	ICMP_UNREACH_TOSNET },
	{ "host-tos",		ICMP_UNREACH,	ICMP_UNREACH_TOSHOST },
	{ "filter-prohib",	ICMP_UNREACH,	ICMP_UNREACH_FILTER_PROHIB },
	{ "host-preced",	ICMP_UNREACH,	ICMP_UNREACH_HOST_PRECEDENCE },
	{ "cutoff-preced",	ICMP_UNREACH,	ICMP_UNREACH_PRECEDENCE_CUTOFF },
	{ "redir-net",		ICMP_REDIRECT,	ICMP_REDIRECT_NET },
	{ "redir-host",		ICMP_REDIRECT,	ICMP_REDIRECT_HOST },
	{ "redir-tos-net",	ICMP_REDIRECT,	ICMP_REDIRECT_TOSNET },
	{ "redir-tos-host",	ICMP_REDIRECT,	ICMP_REDIRECT_TOSHOST },
	{ "normal-adv",		ICMP_ROUTERADVERT, ICMP_ROUTERADVERT_NORMAL },
	{ "common-adv",		ICMP_ROUTERADVERT, ICMP_ROUTERADVERT_NOROUTE_COMMON },
	{ "transit",		ICMP_TIMXCEED,	ICMP_TIMXCEED_INTRANS },
	{ "reassemb",		ICMP_TIMXCEED,	ICMP_TIMXCEED_REASS },
	{ "badhead",		ICMP_PARAMPROB,	ICMP_PARAMPROB_ERRATPTR },
	{ "optmiss",		ICMP_PARAMPROB,	ICMP_PARAMPROB_OPTABSENT },
	{ "badlen",		ICMP_PARAMPROB,	ICMP_PARAMPROB_LENGTH },
	{ "unknown-ind",	ICMP_PHOTURIS,	ICMP_PHOTURIS_UNKNOWN_INDEX },
	{ "auth-fail",		ICMP_PHOTURIS,	ICMP_PHOTURIS_AUTH_FAILED },
	{ "decrypt-fail",	ICMP_PHOTURIS,	ICMP_PHOTURIS_DECRYPT_FAILED },
};

static const struct icmpcodeent icmp6_code[] = {
	{ "admin-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_ADMIN },
	{ "noroute-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOROUTE },
	{ "beyond-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_BEYONDSCOPE },
	{ "addr-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_ADDR },
	{ "port-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOPORT },
	{ "transit", ICMP6_TIME_EXCEEDED, ICMP6_TIME_EXCEED_TRANSIT },
	{ "reassemb", ICMP6_TIME_EXCEEDED, ICMP6_TIME_EXCEED_REASSEMBLY },
	{ "badhead", ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER },
	{ "nxthdr", ICMP6_PARAM_PROB, ICMP6_PARAMPROB_NEXTHEADER },
	{ "redironlink", ND_REDIRECT, ND_REDIRECT_ONLINK },
	{ "redirrouter", ND_REDIRECT, ND_REDIRECT_ROUTER },
};

static int
geticmptypebyname(char *w, uint8_t aid)
{
	size_t	i;

	switch (aid) {
	case AID_INET:
		for (i = 0; i < nitems(icmp_type); i++) {
			if (!strcmp(w, icmp_type[i].name))
				return (icmp_type[i].type);
		}
		break;
	case AID_INET6:
		for (i = 0; i < nitems(icmp6_type); i++) {
			if (!strcmp(w, icmp6_type[i].name))
				return (icmp6_type[i].type);
		}
		break;
	}
	return -1;
}

static int
geticmpcodebyname(u_long type, char *w, uint8_t aid)
{
	size_t	i;

	switch (aid) {
	case AID_INET:
		for (i = 0; i < nitems(icmp_code); i++) {
			if (type == icmp_code[i].type &&
			    !strcmp(w, icmp_code[i].name))
				return (icmp_code[i].code);
		}
		break;
	case AID_INET6:
		for (i = 0; i < nitems(icmp6_code); i++) {
			if (type == icmp6_code[i].type &&
			    !strcmp(w, icmp6_code[i].name))
				return (icmp6_code[i].code);
		}
		break;
	}
	return -1;
}

static int
merge_auth_conf(struct auth_config *to, struct auth_config *from)
{
	if (to->method != 0) {
		/* extra magic for manual ipsec rules */
		if (to->method == from->method &&
		    (to->method == AUTH_IPSEC_MANUAL_ESP ||
		    to->method == AUTH_IPSEC_MANUAL_AH)) {
			if (to->spi_in == 0 && from->spi_in != 0) {
				to->spi_in = from->spi_in;
				to->auth_alg_in = from->auth_alg_in;
				to->enc_alg_in = from->enc_alg_in;
				memcpy(to->enc_key_in, from->enc_key_in,
				    sizeof(to->enc_key_in));
				to->enc_keylen_in = from->enc_keylen_in;
				to->auth_keylen_in = from->auth_keylen_in;
				return 1;
			} else if (to->spi_out == 0 && from->spi_out != 0) {
				to->spi_out = from->spi_out;
				to->auth_alg_out = from->auth_alg_out;
				to->enc_alg_out = from->enc_alg_out;
				memcpy(to->enc_key_out, from->enc_key_out,
				    sizeof(to->enc_key_out));
				to->enc_keylen_out = from->enc_keylen_out;
				to->auth_keylen_out = from->auth_keylen_out;
				return 1;
			}
		}
		yyerror("auth method cannot be redefined");
		return 0;
	}
	*to = *from;
	return 1;
}

