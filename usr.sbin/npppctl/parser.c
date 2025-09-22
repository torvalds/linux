/*	$OpenBSD: parser.c,v 1.3 2015/01/19 01:48:57 deraadt Exp $	*/

/* This file is derived from OpenBSD:src/usr.sbin/ikectl/parser.c 1.9 */
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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

enum token_type {
	NOTOKEN,
	ENDTOKEN,
	KEYWORD,
	PPP_ID,
	ADDRESS,
	INTERFACE,
	PROTOCOL,
	REALM,
	USERNAME
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static struct parse_result res;

static const struct token t_main[];
static const struct token t_session[];
static const struct token t_clear[];
static const struct token t_monitor[];
static const struct token t_filter[];
static const struct token t_ppp_id[];
static const struct token t_address[];
static const struct token t_interface[];
static const struct token t_protocol[];
static const struct token t_realm[];
static const struct token t_username[];

static const struct token t_main[] = {
	{ KEYWORD,	"session",	NONE,		t_session },
	{ KEYWORD,	"clear",	NONE,		t_clear },
	{ KEYWORD,	"monitor",	NONE,		t_monitor },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_session[] = {
	{ KEYWORD,	"brief",	SESSION_BRIEF,	NULL },
	{ KEYWORD,	"packets",	SESSION_PKTS,	NULL },
	{ KEYWORD,	"all",		SESSION_ALL,	t_filter },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_clear[] = {
	{ KEYWORD,	"all",		CLEAR_SESSION,	NULL },
	{ KEYWORD,	"ppp-id",	CLEAR_SESSION,	t_ppp_id },
	{ KEYWORD,	"address",	CLEAR_SESSION,	t_address },
	{ KEYWORD,	"interface",	CLEAR_SESSION,	t_interface },
	{ KEYWORD,	"protocol",	CLEAR_SESSION,	t_protocol },
	{ KEYWORD,	"realm",	CLEAR_SESSION,	t_realm },
	{ KEYWORD,	"username",	CLEAR_SESSION,	t_username },
	{ ENDTOKEN,	"",		CLEAR_SESSION,	NULL }
};

static const struct token t_monitor[] = {
	{ KEYWORD,	"all",		MONITOR_SESSION,	NULL },
	{ KEYWORD,	"ppp-id",	MONITOR_SESSION,	t_ppp_id },
	{ KEYWORD,	"address",	MONITOR_SESSION,	t_address },
	{ KEYWORD,	"interface",	MONITOR_SESSION,	t_interface },
	{ KEYWORD,	"protocol",	MONITOR_SESSION,	t_protocol },
	{ KEYWORD,	"realm",	MONITOR_SESSION,	t_realm },
	{ KEYWORD,	"username",	MONITOR_SESSION,	t_username },
	{ ENDTOKEN,	"",		MONITOR_SESSION,	NULL }
};

static const struct token t_filter[] = {
	{ NOTOKEN,	"",		NONE,		NULL },
	{ KEYWORD,	"ppp-id",	NONE,		t_ppp_id },
	{ KEYWORD,	"address",	NONE,		t_address },
	{ KEYWORD,	"interface",	NONE,		t_interface },
	{ KEYWORD,	"protocol",	NONE,		t_protocol },
	{ KEYWORD,	"realm",	NONE,		t_realm },
	{ KEYWORD,	"username",	NONE,		t_username },
	{ ENDTOKEN,	"",		NONE,		NULL }
};

static const struct token t_ppp_id[] = {
	{ PPP_ID,	"",		NONE,			t_filter },
	{ ENDTOKEN,	"",		NONE,			NULL }
};
static const struct token t_address[] = {
	{ ADDRESS,	"",		NONE,			t_filter },
	{ ENDTOKEN,	"",		NONE,			NULL }
};
static const struct token t_interface[] = {
	{ INTERFACE,	"",		NONE,			t_filter },
	{ ENDTOKEN,	"",		NONE,			NULL }
};
static const struct token t_protocol[] = {
	{ PROTOCOL,	"",		NONE,			t_filter },
	{ ENDTOKEN,	"",		NONE,			NULL }
};
static const struct token t_realm[] = {
	{ REALM,	"",		NONE,			t_filter },
	{ ENDTOKEN,	"",		NONE,			NULL }
};
static const struct token t_username[] = {
	{ USERNAME,	"",		NONE,			t_filter },
	{ ENDTOKEN,	"",		NONE,			NULL }
};

static const struct token	*match_token(char *, const struct token []);
static void			 show_valid_args(const struct token []);

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

static const struct token *
match_token(char *word, const struct token table[])
{
	u_int			 i, match = 0;
	unsigned long int	 ulval;
	const struct token	*t = NULL;
	char			*ep;

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
		case PPP_ID:
			if (word == NULL)
				break;
			errno = 0;
			ulval = strtoul(word, &ep, 10);
			if (isdigit((unsigned char)*word) && *ep == '\0' &&
			    !(errno == ERANGE && ulval == ULONG_MAX)) {
				res.ppp_id = ulval;
				res.has_ppp_id = 1;
				match++;
				t = &table[i];
			}
			break;
		case ADDRESS:
		    {
			struct sockaddr_in sin4 = {
				.sin_family = AF_INET,
				.sin_len = sizeof(struct sockaddr_in)
			};
			struct sockaddr_in6 sin6 = {
				.sin6_family = AF_INET6,
				.sin6_len = sizeof(struct sockaddr_in6)
			};
			if (word == NULL)
				break;
			if (inet_pton(AF_INET, word, &sin4.sin_addr) == 1)
				memcpy(&res.address, &sin4, sin4.sin_len);
			else
			if (inet_pton(AF_INET6, word, &sin6.sin6_addr) == 1)
				memcpy(&res.address, &sin6, sin6.sin6_len);
			else
				break;
			match++;
			t = &table[i];
		    }
			break;
		case INTERFACE:
			if (word == NULL)
				break;
			res.interface = word;
			match++;
			t = &table[i];
			break;
		case PROTOCOL:
			if (word == NULL)
				break;
			if ((res.protocol = parse_protocol(word)) ==
			    PROTO_UNSPEC)
				break;
			match++;
			t = &table[i];
			break;
		case REALM:
			if (word == NULL)
				break;
			res.realm = word;
			match++;
			t = &table[i];
			break;
		case USERNAME:
			if (word == NULL)
				break;
			res.username = word;
			match++;
			t = &table[i];
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
		case PPP_ID:
			fprintf(stderr, "  <ppp-id>\n");
			break;
		case ADDRESS:
			fprintf(stderr, "  <address>\n");
			break;
		case INTERFACE:
			fprintf(stderr, "  <interface>\n");
			break;
		case PROTOCOL:
			fprintf(stderr, "  [ pppoe | l2tp | pptp | sstp ]\n");
			break;
		case REALM:
			fprintf(stderr, "  <realm>\n");
			break;
		case USERNAME:
			fprintf(stderr, "  <username>\n");
			break;
		case ENDTOKEN:
			break;
		}
	}
}

enum protocol
parse_protocol(const char *str)
{
	return
	    (strcasecmp(str, "PPTP" ) == 0)? PPTP  :
	    (strcasecmp(str, "L2TP" ) == 0)? L2TP  :
	    (strcasecmp(str, "PPPoE") == 0)? PPPOE :
	    (strcasecmp(str, "SSTP" ) == 0)? SSTP  : PROTO_UNSPEC;
}

