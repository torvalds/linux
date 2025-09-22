/*	$OpenBSD: parser.c,v 1.6 2024/09/15 05:26:05 yasuoka Exp $	*/

/*
 * Copyright (c) 2010 Reyk Floeter <reyk@vantronix.net>
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

#include <sys/time.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

#include "parser.h"

enum token_type {
	NOTOKEN,
	KEYWORD,
	HOSTNAME,
	SECRET,
	USERNAME,
	PASSWORD,
	PORT,
	METHOD,
	NAS_PORT,
	TRIES,
	INTERVAL,
	MAXWAIT,
	FLAGS,
	SESSION_SEQ,
	MSGAUTH,
	ENDTOKEN
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static struct parse_result res = {
	.tries		= TEST_TRIES_DEFAULT,
	.interval	= { TEST_INTERVAL_DEFAULT, 0 },
	.maxwait	= { TEST_MAXWAIT_DEFAULT, 0 },
	.msgauth	= 1
};

static const struct token t_test[];
static const struct token t_secret[];
static const struct token t_username[];
static const struct token t_test_opts[];
static const struct token t_password[];
static const struct token t_port[];
static const struct token t_method[];
static const struct token t_nas_port[];
static const struct token t_tries[];
static const struct token t_interval[];
static const struct token t_maxwait[];
static const struct token t_yesno[];
static const struct token t_ipcp[];
static const struct token t_ipcp_flags[];
static const struct token t_ipcp_session_seq[];

static const struct token t_main[] = {
	{ KEYWORD,	"test",		TEST,		t_test },
	{ KEYWORD,	"ipcp",		NONE,		t_ipcp },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_test[] = {
	{ HOSTNAME,	"",		NONE,		t_secret },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_secret[] = {
	{ SECRET,	"",		NONE,		t_username },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_username[] = {
	{ USERNAME,	"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_test_opts[] = {
	{ NOTOKEN,	"",		NONE,		NULL },
	{ KEYWORD,	"password",	NONE,		t_password },
	{ KEYWORD,	"port",		NONE,		t_port },
	{ KEYWORD,	"method",	NONE,		t_method },
	{ KEYWORD,	"nas-port",	NONE,		t_nas_port },
	{ KEYWORD,	"interval",	NONE,		t_interval },
	{ KEYWORD,	"tries",	NONE,		t_tries },
	{ KEYWORD,	"maxwait",	NONE,		t_maxwait },
	{ KEYWORD,	"msgauth",	NONE,		t_yesno },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_password[] = {
	{ PASSWORD,	"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_port[] = {
	{ PORT,		"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_method[] = {
	{ METHOD,	"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_nas_port[] = {
	{ NAS_PORT,	"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_tries[] = {
	{ TRIES,	"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_interval[] = {
	{ INTERVAL,	"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_maxwait[] = {
	{ MAXWAIT,	"",		NONE,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_yesno[] = {
	{ MSGAUTH,	"yes",		1,		t_test_opts },
	{ MSGAUTH,	"no",		0,		t_test_opts },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_ipcp[] = {
	{ KEYWORD,	"show",		IPCP_SHOW,	NULL },
	{ KEYWORD,	"dump",		IPCP_DUMP,	t_ipcp_flags },
	{ KEYWORD,	"monitor",	IPCP_MONITOR,	t_ipcp_flags },
	{ KEYWORD,	"disconnect",	IPCP_DISCONNECT,t_ipcp_session_seq },
	{ KEYWORD,	"delete",	IPCP_DELETE,	t_ipcp_session_seq },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_ipcp_flags[] = {
	{ NOTOKEN,	"",		NONE,		NULL },
	{ FLAGS,	"-json",	FLAGS_JSON,	NULL },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_ipcp_session_seq[] = {
	{ SESSION_SEQ,	"",		NONE,		NULL },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token	*match_token(char *, const struct token []);
static void			 show_valid_args(const struct token []);

struct parse_result *
parse(int argc, char *argv[])
{
	const struct token	*table = t_main;
	const struct token	*match;

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

	if (res.tries * res.interval.tv_sec > res.maxwait.tv_sec) {
		fprintf(stderr, "tries %u by interval %lld > maxwait %lld",
		    res.tries, res.interval.tv_sec, res.maxwait.tv_sec);
		return (NULL);
	}

	return (&res);
}

static const struct token *
match_token(char *word, const struct token table[])
{
	u_int			 i, match = 0;
	const struct token	*t = NULL;
	long long		 num;
	const char		*errstr;
	size_t			 wordlen = 0;

	if (word != NULL)
		wordlen = strlen(word);

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
			    wordlen) == 0) {
				match++;
				t = &table[i];
				if (t->value)
					res.action = t->value;
			}
			break;
		case HOSTNAME:
			if (word == NULL)
				break;
			match++;
			res.hostname = word;
			t = &table[i];
			break;
		case SECRET:
			if (word == NULL)
				break;
			match++;
			res.secret = word;
			t = &table[i];
			break;
		case USERNAME:
			if (word == NULL)
				break;
			match++;
			res.username = word;
			t = &table[i];
			break;
		case PASSWORD:
			if (word == NULL)
				break;
			match++;
			res.password = word;
			t = &table[i];
			break;
		case PORT:
			if (word == NULL)
				break;
			num = strtonum(word, 1, UINT16_MAX, &errstr);
			if (errstr != NULL) {
				fprintf(stderr,
				    "invalid argument: %s is %s for \"port\"\n",
				    word, errstr);
				return (NULL);
			}
			match++;
			res.port = num;
			t = &table[i];
			break;
		case METHOD:
			if (word == NULL)
				break;
			if (strcasecmp(word, "pap") == 0)
				res.auth_method = PAP;
			else if (strcasecmp(word, "chap") == 0)
				res.auth_method = CHAP;
			else if (strcasecmp(word, "mschapv2") == 0)
				res.auth_method = MSCHAPV2;
			else {
				fprintf(stderr,
				    "invalid argument: %s for \"method\"\n",
				    word);
				return (NULL);
			}
			match++;
			t = &table[i];
			break;
		case NAS_PORT:
			if (word == NULL)
				break;
			num = strtonum(word, 0, 65535, &errstr);
			if (errstr != NULL) {
				fprintf(stderr,
				    "invalid argument: %s is %s for "
				    "\"nas-port\"\n", word, errstr);
				return (NULL);
			}
			match++;
			res.nas_port = num;
			t = &table[i];
			break;

		case TRIES:
			if (word == NULL)
				break;
			num = strtonum(word,
			    TEST_TRIES_MIN, TEST_TRIES_MAX, &errstr);
			if (errstr != NULL) {
				printf("invalid argument: %s is %s"
				    " for \"tries\"\n", word, errstr);
				return (NULL);
			}
			match++;
			res.tries = num;
			t = &table[i];
			break;
		case INTERVAL:
			if (word == NULL)
				break;
			num = strtonum(word,
			    TEST_INTERVAL_MIN, TEST_INTERVAL_MAX, &errstr);
			if (errstr != NULL) {
				printf("invalid argument: %s is %s"
				    " for \"interval\"\n", word, errstr);
				return (NULL);
			}
			match++;
			res.interval.tv_sec = num;
			t = &table[i];
			break;
		case MAXWAIT:
			if (word == NULL)
				break;
			num = strtonum(word,
			    TEST_MAXWAIT_MIN, TEST_MAXWAIT_MAX, &errstr);
			if (errstr != NULL) {
				printf("invalid argument: %s is %s"
				    " for \"maxwait\"\n", word, errstr);
				return (NULL);
			}
			match++;
			res.maxwait.tv_sec = num;
			t = &table[i];
			break;
		case FLAGS:
			if (word != NULL && wordlen >= 2 &&
			    strncmp(word, table[i].keyword, wordlen) == 0) {
				match++;
				t = &table[i];
				if (t->value)
					res.flags |= t->value;
			}
			break;
		case SESSION_SEQ:
			if (word == NULL)
				break;
			match++;
			res.session_seq = strtonum(word, 1, UINT_MAX, &errstr);
			if (errstr != NULL) {
				printf("invalid argument: %s is %s for "
				    "\"session-id\"\n", word, errstr);
				return (NULL);
			}
			t = &table[i];
		case MSGAUTH:
			if (word != NULL &&
			    strcmp(word, table[i].keyword) == 0) {
				match++;
				res.msgauth = table[i].value;
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
show_valid_args(const struct token table[])
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
		case HOSTNAME:
			fprintf(stderr, "  <hostname>\n");
			break;
		case SECRET:
			fprintf(stderr, "  <radius secret>\n");
			break;
		case USERNAME:
			fprintf(stderr, "  <username>\n");
			break;
		case PASSWORD:
			fprintf(stderr, "  <password>\n");
			break;
		case PORT:
			fprintf(stderr, "  <port number>\n");
			break;
		case METHOD:
			fprintf(stderr, "  <auth method (pap, chap, "
			    "mschapv2)>\n");
			break;
		case NAS_PORT:
			fprintf(stderr, "  <nas-port (0-65535)>\n");
			break;
		case TRIES:
			fprintf(stderr, "  <tries (%u-%u)>\n",
			    TEST_TRIES_MIN, TEST_TRIES_MAX);
			break;
		case INTERVAL:
			fprintf(stderr, "  <interval (%u-%u)>\n",
			    TEST_INTERVAL_MIN, TEST_INTERVAL_MAX);
			break;
		case MAXWAIT:
			fprintf(stderr, "  <maxwait (%u-%u)>\n",
			    TEST_MAXWAIT_MIN, TEST_MAXWAIT_MAX);
			break;
		case FLAGS:
			fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case SESSION_SEQ:
			fprintf(stderr, "  <sequence number>\n");
			break;
		case MSGAUTH:
			fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case ENDTOKEN:
			break;
		}
	}
}
