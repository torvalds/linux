/*	$OpenBSD: parse.y,v 1.74 2024/08/22 08:17:54 florian Exp $ */

/*
 * Copyright (c) 2013, 2015, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2004, 2005, 2008 Esben Norby <norby@openbsd.org>
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
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if_types.h>
#include <limits.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <netdb.h>

#include "ldpd.h"
#include "ldpe.h"
#include "lde.h"
#include "log.h"

struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t			 ungetpos;
	size_t			 ungetsize;
	u_char			*ungetbuf;
	int			 eof_reached;
	int			 lineno;
	int			 errors;
};
TAILQ_HEAD(files, file);

struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
TAILQ_HEAD(symhead, sym);

struct config_defaults {
	uint16_t	keepalive;
	uint16_t	lhello_holdtime;
	uint16_t	lhello_interval;
	uint16_t	thello_holdtime;
	uint16_t	thello_interval;
	union ldpd_addr	trans_addr;
	int		afflags;
	uint8_t		pwflags;
};

typedef struct {
	union {
		int64_t		 number;
		char		*string;
		struct in_addr	 routerid;
		struct ldp_auth	*auth;
	} v;
	int lineno;
} YYSTYPE;

static int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
static int		 kw_cmp(const void *, const void *);
static int		 lookup(char *);
static int		 igetc(void);
static int		 lgetc(int);
void			 lungetc(int);
static int		 findeol(void);
static int		 yylex(void);
static int		 check_file_secrecy(int, const char *);
static struct file	*pushfile(const char *, int);
static int		 popfile(void);
static int		 yyparse(void);
static int		 symset(const char *, const char *, int);
static char		*symget(const char *);
static struct iface	*conf_get_if(struct kif *);
static struct tnbr	*conf_get_tnbr(union ldpd_addr *);
static struct nbr_params *conf_get_nbrp(struct in_addr);
static struct l2vpn	*conf_get_l2vpn(char *);
static struct l2vpn_if	*conf_get_l2vpn_if(struct l2vpn *, struct kif *);
static struct l2vpn_pw	*conf_get_l2vpn_pw(struct l2vpn *, struct kif *);
int			 conf_check_rdomain(unsigned int);
static void		 clear_config(struct ldpd_conf *xconf);
static uint32_t		 get_rtr_id(void);
static int		 get_address(const char *, union ldpd_addr *);
static int		 get_af_address(const char *, int *, union ldpd_addr *);
static int		 str2key(char *, const char *, int);

static struct file		*file, *topfile;
static struct files		 files = TAILQ_HEAD_INITIALIZER(files);
static struct symhead		 symhead = TAILQ_HEAD_INITIALIZER(symhead);
static struct ldpd_conf		*conf;
static int			 errors;

static int			 af;
static struct ldpd_af_conf	*af_conf;
static struct iface		*iface;
static struct iface_af		*ia;
static struct tnbr		*tnbr;
static struct nbr_params	*nbrp;
static struct l2vpn		*l2vpn;
static struct l2vpn_pw		*pw;

static struct config_defaults	 globaldefs;
static struct config_defaults	 afdefs;
static struct config_defaults	 ifacedefs;
static struct config_defaults	 tnbrdefs;
static struct config_defaults	 pwdefs;
static struct config_defaults	*defs;

%}

%token	INTERFACE TNEIGHBOR ROUTERID FIBUPDATE RDOMAIN EXPNULL
%token	LHELLOHOLDTIME LHELLOINTERVAL
%token	THELLOHOLDTIME THELLOINTERVAL
%token	THELLOACCEPT AF IPV4 IPV6 INET INET6 GTSMENABLE GTSMHOPS
%token	KEEPALIVE TRANSADDRESS TRANSPREFERENCE DSCISCOINTEROP
%token	NEIGHBOR
%token	TCP MD5SIG PASSWORD KEY
%token	L2VPN TYPE VPLS PWTYPE MTU BRIDGE
%token	ETHERNET ETHERNETTAGGED STATUSTLV CONTROLWORD
%token	PSEUDOWIRE NEIGHBORID NEIGHBORADDR PWID
%token	EXTTAG
%token	YES NO
%token	INCLUDE
%token	ERROR
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.number>	yesno ldp_af l2vpn_type pw_type
%type	<v.string>	string
%type	<v.routerid>	routerid
%type	<v.auth>	auth tcpmd5 optnbrprefix

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar conf_main '\n'
		| grammar varset '\n'
		| grammar af '\n'
		| grammar neighbor '\n'
		| grammar l2vpn '\n'
		| grammar error '\n'		{ file->errors++; }
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2,
			    !(global.cmd_opts & LDPD_OPT_NOACTION))) == NULL) {
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

routerid	: STRING {
			if (inet_pton(AF_INET, $1, &$$) != 1) {
				yyerror("%s: error parsing router id", $1);
				free($1);
				YYERROR;
			}
			if (bad_addr_v4($$)) {
				yyerror("%s: invalid router id", $1);
				free($1);
				YYERROR;
			}
			free($1);

			break;
		}
		;

yesno		: YES	{ $$ = 1; }
		| NO	{ $$ = 0; }
		;

ldp_af		: IPV4	{ $$ = AF_INET; }
		| IPV6	{ $$ = AF_INET6; }
		;

l2vpn_type	: VPLS	{ $$ = L2VPN_TYPE_VPLS; }
		;

pw_type		: ETHERNET		{ $$ = PW_TYPE_ETHERNET; }
		| ETHERNETTAGGED	{ $$ = PW_TYPE_ETHERNET_TAGGED; }
		;

varset		: STRING '=' string {
			char *s = $1;
			if (global.cmd_opts & LDPD_OPT_VERBOSE)
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

conf_main	: ROUTERID routerid {
			conf->rtr_id = $2;
		}
		| FIBUPDATE yesno {
			if ($2 == 0)
				conf->flags |= F_LDPD_NO_FIB_UPDATE;
			else
				conf->flags &= ~F_LDPD_NO_FIB_UPDATE;
		}
		| RDOMAIN NUMBER {
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rdomain");
				YYERROR;
			}
			conf->rdomain = $2;
		}
		| TRANSPREFERENCE ldp_af {
			conf->trans_pref = $2;

			switch (conf->trans_pref) {
			case AF_INET:
				conf->trans_pref = DUAL_STACK_LDPOV4;
				break;
			case AF_INET6:
				conf->trans_pref = DUAL_STACK_LDPOV6;
				break;
			default:
				yyerror("invalid address-family");
				YYERROR;
			}
		}
		| DSCISCOINTEROP yesno {
			if ($2 == 1)
				conf->flags |= F_LDPD_DS_CISCO_INTEROP;
			else
				conf->flags &= ~F_LDPD_DS_CISCO_INTEROP;
		}
		| auth {
			LIST_INSERT_HEAD(&conf->auth_list, $1, entry);
		}
		| af_defaults
		| iface_defaults
		| tnbr_defaults
		;

af		: AF ldp_af {
			af = $2;
			switch (af) {
			case AF_INET:
				af_conf = &conf->ipv4;
				break;
			case AF_INET6:
				af_conf = &conf->ipv6;
				break;
			default:
				yyerror("invalid address-family");
				YYERROR;
			}

			afdefs = *defs;
			defs = &afdefs;
		} af_block {
			af_conf->keepalive = defs->keepalive;
			af_conf->thello_holdtime = defs->thello_holdtime;
			af_conf->thello_interval = defs->thello_interval;
			af_conf->flags = defs->afflags;
			af_conf->flags |= F_LDPD_AF_ENABLED;
			af_conf = NULL;
			af = AF_UNSPEC;
			defs = &globaldefs;
		}
		;

af_block	: '{' optnl afopts_l '}'
		| '{' optnl '}'
		|
		;

afopts_l	: afopts_l afoptsl nl
		| afoptsl optnl
		;

afoptsl		:  TRANSADDRESS STRING {
			if (get_address($2, &af_conf->trans_addr) == -1) {
				yyerror("error parsing transport-address");
				free($2);
				YYERROR;
			}
			free($2);
			if (bad_addr(af, &af_conf->trans_addr)) {
				yyerror("invalid transport-address");
				YYERROR;
			}
			if (af == AF_INET6 &&
			   IN6_IS_SCOPE_EMBED(&af_conf->trans_addr.v6)) {
				yyerror("ipv6 transport-address can not be "
				    "link-local");
				YYERROR;
			}
		}
		| GTSMENABLE yesno {
			if ($2 == 0)
				defs->afflags |= F_LDPD_AF_NO_GTSM;
		}
		| af_defaults
		| iface_defaults
		| tnbr_defaults
		| interface
		| tneighbor
		;

af_defaults	: THELLOACCEPT yesno {
			if ($2 == 0)
				defs->afflags &= ~F_LDPD_AF_THELLO_ACCEPT;
			else
				defs->afflags |= F_LDPD_AF_THELLO_ACCEPT;
		}
		| EXPNULL yesno {
			if ($2 == 0)
				defs->afflags &= ~F_LDPD_AF_EXPNULL;
			else
				defs->afflags |= F_LDPD_AF_EXPNULL;
		}
		| KEEPALIVE NUMBER {
			if ($2 < MIN_KEEPALIVE || $2 > MAX_KEEPALIVE) {
				yyerror("keepalive out of range (%d-%d)",
				    MIN_KEEPALIVE, MAX_KEEPALIVE);
				YYERROR;
			}
			defs->keepalive = $2;
		}
		;

iface_defaults	: LHELLOHOLDTIME NUMBER {
			if ($2 < MIN_HOLDTIME || $2 > MAX_HOLDTIME) {
				yyerror("hello-holdtime out of range (%d-%d)",
				    MIN_HOLDTIME, MAX_HOLDTIME);
				YYERROR;
			}
			defs->lhello_holdtime = $2;
		}
		| LHELLOINTERVAL NUMBER {
			if ($2 < MIN_HELLO_INTERVAL ||
			    $2 > MAX_HELLO_INTERVAL) {
				yyerror("hello-interval out of range (%d-%d)",
				    MIN_HELLO_INTERVAL, MAX_HELLO_INTERVAL);
				YYERROR;
			}
			defs->lhello_interval = $2;
		}
		;

tnbr_defaults	: THELLOHOLDTIME NUMBER {
			if ($2 < MIN_HOLDTIME || $2 > MAX_HOLDTIME) {
				yyerror("hello-holdtime out of range (%d-%d)",
				    MIN_HOLDTIME, MAX_HOLDTIME);
				YYERROR;
			}
			defs->thello_holdtime = $2;
		}
		| THELLOINTERVAL NUMBER {
			if ($2 < MIN_HELLO_INTERVAL ||
			    $2 > MAX_HELLO_INTERVAL) {
				yyerror("hello-interval out of range (%d-%d)",
				    MIN_HELLO_INTERVAL, MAX_HELLO_INTERVAL);
				YYERROR;
			}
			defs->thello_interval = $2;
		}
		;

tcpmd5		: TCP MD5SIG PASSWORD STRING {
			size_t len;

			$$ = calloc(1, sizeof(*$$));
			if ($$ == NULL) {
				free($4);
				yyerror("unable to allocate md5 key");
				YYERROR;
			}

			len = strlen($4);
			if (len > sizeof($$->md5key)) {
				free($$);
				free($4);
				yyerror("tcp md5sig password too long: "
				    "max %zu", sizeof($$->md5key));
				YYERROR;
			}

			memcpy($$->md5key, $4, len);
			$$->md5key_len = len;

			free($4);
		}
		| TCP MD5SIG KEY STRING {
			int len;

			$$ = calloc(1, sizeof(*$$));
			if ($$ == NULL) {
				free($4);
				yyerror("unable to allocate md5 key");
				YYERROR;
			}

			len = str2key($$->md5key, $4, sizeof($$->md5key));
			if (len == -1) {
				free($$);
				free($4);
				yyerror("invalid hex string");
				YYERROR;
			}
			if ((size_t)len > sizeof($$->md5key_len)) {
				free($$);
				free($4);
				yyerror("tcp md5sig key too long: %d "
				    "max %zu", len, sizeof($$->md5key));
				YYERROR;
			}

			$$->md5key_len = len;

			free($4);
		}
		| NO TCP MD5SIG {
			$$ = calloc(1, sizeof(*$$));
			if ($$ == NULL) {
				yyerror("unable to allocate no md5 key");
				YYERROR;
			}
			$$->md5key_len = 0;
		}
		;

optnbrprefix	: STRING {
			$$ = calloc(1, sizeof(*$$));
			if ($$ == NULL) {
				yyerror("unable to allocate auth");
				free($1);
				YYERROR;
			}

			$$->idlen = inet_net_pton(AF_INET, $1,
			    &$$->id, sizeof($$->id));
			if ($$->idlen == -1) {
				yyerror("%s: %s", $1, strerror(errno));
				free($1);
				YYERROR;
			}
		}
		| /* empty */ {
			$$ = NULL;
		}
		;

auth		: tcpmd5 optnbrprefix {
			$$ = $1;
			if ($2 != NULL) {
				$$->id = $2->id;
				$$->idlen = $2->idlen;
				free($2);
			} else {
				$$->id.s_addr = 0;
				$$->idlen = 0;
			}
		}
		;

nbr_opts	: KEEPALIVE NUMBER {
			if ($2 < MIN_KEEPALIVE || $2 > MAX_KEEPALIVE) {
				yyerror("keepalive out of range (%d-%d)",
				    MIN_KEEPALIVE, MAX_KEEPALIVE);
				YYERROR;
			}
			nbrp->keepalive = $2;
			nbrp->flags |= F_NBRP_KEEPALIVE;
		}
		| tcpmd5 {
			/* this is syntactic sugar... */
			$1->id = nbrp->lsr_id;
			$1->idlen = 32;
			LIST_INSERT_HEAD(&conf->auth_list, $1, entry);
		}
		| GTSMENABLE yesno {
			nbrp->flags |= F_NBRP_GTSM;
			nbrp->gtsm_enabled = $2;
		}
		| GTSMHOPS NUMBER {
			if ($2 < 1 || $2 > 255) {
				yyerror("invalid number of hops %lld", $2);
				YYERROR;
			}
			nbrp->gtsm_hops = $2;
			nbrp->flags |= F_NBRP_GTSM_HOPS;
		}
		;

pw_defaults	: STATUSTLV yesno {
			if ($2 == 1)
				defs->pwflags |= F_PW_STATUSTLV_CONF;
			else
				defs->pwflags &= ~F_PW_STATUSTLV_CONF;
		}
		| CONTROLWORD yesno {
			if ($2 == 1)
				defs->pwflags |= F_PW_CWORD_CONF;
			else
				defs->pwflags &= ~F_PW_CWORD_CONF;
		}
		;

pwopts		: PWID NUMBER {
			if ($2 < MIN_PWID_ID ||
			    $2 > MAX_PWID_ID) {
				yyerror("pw-id out of range (%d-%d)",
				    MIN_PWID_ID, MAX_PWID_ID);
				YYERROR;
			}

			pw->pwid = $2;
		}
		| NEIGHBORID routerid {
			pw->lsr_id = $2;
		}
		| NEIGHBORADDR STRING {
			int		 family;
			union ldpd_addr	 addr;

			if (get_af_address($2, &family, &addr) == -1) {
				yyerror("error parsing neighbor address");
				free($2);
				YYERROR;
			}
			free($2);
			if (bad_addr(family, &addr)) {
				yyerror("invalid neighbor address");
				YYERROR;
			}
			if (family == AF_INET6 &&
			    IN6_IS_SCOPE_EMBED(&addr.v6)) {
				yyerror("neighbor address can not be "
				    "link-local");
				YYERROR;
			}

			pw->af = family;
			pw->addr = addr;
		}
		| pw_defaults
		;

pseudowire	: PSEUDOWIRE STRING {
			struct kif	*kif;

			if ((kif = kif_findname($2)) == NULL) {
				yyerror("unknown interface %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			if (kif->if_type != IFT_MPLSTUNNEL &&
			    kmpw_find(kif->ifname) == -1) {
				yyerror("unsupported interface type on "
				    "interface %s", kif->ifname);
				YYERROR;
			}

			pw = conf_get_l2vpn_pw(l2vpn, kif);
			if (pw == NULL)
				YYERROR;

			pwdefs = *defs;
			defs = &pwdefs;
		} pw_block {
			struct l2vpn	*l;
			struct l2vpn_pw *p;

			/* check for errors */
			if (pw->pwid == 0) {
				yyerror("missing pseudowire id");
				YYERROR;
			}
			if (pw->lsr_id.s_addr == INADDR_ANY) {
				yyerror("missing pseudowire neighbor-id");
				YYERROR;
			}
			LIST_FOREACH(l, &conf->l2vpn_list, entry) {
				LIST_FOREACH(p, &l->pw_list, entry) {
					if (pw != p &&
					    pw->pwid == p->pwid &&
					    pw->af == p->af &&
					    pw->lsr_id.s_addr ==
					    p->lsr_id.s_addr) {
						yyerror("pseudowire already "
						    "configured");
						YYERROR;
					}
				}
			}

			/*
			 * If the neighbor address is not specified, use the
			 * neighbor id.
			 */
			if (pw->af == AF_UNSPEC) {
				pw->af = AF_INET;
				pw->addr.v4 = pw->lsr_id;
			}

			pw->flags = defs->pwflags;
			pw = NULL;
			defs = &globaldefs;
		}
		;

pw_block	: '{' optnl pwopts_l '}'
		| '{' optnl '}'
		| /* nothing */
		;

pwopts_l	: pwopts_l pwopts nl
		| pwopts optnl
		;

l2vpnopts	: PWTYPE pw_type {
			l2vpn->pw_type = $2;
		}
		| MTU NUMBER {
			if ($2 < MIN_L2VPN_MTU ||
			    $2 > MAX_L2VPN_MTU) {
				yyerror("l2vpn mtu out of range (%d-%d)",
				    MIN_L2VPN_MTU, MAX_L2VPN_MTU);
				YYERROR;
			}
			l2vpn->mtu = $2;
		}
		| pw_defaults
		| BRIDGE STRING {
			struct l2vpn	 *l;
			struct kif	 *kif;

			if ((kif = kif_findname($2)) == NULL) {
				yyerror("unknown interface %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			if (l2vpn->br_ifindex != 0) {
				yyerror("bridge interface cannot be "
				    "redefined on l2vpn %s", l2vpn->name);
				YYERROR;
			}

			if (kif->if_type != IFT_BRIDGE) {
				yyerror("unsupported interface type on "
				    "interface %s", kif->ifname);
				YYERROR;
			}

			LIST_FOREACH(l, &conf->l2vpn_list, entry) {
				if (l->br_ifindex == kif->ifindex) {
					yyerror("bridge %s is already being "
					    "used by l2vpn %s", kif->ifname,
					    l->name);
					YYERROR;
				}
			}

			l2vpn->br_ifindex = kif->ifindex;
			strlcpy(l2vpn->br_ifname, kif->ifname,
			    sizeof(l2vpn->br_ifname));
		}
		| INTERFACE STRING {
			struct kif	*kif;
			struct l2vpn_if	*lif;

			if ((kif = kif_findname($2)) == NULL) {
				yyerror("unknown interface %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			lif = conf_get_l2vpn_if(l2vpn, kif);
			if (lif == NULL)
				YYERROR;
		}
		| pseudowire
		;

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl		/* one newline or more */
		;

interface	: INTERFACE STRING	{
			struct kif	*kif;

			if ((kif = kif_findname($2)) == NULL) {
				yyerror("unknown interface %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			iface = conf_get_if(kif);
			if (iface == NULL)
				YYERROR;

			ia = iface_af_get(iface, af);
			if (ia->enabled) {
				yyerror("interface %s already configured for "
				    "address-family %s", kif->ifname,
				    af_name(af));
				YYERROR;
			}
			ia->enabled = 1;

			ifacedefs = *defs;
			defs = &ifacedefs;
		} interface_block {
			ia->hello_holdtime = defs->lhello_holdtime;
			ia->hello_interval = defs->lhello_interval;
			iface = NULL;
			defs = &afdefs;
		}
		;

interface_block	: '{' optnl interfaceopts_l '}'
		| '{' optnl '}'
		| /* nothing */
		;

interfaceopts_l	: interfaceopts_l iface_defaults nl
		| iface_defaults optnl
		;

tneighbor	: TNEIGHBOR STRING	{
			union ldpd_addr	 addr;

			if (get_address($2, &addr) == -1) {
				yyerror("error parsing targeted-neighbor "
				    "address");
				free($2);
				YYERROR;
			}
			free($2);
			if (bad_addr(af, &addr)) {
				yyerror("invalid targeted-neighbor address");
				YYERROR;
			}
			if (af == AF_INET6 &&
			   IN6_IS_SCOPE_EMBED(&addr.v6)) {
				yyerror("targeted-neighbor address can not be "
				    "link-local");
				YYERROR;
			}

			tnbr = conf_get_tnbr(&addr);
			if (tnbr == NULL)
				YYERROR;

			tnbrdefs = *defs;
			defs = &tnbrdefs;
		} tneighbor_block {
			tnbr->hello_holdtime = defs->thello_holdtime;
			tnbr->hello_interval = defs->thello_interval;
			tnbr = NULL;
			defs = &afdefs;
		}
		;

tneighbor_block	: '{' optnl tneighboropts_l '}'
		| '{' optnl '}'
		| /* nothing */
		;

tneighboropts_l	: tneighboropts_l tnbr_defaults nl
		| tnbr_defaults optnl
		;

neighbor	: NEIGHBOR routerid	{
			nbrp = conf_get_nbrp($2);
			if (nbrp == NULL)
				YYERROR;
		} neighbor_block {
			nbrp = NULL;
		}
		;

neighbor_block	: '{' optnl neighboropts_l '}'
		| '{' optnl '}'
		| /* nothing */
		;

neighboropts_l	: neighboropts_l nbr_opts nl
		| nbr_opts optnl
		;

l2vpn		: L2VPN STRING TYPE l2vpn_type {
			l2vpn = conf_get_l2vpn($2);
			if (l2vpn == NULL)
				YYERROR;
			l2vpn->type = $4;
		} l2vpn_block {
			l2vpn = NULL;
		}
		;

l2vpn_block	: '{' optnl l2vpnopts_l '}'
		| '{' optnl '}'
		| /* nothing */
		;

l2vpnopts_l	: l2vpnopts_l l2vpnopts nl
		| l2vpnopts optnl
		;

%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

static int
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

static int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

static int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{"address-family",		AF},
		{"bridge",			BRIDGE},
		{"control-word",		CONTROLWORD},
		{"ds-cisco-interop",		DSCISCOINTEROP},
		{"ethernet",			ETHERNET},
		{"ethernet-tagged",		ETHERNETTAGGED},
		{"explicit-null",		EXPNULL},
		{"fib-update",			FIBUPDATE},
		{"gtsm-enable",			GTSMENABLE},
		{"gtsm-hops",			GTSMHOPS},
		{"include",			INCLUDE},
		{"inet",			INET},
		{"inet6",			INET6},
		{"interface",			INTERFACE},
		{"ipv4",			IPV4},
		{"ipv6",			IPV6},
		{"keepalive",			KEEPALIVE},
		{"key",				KEY},
		{"l2vpn",			L2VPN},
		{"link-hello-holdtime",		LHELLOHOLDTIME},
		{"link-hello-interval",		LHELLOINTERVAL},
		{"md5sig",			MD5SIG},
		{"mtu",				MTU},
		{"neighbor",			NEIGHBOR},
		{"neighbor-addr",		NEIGHBORADDR},
		{"neighbor-id",			NEIGHBORID},
		{"no",				NO},
		{"password",			PASSWORD},
		{"pseudowire",			PSEUDOWIRE},
		{"pw-id",			PWID},
		{"pw-type",			PWTYPE},
		{"rdomain",			RDOMAIN},
		{"router-id",			ROUTERID},
		{"status-tlv",			STATUSTLV},
		{"targeted-hello-accept",	THELLOACCEPT},
		{"targeted-hello-holdtime",	THELLOHOLDTIME},
		{"targeted-hello-interval",	THELLOINTERVAL},
		{"targeted-neighbor",		TNEIGHBOR},
		{"tcp",				TCP},
		{"transport-address",		TRANSADDRESS},
		{"transport-preference",	TRANSPREFERENCE},
		{"type",			TYPE},
		{"vpls",			VPLS},
		{"yes",				YES}
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

static int
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

static int
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

static int
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

static int
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

static struct file *
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

static int
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

struct ldpd_conf *
parse_config(char *filename)
{
	struct sym	*sym, *next;

	conf = config_new_empty();
	conf->rdomain = 0;
	conf->trans_pref = DUAL_STACK_LDPOV6;

	defs = &globaldefs;
	defs->keepalive = DEFAULT_KEEPALIVE;
	defs->lhello_holdtime = LINK_DFLT_HOLDTIME;
	defs->lhello_interval = DEFAULT_HELLO_INTERVAL;
	defs->thello_holdtime = TARGETED_DFLT_HOLDTIME;
	defs->thello_interval = DEFAULT_HELLO_INTERVAL;
	defs->pwflags = F_PW_STATUSTLV_CONF|F_PW_CWORD_CONF;

	if ((file = pushfile(filename,
	    !(global.cmd_opts & LDPD_OPT_NOACTION))) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((global.cmd_opts & LDPD_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	/* check that all interfaces belong to the configured rdomain */
	errors += conf_check_rdomain(conf->rdomain);

	/* free global config defaults */
	if (errors) {
		clear_config(conf);
		return (NULL);
	}

	if (conf->rtr_id.s_addr == INADDR_ANY)
		conf->rtr_id.s_addr = get_rtr_id();

	/* if the ipv4 transport-address is not set, use the router-id */
	if ((conf->ipv4.flags & F_LDPD_AF_ENABLED) &&
	    conf->ipv4.trans_addr.v4.s_addr == INADDR_ANY)
		conf->ipv4.trans_addr.v4 = conf->rtr_id;

	return (conf);
}

static int
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

static char *
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

static struct iface *
conf_get_if(struct kif *kif)
{
	struct iface	*i;
	struct l2vpn	*l;

	if (kif->if_type == IFT_LOOP ||
	    kif->if_type == IFT_CARP ||
	    kif->if_type == IFT_BRIDGE ||
	    kif->if_type == IFT_MPLSTUNNEL) {
		yyerror("unsupported interface type on interface %s",
		    kif->ifname);
		return (NULL);
	}

	LIST_FOREACH(l, &conf->l2vpn_list, entry)
		if (l2vpn_if_find(l, kif->ifindex)) {
			yyerror("interface %s already configured under "
			    "l2vpn %s", kif->ifname, l->name);
			return (NULL);
		}

	LIST_FOREACH(i, &conf->iface_list, entry)
		if (i->ifindex == kif->ifindex)
			return (i);

	i = if_new(kif);
	LIST_INSERT_HEAD(&conf->iface_list, i, entry);
	return (i);
}

static struct tnbr *
conf_get_tnbr(union ldpd_addr *addr)
{
	struct tnbr	*t;

	t = tnbr_find(conf, af, addr);
	if (t) {
		yyerror("targeted neighbor %s already configured",
		    log_addr(af, addr));
		return (NULL);
	}

	t = tnbr_new(conf, af, addr);
	t->flags |= F_TNBR_CONFIGURED;
	LIST_INSERT_HEAD(&conf->tnbr_list, t, entry);
	return (t);
}

static struct nbr_params *
conf_get_nbrp(struct in_addr lsr_id)
{
	struct nbr_params	*n;

	LIST_FOREACH(n, &conf->nbrp_list, entry) {
		if (n->lsr_id.s_addr == lsr_id.s_addr) {
			yyerror("neighbor %s already configured",
			    inet_ntoa(lsr_id));
			return (NULL);
		}
	}

	n = nbr_params_new(lsr_id);
	LIST_INSERT_HEAD(&conf->nbrp_list, n, entry);
	return (n);
}

static struct l2vpn *
conf_get_l2vpn(char *name)
{
	struct l2vpn	 *l;

	if (l2vpn_find(conf, name)) {
		yyerror("l2vpn %s already configured", name);
		return (NULL);
	}

	l = l2vpn_new(name);
	LIST_INSERT_HEAD(&conf->l2vpn_list, l, entry);
	return (l);
}

static struct l2vpn_if *
conf_get_l2vpn_if(struct l2vpn *l, struct kif *kif)
{
	struct iface	*i;
	struct l2vpn	*ltmp;
	struct l2vpn_if	*f;

	if (kif->if_type == IFT_LOOP ||
	    kif->if_type == IFT_CARP ||
	    kif->if_type == IFT_BRIDGE ||
	    kif->if_type == IFT_MPLSTUNNEL) {
		yyerror("unsupported interface type on interface %s",
		    kif->ifname);
		return (NULL);
	}

	LIST_FOREACH(ltmp, &conf->l2vpn_list, entry)
		if (l2vpn_if_find(ltmp, kif->ifindex)) {
			yyerror("interface %s already configured under "
			    "l2vpn %s", kif->ifname, ltmp->name);
			return (NULL);
		}

	LIST_FOREACH(i, &conf->iface_list, entry) {
		if (i->ifindex == kif->ifindex) {
			yyerror("interface %s already configured",
			    kif->ifname);
			return (NULL);
		}
	}

	f = l2vpn_if_new(l, kif);
	LIST_INSERT_HEAD(&l2vpn->if_list, f, entry);
	return (f);
}

static struct l2vpn_pw *
conf_get_l2vpn_pw(struct l2vpn *l, struct kif *kif)
{
	struct l2vpn	*ltmp;
	struct l2vpn_pw	*p;

	LIST_FOREACH(ltmp, &conf->l2vpn_list, entry) {
		if (l2vpn_pw_find(ltmp, kif->ifindex)) {
			yyerror("pseudowire %s is already being "
			    "used by l2vpn %s", kif->ifname, ltmp->name);
			return (NULL);
		}
	}

	p = l2vpn_pw_new(l, kif);
	LIST_INSERT_HEAD(&l2vpn->pw_list, p, entry);
	return (p);
}

int
conf_check_rdomain(unsigned int rdomain)
{
	struct iface	*i;
	int		 errs = 0;

	LIST_FOREACH(i, &conf->iface_list, entry) {
		if (i->rdomain != rdomain) {
			logit(LOG_CRIT, "interface %s not in rdomain %u",
			    i->name, rdomain);
			errs++;
		}
	}

	return (errs);
}

static void
clear_config(struct ldpd_conf *xconf)
{
	struct iface		*i;
	struct tnbr		*t;
	struct nbr_params	*n;
	struct l2vpn		*l;
	struct l2vpn_if		*f;
	struct l2vpn_pw		*p;

	while ((i = LIST_FIRST(&xconf->iface_list)) != NULL) {
		LIST_REMOVE(i, entry);
		free(i);
	}

	while ((t = LIST_FIRST(&xconf->tnbr_list)) != NULL) {
		LIST_REMOVE(t, entry);
		free(t);
	}

	while ((n = LIST_FIRST(&xconf->nbrp_list)) != NULL) {
		LIST_REMOVE(n, entry);
		free(n);
	}

	while ((l = LIST_FIRST(&xconf->l2vpn_list)) != NULL) {
		while ((f = LIST_FIRST(&l->if_list)) != NULL) {
			LIST_REMOVE(f, entry);
			free(f);
		}
		while ((p = LIST_FIRST(&l->pw_list)) != NULL) {
			LIST_REMOVE(p, entry);
			free(p);
		}
		LIST_REMOVE(l, entry);
		free(l);
	}

	free(xconf);
}

static uint32_t
get_rtr_id(void)
{
	struct ifaddrs		*ifap, *ifa;
	uint32_t		 ip = 0, cur, localnet;

	localnet = htonl(INADDR_LOOPBACK & IN_CLASSA_NET);

	if (getifaddrs(&ifap) == -1) {
		log_warn("getifaddrs");
		return (0);
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (strncmp(ifa->ifa_name, "carp", 4) == 0)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;
		cur = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
		if ((cur & localnet) == localnet)	/* skip 127/8 */
			continue;
		if (ntohl(cur) < ntohl(ip) || ip == 0)
			ip = cur;
	}
	freeifaddrs(ifap);

	return (ip);
}

static int
get_address(const char *s, union ldpd_addr *addr)
{
	switch (af) {
	case AF_INET:
		if (inet_pton(AF_INET, s, &addr->v4) != 1)
			return (-1);
		break;
	case AF_INET6:
		if (inet_pton(AF_INET6, s, &addr->v6) != 1)
			return (-1);
		break;
	default:
		return (-1);
	}

	return (0);
}

static int
get_af_address(const char *s, int *family, union ldpd_addr *addr)
{
	if (inet_pton(AF_INET, s, &addr->v4) == 1) {
		*family = AF_INET;
		return (0);
	}

	if (inet_pton(AF_INET6, s, &addr->v6) == 1) {
		*family = AF_INET6;
		return (0);
	}

	return (-1);
}

static int
hexchar(int ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a');
	if (ch >= 'A' && ch <= 'F')
		return (ch - 'A');

	return (-1);
}

static int
str2key(char *dst, const char *src, int dstlen)
{
	int		i = 0;
	int		digit;

	while (*src != '\0') {
		digit = hexchar(*src);
		if (digit == -1)
			return (-1);

		if (i < dstlen)
			*dst = digit << 4;

		src++;
		if (*src == '\0')
			return (-1);
		digit = hexchar(*src);
		if (digit == -1)
			return (-1);

		if (i < dstlen)
			*dst |= digit;

		src++;
		i++;
	}

	return (i);
}
