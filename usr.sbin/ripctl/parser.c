/*	$OpenBSD: parser.c,v 1.7 2017/07/28 13:02:35 florian Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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

#include "ripd.h"

#include "parser.h"

enum token_type {
	NOTOKEN,
	ENDTOKEN,
	KEYWORD,
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
static const struct token t_show_nbr[];
static const struct token t_show_rib[];
static const struct token t_show_fib[];
static const struct token t_log[];

static const struct token t_main[] = {
/*	{KEYWORD,	"reload",	RELOAD,		NULL}, */
	{KEYWORD,	"fib",		FIB,		t_fib},
	{KEYWORD,	"show",		SHOW,		t_show},
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
	{KEYWORD,	"rib",		SHOW_RIB,	t_show_rib},
	{KEYWORD,	"fib",		SHOW_FIB,	t_show_fib},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_iface[] = {
	{NOTOKEN,	"",		NONE,			NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_show_nbr[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_rib[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_fib[] = {
	{NOTOKEN,	"",		NONE,			NULL},
	{KEYWORD,	"interface",	SHOW_FIB_IFACE,		t_show_iface},
	{FLAG,		"connected",	F_CONNECTED,		t_show_fib},
	{FLAG,		"static",	F_STATIC,		t_show_fib},
	{FLAG,		"rip",		F_RIPD_INSERTED,	t_show_fib},
	{ADDRESS,	"",		NONE,			NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_log[] = {
	{KEYWORD,	"verbose",	LOG_VERBOSE,		NULL},
	{KEYWORD,	"brief",	LOG_BRIEF,		NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token *match_token(const char *, const struct token *,
    struct parse_result *);
static void show_valid_args(const struct token *table);

struct parse_result *
parse(int argc, char *argv[])
{
	static struct parse_result res;
	const struct token	*table = t_main;
	const struct token	*match;

	bzero(&res, sizeof(res));

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
	u_int			 i, match;
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
		case ADDRESS:
			if (parse_addr(word, &res->addr)) {
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case PREFIX:
			if (parse_prefix(word, &res->addr, &res->prefixlen)) {
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
		case ADDRESS:
			fprintf(stderr, "  <address>\n");
			break;
		case PREFIX:
			fprintf(stderr, "  <address>[/<len>]\n");
			break;
		case IFNAME:
			fprintf(stderr, "  <interface>\n");
		case ENDTOKEN:
			break;
		}
	}
}

int
parse_addr(const char *word, struct in_addr *addr)
{
	struct in_addr	ina;

	if (word == NULL)
		return (0);

	bzero(addr, sizeof(struct in_addr));
	bzero(&ina, sizeof(ina));

	if (inet_pton(AF_INET, word, &ina)) {
		addr->s_addr = ina.s_addr;
		return (1);
	}

	return (0);
}

int
parse_prefix(const char *word, struct in_addr *addr, u_int8_t *prefixlen)
{
	struct in_addr	 ina;
	int		 bits = 32;

	if (word == NULL)
		return (0);

	bzero(addr, sizeof(struct in_addr));
	bzero(&ina, sizeof(ina));

	if (strrchr(word, '/') != NULL) {
		if ((bits = inet_net_pton(AF_INET, word,
		    &ina, sizeof(ina))) == -1)
			return (0);
		addr->s_addr = ina.s_addr & htonl(prefixlen2mask(bits));
		*prefixlen = bits;
		return (1);
	} else {
		*prefixlen = 32;
		return (parse_addr(word, addr));
	}

	return (0);
}

/* XXX local copy from kroute.c, should go to shared file */
in_addr_t
prefixlen2mask(u_int8_t prefixlen)
{
	if (prefixlen == 0)
		return (0);

	return (0xffffffff << (32 - prefixlen));
}
