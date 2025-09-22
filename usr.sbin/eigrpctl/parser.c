/*	$OpenBSD: parser.c,v 1.4 2016/01/15 12:57:49 renato Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include "eigrpd.h"

#include "parser.h"

enum token_type {
	NOTOKEN,
	ENDTOKEN,
	KEYWORD,
	FAMILY,
	ASNUM,
	ADDRESS,
	FLAG,
	PREFIX,
	IFNAME
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static const struct token t_main[];
static const struct token t_fib[];
static const struct token t_show[];
static const struct token t_show_iface[];
static const struct token t_show_iface_af[];
static const struct token t_show_iface_as[];
static const struct token t_show_nbr[];
static const struct token t_show_nbr_af[];
static const struct token t_show_nbr_as[];
static const struct token t_show_topology[];
static const struct token t_show_topology_af[];
static const struct token t_show_topology_as[];
static const struct token t_show_fib[];
static const struct token t_show_fib_af[];
static const struct token t_show_stats[];
static const struct token t_show_stats_af[];
static const struct token t_show_stats_as[];
static const struct token t_log[];
static const struct token t_clear[];
static const struct token t_clear_nbr[];
static const struct token t_clear_nbr_af[];
static const struct token t_clear_nbr_as[];

static const struct token t_main[] = {
	{KEYWORD,	"reload",	RELOAD,		NULL},
	{KEYWORD,	"fib",		FIB,		t_fib},
	{KEYWORD,	"show",		SHOW,		t_show},
	{KEYWORD,	"clear",	NONE,		t_clear},
	{KEYWORD,	"log",		NONE,		t_log},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_fib[] = {
	{ KEYWORD,	"couple",	FIB_COUPLE,	NULL},
	{ KEYWORD,	"decouple",	FIB_DECOUPLE,	NULL},
	{ ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{KEYWORD,	"interfaces",	SHOW_IFACE,	t_show_iface},
	{KEYWORD,	"neighbor",	SHOW_NBR,	t_show_nbr},
	{KEYWORD,	"topology",	SHOW_TOPOLOGY,	t_show_topology},
	{KEYWORD,	"fib",		SHOW_FIB,	t_show_fib},
	{KEYWORD,	"traffic",	SHOW_STATS,	t_show_stats},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_iface[] = {
	{NOTOKEN,	"",		NONE,			NULL},
	{KEYWORD,	"family",	NONE,			t_show_iface_af},
	{KEYWORD,	"as",		NONE,			t_show_iface_as},
	{KEYWORD,	"detail",	SHOW_IFACE_DTAIL,	NULL},
	{IFNAME,	"",		SHOW_IFACE_DTAIL,	NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_show_iface_af[] = {
	{FAMILY,	"",		NONE,		t_show_iface},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_iface_as[] = {
	{ASNUM,		"",		NONE,		t_show_iface},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_nbr[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{KEYWORD,	"family",	NONE,		t_show_nbr_af},
	{KEYWORD,	"as",		NONE,		t_show_nbr_as},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_nbr_af[] = {
	{FAMILY,	"",		NONE,		t_show_nbr},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_nbr_as[] = {
	{ASNUM,		"",		NONE,		t_show_nbr},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_topology[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{KEYWORD,	"family",	NONE,		t_show_topology_af},
	{KEYWORD,	"as",		NONE,		t_show_topology_as},
	{PREFIX,	"",		NONE,		NULL},
	{FLAG,		"active",	F_CTL_ACTIVE,	NULL},
	{FLAG,		"all-links",	F_CTL_ALLLINKS,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_topology_af[] = {
	{FAMILY,	"",		NONE,		t_show_topology},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_topology_as[] = {
	{ASNUM,		"",		NONE,		t_show_topology},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_fib[] = {
	{NOTOKEN,	"",		NONE,			NULL},
	{KEYWORD,	"family",	NONE,			t_show_fib_af},
	{KEYWORD,	"interface",	SHOW_FIB_IFACE,		t_show_iface},
	{FLAG,		"connected",	F_CONNECTED,		t_show_fib},
	{FLAG,		"static",	F_STATIC,		t_show_fib},
	{FLAG,		"eigrp",	F_EIGRPD_INSERTED,	t_show_fib},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_show_fib_af[] = {
	{FAMILY,	"",		NONE,		t_show_fib},
	{ENDTOKEN,	"",		NONE,		NULL}
};


static const struct token t_show_stats[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{KEYWORD,	"family",	NONE,		t_show_stats_af},
	{KEYWORD,	"as",		NONE,		t_show_stats_as},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_stats_af[] = {
	{FAMILY,	"",		NONE,		t_show_stats},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_stats_as[] = {
	{ASNUM,		"",		NONE,		t_show_stats},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_clear[] = {
	{KEYWORD,	"neighbors",	CLEAR_NBR,	t_clear_nbr},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_clear_nbr[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{KEYWORD,	"as",		NONE,		t_clear_nbr_as},
	{KEYWORD,	"family",	NONE,		t_clear_nbr_af},
	{ADDRESS,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_clear_nbr_af[] = {
	{FAMILY,	"",		NONE,		t_clear_nbr},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_clear_nbr_as[] = {
	{ASNUM,		"",		NONE,		t_clear_nbr},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_log[] = {
	{KEYWORD,	"verbose",	LOG_VERBOSE,		NULL},
	{KEYWORD,	"brief",	LOG_BRIEF,		NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token *match_token(const char *, const struct token *,
    struct parse_result *);
static void show_valid_args(const struct token *);

struct parse_result *
parse(int argc, char *argv[])
{
	static struct parse_result	res;
	const struct token	*table = t_main;
	const struct token	*match;

	memset(&res, 0, sizeof(res));

	while (argc >= 0) {
		if ((match = match_token(argv[0], table, &res)) == NULL) {
			fprintf(stderr, "valid commands/args:\n");
			show_valid_args(table);
			return (NULL);
		}

		argc--;
		argv++;

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

static const struct token *
match_token(const char *word, const struct token *table,
    struct parse_result *res)
{
	unsigned int		 i, match;
	const struct token	*t = NULL;

	match = 0;

	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			if (word == NULL || strlen(word) == 0) {
				match++;
				t = &table[i];
			}
			break;
		case KEYWORD:
			if (word != NULL && strncmp(word, table[i].keyword,
			    strlen(word)) == 0) {
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case FLAG:
			if (word != NULL && strncmp(word, table[i].keyword,
			    strlen(word)) == 0) {
				match++;
				t = &table[i];
				res->flags |= t->value;
			}
			break;
		case FAMILY:
			if (word == NULL)
				break;
			if (!strcmp(word, "inet") ||
			    !strcasecmp(word, "IPv4")) {
				match++;
				t = &table[i];
				res->family = AF_INET;
			}
			if (!strcmp(word, "inet6") ||
			    !strcasecmp(word, "IPv6")) {
				match++;
				t = &table[i];
				res->family = AF_INET6;
			}
			break;
		case ASNUM:
			if (parse_asnum(word, &res->as)) {
				match++;
				t = &table[i];
			}
			break;
		case ADDRESS:
			if (parse_addr(word, &res->family, &res->addr)) {
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case PREFIX:
			if (parse_prefix(word, &res->family, &res->addr,
			    &res->prefixlen)) {
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case IFNAME:
			if (!match && word != NULL && strlen(word) > 0) {
				if (strlcpy(res->ifname, word,
				    sizeof(res->ifname)) >=
				    sizeof(res->ifname))
					err(1, "interface name too long");
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
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

static void
show_valid_args(const struct token *table)
{
	int	i;

	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			fprintf(stderr, "  <cr>\n");
			break;
		case KEYWORD:
		case FLAG:
			fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case FAMILY:
			fprintf(stderr, "  [ inet | inet6 | IPv4 | IPv6 ]\n");
			break;
		case ASNUM:
			fprintf(stderr, "  <asnum>\n");
			break;
		case ADDRESS:
			fprintf(stderr, "  <address>\n");
			break;
		case PREFIX:
			fprintf(stderr, "  <address>[/<len>]\n");
			break;
		case IFNAME:
			fprintf(stderr, "  <interface>\n");
			break;
		case ENDTOKEN:
			break;
		}
	}
}

int
parse_asnum(const char *word, uint16_t *asnum)
{
	const char	*errstr;
	uint32_t	 uval;

	if (word == NULL)
		return (0);

	uval = strtonum(word, EIGRP_MIN_AS, EIGRP_MAX_AS, &errstr);
	if (errstr)
		errx(1, "AS number is %s: %s", errstr, word);

	*asnum = uval;
	return (1);
}

int
parse_addr(const char *word, int *family, union eigrpd_addr *addr)
{
	struct in_addr		 ina;
	struct addrinfo		 hints, *r;
	struct sockaddr_in6	*sa_in6;

	if (word == NULL)
		return (0);

	memset(addr, 0, sizeof(union eigrpd_addr));
	memset(&ina, 0, sizeof(ina));

	if (inet_net_pton(AF_INET, word, &ina, sizeof(ina)) != -1) {
		*family = AF_INET;
		addr->v4.s_addr = ina.s_addr;
		return (1);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(word, "0", &hints, &r) == 0) {
		sa_in6 = (struct sockaddr_in6 *)r->ai_addr;
		*family = AF_INET6;
		addr->v6 = sa_in6->sin6_addr;
		freeaddrinfo(r);
		return (1);
	}

	return (0);
}

int
parse_prefix(const char *word, int *family, union eigrpd_addr *addr,
    uint8_t *prefixlen)
{
	char		*p, *ps;
	const char	*errstr;
	size_t		 wordlen;
	int		 mask = -1;

	if (word == NULL)
		return (0);
	wordlen = strlen(word);

	memset(addr, 0, sizeof(union eigrpd_addr));

	if ((p = strrchr(word, '/')) != NULL) {
		size_t plen = strlen(p);
		mask = strtonum(p + 1, 0, 128, &errstr);
		if (errstr)
			errx(1, "netmask %s", errstr);

		if ((ps = malloc(wordlen - plen + 1)) == NULL)
			err(1, "parse_prefix: malloc");
		strlcpy(ps, word, wordlen - plen + 1);

		if (parse_addr(ps, family, addr) == 0) {
			free(ps);
			return (0);
		}

		free(ps);
	} else
		if (parse_addr(word, family, addr) == 0)
			return (0);

	switch (*family) {
	case AF_INET:
		if (mask == UINT8_MAX)
			mask = 32;
		if (mask > 32)
			errx(1, "invalid netmask: too large");
		break;
	case AF_INET6:
		if (mask == UINT8_MAX)
			mask = 128;
		if (mask > 128)
			errx(1, "invalid netmask: too large");
		break;
	default:
		return (0);
	}
	eigrp_applymask(*family, addr, addr, mask);
	*prefixlen = mask;

	return (1);
}
