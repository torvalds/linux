%{
/*	$NetBSD: cfparse.y,v 1.4 1995/12/10 10:06:57 mycroft Exp $	*/

/*
 * Configuration file parser for mrouted.
 *
 * Written by Bill Fenner, NRL, 1994
 * Copyright (c) 1994
 * Naval Research Laboratory (NRL/CCS)
 *                    and the
 * Defense Advanced Research Projects Agency (DARPA)
 *
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright notice and
 * this permission notice appear in all copies of the software, derivative
 * works or modified versions, and any portions thereof, and that both notices
 * appear in supporting documentation.
 *
 * NRL AND DARPA ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION AND
 * DISCLAIM ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM
 * THE USE OF THIS SOFTWARE.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include <netdb.h>
#include <ifaddrs.h>

/*
 * Local function declarations
 */
static void		fatal(const char *fmt, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
static void		warn(const char *fmt, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
static void		yyerror(char *s);
static char *		next_word(void);
static int		yylex(void);
static u_int32_t	valid_if(char *s);
static const char *	ifconfaddr(u_int32_t a);
int			yyparse(void);

static FILE *f;

extern int udp_socket;
char *configfilename = _PATH_MROUTED_CONF;

extern int cache_lifetime;
extern int max_prune_lifetime;

static int lineno;

static struct uvif *v;

static int order;

struct addrmask {
	u_int32_t	addr;
	int	mask;
};

struct boundnam {
	char		*name;
	struct addrmask	 bound;
};

#define MAXBOUNDS 20

struct boundnam boundlist[MAXBOUNDS];	/* Max. of 20 named boundaries */
int numbounds = 0;			/* Number of named boundaries */

%}

%union
{
	int num;
	char *ptr;
	struct addrmask addrmask;
	u_int32_t addr;
};

%token CACHE_LIFETIME PRUNING
%token PHYINT TUNNEL NAME
%token DISABLE IGMPV1 SRCRT
%token METRIC THRESHOLD RATE_LIMIT BOUNDARY NETMASK ALTNET
%token <num> BOOLEAN
%token <num> NUMBER
%token <ptr> STRING
%token <addrmask> ADDRMASK
%token <addr> ADDR

%type <addr> interface addrname
%type <addrmask> bound boundary addrmask

%start conf

%%

conf	: stmts
	;

stmts	: /* Empty */
	| stmts stmt
	;

stmt	: error
	| PHYINT interface		{

			vifi_t vifi;

			if (order)
			    fatal("phyints must appear before tunnels");

			for (vifi = 0, v = uvifs;
			     vifi < numvifs;
			     ++vifi, ++v)
			    if (!(v->uv_flags & VIFF_TUNNEL) &&
				$2 == v->uv_lcl_addr)
				break;

			if (vifi == numvifs)
			    fatal("%s is not a configured interface",
				inet_fmt($2,s1));

					}
		ifmods
	| TUNNEL interface addrname	{
			const char *ifname;
			struct ifreq ffr;
			vifi_t vifi;

			order++;

			ifname = ifconfaddr($2);
			if (ifname == 0)
			    fatal("Tunnel local address %s is not mine",
				inet_fmt($2, s1));

			strlcpy(ffr.ifr_name, ifname, sizeof(ffr.ifr_name));
			if (ioctl(udp_socket, SIOCGIFFLAGS, (char *)&ffr) == -1)
			    fatal("ioctl SIOCGIFFLAGS on %s",ffr.ifr_name);
			if (ffr.ifr_flags & IFF_LOOPBACK)
			    fatal("Tunnel local address %s is a loopback interface",
				inet_fmt($2, s1));

			if (ifconfaddr($3) != 0)
			    fatal("Tunnel remote address %s is one of mine",
				inet_fmt($3, s1));

			for (vifi = 0, v = uvifs;
			     vifi < numvifs;
			     ++vifi, ++v)
			    if (v->uv_flags & VIFF_TUNNEL) {
				if ($3 == v->uv_rmt_addr)
				    fatal("Duplicate tunnel to %s",
					inet_fmt($3, s1));
			    } else if (!(v->uv_flags & VIFF_DISABLED)) {
				if (($3 & v->uv_subnetmask) == v->uv_subnet)
				    fatal("Unnecessary tunnel to %s",
					inet_fmt($3,s1));
			    }

			if (numvifs == MAXVIFS)
			    fatal("too many vifs");

			v = &uvifs[numvifs];
			v->uv_flags	= VIFF_TUNNEL;
			v->uv_metric	= DEFAULT_METRIC;
			v->uv_rate_limit= DEFAULT_TUN_RATE_LIMIT;
			v->uv_threshold	= DEFAULT_THRESHOLD;
			v->uv_lcl_addr	= $2;
			v->uv_rmt_addr	= $3;
			v->uv_subnet	= 0;
			v->uv_subnetmask= 0;
			v->uv_subnetbcast= 0;
			strncpy(v->uv_name, ffr.ifr_name, IFNAMSIZ);
			v->uv_groups	= NULL;
			v->uv_neighbors	= NULL;
			v->uv_acl	= NULL;
			v->uv_addrs	= NULL;

			if (!(ffr.ifr_flags & IFF_UP)) {
			    v->uv_flags |= VIFF_DOWN;
			    vifs_down = TRUE;
			}
					}
		tunnelmods
					{
			logit(LOG_INFO, 0,
			    "installing tunnel from %s to %s as vif #%u - rate=%d",
			    inet_fmt($2, s1), inet_fmt($3, s2),
			    numvifs, v->uv_rate_limit);

			++numvifs;
					}
	| PRUNING BOOLEAN	    { pruning = $2; }
	| CACHE_LIFETIME NUMBER     { cache_lifetime = $2;
				      max_prune_lifetime = cache_lifetime * 2;
				    }
	| NAME STRING boundary	    { if (numbounds >= MAXBOUNDS) {
					fatal("Too many named boundaries (max %d)", MAXBOUNDS);
				      }

				      boundlist[numbounds].name = strdup($2);
				      boundlist[numbounds++].bound = $3;
				    }
	;

tunnelmods	: /* empty */
	| tunnelmods tunnelmod
	;

tunnelmod	: mod
	| SRCRT			{ fatal("Source-route tunnels not supported"); }
	;

ifmods	: /* empty */
	| ifmods ifmod
	;

ifmod	: mod
	| DISABLE		{ v->uv_flags |= VIFF_DISABLED; }
	| IGMPV1		{ v->uv_flags |= VIFF_IGMPV1; }
	| NETMASK addrname	{
				  u_int32_t subnet, mask;

				  mask = $2;
				  subnet = v->uv_lcl_addr & mask;
				  if (!inet_valid_subnet(subnet, mask))
					fatal("Invalid netmask");
				  v->uv_subnet = subnet;
				  v->uv_subnetmask = mask;
				  v->uv_subnetbcast = subnet | ~mask;
				}
	| NETMASK		{

		    warn("Expected address after netmask keyword, ignored");

				}
	| ALTNET addrmask	{

		    struct phaddr *ph;

		    ph = malloc(sizeof(struct phaddr));
		    if (ph == NULL)
			fatal("out of memory");
		    if ($2.mask) {
			VAL_TO_MASK(ph->pa_subnetmask, $2.mask);
		    } else
			ph->pa_subnetmask = v->uv_subnetmask;
		    ph->pa_subnet = $2.addr & ph->pa_subnetmask;
		    ph->pa_subnetbcast = ph->pa_subnet | ~ph->pa_subnetmask;
		    if ($2.addr & ~ph->pa_subnetmask)
			warn("Extra subnet %s/%d has host bits set",
				inet_fmt($2.addr,s1), $2.mask);
		    ph->pa_next = v->uv_addrs;
		    v->uv_addrs = ph;

				}
	| ALTNET		{

		    warn("Expected address after altnet keyword, ignored");

				}
	;

mod	: THRESHOLD NUMBER	{ if ($2 < 1 || $2 > 255)
				    fatal("Invalid threshold %d",$2);
				  v->uv_threshold = $2;
				}
	| THRESHOLD		{

		    warn("Expected number after threshold keyword, ignored");

				}
	| METRIC NUMBER		{ if ($2 < 1 || $2 > UNREACHABLE)
				    fatal("Invalid metric %d",$2);
				  v->uv_metric = $2;
				}
	| METRIC		{

		    warn("Expected number after metric keyword, ignored");

				}
	| RATE_LIMIT NUMBER	{ if ($2 > MAX_RATE_LIMIT)
				    fatal("Invalid rate_limit %d",$2);
				  v->uv_rate_limit = $2;
				}
	| RATE_LIMIT		{

		    warn("Expected number after rate_limit keyword, ignored");

				}
	| BOUNDARY bound	{

		    struct vif_acl *v_acl;

		    v_acl = malloc(sizeof(struct vif_acl));
		    if (v_acl == NULL)
			fatal("out of memory");
		    VAL_TO_MASK(v_acl->acl_mask, $2.mask);
		    v_acl->acl_addr = $2.addr & v_acl->acl_mask;
		    if ($2.addr & ~v_acl->acl_mask)
			warn("Boundary spec %s/%d has host bits set",
				inet_fmt($2.addr,s1),$2.mask);
		    v_acl->acl_next = v->uv_acl;
		    v->uv_acl = v_acl;

				}
	| BOUNDARY		{

		warn("Expected boundary spec after boundary keyword, ignored");

				}
	;

interface	: ADDR		{ $$ = $1; }
	| STRING		{
				  $$ = valid_if($1);
				  if ($$ == 0)
					fatal("Invalid interface name %s",$1);
				}
	;

addrname	: ADDR		{ $$ = $1; }
	| STRING		{ struct hostent *hp;

				  if ((hp = gethostbyname($1)) == NULL)
				    fatal("No such host %s", $1);

				  if (hp->h_addr_list[1])
				    fatal("Hostname %s does not %s",
					$1, "map to a unique address");

				  bcopy(hp->h_addr_list[0], &$$,
					    hp->h_length);
				}

bound	: boundary		{ $$ = $1; }
	| STRING		{ int i;

				  for (i=0; i < numbounds; i++) {
				    if (!strcmp(boundlist[i].name, $1)) {
					$$ = boundlist[i].bound;
					break;
				    }
				  }
				  if (i == numbounds) {
				    fatal("Invalid boundary name %s",$1);
				  }
				}
	;

boundary	: ADDRMASK	{

			if ((ntohl($1.addr) & 0xff000000) != 0xef000000) {
			    fatal("Boundaries must be 239.x.x.x, not %s/%d",
				inet_fmt($1.addr, s1), $1.mask);
			}
			$$ = $1;

				}
	;

addrmask	: ADDRMASK	{ $$ = $1; }
	| ADDR			{ $$.addr = $1; $$.mask = 0; }
	;
%%
static void
fatal(const char *fmt, ...)
{
	va_list ap;
	char buf[200];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	logit(LOG_ERR,0,"%s: %s near line %d", configfilename, buf, lineno);
}

static void
warn(const char *fmt, ...)
{
	va_list ap;
	char buf[200];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	logit(LOG_WARNING,0,"%s: %s near line %d", configfilename, buf, lineno);
}

static void
yyerror(char *s)
{
	logit(LOG_ERR, 0, "%s: %s near line %d", configfilename, s, lineno);
}

static char *
next_word(void)
{
	static char buf[1024];
	static char *p=NULL;
	extern FILE *f;
	char *q;

	while (1) {
	    if (!p || !*p) {
		lineno++;
		if (fgets(buf, sizeof(buf), f) == NULL)
		    return NULL;
		p = buf;
	    }
	    while (*p && (*p == ' ' || *p == '\t'))	/* skip whitespace */
		p++;
	    if (*p == '#') {
		p = NULL;		/* skip comments */
		continue;
	    }
	    q = p;
	    while (*p && *p != ' ' && *p != '\t' && *p != '\n')
		p++;		/* find next whitespace */
	    *p++ = '\0';	/* null-terminate string */

	    if (!*q) {
		p = NULL;
		continue;	/* if 0-length string, read another line */
	    }

	    return q;
	}
}

static int
yylex(void)
{
	int n;
	u_int32_t addr;
	char *q;

	if ((q = next_word()) == NULL) {
		return 0;
	}

	if (!strcmp(q,"cache_lifetime"))
		return CACHE_LIFETIME;
	if (!strcmp(q,"pruning"))
		return PRUNING;
	if (!strcmp(q,"phyint"))
		return PHYINT;
	if (!strcmp(q,"tunnel"))
		return TUNNEL;
	if (!strcmp(q,"disable"))
		return DISABLE;
	if (!strcmp(q,"metric"))
		return METRIC;
	if (!strcmp(q,"threshold"))
		return THRESHOLD;
	if (!strcmp(q,"rate_limit"))
		return RATE_LIMIT;
	if (!strcmp(q,"srcrt") || !strcmp(q,"sourceroute"))
		return SRCRT;
	if (!strcmp(q,"boundary"))
		return BOUNDARY;
	if (!strcmp(q,"netmask"))
		return NETMASK;
	if (!strcmp(q,"igmpv1"))
		return IGMPV1;
	if (!strcmp(q,"altnet"))
		return ALTNET;
	if (!strcmp(q,"name"))
		return NAME;
	if (!strcmp(q,"on") || !strcmp(q,"yes")) {
		yylval.num = 1;
		return BOOLEAN;
	}
	if (!strcmp(q,"off") || !strcmp(q,"no")) {
		yylval.num = 0;
		return BOOLEAN;
	}
	if (sscanf(q,"%[.0-9]/%d%c",s1,&n,s2) == 2) {
		if ((addr = inet_parse(s1)) != 0xffffffff) {
			yylval.addrmask.mask = n;
			yylval.addrmask.addr = addr;
			return ADDRMASK;
		}
		/* fall through to returning STRING */
	}
	if (sscanf(q,"%[.0-9]%c",s1,s2) == 1) {
		if ((addr = inet_parse(s1)) != 0xffffffff &&
		    inet_valid_host(addr)) {
			yylval.addr = addr;
			return ADDR;
		}
	}
	if (sscanf(q,"0x%8x%c",&n,s1) == 1) {
		yylval.addr = n;
		return ADDR;
	}
	if (sscanf(q,"%d%c",&n,s1) == 1) {
		yylval.num = n;
		return NUMBER;
	}
	yylval.ptr = q;
	return STRING;
}

void
config_vifs_from_file(void)
{
	extern FILE *f;

	order = 0;
	numbounds = 0;
	lineno = 0;

	if ((f = fopen(configfilename, "r")) == NULL) {
	    if (errno != ENOENT)
		logit(LOG_ERR, errno, "can't open %s", configfilename);
	    return;
	}

	yyparse();

	fclose(f);
}

static u_int32_t
valid_if(char *s)
{
	register vifi_t vifi;
	register struct uvif *v;

	for (vifi=0, v=uvifs; vifi<numvifs; vifi++, v++)
	    if (!strcmp(v->uv_name, s))
		return v->uv_lcl_addr;

	return 0;
}

static const char *
ifconfaddr(u_int32_t a)
{
    static char ifname[IFNAMSIZ];
    struct ifaddrs *ifap, *ifa;

    if (getifaddrs(&ifap) != 0)
	return (NULL);

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
	    if (ifa->ifa_addr != NULL &&
		ifa->ifa_addr->sa_family == AF_INET &&
		((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == a) {
		strlcpy(ifname, ifa->ifa_name, sizeof(ifname));
		freeifaddrs(ifap);
		return (ifname);
	    }
    }
    freeifaddrs(ifap);
    return (0);
}
