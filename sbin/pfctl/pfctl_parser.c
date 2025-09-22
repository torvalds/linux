/*	$OpenBSD: pfctl_parser.c,v 1.352 2024/11/12 04:14:51 dlg Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2013 Henning Brauer <henning@openbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if_dl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <net/pfvar.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SYSLOG_NAMES
#include <syslog.h>

#include "pfctl_parser.h"
#include "pfctl.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

void		 print_op (u_int8_t, const char *, const char *);
void		 print_port (u_int8_t, u_int16_t, u_int16_t, const char *, int);
void		 print_ugid (u_int8_t, id_t, id_t, const char *);
void		 print_flags (u_int8_t);
void		 print_fromto(struct pf_rule_addr *, pf_osfp_t,
		    struct pf_rule_addr *, u_int8_t, u_int8_t, int);
void		 print_bwspec(const char *index, struct pf_queue_bwspec *);
void		 print_scspec(const char *, struct pf_queue_scspec *);
int		 ifa_skip_if(const char *filter, struct node_host *p);

struct node_host	*ifa_grouplookup(const char *, int);
struct node_host	*host_if(const char *, int);
struct node_host	*host_ip(const char *, int);
struct node_host	*host_dns(const char *, int, int);

const char *tcpflags = "FSRPAUEW";

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
	{ "photuris",	ICMP_PHOTURIS }
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
	{ "decrypt-fail",	ICMP_PHOTURIS,	ICMP_PHOTURIS_DECRYPT_FAILED }
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
	{ "redirrouter", ND_REDIRECT, ND_REDIRECT_ROUTER }
};

const struct pf_timeout pf_timeouts[] = {
	{ "tcp.first",		PFTM_TCP_FIRST_PACKET },
	{ "tcp.opening",	PFTM_TCP_OPENING },
	{ "tcp.established",	PFTM_TCP_ESTABLISHED },
	{ "tcp.closing",	PFTM_TCP_CLOSING },
	{ "tcp.finwait",	PFTM_TCP_FIN_WAIT },
	{ "tcp.closed",		PFTM_TCP_CLOSED },
	{ "tcp.tsdiff",		PFTM_TS_DIFF },
	{ "udp.first",		PFTM_UDP_FIRST_PACKET },
	{ "udp.single",		PFTM_UDP_SINGLE },
	{ "udp.multiple",	PFTM_UDP_MULTIPLE },
	{ "icmp.first",		PFTM_ICMP_FIRST_PACKET },
	{ "icmp.error",		PFTM_ICMP_ERROR_REPLY },
	{ "other.first",	PFTM_OTHER_FIRST_PACKET },
	{ "other.single",	PFTM_OTHER_SINGLE },
	{ "other.multiple",	PFTM_OTHER_MULTIPLE },
	{ "frag",		PFTM_FRAG },
	{ "interval",		PFTM_INTERVAL },
	{ "adaptive.start",	PFTM_ADAPTIVE_START },
	{ "adaptive.end",	PFTM_ADAPTIVE_END },
	{ "src.track",		PFTM_SRC_NODE },
	{ NULL,			0 }
};

enum { PF_POOL_ROUTE, PF_POOL_NAT, PF_POOL_RDR };

void
copy_satopfaddr(struct pf_addr *pfa, struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET6)
		pfa->v6 = ((struct sockaddr_in6 *)sa)->sin6_addr;
	else if (sa->sa_family == AF_INET)
		pfa->v4 = ((struct sockaddr_in *)sa)->sin_addr;
	else
		warnx("unhandled af %d", sa->sa_family);
}

const struct icmptypeent *
geticmptypebynumber(u_int8_t type, sa_family_t af)
{
	size_t	i;

	if (af != AF_INET6) {
		for (i = 0; i < nitems(icmp_type); i++) {
			if (type == icmp_type[i].type)
				return (&icmp_type[i]);
		}
	} else {
		for (i = 0; i < nitems(icmp6_type); i++) {
			if (type == icmp6_type[i].type)
				 return (&icmp6_type[i]);
		}
	}
	return (NULL);
}

const struct icmptypeent *
geticmptypebyname(char *w, sa_family_t af)
{
	size_t	i;

	if (af != AF_INET6) {
		for (i = 0; i < nitems(icmp_type); i++) {
			if (!strcmp(w, icmp_type[i].name))
				return (&icmp_type[i]);
		}
	} else {
		for (i = 0; i < nitems(icmp6_type); i++) {
			if (!strcmp(w, icmp6_type[i].name))
				return (&icmp6_type[i]);
		}
	}
	return (NULL);
}

const struct icmpcodeent *
geticmpcodebynumber(u_int8_t type, u_int8_t code, sa_family_t af)
{
	size_t	i;

	if (af != AF_INET6) {
		for (i = 0; i < nitems(icmp_code); i++) {
			if (type == icmp_code[i].type &&
			    code == icmp_code[i].code)
				return (&icmp_code[i]);
		}
	} else {
		for (i = 0; i < nitems(icmp6_code); i++) {
			if (type == icmp6_code[i].type &&
			    code == icmp6_code[i].code)
				return (&icmp6_code[i]);
		}
	}
	return (NULL);
}

const struct icmpcodeent *
geticmpcodebyname(u_long type, char *w, sa_family_t af)
{
	size_t	i;

	if (af != AF_INET6) {
		for (i = 0; i < nitems(icmp_code); i++) {
			if (type == icmp_code[i].type &&
			    !strcmp(w, icmp_code[i].name))
				return (&icmp_code[i]);
		}
	} else {
		for (i = 0; i < nitems(icmp6_code); i++) {
			if (type == icmp6_code[i].type &&
			    !strcmp(w, icmp6_code[i].name))
				return (&icmp6_code[i]);
		}
	}
	return (NULL);
}

/*
 *  Decode a symbolic name to a numeric value.
 *  From syslogd.
 */
int
string_to_loglevel(const char *name)
{
	CODE *c;
	char *p, buf[40];

	if (isdigit((unsigned char)*name)) {
		const char *errstr;
		int val;

		val = strtonum(name, 0, LOG_DEBUG, &errstr);
		if (errstr)
			return -1;
		return val;
	}

	for (p = buf; *name && p < &buf[sizeof(buf) - 1]; p++, name++) {
		if (isupper((unsigned char)*name))
			*p = tolower((unsigned char)*name);
		else
			*p = *name;
	}
	*p = '\0';
	for (c = prioritynames; c->c_name; c++)
		if (!strcmp(buf, c->c_name) && c->c_val != INTERNAL_NOPRI)
			return (c->c_val);

	return (-1);
}

const char *
loglevel_to_string(int level)
{
	CODE *c;

	for (c = prioritynames; c->c_name; c++)
		if (c->c_val == level)
			return (c->c_name);

	return ("unknown");
}

void
print_op(u_int8_t op, const char *a1, const char *a2)
{
	if (op == PF_OP_IRG)
		printf(" %s >< %s", a1, a2);
	else if (op == PF_OP_XRG)
		printf(" %s <> %s", a1, a2);
	else if (op == PF_OP_EQ)
		printf(" = %s", a1);
	else if (op == PF_OP_NE)
		printf(" != %s", a1);
	else if (op == PF_OP_LT)
		printf(" < %s", a1);
	else if (op == PF_OP_LE)
		printf(" <= %s", a1);
	else if (op == PF_OP_GT)
		printf(" > %s", a1);
	else if (op == PF_OP_GE)
		printf(" >= %s", a1);
	else if (op == PF_OP_RRG)
		printf(" %s:%s", a1, a2);
}

void
print_port(u_int8_t op, u_int16_t p1, u_int16_t p2, const char *proto, int opts)
{
	char		 a1[6], a2[6];
	struct servent	*s = NULL;

	if (opts & PF_OPT_PORTNAMES)
		s = getservbyport(p1, proto);
	p1 = ntohs(p1);
	p2 = ntohs(p2);
	snprintf(a1, sizeof(a1), "%u", p1);
	snprintf(a2, sizeof(a2), "%u", p2);
	printf(" port");
	if (s != NULL && (op == PF_OP_EQ || op == PF_OP_NE))
		print_op(op, s->s_name, a2);
	else
		print_op(op, a1, a2);
}

void
print_ugid(u_int8_t op, id_t i1, id_t i2, const char *t)
{
	char	a1[11], a2[11];

	snprintf(a1, sizeof(a1), "%u", i1);
	snprintf(a2, sizeof(a2), "%u", i2);
	printf(" %s", t);
	if (i1 == -1 && (op == PF_OP_EQ || op == PF_OP_NE))
		print_op(op, "unknown", a2);
	else
		print_op(op, a1, a2);
}

void
print_flags(u_int8_t f)
{
	int	i;

	for (i = 0; tcpflags[i]; ++i)
		if (f & (1 << i))
			printf("%c", tcpflags[i]);
}

void
print_fromto(struct pf_rule_addr *src, pf_osfp_t osfp, struct pf_rule_addr *dst,
    sa_family_t af, u_int8_t proto, int opts)
{
	char buf[PF_OSFP_LEN*3];
	int verbose = opts & (PF_OPT_VERBOSE2 | PF_OPT_DEBUG);
	if (src->addr.type == PF_ADDR_ADDRMASK &&
	    dst->addr.type == PF_ADDR_ADDRMASK &&
	    PF_AZERO(&src->addr.v.a.addr, AF_INET6) &&
	    PF_AZERO(&src->addr.v.a.mask, AF_INET6) &&
	    PF_AZERO(&dst->addr.v.a.addr, AF_INET6) &&
	    PF_AZERO(&dst->addr.v.a.mask, AF_INET6) &&
	    !src->neg && !dst->neg &&
	    !src->port_op && !dst->port_op &&
	    osfp == PF_OSFP_ANY)
		printf(" all");
	else {
		printf(" from ");
		if (src->neg)
			printf("! ");
		print_addr(&src->addr, af, verbose);
		if (src->port_op)
			print_port(src->port_op, src->port[0],
			    src->port[1],
			    proto == IPPROTO_TCP ? "tcp" : "udp", opts);
		if (osfp != PF_OSFP_ANY)
			printf(" os \"%s\"", pfctl_lookup_fingerprint(osfp, buf,
			    sizeof(buf)));

		printf(" to ");
		if (dst->neg)
			printf("! ");
		print_addr(&dst->addr, af, verbose);
		if (dst->port_op)
			print_port(dst->port_op, dst->port[0],
			    dst->port[1],
			    proto == IPPROTO_TCP ? "tcp" : "udp", opts);
	}
}

void
print_pool(struct pf_pool *pool, u_int16_t p1, u_int16_t p2,
    sa_family_t af, int id, int verbose)
{
	if (pool->ifname[0]) {
		if (!PF_AZERO(&pool->addr.v.a.addr, af)) {
			print_addr(&pool->addr, af, verbose);
			printf("@");
		}
		printf("%s", pool->ifname);
	} else
		print_addr(&pool->addr, af, verbose);
	switch (id) {
	case PF_POOL_NAT:
		if ((p1 != PF_NAT_PROXY_PORT_LOW ||
		    p2 != PF_NAT_PROXY_PORT_HIGH) && (p1 != 0 || p2 != 0)) {
			if (p1 == p2)
				printf(" port %u", p1);
			else
				printf(" port %u:%u", p1, p2);
		}
		break;
	case PF_POOL_RDR:
		if (p1) {
			printf(" port %u", p1);
			if (p2 && (p2 != p1))
				printf(":%u", p2);
		}
		break;
	default:
		break;
	}
	switch (pool->opts & PF_POOL_TYPEMASK) {
	case PF_POOL_NONE:
		break;
	case PF_POOL_BITMASK:
		printf(" bitmask");
		break;
	case PF_POOL_RANDOM:
		printf(" random");
		break;
	case PF_POOL_SRCHASH:
		printf(" source-hash 0x%08x%08x%08x%08x",
		    pool->key.key32[0], pool->key.key32[1],
		    pool->key.key32[2], pool->key.key32[3]);
		break;
	case PF_POOL_ROUNDROBIN:
		printf(" round-robin");
		break;
	case PF_POOL_LEASTSTATES:
		printf(" least-states");
		break;
	}
	if (pool->opts & PF_POOL_STICKYADDR)
		printf(" sticky-address");
	if (id == PF_POOL_NAT && p1 == 0 && p2 == 0)
		printf(" static-port");
}

const char	*pf_reasons[PFRES_MAX+1] = PFRES_NAMES;
const char	*pf_lcounters[LCNT_MAX+1] = LCNT_NAMES;
const char	*pf_fcounters[FCNT_MAX+1] = FCNT_NAMES;
const char	*pf_scounters[SCNT_MAX+1] = FCNT_NAMES;
const char	*pf_ncounters[NCNT_MAX+1] = FCNT_NAMES;

void
print_status(struct pf_status *s, struct pfctl_watermarks *synflwats, int opts)
{
	char			statline[80], *running, *debug;
	time_t			runtime = 0;
	struct timespec		uptime;
	int			i;
	char			buf[PF_MD5_DIGEST_LENGTH * 2 + 1];
	static const char	hex[] = "0123456789abcdef";

	if (!clock_gettime(CLOCK_BOOTTIME, &uptime))
		runtime = uptime.tv_sec - s->since;
	running = s->running ? "Enabled" : "Disabled";

	if (runtime) {
		unsigned int	sec, min, hrs;
		time_t		day = runtime;

		sec = day % 60;
		day /= 60;
		min = day % 60;
		day /= 60;
		hrs = day % 24;
		day /= 24;
		snprintf(statline, sizeof(statline),
		    "Status: %s for %lld days %.2u:%.2u:%.2u",
		    running, (long long)day, hrs, min, sec);
	} else
		snprintf(statline, sizeof(statline), "Status: %s", running);
	printf("%-44s", statline);
	if (asprintf(&debug, "Debug: %s", loglevel_to_string(s->debug)) != -1) {
		printf("%15s\n\n", debug);
		free(debug);
	}

	if (opts & PF_OPT_VERBOSE) {
		printf("Hostid:   0x%08x\n", ntohl(s->hostid));

		for (i = 0; i < PF_MD5_DIGEST_LENGTH; i++) {
			buf[i + i] = hex[s->pf_chksum[i] >> 4];
			buf[i + i + 1] = hex[s->pf_chksum[i] & 0x0f];
		}
		buf[i + i] = '\0';
		printf("Checksum: 0x%s\n\n", buf);
	}

	if (s->ifname[0] != 0) {
		printf("Interface Stats for %-16s %5s %16s\n",
		    s->ifname, "IPv4", "IPv6");
		printf("  %-25s %14llu %16llu\n", "Bytes In",
		    (unsigned long long)s->bcounters[0][0],
		    (unsigned long long)s->bcounters[1][0]);
		printf("  %-25s %14llu %16llu\n", "Bytes Out",
		    (unsigned long long)s->bcounters[0][1],
		    (unsigned long long)s->bcounters[1][1]);
		printf("  Packets In\n");
		printf("    %-23s %14llu %16llu\n", "Passed",
		    (unsigned long long)s->pcounters[0][0][PF_PASS],
		    (unsigned long long)s->pcounters[1][0][PF_PASS]);
		printf("    %-23s %14llu %16llu\n", "Blocked",
		    (unsigned long long)s->pcounters[0][0][PF_DROP],
		    (unsigned long long)s->pcounters[1][0][PF_DROP]);
		printf("  Packets Out\n");
		printf("    %-23s %14llu %16llu\n", "Passed",
		    (unsigned long long)s->pcounters[0][1][PF_PASS],
		    (unsigned long long)s->pcounters[1][1][PF_PASS]);
		printf("    %-23s %14llu %16llu\n\n", "Blocked",
		    (unsigned long long)s->pcounters[0][1][PF_DROP],
		    (unsigned long long)s->pcounters[1][1][PF_DROP]);
	}
	printf("%-27s %14s %16s\n", "State Table", "Total", "Rate");
	printf("  %-25s %14u %14s\n", "current entries", s->states, "");
	printf("  %-25s %14u %14s\n", "half-open tcp", s->states_halfopen, "");
	for (i = 0; i < FCNT_MAX; i++) {
		printf("  %-25s %14llu ", pf_fcounters[i],
			    (unsigned long long)s->fcounters[i]);
		if (runtime > 0)
			printf("%14.1f/s\n",
			    (double)s->fcounters[i] / (double)runtime);
		else
			printf("%14s\n", "");
	}
	if (opts & PF_OPT_VERBOSE) {
		printf("Source Tracking Table\n");
		printf("  %-25s %14u %14s\n", "current entries",
		    s->src_nodes, "");
		for (i = 0; i < SCNT_MAX; i++) {
			printf("  %-25s %14lld ", pf_scounters[i],
				    s->scounters[i]);
			if (runtime > 0)
				printf("%14.1f/s\n",
				    (double)s->scounters[i] / (double)runtime);
			else
				printf("%14s\n", "");
		}
	}
	if (opts & PF_OPT_VERBOSE) {
		printf("Fragments\n");
		printf("  %-25s %14u %14s\n", "current entries",
		    s->fragments, "");
		for (i = 0; i < NCNT_MAX; i++) {
			printf("  %-25s %14lld ", pf_ncounters[i],
				    s->ncounters[i]);
			if (runtime > 0)
				printf("%14.1f/s\n",
				    (double)s->ncounters[i] / (double)runtime);
			else
				printf("%14s\n", "");
		}
	}
	printf("Counters\n");
	for (i = 0; i < PFRES_MAX; i++) {
		printf("  %-25s %14llu ", pf_reasons[i],
		    (unsigned long long)s->counters[i]);
		if (runtime > 0)
			printf("%14.1f/s\n",
			    (double)s->counters[i] / (double)runtime);
		else
			printf("%14s\n", "");
	}
	if (opts & PF_OPT_VERBOSE) {
		printf("Limit Counters\n");
		for (i = 0; i < LCNT_MAX; i++) {
			printf("  %-25s %14lld ", pf_lcounters[i],
				    s->lcounters[i]);
			if (runtime > 0)
				printf("%14.1f/s\n",
				    (double)s->lcounters[i] / (double)runtime);
			else
				printf("%14s\n", "");
		}
	}
	if (opts & PF_OPT_VERBOSE) {
		printf("Adaptive Syncookies Watermarks\n");
		printf("  %-25s %14d states\n", "start", synflwats->hi);
		printf("  %-25s %14d states\n", "end", synflwats->lo);
	}
}

void
print_src_node(struct pf_src_node *sn, int opts)
{
	struct pf_addr_wrap aw;
	int min, sec;

	memset(&aw, 0, sizeof(aw));
	if (sn->af == AF_INET)
		aw.v.a.mask.addr32[0] = 0xffffffff;
	else
		memset(&aw.v.a.mask, 0xff, sizeof(aw.v.a.mask));

	aw.v.a.addr = sn->addr;
	print_addr(&aw, sn->af, opts & PF_OPT_VERBOSE2);

	if (!PF_AZERO(&sn->raddr, sn->af)) {
		if (sn->type == PF_SN_NAT)
			printf(" nat-to ");
		else if (sn->type == PF_SN_RDR)
			printf(" rdr-to ");
		else if (sn->type == PF_SN_ROUTE)
			printf(" route-to ");
		else
			printf(" ??? (%u) ", sn->type);
		aw.v.a.addr = sn->raddr;
		print_addr(&aw, sn->naf ? sn->naf : sn->af,
		    opts & PF_OPT_VERBOSE2);
	}

	printf(" ( states %u, connections %u, rate %u.%u/%us )\n", sn->states,
	    sn->conn, sn->conn_rate.count / 1000,
	    (sn->conn_rate.count % 1000) / 100, sn->conn_rate.seconds);
	if (opts & PF_OPT_VERBOSE) {
		sec = sn->creation % 60;
		sn->creation /= 60;
		min = sn->creation % 60;
		sn->creation /= 60;
		printf("   age %.2u:%.2u:%.2u", sn->creation, min, sec);
		if (sn->states == 0) {
			sec = sn->expire % 60;
			sn->expire /= 60;
			min = sn->expire % 60;
			sn->expire /= 60;
			printf(", expires in %.2u:%.2u:%.2u",
			    sn->expire, min, sec);
		}
		printf(", %llu pkts, %llu bytes",
		    sn->packets[0] + sn->packets[1],
		    sn->bytes[0] + sn->bytes[1]);
		if (sn->rule.nr != -1)
			printf(", rule %u", sn->rule.nr);
		printf("\n");
	}
}

void
print_rule(struct pf_rule *r, const char *anchor_call, int opts)
{
	static const char *actiontypes[] = { "pass", "block", "scrub",
	    "no scrub", "nat", "no nat", "binat", "no binat", "rdr", "no rdr",
	    "", "", "match"};
	static const char *anchortypes[] = { "anchor", "anchor", "anchor",
	    "anchor", "nat-anchor", "nat-anchor", "binat-anchor",
	    "binat-anchor", "rdr-anchor", "rdr-anchor" };
	int	i, ropts;
	int	verbose = opts & (PF_OPT_VERBOSE2 | PF_OPT_DEBUG);
	char	*p;

	if ((r->rule_flag & PFRULE_EXPIRED) && (!verbose))
		return;

	if (verbose)
		printf("@%d ", r->nr);

	if (anchor_call[0]) {
		if (r->action >= nitems(anchortypes)) {
			printf("anchor(%d)", r->action);
		} else {
			p = strrchr(anchor_call, '/');
			if (p ? p[1] == '_' : anchor_call[0] == '_')
				printf("%s", anchortypes[r->action]);
			else
				printf("%s \"%s\"", anchortypes[r->action],
				    anchor_call);
		}
	} else {
		if (r->action >= nitems(actiontypes))
			printf("action(%d)", r->action);
		else
			printf("%s", actiontypes[r->action]);
	}
	if (r->action == PF_DROP) {
		if (r->rule_flag & PFRULE_RETURN)
			printf(" return");
		else if (r->rule_flag & PFRULE_RETURNRST) {
			if (!r->return_ttl)
				printf(" return-rst");
			else
				printf(" return-rst(ttl %d)", r->return_ttl);
		} else if (r->rule_flag & PFRULE_RETURNICMP) {
			const struct icmpcodeent	*ic, *ic6;

			ic = geticmpcodebynumber(r->return_icmp >> 8,
			    r->return_icmp & 255, AF_INET);
			ic6 = geticmpcodebynumber(r->return_icmp6 >> 8,
			    r->return_icmp6 & 255, AF_INET6);

			switch (r->af) {
			case AF_INET:
				printf(" return-icmp");
				if (ic == NULL)
					printf("(%u)", r->return_icmp & 255);
				else
					printf("(%s)", ic->name);
				break;
			case AF_INET6:
				printf(" return-icmp6");
				if (ic6 == NULL)
					printf("(%u)", r->return_icmp6 & 255);
				else
					printf("(%s)", ic6->name);
				break;
			default:
				printf(" return-icmp");
				if (ic == NULL)
					printf("(%u, ", r->return_icmp & 255);
				else
					printf("(%s, ", ic->name);
				if (ic6 == NULL)
					printf("%u)", r->return_icmp6 & 255);
				else
					printf("%s)", ic6->name);
				break;
			}
		} else
			printf(" drop");
	}
	if (r->direction == PF_IN)
		printf(" in");
	else if (r->direction == PF_OUT)
		printf(" out");
	if (r->log) {
		printf(" log");
		if (r->log & ~PF_LOG || r->logif) {
			int count = 0;

			printf(" (");
			if (r->log & PF_LOG_ALL)
				printf("%sall", count++ ? ", " : "");
			if (r->log & PF_LOG_MATCHES)
				printf("%smatches", count++ ? ", " : "");
			if (r->log & PF_LOG_USER)
				printf("%suser", count++ ? ", " : "");
			if (r->logif)
				printf("%sto pflog%u", count++ ? ", " : "",
				    r->logif);
			printf(")");
		}
	}
	if (r->quick)
		printf(" quick");
	if (r->ifname[0]) {
		if (r->ifnot)
			printf(" on ! %s", r->ifname);
		else
			printf(" on %s", r->ifname);
	}
	if (r->onrdomain >= 0) {
		if (r->ifnot)
			printf(" on ! rdomain %d", r->onrdomain);
		else
			printf(" on rdomain %d", r->onrdomain);
	}
	if (r->af) {
		if (r->af == AF_INET)
			printf(" inet");
		else
			printf(" inet6");
	}
	if (r->proto) {
		struct protoent	*p;

		if ((p = getprotobynumber(r->proto)) != NULL)
			printf(" proto %s", p->p_name);
		else
			printf(" proto %u", r->proto);
	}
	print_fromto(&r->src, r->os_fingerprint, &r->dst, r->af, r->proto,
	    opts);
	if (r->rcv_ifname[0])
		printf(" %sreceived-on %s", r->rcvifnot ? "!" : "",
		    r->rcv_ifname);
	if (r->uid.op)
		print_ugid(r->uid.op, r->uid.uid[0], r->uid.uid[1], "user");
	if (r->gid.op)
		print_ugid(r->gid.op, r->gid.gid[0], r->gid.gid[1], "group");
	if (r->flags || r->flagset) {
		printf(" flags ");
		print_flags(r->flags);
		printf("/");
		print_flags(r->flagset);
	} else if ((r->action == PF_PASS || r->action == PF_MATCH) &&
	    (!r->proto || r->proto == IPPROTO_TCP) &&
	    !(r->rule_flag & PFRULE_FRAGMENT) &&
	    !anchor_call[0] && r->keep_state)
		printf(" flags any");
	if (r->type) {
		const struct icmptypeent	*it;

		it = geticmptypebynumber(r->type-1, r->af);
		if (r->af != AF_INET6)
			printf(" icmp-type");
		else
			printf(" icmp6-type");
		if (it != NULL)
			printf(" %s", it->name);
		else
			printf(" %u", r->type-1);
		if (r->code) {
			const struct icmpcodeent	*ic;

			ic = geticmpcodebynumber(r->type-1, r->code-1, r->af);
			if (ic != NULL)
				printf(" code %s", ic->name);
			else
				printf(" code %u", r->code-1);
		}
	}
	if (r->tos)
		printf(" tos 0x%2.2x", r->tos);
	if (r->prio)
		printf(" prio %u", r->prio == PF_PRIO_ZERO ? 0 : r->prio);
	if (r->pktrate.limit)
		printf(" max-pkt-rate %u/%u", r->pktrate.limit,
		    r->pktrate.seconds);

	if (r->scrub_flags & PFSTATE_SETMASK || r->qname[0] ||
	    r->rule_flag & PFRULE_SETDELAY) {
		char *comma = "";
		printf(" set (");
		if (r->scrub_flags & PFSTATE_SETPRIO) {
			if (r->set_prio[0] == r->set_prio[1])
				printf("%sprio %u", comma, r->set_prio[0]);
			else
				printf("%sprio(%u, %u)", comma, r->set_prio[0],
				    r->set_prio[1]);
			comma = ", ";
		}
		if (r->qname[0]) {
			if (r->pqname[0])
				printf("%squeue(%s, %s)", comma, r->qname,
				    r->pqname);
			else
				printf("%squeue %s", comma, r->qname);
			comma = ", ";
		}
		if (r->scrub_flags & PFSTATE_SETTOS) {
			printf("%stos 0x%2.2x", comma, r->set_tos);
			comma = ", ";
		}
		if (r->rule_flag & PFRULE_SETDELAY) {
			printf("%sdelay %u", comma, r->delay);
			comma = ", ";
		}
		printf(")");
	}

	ropts = 0;
	if (r->max_states || r->max_src_nodes || r->max_src_states)
		ropts = 1;
	if (r->rule_flag & PFRULE_NOSYNC)
		ropts = 1;
	if (r->rule_flag & PFRULE_SRCTRACK)
		ropts = 1;
	if (r->rule_flag & PFRULE_IFBOUND)
		ropts = 1;
	if (r->rule_flag & PFRULE_STATESLOPPY)
		ropts = 1;
	if (r->rule_flag & PFRULE_PFLOW)
		ropts = 1;
	for (i = 0; !ropts && i < PFTM_MAX; ++i)
		if (r->timeout[i])
			ropts = 1;

	if (!r->keep_state && r->action == PF_PASS && !anchor_call[0])
		printf(" no state");
	else if (r->keep_state == PF_STATE_NORMAL && ropts)
		printf(" keep state");
	else if (r->keep_state == PF_STATE_MODULATE)
		printf(" modulate state");
	else if (r->keep_state == PF_STATE_SYNPROXY)
		printf(" synproxy state");
	if (r->prob) {
		char	buf[20];

		snprintf(buf, sizeof(buf), "%f", r->prob*100.0/(UINT_MAX+1.0));
		for (i = strlen(buf)-1; i > 0; i--) {
			if (buf[i] == '0')
				buf[i] = '\0';
			else {
				if (buf[i] == '.')
					buf[i] = '\0';
				break;
			}
		}
		printf(" probability %s%%", buf);
	}
	if (ropts) {
		printf(" (");
		if (r->max_states) {
			printf("max %u", r->max_states);
			ropts = 0;
		}
		if (r->rule_flag & PFRULE_NOSYNC) {
			if (!ropts)
				printf(", ");
			printf("no-sync");
			ropts = 0;
		}
		if (r->rule_flag & PFRULE_SRCTRACK) {
			if (!ropts)
				printf(", ");
			printf("source-track");
			if (r->rule_flag & PFRULE_RULESRCTRACK)
				printf(" rule");
			else
				printf(" global");
			ropts = 0;
		}
		if (r->max_src_states) {
			if (!ropts)
				printf(", ");
			printf("max-src-states %u", r->max_src_states);
			ropts = 0;
		}
		if (r->max_src_conn) {
			if (!ropts)
				printf(", ");
			printf("max-src-conn %u", r->max_src_conn);
			ropts = 0;
		}
		if (r->max_src_conn_rate.limit) {
			if (!ropts)
				printf(", ");
			printf("max-src-conn-rate %u/%u",
			    r->max_src_conn_rate.limit,
			    r->max_src_conn_rate.seconds);
			ropts = 0;
		}
		if (r->max_src_nodes) {
			if (!ropts)
				printf(", ");
			printf("max-src-nodes %u", r->max_src_nodes);
			ropts = 0;
		}
		if (r->overload_tblname[0]) {
			if (!ropts)
				printf(", ");
			printf("overload <%s>", r->overload_tblname);
			if (r->flush)
				printf(" flush");
			if (r->flush & PF_FLUSH_GLOBAL)
				printf(" global");
		}
		if (r->rule_flag & PFRULE_IFBOUND) {
			if (!ropts)
				printf(", ");
			printf("if-bound");
			ropts = 0;
		}
		if (r->rule_flag & PFRULE_STATESLOPPY) {
			if (!ropts)
				printf(", ");
			printf("sloppy");
			ropts = 0;
		}
		if (r->rule_flag & PFRULE_PFLOW) {
			if (!ropts)
				printf(", ");
			printf("pflow");
			ropts = 0;
		}
		for (i = 0; i < PFTM_MAX; ++i)
			if (r->timeout[i]) {
				int j;

				if (!ropts)
					printf(", ");
				ropts = 0;
				for (j = 0; pf_timeouts[j].name != NULL;
				    ++j)
					if (pf_timeouts[j].timeout == i)
						break;
				printf("%s %u", pf_timeouts[j].name == NULL ?
				    "inv.timeout" : pf_timeouts[j].name,
				    r->timeout[i]);
			}
		printf(")");
	}

	if (r->rule_flag & PFRULE_FRAGMENT)
		printf(" fragment");

	if (r->scrub_flags & PFSTATE_SCRUBMASK || r->min_ttl || r->max_mss) {
		printf(" scrub (");
		ropts = 1;
		if (r->scrub_flags & PFSTATE_NODF) {
			printf("no-df");
			ropts = 0;
		}
		if (r->scrub_flags & PFSTATE_RANDOMID) {
			if (!ropts)
				printf(" ");
			printf("random-id");
			ropts = 0;
		}
		if (r->min_ttl) {
			if (!ropts)
				printf(" ");
			printf("min-ttl %d", r->min_ttl);
			ropts = 0;
		}
		if (r->scrub_flags & PFSTATE_SCRUB_TCP) {
			if (!ropts)
				printf(" ");
			printf("reassemble tcp");
			ropts = 0;
		}
		if (r->max_mss) {
			if (!ropts)
				printf(" ");
			printf("max-mss %d", r->max_mss);
			ropts = 0;
		}
		printf(")");
	}

	if (r->allow_opts)
		printf(" allow-opts");
	if (r->label[0])
		printf(" label \"%s\"", r->label);
	if (r->rule_flag & PFRULE_ONCE)
		printf(" once");
	if (r->tagname[0])
		printf(" tag %s", r->tagname);
	if (r->match_tagname[0]) {
		if (r->match_tag_not)
			printf(" !");
		printf(" tagged %s", r->match_tagname);
	}
	if (r->rtableid != -1)
		printf(" rtable %u", r->rtableid);
	switch (r->divert.type) {
	case PF_DIVERT_NONE:
		break;
	case PF_DIVERT_TO: {
		printf(" divert-to ");
		print_addr_str(r->af, &r->divert.addr);
		printf(" port %u", ntohs(r->divert.port));
		break;
	}
	case PF_DIVERT_REPLY:
		printf(" divert-reply");
		break;
	case PF_DIVERT_PACKET:
		printf(" divert-packet port %u", ntohs(r->divert.port));
		break;
	default:
		printf(" divert ???");
		break;
	}

	if (!anchor_call[0] && r->nat.addr.type != PF_ADDR_NONE &&
	    r->rule_flag & PFRULE_AFTO) {
		printf(" af-to %s from ", r->naf == AF_INET ? "inet" : "inet6");
		print_pool(&r->nat, r->nat.proxy_port[0],
		    r->nat.proxy_port[1], r->naf ? r->naf : r->af,
		    PF_POOL_NAT, verbose);
		if (r->rdr.addr.type != PF_ADDR_NONE) {
			printf(" to ");
			print_pool(&r->rdr, r->rdr.proxy_port[0],
			    r->rdr.proxy_port[1], r->naf ? r->naf : r->af,
			    PF_POOL_RDR, verbose);
		}
	} else if (!anchor_call[0] && r->nat.addr.type != PF_ADDR_NONE) {
		printf (" nat-to ");
		print_pool(&r->nat, r->nat.proxy_port[0],
		    r->nat.proxy_port[1], r->naf ? r->naf : r->af,
		    PF_POOL_NAT, verbose);
	} else if (!anchor_call[0] && r->rdr.addr.type != PF_ADDR_NONE) {
		printf (" rdr-to ");
		print_pool(&r->rdr, r->rdr.proxy_port[0],
		    r->rdr.proxy_port[1], r->af, PF_POOL_RDR, verbose);
	}
	if (r->rt) {
		if (r->rt == PF_ROUTETO)
			printf(" route-to");
		else if (r->rt == PF_REPLYTO)
			printf(" reply-to");
		else if (r->rt == PF_DUPTO)
			printf(" dup-to");
		printf(" ");
		print_pool(&r->route, 0, 0, r->af, PF_POOL_ROUTE, verbose);
	}

	if (r->rule_flag & PFRULE_EXPIRED)
		printf(" # expired");
}

void
print_tabledef(const char *name, int flags, int addrs,
    struct node_tinithead *nodes)
{
	struct node_tinit	*ti, *nti;
	struct node_host	*h;

	printf("table <%s>", name);
	if (flags & PFR_TFLAG_CONST)
		printf(" const");
	if (flags & PFR_TFLAG_PERSIST)
		printf(" persist");
	if (flags & PFR_TFLAG_COUNTERS)
		printf(" counters");
	SIMPLEQ_FOREACH(ti, nodes, entries) {
		if (ti->file) {
			printf(" file \"%s\"", ti->file);
			continue;
		}
		printf(" {");
		for (;;) {
			for (h = ti->host; h != NULL; h = h->next) {
				printf(h->not ? " !" : " ");
				print_addr(&h->addr, h->af, 0);
				if (h->ifname)
					printf("@%s", h->ifname);
			}
			nti = SIMPLEQ_NEXT(ti, entries);
			if (nti != NULL && nti->file == NULL)
				ti = nti;	/* merge lists */
			else
				break;
		}
		printf(" }");
	}
	if (addrs && SIMPLEQ_EMPTY(nodes))
		printf(" { }");
	printf("\n");
}

void
print_bwspec(const char *prefix, struct pf_queue_bwspec *bw)
{
	uint64_t rate;
	int	i;
	static const char unit[] = " KMG";

	if (bw->percent)
		printf("%s%u%%", prefix, bw->percent);
	else if (bw->absolute) {
		rate = bw->absolute;
		for (i = 0; rate >= 1000 && i <= 3 && (rate % 1000 == 0); i++)
			rate /= 1000;
		printf("%s%llu%c", prefix, rate, unit[i]);
	}
}

void
print_scspec(const char *prefix, struct pf_queue_scspec *sc)
{
	print_bwspec(prefix, &sc->m2);
	if (sc->d) {
		printf(" burst ");
		print_bwspec("", &sc->m1);
		printf(" for %ums", sc->d);
	}
}

void
print_queuespec(struct pf_queuespec *q)
{
	printf("queue %s", q->qname);
	if (q->parent[0])
		printf(" parent %s", q->parent);
	else if (q->ifname[0])
		printf(" on %s", q->ifname);
	if (q->flags & PFQS_FLOWQUEUE) {
		printf(" flows %u", q->flowqueue.flows);
		if (q->flowqueue.quantum > 0)
			printf(" quantum %u", q->flowqueue.quantum);
		if (q->flowqueue.interval > 0)
			printf(" interval %ums",
			    q->flowqueue.interval / 1000000);
		if (q->flowqueue.target > 0)
			printf(" target %ums",
			    q->flowqueue.target / 1000000);
	}
	if (q->linkshare.m1.absolute || q->linkshare.m2.absolute) {
		print_scspec(" bandwidth ", &q->linkshare);
		print_scspec(", min ", &q->realtime);
		print_scspec(", max ", &q->upperlimit);
	}
	if (q->flags & PFQS_DEFAULT)
		printf(" default");
	if (q->qlimit)
		printf(" qlimit %u", q->qlimit);
	printf("\n");
}

int
parse_flags(char *s)
{
	char		*p, *q;
	u_int8_t	 f = 0;

	for (p = s; *p; p++) {
		if ((q = strchr(tcpflags, *p)) == NULL)
			return -1;
		else
			f |= 1 << (q - tcpflags);
	}
	return (f ? f : PF_TH_ALL);
}

void
set_ipmask(struct node_host *h, int bb)
{
	struct pf_addr	*m, *n;
	int		 i, j = 0;
	u_int8_t	 b;

	m = &h->addr.v.a.mask;
	memset(m, 0, sizeof(*m));

	if (bb == -1)
		b = h->af == AF_INET ? 32 : 128;
	else
		b = bb;

	while (b >= 32) {
		m->addr32[j++] = 0xffffffff;
		b -= 32;
	}
	for (i = 31; i > 31-b; --i)
		m->addr32[j] |= (1 << i);
	if (b)
		m->addr32[j] = htonl(m->addr32[j]);

	/* Mask off bits of the address that will never be used. */
	n = &h->addr.v.a.addr;
	if (h->addr.type == PF_ADDR_ADDRMASK)
		for (i = 0; i < 4; i++)
			n->addr32[i] = n->addr32[i] & m->addr32[i];
}

int
check_netmask(struct node_host *h, sa_family_t af)
{
	struct node_host	*n = NULL;
	struct pf_addr		*m;

	for (n = h; n != NULL; n = n->next) {
		if (h->addr.type == PF_ADDR_TABLE)
			continue;
		m = &h->addr.v.a.mask;
		/* netmasks > 32 bit are invalid on v4 */
		if (af == AF_INET &&
		    (m->addr32[1] || m->addr32[2] || m->addr32[3])) {
			fprintf(stderr, "netmask %u invalid for IPv4 address\n",
			    unmask(m));
			return (1);
		}
	}
	return (0);
}

struct node_host *
gen_dynnode(struct node_host *h, sa_family_t af)
{
	struct node_host	*n;

	if (h->addr.type != PF_ADDR_DYNIFTL)
		return (NULL);

	if ((n = calloc(1, sizeof(*n))) == NULL)
		return (NULL);
	bcopy(h, n, sizeof(*n));
	n->ifname = NULL;
	n->next = NULL;
	n->tail = NULL;

	/* fix up netmask */
	if (af == AF_INET && unmask(&n->addr.v.a.mask) > 32)
		set_ipmask(n, 32);

	return (n);
}

/* interface lookup routines */

struct node_host	*iftab;

void
ifa_load(void)
{
	struct ifaddrs		*ifap, *ifa;
	struct node_host	*n = NULL, *h = NULL;

	if (getifaddrs(&ifap) == -1)
		err(1, "getifaddrs");

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    !(ifa->ifa_addr->sa_family == AF_INET ||
		    ifa->ifa_addr->sa_family == AF_INET6 ||
		    ifa->ifa_addr->sa_family == AF_LINK))
				continue;
		n = calloc(1, sizeof(struct node_host));
		if (n == NULL)
			err(1, "%s: calloc", __func__);
		n->af = ifa->ifa_addr->sa_family;
		n->ifa_flags = ifa->ifa_flags;
#ifdef __KAME__
		if (n->af == AF_INET6 &&
		    IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6 *)
		    ifa->ifa_addr)->sin6_addr) &&
		    ((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_scope_id ==
		    0) {
			struct sockaddr_in6	*sin6;

			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			sin6->sin6_scope_id = sin6->sin6_addr.s6_addr[2] << 8 |
			    sin6->sin6_addr.s6_addr[3];
			sin6->sin6_addr.s6_addr[2] = 0;
			sin6->sin6_addr.s6_addr[3] = 0;
		}
#endif
		n->ifindex = 0;
		if (n->af == AF_LINK)
			n->ifindex = ((struct sockaddr_dl *)
			    ifa->ifa_addr)->sdl_index;
		else {
			copy_satopfaddr(&n->addr.v.a.addr, ifa->ifa_addr);
			ifa->ifa_netmask->sa_family = ifa->ifa_addr->sa_family;
			copy_satopfaddr(&n->addr.v.a.mask, ifa->ifa_netmask);
			if (ifa->ifa_broadaddr != NULL) {
				ifa->ifa_broadaddr->sa_family = ifa->ifa_addr->sa_family;
				copy_satopfaddr(&n->bcast, ifa->ifa_broadaddr);
			}
			if (ifa->ifa_dstaddr != NULL) {
				ifa->ifa_dstaddr->sa_family = ifa->ifa_addr->sa_family;
				copy_satopfaddr(&n->peer, ifa->ifa_dstaddr);
			}
			if (n->af == AF_INET6)
				n->ifindex = ((struct sockaddr_in6 *)
				    ifa->ifa_addr)->sin6_scope_id;
		}
		if ((n->ifname = strdup(ifa->ifa_name)) == NULL)
			err(1, "%s: strdup", __func__);
		n->next = NULL;
		n->tail = n;
		if (h == NULL)
			h = n;
		else {
			h->tail->next = n;
			h->tail = n;
		}
	}

	iftab = h;
	freeifaddrs(ifap);
}

unsigned int
ifa_nametoindex(const char *ifa_name)
{
	struct node_host	*p;

	for (p = iftab; p; p = p->next) {
		if (p->af == AF_LINK && strcmp(p->ifname, ifa_name) == 0)
			return (p->ifindex);
	}
	errno = ENXIO;
	return (0);
}

char *
ifa_indextoname(unsigned int ifindex, char *ifa_name)
{
	struct node_host	*p;

	for (p = iftab; p; p = p->next) {
		if (p->af == AF_LINK && ifindex == p->ifindex) {
			strlcpy(ifa_name, p->ifname, IFNAMSIZ);
			return (ifa_name);
		}
	}
	errno = ENXIO;
	return (NULL);
}

struct node_host *
ifa_exists(const char *ifa_name)
{
	struct node_host	*n;
	struct ifgroupreq	ifgr;
	int			s;

	if (iftab == NULL)
		ifa_load();

	/* check whether this is a group */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "socket");
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifa_name, sizeof(ifgr.ifgr_name));
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == 0) {
		/* fake a node_host */
		if ((n = calloc(1, sizeof(*n))) == NULL)
			err(1, "calloc");
		if ((n->ifname = strdup(ifa_name)) == NULL)
			err(1, "strdup");
		close(s);
		return (n);
	}
	close(s);

	for (n = iftab; n; n = n->next) {
		if (n->af == AF_LINK && !strncmp(n->ifname, ifa_name, IFNAMSIZ))
			return (n);
	}

	return (NULL);
}

struct node_host *
ifa_grouplookup(const char *ifa_name, int flags)
{
	struct ifg_req		*ifg;
	struct ifgroupreq	 ifgr;
	int			 s, len;
	struct node_host	*n, *h = NULL;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "socket");
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifa_name, sizeof(ifgr.ifgr_name));
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1) {
		close(s);
		return (NULL);
	}

	len = ifgr.ifgr_len;
	if ((ifgr.ifgr_groups = calloc(1, len)) == NULL)
		err(1, "calloc");
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1)
		err(1, "SIOCGIFGMEMB");

	for (ifg = ifgr.ifgr_groups; ifg && len >= sizeof(struct ifg_req);
	    ifg++) {
		len -= sizeof(struct ifg_req);
		if ((n = ifa_lookup(ifg->ifgrq_member, flags)) == NULL)
			continue;
		if (h == NULL)
			h = n;
		else {
			h->tail->next = n;
			h->tail = n->tail;
		}
	}
	free(ifgr.ifgr_groups);
	close(s);

	return (h);
}

struct node_host *
ifa_lookup(const char *ifa_name, int flags)
{
	struct node_host	*p = NULL, *h = NULL, *n = NULL;
	int			 got4 = 0, got6 = 0;
	const char		 *last_if = NULL;

	if ((h = ifa_grouplookup(ifa_name, flags)) != NULL)
		return (h);

	if (!strncmp(ifa_name, "self", IFNAMSIZ))
		ifa_name = NULL;

	if (iftab == NULL)
		ifa_load();

	for (p = iftab; p; p = p->next) {
		if (ifa_skip_if(ifa_name, p))
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) && p->af != AF_INET)
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) &&
		    !(p->ifa_flags & IFF_BROADCAST))
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) && p->bcast.v4.s_addr == 0)
			continue;
		if ((flags & PFI_AFLAG_PEER) &&
		    !(p->ifa_flags & IFF_POINTOPOINT))
			continue;
		if ((flags & PFI_AFLAG_NETWORK) && p->ifindex > 0)
			continue;
		if (last_if == NULL || strcmp(last_if, p->ifname))
			got4 = got6 = 0;
		last_if = p->ifname;
		if ((flags & PFI_AFLAG_NOALIAS) && p->af == AF_INET && got4)
			continue;
		if ((flags & PFI_AFLAG_NOALIAS) && p->af == AF_INET6 && got6)
			continue;
		if (p->af == AF_INET)
			got4 = 1;
		else
			got6 = 1;
		n = calloc(1, sizeof(struct node_host));
		if (n == NULL)
			err(1, "%s: calloc", __func__);
		n->af = p->af;
		if (flags & PFI_AFLAG_BROADCAST)
			memcpy(&n->addr.v.a.addr, &p->bcast,
			    sizeof(struct pf_addr));
		else if (flags & PFI_AFLAG_PEER)
			memcpy(&n->addr.v.a.addr, &p->peer,
			    sizeof(struct pf_addr));
		else
			memcpy(&n->addr.v.a.addr, &p->addr.v.a.addr,
			    sizeof(struct pf_addr));
		if (flags & PFI_AFLAG_NETWORK)
			set_ipmask(n, unmask(&p->addr.v.a.mask));
		else
			set_ipmask(n, -1);
		n->ifindex = p->ifindex;

		n->next = NULL;
		n->tail = n;
		if (h == NULL)
			h = n;
		else {
			h->tail->next = n;
			h->tail = n;
		}
	}
	return (h);
}

int
ifa_skip_if(const char *filter, struct node_host *p)
{
	int	n;

	if (p->af != AF_INET && p->af != AF_INET6)
		return (1);
	if (filter == NULL || !*filter)
		return (0);
	if (!strcmp(p->ifname, filter))
		return (0);	/* exact match */
	n = strlen(filter);
	if (n < 1 || n >= IFNAMSIZ)
		return (1);	/* sanity check */
	if (filter[n-1] >= '0' && filter[n-1] <= '9')
		return (1);	/* only do exact match in that case */
	if (strncmp(p->ifname, filter, n))
		return (1);	/* prefix doesn't match */
	return (p->ifname[n] < '0' || p->ifname[n] > '9');
}

struct node_host *
host(const char *s, int opts)
{
	struct node_host	*h = NULL, *n;
	int			 mask = -1;
	char			*p, *ps;
	const char		*errstr;

	if ((ps = strdup(s)) == NULL)
		err(1, "%s: strdup", __func__);

	if ((p = strchr(ps, '/')) != NULL) {
		mask = strtonum(p+1, 0, 128, &errstr);
		if (errstr) {
			fprintf(stderr, "netmask is %s: %s\n", errstr, p);
			goto error;
		}
		p[0] = '\0';
	}

	if ((h = host_if(ps, mask)) == NULL &&
	    (h = host_ip(ps, mask)) == NULL &&
	    (h = host_dns(ps, mask, (opts & PF_OPT_NODNS))) == NULL) {
		fprintf(stderr, "no IP address found for %s\n", s);
		goto error;
	}

	for (n = h; n != NULL; n = n->next) {
		n->addr.type = PF_ADDR_ADDRMASK;
		n->weight = 0;
	}

error:
	free(ps);
	return (h);
}

struct node_host *
host_if(const char *s, int mask)
{
	struct node_host	*n, *h = NULL;
	char			*p, *ps;
	int			 flags = 0;

	if ((ps = strdup(s)) == NULL)
		err(1, "host_if: strdup");
	while ((p = strrchr(ps, ':')) != NULL) {
		if (!strcmp(p+1, "network"))
			flags |= PFI_AFLAG_NETWORK;
		else if (!strcmp(p+1, "broadcast"))
			flags |= PFI_AFLAG_BROADCAST;
		else if (!strcmp(p+1, "peer"))
			flags |= PFI_AFLAG_PEER;
		else if (!strcmp(p+1, "0"))
			flags |= PFI_AFLAG_NOALIAS;
		else
			goto error;
		*p = '\0';
	}
	if (flags & (flags - 1) & PFI_AFLAG_MODEMASK) { /* Yep! */
		fprintf(stderr, "illegal combination of interface modifiers\n");
		goto error;
	}
	if ((flags & (PFI_AFLAG_NETWORK|PFI_AFLAG_BROADCAST)) && mask > -1) {
		fprintf(stderr, "network or broadcast lookup, but "
		    "extra netmask given\n");
		goto error;
	}
	if (ifa_exists(ps) || !strncmp(ps, "self", IFNAMSIZ)) {
		/* interface with this name exists */
		h = ifa_lookup(ps, flags);
		if (mask > -1)
			for (n = h; n != NULL; n = n->next)
				set_ipmask(n, mask);
	}

error:
	free(ps);
	return (h);
}

struct node_host *
host_ip(const char *s, int mask)
{
	struct addrinfo		 hints, *res;
	struct node_host	*h = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, NULL, &hints, &res) == 0) {
		h = calloc(1, sizeof(*h));
		if (h == NULL)
			err(1, "%s: calloc", __func__);
		h->af = res->ai_family;
		copy_satopfaddr(&h->addr.v.a.addr, res->ai_addr);
		if (h->af == AF_INET6)
			h->ifindex =
			    ((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id;
		freeaddrinfo(res);
	} else {	/* ie. for 10/8 parsing */
		if (mask == -1)
			return (NULL);
		h = calloc(1, sizeof(*h));
		if (h == NULL)
			err(1, "%s: calloc", __func__);
		h->af = AF_INET;
		if (inet_net_pton(AF_INET, s, &h->addr.v.a.addr.v4,
		    sizeof(h->addr.v.a.addr.v4)) == -1) {
			free(h);
			return (NULL);
		}
	}
	set_ipmask(h, mask);
	h->ifname = NULL;
	h->next = NULL;
	h->tail = h;

	return (h);
}

struct node_host *
host_dns(const char *s, int mask, int numeric)
{
	struct addrinfo		 hints, *res0, *res;
	struct node_host	*n, *h = NULL;
	int			 noalias = 0, got4 = 0, got6 = 0;
	char			*p, *ps;

	if ((ps = strdup(s)) == NULL)
		err(1, "host_dns: strdup");
	if ((p = strrchr(ps, ':')) != NULL && !strcmp(p, ":0")) {
		noalias = 1;
		*p = '\0';
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM; /* DUMMY */
	if (numeric)
		hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(ps, NULL, &hints, &res0) != 0)
		goto error;

	for (res = res0; res; res = res->ai_next) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;
		if (noalias) {
			if (res->ai_family == AF_INET) {
				if (got4)
					continue;
				got4 = 1;
			} else {
				if (got6)
					continue;
				got6 = 1;
			}
		}
		n = calloc(1, sizeof(struct node_host));
		if (n == NULL)
			err(1, "host_dns: calloc");
		n->ifname = NULL;
		n->af = res->ai_family;
		copy_satopfaddr(&n->addr.v.a.addr, res->ai_addr);
		if (res->ai_family == AF_INET6)
			n->ifindex =
			    ((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id;
		set_ipmask(n, mask);
		n->next = NULL;
		n->tail = n;
		if (h == NULL)
			h = n;
		else {
			h->tail->next = n;
			h->tail = n;
		}
	}
	freeaddrinfo(res0);
error:
	free(ps);

	return (h);
}

/*
 * convert a hostname to a list of addresses and put them in the given buffer.
 * test:
 *	if set to 1, only simple addresses are accepted (no netblock, no "!").
 */
int
append_addr(struct pfr_buffer *b, char *s, int test, int opts)
{
	static int		 previous = 0;
	static int		 expect = 0;
	struct pfr_addr		*a;
	struct node_host	*h, *n;
	char			*r;
	const char		*errstr;
	int			 rv, not = 0, i = 0;
	u_int16_t		 weight;

	/* skip weight if given */
	if (strcmp(s, "weight") == 0) {
		expect = 1;
		return (1); /* expecting further call */
	}

	/* check if previous host is set */
	if (expect) {
		/* parse and append load balancing weight */
		weight = strtonum(s, 1, USHRT_MAX, &errstr);
		if (errstr) {
			fprintf(stderr, "failed to convert weight %s\n", s);
			return (-1);
		}
		if (previous != -1) {
			PFRB_FOREACH(a, b) {
				if (++i >= previous) {
					a->pfra_weight = weight;
					a->pfra_type = PFRKE_COST;
				}
			}
		}
		expect = 0;
		return (0);
	}

	for (r = s; *r == '!'; r++)
		not = !not;
	if ((n = host(r, opts)) == NULL) {
		errno = 0;
		return (-1);
	}
	rv = append_addr_host(b, n, test, not);
	previous = b->pfrb_size;
	do {
		h = n;
		n = n->next;
		free(h);
	} while (n != NULL);
	return (rv);
}

/*
 * same as previous function, but with a pre-parsed input and the ability
 * to "negate" the result. Does not free the node_host list.
 * not:
 *      setting it to 1 is equivalent to adding "!" in front of parameter s.
 */
int
append_addr_host(struct pfr_buffer *b, struct node_host *n, int test, int not)
{
	int			 bits;
	struct pfr_addr		 addr;

	do {
		bzero(&addr, sizeof(addr));
		addr.pfra_not = n->not ^ not;
		addr.pfra_af = n->af;
		addr.pfra_net = unmask(&n->addr.v.a.mask);
		if (n->ifname) {
			if (strlcpy(addr.pfra_ifname, n->ifname,
			   sizeof(addr.pfra_ifname)) >= sizeof(addr.pfra_ifname))
				errx(1, "append_addr_host: strlcpy");
			addr.pfra_type = PFRKE_ROUTE;
		}
		if (n->weight > 0) {
			addr.pfra_weight = n->weight;
			addr.pfra_type = PFRKE_COST;
		}
		switch (n->af) {
		case AF_INET:
			addr.pfra_ip4addr.s_addr = n->addr.v.a.addr.addr32[0];
			bits = 32;
			break;
		case AF_INET6:
			memcpy(&addr.pfra_ip6addr, &n->addr.v.a.addr.v6,
			    sizeof(struct in6_addr));
			bits = 128;
			break;
		default:
			errno = EINVAL;
			return (-1);
		}
		if ((test && (not || addr.pfra_net != bits)) ||
		    addr.pfra_net > bits) {
			errno = EINVAL;
			return (-1);
		}
		if (pfr_buf_add(b, &addr))
			return (-1);
	} while ((n = n->next) != NULL);

	return (0);
}

int
pfctl_add_trans(struct pfr_buffer *buf, int type, const char *anchor)
{
	struct pfioc_trans_e trans;

	bzero(&trans, sizeof(trans));
	trans.type = type;
	if (strlcpy(trans.anchor, anchor,
	    sizeof(trans.anchor)) >= sizeof(trans.anchor))
		errx(1, "pfctl_add_trans: strlcpy");

	return pfr_buf_add(buf, &trans);
}

u_int32_t
pfctl_get_ticket(struct pfr_buffer *buf, int type, const char *anchor)
{
	struct pfioc_trans_e *p;

	PFRB_FOREACH(p, buf)
		if (type == p->type && !strcmp(anchor, p->anchor))
			return (p->ticket);
	errx(1, "pfctl_get_ticket: assertion failed");
}

int
pfctl_trans(int dev, struct pfr_buffer *buf, u_long cmd, int from)
{
	struct pfioc_trans trans;

	bzero(&trans, sizeof(trans));
	trans.size = buf->pfrb_size - from;
	trans.esize = sizeof(struct pfioc_trans_e);
	trans.array = ((struct pfioc_trans_e *)buf->pfrb_caddr) + from;
	return ioctl(dev, cmd, &trans);
}
