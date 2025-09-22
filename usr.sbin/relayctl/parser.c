/*	$OpenBSD: parser.c,v 1.28 2018/05/11 20:33:54 reyk Exp $	*/

/*
 * Copyright (c) 2006 Pierre-Yves Ritschard <pyr@openbsd.org>
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "relayd.h"
#include "parser.h"

enum token_type {
	NOTOKEN,
	ENDTOKEN,
	HOSTID,
	TABLEID,
	RDRID,
	KEYWORD,
	PATH
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static const struct token t_main[];
static const struct token t_show[];
static const struct token t_rdr[];
static const struct token t_table[];
static const struct token t_host[];
static const struct token t_rdr_id[];
static const struct token t_table_id[];
static const struct token t_host_id[];
static const struct token t_log[];
static const struct token t_load[];

static const struct token t_main[] = {
	{KEYWORD,	"monitor",	MONITOR,	NULL},
	{KEYWORD,	"show",		NONE,		t_show},
	{KEYWORD,	"load",		LOAD,		t_load},
	{KEYWORD,	"poll",		POLL,		NULL},
	{KEYWORD,	"reload",	RELOAD,		NULL},
	{KEYWORD,	"stop",		SHUTDOWN,	NULL},
	{KEYWORD,	"redirect",	NONE,		t_rdr},
	{KEYWORD,	"table",	NONE,		t_table},
	{KEYWORD,	"host",		NONE,		t_host},
	{KEYWORD,	"log",		NONE,		t_log},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show[] = {
	{KEYWORD,	"summary",	SHOW_SUM,	NULL},
	{KEYWORD,	"hosts",	SHOW_HOSTS,	NULL},
	{KEYWORD,	"redirects",	SHOW_RDRS,	NULL},
	{KEYWORD,	"relays",	SHOW_RELAYS,	NULL},
	{KEYWORD,	"routers",	SHOW_ROUTERS,	NULL},
	{KEYWORD,	"sessions",	SHOW_SESSIONS,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_rdr[] = {
	{KEYWORD,	"disable",	RDR_DISABLE,	t_rdr_id},
	{KEYWORD,	"enable",	RDR_ENABLE,	t_rdr_id},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_table[] = {
	{KEYWORD,	"disable",	TABLE_DISABLE,	t_table_id},
	{KEYWORD,	"enable",	TABLE_ENABLE,	t_table_id},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_host[] = {
	{KEYWORD,	"disable",	HOST_DISABLE,	t_host_id},
	{KEYWORD,	"enable",	HOST_ENABLE,	t_host_id},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_rdr_id[] = {
	{RDRID,		"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_table_id[] = {
	{TABLEID,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_host_id[] = {
	{HOSTID,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_log[] = {
	{KEYWORD,	"verbose",	LOG_VERBOSE,	NULL},
	{KEYWORD,	"brief",	LOG_BRIEF,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_load[] = {
	{PATH,		"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
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
	const char		*errstr;

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
		case HOSTID:
			if (word == NULL)
				break;
			res->id.id = strtonum(word, 0, UINT_MAX, &errstr);
			if (errstr) {
				strlcpy(res->id.name, word,
				    sizeof(res->id.name));
				res->id.id = EMPTY_ID;
			}
			t = &table[i];
			match++;
			break;
		case TABLEID:
			if (word == NULL)
				break;
			res->id.id = strtonum(word, 0, UINT_MAX, &errstr);
			if (errstr) {
				strlcpy(res->id.name, word,
				    sizeof(res->id.name));
				res->id.id = EMPTY_ID;
			}
			t = &table[i];
			match++;
			break;
		case RDRID:
			if (word == NULL)
				break;
			res->id.id = strtonum(word, 0, UINT_MAX, &errstr);
			if (errstr) {
				strlcpy(res->id.name, word,
				    sizeof(res->id.name));
				res->id.id = EMPTY_ID;
			}
			t = &table[i];
			match++;
			break;
		case PATH:
			if (!match && word != NULL && strlen(word) > 0) {
				res->path = strdup(word);
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
			fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case RDRID:
			fprintf(stderr, "  <redirectid>\n");
			break;
		case TABLEID:
			fprintf(stderr, "  <tableid>\n");
			break;
		case HOSTID:
			fprintf(stderr, "  <hostid>\n");
			break;
		case PATH:
			fprintf(stderr, "  <path>\n");
			break;
		case ENDTOKEN:
			break;
		}
	}
}
