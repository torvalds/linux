/*	$OpenBSD: parser.c,v 1.138 2025/03/10 14:08:25 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2016 Job Snijders <job@instituut.net>
 * Copyright (c) 2016 Peter Hessler <phessler@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"

enum token_type {
	ENDTOKEN,
	NOTOKEN,
	ANYTOKEN,
	KEYWORD,
	ADDRESS,
	PEERADDRESS,
	FLAG,
	ASNUM,
	ASTYPE,
	PREFIX,
	PEERDESC,
	GROUPDESC,
	RIBNAME,
	COMMUNICATION,
	COMMUNITY,
	EXTCOMMUNITY,
	LRGCOMMUNITY,
	LOCALPREF,
	MED,
	NEXTHOP,
	PFTABLE,
	PREPNBR,
	PREPSELF,
	WEIGHT,
	RD,
	FAMILY,
	RTABLE,
	FILENAME,
	PATHID,
	FLOW_PROTO,
	FLOW_SRC,
	FLOW_DST,
	FLOW_SRCPORT,
	FLOW_DSTPORT,
	FLOW_ICMPTYPE,
	FLOW_ICMPCODE,
	FLOW_LENGTH,
	FLOW_DSCP,
	FLOW_FLAGS,
	FLOW_FRAGS,
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static const struct token *prevtable;

static const struct token t_main[];
static const struct token t_show[];
static const struct token t_show_summary[];
static const struct token t_show_fib[];
static const struct token t_show_rib[];
static const struct token t_show_avs[];
static const struct token t_show_ovs[];
static const struct token t_show_mrt[];
static const struct token t_show_mrt_file[];
static const struct token t_show_rib_neigh[];
static const struct token t_show_mrt_neigh[];
static const struct token t_show_rib_rib[];
static const struct token t_show_neighbor[];
static const struct token t_show_neighbor_modifiers[];
static const struct token t_fib[];
static const struct token t_neighbor[];
static const struct token t_neighbor_modifiers[];
static const struct token t_show_rib_as[];
static const struct token t_show_mrt_as[];
static const struct token t_show_prefix[];
static const struct token t_show_ip[];
static const struct token t_network[];
static const struct token t_flowspec[];
static const struct token t_flowfamily[];
static const struct token t_flowrule[];
static const struct token t_flowsrc[];
static const struct token t_flowdst[];
static const struct token t_flowsrcport[];
static const struct token t_flowdstport[];
static const struct token t_flowicmp[];
static const struct token t_bulk[];
static const struct token t_network_show[];
static const struct token t_prefix[];
static const struct token t_set[];
static const struct token t_nexthop[];
static const struct token t_pftable[];
static const struct token t_log[];
static const struct token t_communication[];

static const struct token t_main[] = {
	{ KEYWORD,	"fib",		FIB,		t_fib},
	{ KEYWORD,	"flowspec",	NONE,		t_flowspec},
	{ KEYWORD,	"log",		NONE,		t_log},
	{ KEYWORD,	"neighbor",	NEIGHBOR,	t_neighbor},
	{ KEYWORD,	"network",	NONE,		t_network},
	{ KEYWORD,	"reload",	RELOAD,		t_communication},
	{ KEYWORD,	"show",		SHOW,		t_show},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show[] = {
	{ NOTOKEN,	"",		NONE,		NULL},
	{ KEYWORD,	"fib",		SHOW_FIB,	t_show_fib},
	{ KEYWORD,	"flowspec",	FLOWSPEC_SHOW,	t_network_show},
	{ KEYWORD,	"interfaces",	SHOW_INTERFACE,	NULL},
	{ KEYWORD,	"ip",		NONE,		t_show_ip},
	{ KEYWORD,	"metrics",	SHOW_METRICS,	NULL},
	{ KEYWORD,	"mrt",		SHOW_MRT,	t_show_mrt},
	{ KEYWORD,	"neighbor",	SHOW_NEIGHBOR,	t_show_neighbor},
	{ KEYWORD,	"network",	NETWORK_SHOW,	t_network_show},
	{ KEYWORD,	"nexthop",	SHOW_NEXTHOP,	NULL},
	{ KEYWORD,	"rib",		SHOW_RIB,	t_show_rib},
	{ KEYWORD,	"rtr",		SHOW_RTR,	NULL},
	{ KEYWORD,	"sets",		SHOW_SET,	NULL},
	{ KEYWORD,	"summary",	SHOW_SUMMARY,	t_show_summary},
	{ KEYWORD,	"tables",	SHOW_FIB_TABLES, NULL},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_summary[] = {
	{ NOTOKEN,	"",		NONE,			NULL},
	{ KEYWORD,	"terse",	SHOW_SUMMARY_TERSE,	NULL},
	{ ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_show_fib[] = {
	{ NOTOKEN,	"",		NONE,		NULL},
	{ FLAG,		"bgp",		F_BGPD,		t_show_fib},
	{ FLAG,		"connected",	F_CONNECTED,	t_show_fib},
	{ FLAG,		"nexthop",	F_NEXTHOP,	t_show_fib},
	{ FLAG,		"static",	F_STATIC,	t_show_fib},
	{ RTABLE,	"table",	NONE,		t_show_fib},
	{ FAMILY,	"",		NONE,		t_show_fib},
	{ ADDRESS,	"",		NONE,		NULL},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_rib[] = {
	{ NOTOKEN,	"",		NONE,		NULL},
	{ ASTYPE,	"as",		AS_ALL,		t_show_rib_as},
	{ KEYWORD,	"avs",		NONE,		t_show_avs},
	{ FLAG,		"best",		F_CTL_BEST,	t_show_rib},
	{ COMMUNITY,	"community",	NONE,		t_show_rib},
	{ FLAG,		"detail",	F_CTL_DETAIL,	t_show_rib},
	{ FLAG,		"disqualified",	F_CTL_INELIGIBLE, t_show_rib},
	{ ASTYPE,	"empty-as",	AS_EMPTY,	t_show_rib},
	{ FLAG,		"error",	F_CTL_INVALID,	t_show_rib},
	{ EXTCOMMUNITY,	"ext-community", NONE,		t_show_rib},
	{ FLAG,		"filtered",	F_CTL_FILTERED,	t_show_rib},
	{ FLAG,		"in",		F_CTL_ADJ_IN,	t_show_rib},
	{ LRGCOMMUNITY,	"large-community", NONE,	t_show_rib},
	{ FLAG,		"leaked",	F_CTL_LEAKED,	t_show_rib},
	{ KEYWORD,	"memory",	SHOW_RIB_MEM,	NULL},
	{ KEYWORD,	"neighbor",	NONE,		t_show_rib_neigh},
	{ FLAG,		"out",		F_CTL_ADJ_OUT,	t_show_rib},
	{ KEYWORD,	"ovs",		NONE,		t_show_ovs},
	{ PATHID,	"path-id",	NONE,		t_show_rib},
	{ ASTYPE,	"peer-as",	AS_PEER,	t_show_rib_as},
	{ FLAG,		"selected",	F_CTL_BEST,	t_show_rib},
	{ ASTYPE,	"source-as",	AS_SOURCE,	t_show_rib_as},
	{ FLAG,		"ssv",		F_CTL_SSV,	t_show_rib},
	{ KEYWORD,	"summary",	SHOW_SUMMARY,	t_show_summary},
	{ KEYWORD,	"table",	NONE,		t_show_rib_rib},
	{ ASTYPE,	"transit-as",	AS_TRANSIT,	t_show_rib_as},
	{ FAMILY,	"",		NONE,		t_show_rib},
	{ PREFIX,	"",		NONE,		t_show_prefix},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_avs[] = {
	{ FLAG,		"invalid",	F_CTL_AVS_INVALID,	t_show_rib},
	{ FLAG,		"unknown",	F_CTL_AVS_UNKNOWN,	t_show_rib},
	{ FLAG,		"valid"	,	F_CTL_AVS_VALID,	t_show_rib},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_ovs[] = {
	{ FLAG,		"invalid",	F_CTL_OVS_INVALID,	t_show_rib},
	{ FLAG,		"not-found",	F_CTL_OVS_NOTFOUND,	t_show_rib},
	{ FLAG,		"valid"	,	F_CTL_OVS_VALID,	t_show_rib},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_mrt[] = {
	{ NOTOKEN,	"",		NONE,		NULL},
	{ ASTYPE,	"as",		AS_ALL,		t_show_mrt_as},
	{ FLAG,		"detail",	F_CTL_DETAIL,	t_show_mrt},
	{ ASTYPE,	"empty-as",	AS_EMPTY,	t_show_mrt},
	{ KEYWORD,	"file",		NONE,		t_show_mrt_file},
	{ KEYWORD,	"neighbor",	NONE,		t_show_mrt_neigh},
	{ ASTYPE,	"peer-as",	AS_PEER,	t_show_mrt_as},
	{ FLAG,		"peers",	F_CTL_NEIGHBORS,t_show_mrt},
	{ ASTYPE,	"source-as",	AS_SOURCE,	t_show_mrt_as},
	{ FLAG,		"ssv",		F_CTL_SSV,	t_show_mrt},
	{ ASTYPE,	"transit-as",	AS_TRANSIT,	t_show_mrt_as},
	{ FAMILY,	"",		NONE,		t_show_mrt},
	{ PREFIX,	"",		NONE,		t_show_prefix},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_mrt_file[] = {
	{ FILENAME,	"",		NONE,		t_show_mrt},
	{ ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_show_rib_neigh_group[] = {
	{ GROUPDESC,	"",		NONE,	t_show_rib},
	{ ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_show_rib_neigh[] = {
	{ KEYWORD,	"group",	NONE,	t_show_rib_neigh_group},
	{ PEERADDRESS,	"",		NONE,	t_show_rib},
	{ PEERDESC,	"",		NONE,	t_show_rib},
	{ ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_show_mrt_neigh[] = {
	{ PEERADDRESS,	"",		NONE,	t_show_mrt},
	{ ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_show_rib_rib[] = {
	{ RIBNAME,	"",		NONE,	t_show_rib},
	{ ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_show_neighbor_modifiers[] = {
	{ NOTOKEN,	"",		NONE,			NULL},
	{ KEYWORD,	"messages",	SHOW_NEIGHBOR,		NULL},
	{ KEYWORD,	"terse",	SHOW_NEIGHBOR_TERSE,	NULL},
	{ KEYWORD,	"timers",	SHOW_NEIGHBOR_TIMERS,	NULL},
	{ ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_show_neighbor_group[] = {
	{ GROUPDESC,	"",		NONE,	t_show_neighbor_modifiers},
	{ ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_show_neighbor[] = {
	{ NOTOKEN,	"",		NONE,	NULL},
	{ KEYWORD,	"group",	NONE,	t_show_neighbor_group},
	{ PEERADDRESS,	"",		NONE,	t_show_neighbor_modifiers},
	{ PEERDESC,	"",		NONE,	t_show_neighbor_modifiers},
	{ ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_fib[] = {
	{ KEYWORD,	"couple",	FIB_COUPLE,	NULL},
	{ KEYWORD,	"decouple",	FIB_DECOUPLE,	NULL},
	{ RTABLE,	"table",	NONE,		t_fib},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_neighbor_group[] = {
	{ GROUPDESC,	"",		NONE,		t_neighbor_modifiers},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_neighbor[] = {
	{ KEYWORD,	"group",	NONE,		t_neighbor_group},
	{ PEERADDRESS,	"",		NONE,		t_neighbor_modifiers},
	{ PEERDESC,	"",		NONE,		t_neighbor_modifiers},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_communication[] = {
	{ NOTOKEN,	"",		NONE,		NULL},
	{ COMMUNICATION, "",		NONE,		NULL},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_neighbor_modifiers[] = {
	{ KEYWORD,	"clear",	NEIGHBOR_CLEAR,	t_communication},
	{ KEYWORD,	"destroy",	NEIGHBOR_DESTROY,	NULL},
	{ KEYWORD,	"down",		NEIGHBOR_DOWN,	t_communication},
	{ KEYWORD,	"refresh",	NEIGHBOR_RREFRESH,	NULL},
	{ KEYWORD,	"up",		NEIGHBOR_UP,		NULL},
	{ ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_show_rib_as[] = {
	{ ASNUM,	"",		NONE,		t_show_rib},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_mrt_as[] = {
	{ ASNUM,	"",		NONE,		t_show_mrt},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_prefix[] = {
	{ FLAG,		"all",		F_LONGER,	t_show_rib},
	{ FLAG,		"longer-prefixes", F_LONGER,	t_show_rib},
	{ FLAG,		"or-longer",	F_LONGER,	t_show_rib},
	{ FLAG,		"or-shorter",	F_SHORTER,	t_show_rib},
	{ ANYTOKEN,	"",		NONE,		t_show_rib},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_ip[] = {
	{ KEYWORD,	"bgp",		SHOW_RIB,	t_show_rib},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_network[] = {
	{ KEYWORD,	"add",		NETWORK_ADD,	t_prefix},
	{ KEYWORD,	"bulk",		NONE,		t_bulk},
	{ KEYWORD,	"delete",	NETWORK_REMOVE,	t_prefix},
	{ KEYWORD,	"flush",	NETWORK_FLUSH,	NULL},
	{ KEYWORD,	"mrt",		NETWORK_MRT,	t_show_mrt},
	{ KEYWORD,	"show",		NETWORK_SHOW,	t_network_show},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowspec[] = {
	{ KEYWORD,	"add",		FLOWSPEC_ADD,	t_flowfamily},
	{ KEYWORD,	"delete",	FLOWSPEC_REMOVE,t_flowfamily},
	{ KEYWORD,	"flush",	FLOWSPEC_FLUSH,	NULL},
	{ KEYWORD,	"show",		FLOWSPEC_SHOW,	t_network_show},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowfamily[] = {
	{ FAMILY,	"",		NONE,		t_flowrule},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowrule[] = {
	{ NOTOKEN,	"",		NONE,		NULL},
	{ FLOW_FLAGS,	"flags",	NONE,		t_flowrule},
	{ FLOW_FRAGS,	"fragment",	NONE,		t_flowrule},
	{ KEYWORD,	"from",		NONE,		t_flowsrc},
	{ FLOW_ICMPTYPE,"icmp-type",	NONE,		t_flowicmp},
	{ FLOW_LENGTH,	"length",	NONE,		t_flowrule},
	{ FLOW_PROTO,	"proto",	NONE,		t_flowrule},
	{ KEYWORD,	"set",		NONE,		t_set},
	{ KEYWORD,	"to",		NONE,		t_flowdst},
	{ FLOW_DSCP,	"dscp",		NONE,		t_flowrule},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowsrc[] = {
	{ KEYWORD,	"any",		NONE,		t_flowsrcport},
	{ FLOW_SRC,	"",		NONE,		t_flowsrcport},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowdst[] = {
	{ KEYWORD,	"any",		NONE,		t_flowdstport},
	{ FLOW_DST,	"",		NONE,		t_flowdstport},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowsrcport[] = {
	{ FLOW_SRCPORT,	"port",		NONE,		t_flowrule},
	{ ANYTOKEN,	"",		NONE,		t_flowrule},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowdstport[] = {
	{ FLOW_DSTPORT,	"port",		NONE,		t_flowrule},
	{ ANYTOKEN,	"",		NONE,		t_flowrule},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_flowicmp[] = {
	{ FLOW_ICMPCODE,"code",		NONE,		t_flowrule},
	{ ANYTOKEN,	"",		NONE,		t_flowrule},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_bulk[] = {
	{ KEYWORD,	"add",		NETWORK_BULK_ADD,	t_set},
	{ KEYWORD,	"delete",	NETWORK_BULK_REMOVE,	NULL},
	{ ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_prefix[] = {
	{ PREFIX,	"",		NONE,		t_set},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_network_show[] = {
	{ NOTOKEN,	"",		NONE,		NULL},
	{ FAMILY,	"",		NONE,		NULL},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_rd[] = {
	{ RD,		"",			NONE,	t_set},
	{ ENDTOKEN,	"",			NONE,	NULL}
};

static const struct token t_set[] = {
	{ NOTOKEN,	"",			NONE,	NULL},
	{ COMMUNITY,	"community",		NONE,	t_set},
	{ EXTCOMMUNITY,	"ext-community",	NONE,	t_set},
	{ LRGCOMMUNITY,	"large-community",	NONE,	t_set},
	{ LOCALPREF,	"localpref",		NONE,	t_set},
	{ MED,		"med",			NONE,	t_set},
	{ MED,		"metric",		NONE,	t_set},
	{ KEYWORD,	"nexthop",		NONE,	t_nexthop},
	{ KEYWORD,	"pftable",		NONE,	t_pftable},
	{ PREPNBR,	"prepend-neighbor",	NONE,	t_set},
	{ PREPSELF,	"prepend-self",		NONE,	t_set},
	{ KEYWORD,	"rd",			NONE,	t_rd},
	{ WEIGHT,	"weight",		NONE,	t_set},
	{ ENDTOKEN,	"",			NONE,	NULL}
};

static const struct token t_nexthop[] = {
	{ NEXTHOP,	"",			NONE,	t_set},
	{ ENDTOKEN,	"",			NONE,	NULL}
};

static const struct token t_pftable[] = {
	{ PFTABLE,	"",			NONE,	t_set},
	{ ENDTOKEN,	"",			NONE,	NULL}
};

static const struct token t_log[] = {
	{ KEYWORD,	"brief",	LOG_BRIEF,	NULL},
	{ KEYWORD,	"verbose",	LOG_VERBOSE,	NULL},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static struct parse_result	res;

const struct token	*match_token(int, char *[], const struct token [],
			    int *);
void			 show_valid_args(const struct token []);

int	parse_addr(const char *, struct bgpd_addr *);
int	parse_asnum(const char *, size_t, uint32_t *);
int	parse_number(const char *, struct parse_result *, enum token_type);
void	parsecommunity(struct community *c, char *s);
void	parselargecommunity(struct community *c, char *s);
void	parseextcommunity(struct community *c, const char *t, char *s);
int	parse_nexthop(const char *, struct parse_result *);
int	parse_flow_numop(int, char *[], struct parse_result *, enum token_type);

struct parse_result *
parse(int argc, char *argv[])
{
	const struct token	*table = t_main;
	const struct token	*match;
	int			 used;

	memset(&res, 0, sizeof(res));
	res.rtableid = getrtable();
	TAILQ_INIT(&res.set);

	while (argc >= 0) {
		if ((match = match_token(argc, argv, table, &used)) == NULL) {
			fprintf(stderr, "valid commands/args:\n");
			show_valid_args(table);
			return (NULL);
		}
		if (match->type == ANYTOKEN) {
			if (prevtable == NULL)
				prevtable = table;
			table = match->next;
			continue;
		}

		argc -= used;
		argv += used;

		if (match->type == NOTOKEN || match->next == NULL)
			break;
		table = match->next;
	}

	if (argc > 0) {
		fprintf(stderr, "superfluous argument: %s\n", argv[0]);
		return (NULL);
	}

	return (&res);
}

const struct token *
match_token(int argc, char *argv[], const struct token table[], int *argsused)
{
	u_int			 i, match;
	const struct token	*t = NULL;
	struct filter_set	*fs;
	const char		*word = argv[0];
	size_t			 wordlen = 0;

	*argsused = 1;
	match = 0;
	if (word != NULL)
		wordlen = strlen(word);
	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			if (word == NULL || wordlen == 0) {
				match++;
				t = &table[i];
			}
			break;
		case ANYTOKEN:
			/* match anything if nothing else matched before */
			if (match == 0) {
				match++;
				t = &table[i];
			}
			break;
		case KEYWORD:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0) {
				match++;
				t = &table[i];
				if (t->value)
					res.action = t->value;
			}
			break;
		case FLAG:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0) {
				match++;
				t = &table[i];
				res.flags |= t->value;
			}
			break;
		case FAMILY:
			if (word == NULL)
				break;
			if (!strcmp(word, "inet") ||
			    !strcasecmp(word, "IPv4")) {
				match++;
				t = &table[i];
				res.aid = AID_INET;
			}
			if (!strcmp(word, "inet6") ||
			    !strcasecmp(word, "IPv6")) {
				match++;
				t = &table[i];
				res.aid = AID_INET6;
			}
			if (!strcasecmp(word, "VPNv4")) {
				match++;
				t = &table[i];
				res.aid = AID_VPN_IPv4;
			}
			if (!strcasecmp(word, "VPNv6")) {
				match++;
				t = &table[i];
				res.aid = AID_VPN_IPv6;
			}
			break;
		case ADDRESS:
			if (parse_addr(word, &res.addr)) {
				match++;
				t = &table[i];
			}
			break;
		case PEERADDRESS:
			if (parse_addr(word, &res.peeraddr)) {
				match++;
				t = &table[i];
			}
			break;
		case FLOW_SRC:
			if (parse_prefix(word, wordlen, &res.flow.src,
			    &res.flow.srclen)) {
				match++;
				t = &table[i];
				if (res.aid != res.flow.src.aid)
					errx(1, "wrong address family in "
					    "flowspec rule");
			}
			break;
		case FLOW_DST:
			if (parse_prefix(word, wordlen, &res.flow.dst,
			    &res.flow.dstlen)) {
				match++;
				t = &table[i];
				if (res.aid != res.flow.dst.aid)
					errx(1, "wrong address family in "
					    "flowspec rule");
			}
			break;
		case PREFIX:
			if (parse_prefix(word, wordlen, &res.addr,
			    &res.prefixlen)) {
				match++;
				t = &table[i];
			}
			break;
		case ASTYPE:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0) {
				match++;
				t = &table[i];
				res.as.type = t->value;
			}
			break;
		case ASNUM:
			if (parse_asnum(word, wordlen, &res.as.as_min)) {
				res.as.as_max = res.as.as_min;
				match++;
				t = &table[i];
			}
			break;
		case GROUPDESC:
			res.is_group = 1;
			/* FALLTHROUGH */
		case PEERDESC:
			if (!match && word != NULL && wordlen > 0) {
				if (strlcpy(res.peerdesc, word,
				    sizeof(res.peerdesc)) >=
				    sizeof(res.peerdesc))
					errx(1, "neighbor description too "
					    "long");
				match++;
				t = &table[i];
			}
			break;
		case RIBNAME:
			if (!match && word != NULL && wordlen > 0) {
				if (strlcpy(res.rib, word, sizeof(res.rib)) >=
				    sizeof(res.rib))
					errx(1, "rib name too long");
				match++;
				t = &table[i];
			}
			break;
		case COMMUNICATION:
			if (!match && word != NULL && wordlen > 0) {
				if (strlcpy(res.reason, word,
				    sizeof(res.reason)) >=
				    sizeof(res.reason))
					errx(1, "shutdown reason too long");
				match++;
				t = &table[i];
			}
			break;
		case COMMUNITY:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0 && argc > 1) {
				parsecommunity(&res.community, argv[1]);
				*argsused += 1;

				if ((fs = calloc(1, sizeof(*fs))) == NULL)
					err(1, NULL);
				fs->type = ACTION_SET_COMMUNITY;
				fs->action.community = res.community;
				TAILQ_INSERT_TAIL(&res.set, fs, entry);

				match++;
				t = &table[i];
			}
			break;
		case LRGCOMMUNITY:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0 && argc > 1) {
				parselargecommunity(&res.community, argv[1]);
				*argsused += 1;

				if ((fs = calloc(1, sizeof(*fs))) == NULL)
					err(1, NULL);
				fs->type = ACTION_SET_COMMUNITY;
				fs->action.community = res.community;
				TAILQ_INSERT_TAIL(&res.set, fs, entry);

				match++;
				t = &table[i];
			}
			break;
		case EXTCOMMUNITY:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0 && argc > 2) {
				parseextcommunity(&res.community,
				    argv[1], argv[2]);
				*argsused += 2;

				if ((fs = calloc(1, sizeof(*fs))) == NULL)
					err(1, NULL);
				fs->type = ACTION_SET_COMMUNITY;
				fs->action.community = res.community;
				TAILQ_INSERT_TAIL(&res.set, fs, entry);

				match++;
				t = &table[i];
			}
			break;
		case RD:
			if (word != NULL && wordlen > 0) {
				char *p = strdup(word);
				struct community ext = { 0 };
				uint64_t rd;

				if (p == NULL)
					err(1, NULL);
				parseextcommunity(&ext, "rt", p);
				free(p);

				switch (ext.data3 >> 8) {
				case EXT_COMMUNITY_TRANS_TWO_AS:
					rd = (0ULL << 48);
					rd |= ((uint64_t)ext.data1 & 0xffff)
					    << 32;
					rd |= (uint64_t)ext.data2;
					break;
				case EXT_COMMUNITY_TRANS_IPV4:
					rd = (1ULL << 48);
					rd |= (uint64_t)ext.data1 << 16;
					rd |= (uint64_t)ext.data2 & 0xffff;
					break;
				case EXT_COMMUNITY_TRANS_FOUR_AS:
					rd = (2ULL << 48);
					rd |= (uint64_t)ext.data1 << 16;
					rd |= (uint64_t)ext.data2 & 0xffff;
					break;
				default:
					errx(1, "bad encoding of rd");
				}
				res.rd = htobe64(rd);
				match++;
				t = &table[i];
			}
			break;
		case LOCALPREF:
		case MED:
		case PREPNBR:
		case PREPSELF:
		case WEIGHT:
		case RTABLE:
		case PATHID:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0 && argc > 1 &&
			    parse_number(argv[1], &res, table[i].type)) {
				*argsused += 1;
				match++;
				t = &table[i];
			}
			break;
		case NEXTHOP:
			if (word != NULL && wordlen > 0 &&
			    parse_nexthop(word, &res)) {
				match++;
				t = &table[i];
			}
			break;
		case PFTABLE:
			if (word != NULL && wordlen > 0) {
				if ((fs = calloc(1,
				    sizeof(struct filter_set))) == NULL)
					err(1, NULL);
				if (strlcpy(fs->action.pftable, word,
				    sizeof(fs->action.pftable)) >=
				    sizeof(fs->action.pftable))
					errx(1, "pftable name too long");
				TAILQ_INSERT_TAIL(&res.set, fs, entry);
				match++;
				t = &table[i];
			}
			break;
		case FILENAME:
			if (word != NULL && wordlen > 0) {
				if ((res.mrtfd = open(word, O_RDONLY)) == -1) {
					/*
					 * ignore error if path has no / and
					 * does not exist. In hope to print
					 * usage.
					 */
					if (errno == ENOENT &&
					    !strchr(word, '/'))
						break;
					err(1, "mrt open(%s)", word);
				}
				match++;
				t = &table[i];
			}
			break;
		case FLOW_SRCPORT:
		case FLOW_DSTPORT:
		case FLOW_PROTO:
		case FLOW_ICMPTYPE:
		case FLOW_ICMPCODE:
		case FLOW_LENGTH:
		case FLOW_DSCP:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0 && argc > 1) {
				*argsused += parse_flow_numop(argc, argv, &res,
				    table[i].type);

				match++;
				t = &table[i];
			}
			break;
		case FLOW_FLAGS:
		case FLOW_FRAGS:
			if (word != NULL && strncmp(word, table[i].keyword,
			    wordlen) == 0) {
				errx(1, "%s not yet implemented", word);
			}
			break;
		case ENDTOKEN:
			break;
		}
	}

	if (match != 1) {
		if (word == NULL)
			fprintf(stderr, "missing argument:\n");
		else if (match > 1)
			fprintf(stderr, "ambiguous argument: %s\n", word);
		else if (match < 1)
			fprintf(stderr, "unknown argument: %s\n", word);
		return (NULL);
	}

	return (t);
}

void
show_valid_args(const struct token table[])
{
	int	i;

	if (prevtable != NULL) {
		const struct token *t = prevtable;
		prevtable = NULL;
		show_valid_args(t);
		fprintf(stderr, "or any of\n");
	}

	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			fprintf(stderr, "  <cr>\n");
			break;
		case ANYTOKEN:
			break;
		case KEYWORD:
		case FLAG:
		case ASTYPE:
			fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case ADDRESS:
		case PEERADDRESS:
			fprintf(stderr, "  <address>\n");
			break;
		case PREFIX:
		case FLOW_SRC:
		case FLOW_DST:
			fprintf(stderr, "  <address>[/<len>]\n");
			break;
		case ASNUM:
			fprintf(stderr, "  <asnum>\n");
			break;
		case GROUPDESC:
		case PEERDESC:
			fprintf(stderr, "  <neighbor description>\n");
			break;
		case RIBNAME:
			fprintf(stderr, "  <rib name>\n");
			break;
		case COMMUNICATION:
			fprintf(stderr, "  <reason>\n");
			break;
		case COMMUNITY:
			fprintf(stderr, "  %s <community>\n",
			    table[i].keyword);
			break;
		case LRGCOMMUNITY:
			fprintf(stderr, "  %s <large-community>\n",
			    table[i].keyword);
			break;
		case EXTCOMMUNITY:
			fprintf(stderr, "  %s <extended-community>\n",
			    table[i].keyword);
			break;
		case RD:
			fprintf(stderr, "  <route-distinguisher>\n");
			break;
		case LOCALPREF:
		case MED:
		case PREPNBR:
		case PREPSELF:
		case WEIGHT:
		case RTABLE:
		case PATHID:
			fprintf(stderr, "  %s <number>\n", table[i].keyword);
			break;
		case NEXTHOP:
			fprintf(stderr, "  <address>\n");
			break;
		case PFTABLE:
			fprintf(stderr, "  <pftable>\n");
			break;
		case FAMILY:
			fprintf(stderr, "  [ inet | inet6 | IPv4 | IPv6 | "
			    "VPNv4 | VPNv6 ]\n");
			break;
		case FILENAME:
			fprintf(stderr, "  <filename>\n");
			break;
		case FLOW_SRCPORT:
		case FLOW_DSTPORT:
		case FLOW_PROTO:
		case FLOW_ICMPTYPE:
		case FLOW_ICMPCODE:
		case FLOW_LENGTH:
		case FLOW_DSCP:
			fprintf(stderr, "  %s <numberspec>\n",
			    table[i].keyword);
			break;
		case FLOW_FLAGS:
		case FLOW_FRAGS:
			fprintf(stderr, "  %s <flagspec>\n",
			    table[i].keyword);
			break;
		case ENDTOKEN:
			break;
		}
	}
}

int
parse_addr(const char *word, struct bgpd_addr *addr)
{
	struct in_addr	ina;
	struct addrinfo	hints, *r;

	if (word == NULL)
		return (0);

	memset(&ina, 0, sizeof(ina));

	if (inet_net_pton(AF_INET, word, &ina, sizeof(ina)) != -1) {
		memset(addr, 0, sizeof(*addr));
		addr->aid = AID_INET;
		addr->v4 = ina;
		return (1);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(word, "0", &hints, &r) == 0) {
		sa2addr(r->ai_addr, addr, NULL);
		freeaddrinfo(r);
		return (1);
	}

	return (0);
}

int
parse_prefix(const char *word, size_t wordlen, struct bgpd_addr *addr,
    uint8_t *prefixlen)
{
	struct bgpd_addr tmp;
	char		*p, *ps;
	const char	*errstr;
	int		 mask = -1;

	if (word == NULL)
		return (0);

	memset(&tmp, 0, sizeof(tmp));

	if ((p = strrchr(word, '/')) != NULL) {
		size_t plen = strlen(p);
		mask = strtonum(p + 1, 0, 128, &errstr);
		if (errstr)
			errx(1, "netmask %s", errstr);

		if ((ps = malloc(wordlen - plen + 1)) == NULL)
			err(1, "parse_prefix: malloc");
		strlcpy(ps, word, wordlen - plen + 1);

		if (parse_addr(ps, &tmp) == 0) {
			free(ps);
			return (0);
		}

		free(ps);
	} else
		if (parse_addr(word, &tmp) == 0)
			return (0);

	switch (tmp.aid) {
	case AID_INET:
		if (mask == -1)
			mask = 32;
		if (mask > 32)
			errx(1, "invalid netmask: too large");
		break;
	case AID_INET6:
		if (mask == -1)
			mask = 128;
		break;
	default:
		return (0);
	}

	applymask(addr, &tmp, mask);
	*prefixlen = mask;
	return (1);
}

int
parse_asnum(const char *word, size_t wordlen, uint32_t *asnum)
{
	const char	*errstr;
	char		*dot, *parseword;
	uint32_t	 uval, uvalh = 0;

	if (word == NULL)
		return (0);

	if (wordlen < 1 || word[0] < '0' || word[0] > '9')
		return (0);

	parseword = strdup(word);
	if ((dot = strchr(parseword, '.')) != NULL) {
		*dot++ = '\0';
		uvalh = strtonum(parseword, 0, USHRT_MAX, &errstr);
		if (errstr)
			errx(1, "AS number is %s: %s", errstr, word);
		uval = strtonum(dot, 0, USHRT_MAX, &errstr);
		if (errstr)
			errx(1, "AS number is %s: %s", errstr, word);
	} else {
		uval = strtonum(parseword, 0, UINT_MAX, &errstr);
		if (errstr)
			errx(1, "AS number is %s: %s", errstr, word);
	}

	free(parseword);
	*asnum = uval | (uvalh << 16);
	return (1);
}

int
parse_number(const char *word, struct parse_result *r, enum token_type type)
{
	struct filter_set	*fs;
	const char		*errstr;
	u_int			 uval;

	if (word == NULL)
		return (0);

	uval = strtonum(word, 0, UINT_MAX, &errstr);
	if (errstr)
		errx(1, "number is %s: %s", errstr, word);

	/* number was parseable */
	switch (type) {
	case RTABLE:
		r->rtableid = uval;
		return (1);
	case PATHID:
		r->pathid = uval;
		r->flags |= F_CTL_HAS_PATHID;
		return (1);
	default:
		break;
	}

	if ((fs = calloc(1, sizeof(struct filter_set))) == NULL)
		err(1, NULL);
	switch (type) {
	case LOCALPREF:
		fs->type = ACTION_SET_LOCALPREF;
		fs->action.metric = uval;
		break;
	case MED:
		fs->type = ACTION_SET_MED;
		fs->action.metric = uval;
		break;
	case PREPNBR:
		if (uval > 128) {
			free(fs);
			return (0);
		}
		fs->type = ACTION_SET_PREPEND_PEER;
		fs->action.prepend = uval;
		break;
	case PREPSELF:
		if (uval > 128) {
			free(fs);
			return (0);
		}
		fs->type = ACTION_SET_PREPEND_SELF;
		fs->action.prepend = uval;
		break;
	case WEIGHT:
		fs->type = ACTION_SET_WEIGHT;
		fs->action.metric = uval;
		break;
	default:
		errx(1, "king bula sez bad things happen");
	}

	TAILQ_INSERT_TAIL(&r->set, fs, entry);
	return (1);
}

static void
getcommunity(char *s, int large, uint32_t *val, uint32_t *flag)
{
	long long	 max = USHRT_MAX;
	const char	*errstr;

	*flag = 0;
	*val = 0;
	if (strcmp(s, "*") == 0) {
		*flag = COMMUNITY_ANY;
		return;
	} else if (strcmp(s, "neighbor-as") == 0) {
		*flag = COMMUNITY_NEIGHBOR_AS;
		return;
	} else if (strcmp(s, "local-as") == 0) {
		*flag =  COMMUNITY_LOCAL_AS;
		return;
	}
	if (large)
		max = UINT_MAX;
	*val = strtonum(s, 0, max, &errstr);
	if (errstr)
		errx(1, "Community %s is %s (max: %llu)", s, errstr, max);
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

void
parsecommunity(struct community *c, char *s)
{
	char *p;
	uint32_t as, data, asflag, dataflag;

	/* Well-known communities */
	if (strcasecmp(s, "GRACEFUL_SHUTDOWN") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_GRACEFUL_SHUTDOWN, 0, 0);
		return;
	} else if (strcasecmp(s, "NO_EXPORT") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_EXPORT, 0, 0);
		return;
	} else if (strcasecmp(s, "NO_ADVERTISE") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_ADVERTISE, 0, 0);
		return;
	} else if (strcasecmp(s, "NO_EXPORT_SUBCONFED") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_EXPSUBCONFED, 0, 0);
		return;
	} else if (strcasecmp(s, "NO_PEER") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_NO_PEER, 0, 0);
		return;
	} else if (strcasecmp(s, "BLACKHOLE") == 0) {
		setcommunity(c, COMMUNITY_WELLKNOWN,
		    COMMUNITY_BLACKHOLE, 0, 0);
		return;
	}

	if ((p = strchr(s, ':')) == NULL)
		errx(1, "Bad community syntax");
	*p++ = 0;

	getcommunity(s, 0, &as, &asflag);
	getcommunity(p, 0, &data, &dataflag);
	setcommunity(c, as, data, asflag, dataflag);
}

void
parselargecommunity(struct community *c, char *s)
{
	char *p, *q;
	uint32_t dflag1, dflag2, dflag3;

	if ((p = strchr(s, ':')) == NULL)
		errx(1, "Bad community syntax");
	*p++ = 0;

	if ((q = strchr(p, ':')) == NULL)
		errx(1, "Bad community syntax");
	*q++ = 0;

	getcommunity(s, 1, &c->data1, &dflag1);
	getcommunity(p, 1, &c->data2, &dflag2);
	getcommunity(q, 1, &c->data3, &dflag3);

	c->flags = COMMUNITY_TYPE_LARGE;
	c->flags |= dflag1 << 8;
	c->flags |= dflag2 << 16;
	c->flags |= dflag3 << 24;
}

static int
parsesubtype(const char *name, int *type, int *subtype)
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
		if (errstr)
			errx(1, "Bad ext-community %s is %s", s, errstr);
		*v = uval;
		break;
	case EXT_COMMUNITY_TRANS_FOUR_AS:
		if ((p = strchr(s, '.')) == NULL) {
			uval = strtonum(s, 0, UINT_MAX, &errstr);
			if (errstr)
				errx(1, "Bad ext-community %s is %s", s,
				    errstr);
			*v = uval;
			break;
		}
		*p++ = '\0';
		uvalh = strtonum(s, 0, USHRT_MAX, &errstr);
		if (errstr)
			errx(1, "Bad ext-community %s is %s", s, errstr);
		uval = strtonum(p, 0, USHRT_MAX, &errstr);
		if (errstr)
			errx(1, "Bad ext-community %s is %s", p, errstr);
		*v = uval | (uvalh << 16);
		break;
	case EXT_COMMUNITY_TRANS_IPV4:
		if (inet_pton(AF_INET, s, &ip) != 1)
			errx(1, "Bad ext-community %s not parseable", s);
		*v = ntohl(ip.s_addr);
		break;
	default:
		errx(1, "%s: unexpected type %d", __func__, type);
	}
	return (type);
}

void
parseextcommunity(struct community *c, const char *t, char *s)
{
	const struct ext_comm_pairs *cp;
	char		*p, *ep;
	uint64_t	 ullval;
	uint32_t	 uval, uval2, dflag1 = 0, dflag2 = 0;
	int		 type = 0, subtype = 0;

	if (strcmp(t, "*") == 0 && strcmp(s, "*") == 0) {
		c->flags = COMMUNITY_TYPE_EXT;
		c->flags |= COMMUNITY_ANY << 24;
		return;
	}
	if (parsesubtype(t, &type, &subtype) == 0)
		errx(1, "Bad ext-community unknown type");

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
		if ((p = strchr(s, ':')) == NULL)
			errx(1, "Bad ext-community %s", s);
		*p++ = '\0';
		type = parseextvalue(type, s, &uval, &dflag1);

		switch (type) {
		case EXT_COMMUNITY_TRANS_TWO_AS:
		case EXT_COMMUNITY_GEN_TWO_AS:
			getcommunity(p, 1, &uval2, &dflag2);
			break;
		case EXT_COMMUNITY_TRANS_IPV4:
		case EXT_COMMUNITY_TRANS_FOUR_AS:
		case EXT_COMMUNITY_GEN_IPV4:
		case EXT_COMMUNITY_GEN_FOUR_AS:
			getcommunity(p, 0, &uval2, &dflag2);
			break;
		default:
			errx(1, "parseextcommunity: unexpected result");
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
		if (s[0] == '\0' || *ep != '\0')
			errx(1, "Bad ext-community bad value");
		if (errno == ERANGE && ullval > EXT_COMMUNITY_OPAQUE_MAX)
			errx(1, "Bad ext-community value too big");
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
		errx(1, "Bad ext-community %s", s);
	}

	c->data3 = type << 8 | subtype;

	/* special handling of ext-community rt * since type is not known */
	if (dflag1 == COMMUNITY_ANY && type == -1) {
		c->flags = COMMUNITY_TYPE_EXT;
		c->flags |= dflag1 << 8;
		return;
	}

	/* verify type/subtype combo */
	for (cp = iana_ext_comms; cp->subname != NULL; cp++) {
		if (cp->type == type && cp->subtype == subtype) {
			c->flags = COMMUNITY_TYPE_EXT;
			c->flags |= dflag1 << 8;
			c->flags |= dflag2 << 16;
			return;
		}
	}

	errx(1, "Bad ext-community bad format for type");
}

int
parse_nexthop(const char *word, struct parse_result *r)
{
	struct filter_set	*fs;

	if ((fs = calloc(1, sizeof(struct filter_set))) == NULL)
		err(1, NULL);

	if (strcmp(word, "blackhole") == 0)
		fs->type = ACTION_SET_NEXTHOP_BLACKHOLE;
	else if (strcmp(word, "reject") == 0)
		fs->type = ACTION_SET_NEXTHOP_REJECT;
	else if (strcmp(word, "no-modify") == 0)
		fs->type = ACTION_SET_NEXTHOP_NOMODIFY;
	else if (parse_addr(word, &fs->action.nexthop)) {
		fs->type = ACTION_SET_NEXTHOP;
	} else {
		free(fs);
		return (0);
	}

	TAILQ_INSERT_TAIL(&r->set, fs, entry);
	return (1);
}

static int
unary_op(const char *op)
{
	if (strcmp(op, "=") == 0)
		return FLOWSPEC_OP_NUM_EQ;
	if (strcmp(op, "!=") == 0)
		return FLOWSPEC_OP_NUM_NOT;
	if (strcmp(op, ">") == 0)
		return FLOWSPEC_OP_NUM_GT;
	if (strcmp(op, ">=") == 0)
		return FLOWSPEC_OP_NUM_GE;
	if (strcmp(op, "<") == 0)
		return FLOWSPEC_OP_NUM_LT;
	if (strcmp(op, "<=") == 0)
		return FLOWSPEC_OP_NUM_LE;
	return -1;
}

static enum comp_ops
binary_op(const char *op)
{
	if (strcmp(op, "-") == 0)
		return OP_RANGE;
	if (strcmp(op, "><") == 0)
		return OP_XRANGE;
	return OP_NONE;
}

static void
push_numop(struct parse_result *r, int type, uint8_t op, int and, long long val)
{
	uint8_t *comp;
	void *data;
	uint32_t u32;
	uint16_t u16;
	uint8_t u8, flag = 0;
	int len, complen;

	flag |= op;
	if (and)
		flag |= FLOWSPEC_OP_AND;

	if (val < 0 || val > 0xffffffff) {
		errx(1, "unsupported value for flowspec num_op");
	} else if (val <= 255) {
		len = 1;
		u8 = val;
		data = &u8;
	} else if (val <= 0xffff) {
		len = 2;
		u16 = htons(val);
		data = &u16;
		flag |= 1 << FLOWSPEC_OP_LEN_SHIFT;
	} else {
		len = 4;
		u32 = htonl(val);
		data = &u32;
		flag |= 2 << FLOWSPEC_OP_LEN_SHIFT;
	}

	complen = r->flow.complen[type];
	comp = realloc(r->flow.components[type], complen + len + 1);
	if (comp == NULL)
		err(1, NULL);

	comp[complen++] = flag;
	memcpy(comp + complen, data, len);
	complen += len;
	r->flow.complen[type] = complen;
	r->flow.components[type] = comp;
}

int
parse_flow_numop(int argc, char *argv[], struct parse_result *r,
    enum token_type toktype)
{
	const char *errstr;
	long long val, val2;
	int numargs, type;
	int is_list = 0;
	int op;

	switch (toktype) {
	case FLOW_PROTO:
		type = FLOWSPEC_TYPE_PROTO;
		break;
	case FLOW_SRCPORT:
		type = FLOWSPEC_TYPE_SRC_PORT;
		break;
	case FLOW_DSTPORT:
		type = FLOWSPEC_TYPE_DST_PORT;
		break;
	case FLOW_ICMPTYPE:
		type = FLOWSPEC_TYPE_ICMP_TYPE;
		break;
	case FLOW_ICMPCODE:
		type = FLOWSPEC_TYPE_ICMP_CODE;
		break;
	case FLOW_LENGTH:
		type = FLOWSPEC_TYPE_PKT_LEN;
		break;
	case FLOW_DSCP:
		type = FLOWSPEC_TYPE_DSCP;
		break;
	default:
		errx(1, "parse_flow_numop called with unsupported type");
	}

	/* skip keyword (which is already accounted for) */
	argc--;
	argv++;
	numargs = argc;

	while (argc > 0) {
		if (strcmp(argv[0], "{") == 0) {
			is_list = 1;
			argc--;
			argv++;
		} else if (is_list && strcmp(argv[0], "}") == 0) {
			is_list = 0;
			argc--;
			argv++;
		} else if ((op = unary_op(argv[0])) != -1) {
			if (argc < 2)
				errx(1, "missing argument in flowspec "
				    "definition");

			val = strtonum(argv[1], LLONG_MIN, LLONG_MAX, &errstr);
			if (errstr)
				errx(1, "\"%s\" invalid number: %s", argv[0],
				    errstr);
			push_numop(r, type, op, 0, val);
			argc -= 2;
			argv += 2;
		} else {
			val = strtonum(argv[0], LLONG_MIN, LLONG_MAX, &errstr);
			if (errstr)
				errx(1, "\"%s\" invalid number: %s", argv[0],
				    errstr);
			if (argc >= 3 && (op = binary_op(argv[1])) != OP_NONE) {
				val2 = strtonum(argv[2], LLONG_MIN, LLONG_MAX,
				    &errstr);
				if (errstr)
					errx(1, "\"%s\" invalid number: %s",
					    argv[2], errstr);
				switch (op) {
				case OP_RANGE:
					push_numop(r, type, FLOWSPEC_OP_NUM_GE,
					    0, val);
					push_numop(r, type, FLOWSPEC_OP_NUM_LE,
					    1, val2);
					break;
				case OP_XRANGE:
					push_numop(r, type, FLOWSPEC_OP_NUM_LT,
					    0, val);
					push_numop(r, type, FLOWSPEC_OP_NUM_GT,
					    0, val2);
					break;
				}
				argc -= 3;
				argv += 3;
			} else {
				push_numop(r, type, FLOWSPEC_OP_NUM_EQ, 0, val);
				argc--;
				argv++;
			}
		}
		if (is_list == 0)
			break;
	}

	return numargs - argc;
}
