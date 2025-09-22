/*	$OpenBSD: parser.c,v 1.4 2018/10/15 11:30:01 florian Exp $ */

/*
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"
#include "iscsictl.h"

enum token_type {
	NOTOKEN,
	ENDTOKEN,
	KEYWORD,
	ADDRESS,
	NAME,
	FLAG
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static const struct token t_main[];
static const struct token t_show[];
static const struct token t_log[];
static const struct token t_discovery[];
static const struct token t_session[];
static const struct token t_vscsi[];

static const struct token t_main[] = {
	{KEYWORD,	"reload",	RELOAD,		NULL},
	{KEYWORD,	"discover",	DISCOVERY,	t_discovery},
	{KEYWORD,	"show",		SHOW_SUM,	t_show},
	{KEYWORD,	"log",		NONE,		t_log},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{KEYWORD,	"summary",	SHOW_SUM,	NULL},
	{KEYWORD,	"session",	SHOW_SESS,	t_session},
	{KEYWORD,	"vscsi",	NONE,		t_vscsi},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_log[] = {
	{KEYWORD,	"verbose",	LOG_VERBOSE,		NULL},
	{KEYWORD,	"brief",	LOG_BRIEF,		NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_discovery[] = {
	{ADDRESS,	"",		NONE,			NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_session[] = {
	{NAME,		"",		NONE,			NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static const struct token t_vscsi[] = {
	{KEYWORD,	"stats",	SHOW_VSCSI_STATS,	NULL},
	{ENDTOKEN,	"",		NONE,			NULL}
};

static struct parse_result	res;

struct parse_result *
parse(int argc, char *argv[])
{
	const struct token	*table = t_main;
	const struct token	*match;

	bzero(&res, sizeof(res));

	while (argc >= 0) {
		if ((match = match_token(argv[0], table)) == NULL) {
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

const struct token *
match_token(const char *word, const struct token *table)
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
					res.action = t->value;
			}
			break;
		case FLAG:
			if (word != NULL && strncmp(word, table[i].keyword,
			    strlen(word)) == 0) {
				match++;
				t = &table[i];
				res.flags |= t->value;
			}
			break;
		case ADDRESS:
			if (!parse_addr(word, &res.addr)) {
				match++;
				t = &table[i];
				if (t->value)
					res.action = t->value;
			}
			break;
		case NAME:
			if (word != NULL && strlen(word) > 0) {
				if (strlcpy(res.name, word, sizeof(res.name)) >=
				    sizeof(res.name))
					errx(1, "name too long");
				match++;
				t = &table[i];
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
		case NAME:
			fprintf(stderr, "  <name>\n");
			break;
		case ENDTOKEN:
			break;
		}
	}
}

int
parse_addr(const char *word, struct sockaddr_storage *sa)
{
	struct addrinfo hints, *addrs;
	int rv;

	bzero(&hints, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if ((rv = getaddrinfo(word, "iscsi", &hints, &addrs)) == 0) {
		bcopy(addrs->ai_addr, sa, addrs->ai_addrlen);
		freeaddrinfo(addrs);
		return (0);
	}

	errx(1, "parse_host: %s", gai_strerror(rv));
}
