/*	$OpenBSD: parser.c,v 1.43 2021/06/14 17:58:15 eric Exp $	*/

/*
 * Copyright (c) 2013 Eric Faurot	<eric@openbsd.org>
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

#include <sys/queue.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <net/if.h>

#include <arpa/inet.h>
#include <err.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

uint64_t text_to_evpid(const char *);
uint32_t text_to_msgid(const char *);

struct node {
	int			 type;
	const char		*token;
	struct node		*parent;
	TAILQ_ENTRY(node)	 entry;
	TAILQ_HEAD(, node)	 children;
	int			(*cmd)(int, struct parameter*);
};

static struct node	*root;

static int text_to_sockaddr(struct sockaddr *, int, const char *);

#define ARGVMAX	64

int
cmd_install(const char *pattern, int (*cmd)(int, struct parameter*))
{
	struct node	*node, *tmp;
	char		*s, *str, *argv[ARGVMAX], **ap;
	int		 i, n;

	/* Tokenize */
	str = s = strdup(pattern);
	if (str == NULL)
		err(1, "strdup");
	n = 0;
	for (ap = argv; n < ARGVMAX && (*ap = strsep(&str, " \t")) != NULL;) {
		if (**ap != '\0') {
			ap++;
			n++;
		}
	}
	*ap = NULL;

	if (root == NULL) {
		root = calloc(1, sizeof (*root));
		TAILQ_INIT(&root->children);
	}
	node = root;

	for (i = 0; i < n; i++) {
		TAILQ_FOREACH(tmp, &node->children, entry) {
			if (!strcmp(tmp->token, argv[i])) {
				node = tmp;
				break;
			}
		}
		if (tmp == NULL) {
			tmp = calloc(1, sizeof (*tmp));
			TAILQ_INIT(&tmp->children);
			if (!strcmp(argv[i], "<str>"))
				tmp->type = P_STR;
			else if (!strcmp(argv[i], "<int>"))
				tmp->type = P_INT;
			else if (!strcmp(argv[i], "<msgid>"))
				tmp->type = P_MSGID;
			else if (!strcmp(argv[i], "<evpid>"))
				tmp->type = P_EVPID;
			else if (!strcmp(argv[i], "<routeid>"))
				tmp->type = P_ROUTEID;
			else if (!strcmp(argv[i], "<addr>"))
				tmp->type = P_ADDR;
			else
				tmp->type = P_TOKEN;
			tmp->token = strdup(argv[i]);
			tmp->parent = node;
			TAILQ_INSERT_TAIL(&node->children, tmp, entry);
			node = tmp;
		}
	}

	if (node->cmd)
		errx(1, "duplicate pattern: %s", pattern);
	node->cmd = cmd;

	free(s);
	return (n);
}

static int
cmd_check(const char *str, struct node *node, struct parameter *res)
{
	const char *e;

	switch (node->type) {
	case P_TOKEN:
		if (!strcmp(str, node->token))
			return (1);
		return (0);

	case P_STR:
		res->u.u_str = str;
		return (1);

	case P_INT:
		res->u.u_int = strtonum(str, INT_MIN, INT_MAX, &e);
		if (e)
			return (0);
		return (1);

	case P_MSGID:
		if (strlen(str) != 8)
			return (0);
		res->u.u_msgid = text_to_msgid(str);
		if (res->u.u_msgid == 0)
			return (0);
		return (1);

	case P_EVPID:
		if (strlen(str) != 16)
			return (0);
		res->u.u_evpid = text_to_evpid(str);
		if (res->u.u_evpid == 0)
			return (0);
		return (1);

	case P_ROUTEID:
		res->u.u_routeid = strtonum(str, 1, LLONG_MAX, &e);
		if (e)
			return (0);
		return (1);

	case P_ADDR:
		if (text_to_sockaddr((struct sockaddr *)&res->u.u_ss, PF_UNSPEC, str) == 0)
			return (1);
		return (0);

	default:
		errx(1, "bad token type: %d", node->type);
		return (0);
	}
}

int
cmd_run(int argc, char **argv)
{
	struct parameter param[ARGVMAX];
	struct node	*node, *tmp, *stack[ARGVMAX], *best;
	int		 i, j, np;

	node = root;
	np = 0;

	for (i = 0; i < argc; i++) {
		TAILQ_FOREACH(tmp, &node->children, entry) {
			if (cmd_check(argv[i], tmp, &param[np])) {
				stack[i] = tmp;
				node = tmp;
				param[np].type = node->type;
				if (node->type != P_TOKEN)
					np++;
				break;
			}
		}
		if (tmp == NULL) {
			best = NULL;
			TAILQ_FOREACH(tmp, &node->children, entry) {
				if (tmp->type != P_TOKEN)
					continue;
				if (strstr(tmp->token, argv[i]) != tmp->token)
					continue;
				if (best)
					goto fail;
				best = tmp;
			}
			if (best == NULL)
				goto fail;
			stack[i] = best;
			node = best;
			param[np].type = node->type;
			if (node->type != P_TOKEN)
				np++;
		}
	}

	if (node->cmd == NULL)
		goto fail;

	return (node->cmd(np, np ? param : NULL));

fail:
	if (TAILQ_FIRST(&node->children) == NULL) {
		fprintf(stderr, "invalid command\n");
		return (-1);
	}

	fprintf(stderr, "possibilities are:\n");
	TAILQ_FOREACH(tmp, &node->children, entry) {
		for (j = 0; j < i; j++)
			fprintf(stderr, "%s%s", j?" ":"", stack[j]->token);
		fprintf(stderr, "%s%s\n", i?" ":"", tmp->token);
	}

	return (-1);
}

int
cmd_show_params(int argc, struct parameter *argv)
{
	int	i;

	for (i = 0; i < argc; i++) {
		switch(argv[i].type) {
		case P_STR:
			printf(" str:\"%s\"", argv[i].u.u_str);
			break;
		case P_INT:
			printf(" int:%d", argv[i].u.u_int);
			break;
		case P_MSGID:
			printf(" msgid:%08"PRIx32, argv[i].u.u_msgid);
			break;
		case P_EVPID:
			printf(" evpid:%016"PRIx64, argv[i].u.u_evpid);
			break;
		case P_ROUTEID:
			printf(" routeid:%016"PRIx64, argv[i].u.u_routeid);
			break;
		default:
			printf(" ???:%d", argv[i].type);
		}
	}
	printf ("\n");
	return (1);
}

static int
text_to_sockaddr(struct sockaddr *sa, int family, const char *str)
{
	struct in_addr		 ina;
	struct in6_addr		 in6a;
	struct sockaddr_in	*in;
	struct sockaddr_in6	*in6;
	char			*cp, *str2;
	const char		*errstr;

	switch (family) {
	case PF_UNSPEC:
		if (text_to_sockaddr(sa, PF_INET, str) == 0)
			return (0);
		return text_to_sockaddr(sa, PF_INET6, str);

	case PF_INET:
		if (inet_pton(PF_INET, str, &ina) != 1)
			return (-1);

		in = (struct sockaddr_in *)sa;
		memset(in, 0, sizeof *in);
		in->sin_len = sizeof(struct sockaddr_in);
		in->sin_family = PF_INET;
		in->sin_addr.s_addr = ina.s_addr;
		return (0);

	case PF_INET6:
		cp = strchr(str, SCOPE_DELIMITER);
		if (cp) {
			str2 = strdup(str);
			if (str2 == NULL)
				return (-1);
			str2[cp - str] = '\0';
			if (inet_pton(PF_INET6, str2, &in6a) != 1) {
				free(str2);
				return (-1);
			}
			cp++;
			free(str2);
		} else if (inet_pton(PF_INET6, str, &in6a) != 1)
			return (-1);

		in6 = (struct sockaddr_in6 *)sa;
		memset(in6, 0, sizeof *in6);
		in6->sin6_len = sizeof(struct sockaddr_in6);
		in6->sin6_family = PF_INET6;
		in6->sin6_addr = in6a;

		if (cp == NULL)
			return (0);

		if (IN6_IS_ADDR_LINKLOCAL(&in6a) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(&in6a) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(&in6a))
			if ((in6->sin6_scope_id = if_nametoindex(cp)))
				return (0);

		in6->sin6_scope_id = strtonum(cp, 0, UINT32_MAX, &errstr);
		if (errstr)
			return (-1);
		return (0);

	default:
		break;
	}

	return (-1);
}
